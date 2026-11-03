// =============================================================================
//  server.cpp  --  聊天服务端实现
//
//  一个客户端会话的完整时序：
//    accept -> [等待 HELLO，10s 超时] -> 校验 -> WELCOME -> 广播 JOIN
//           -> 广播 USERLIST -> [循环：收帧 -> 解析 -> 广播] -> 广播 LEAVE
// =============================================================================
#include "server.h"

#include "../common/net.h"
#include "../common/text.h"

#include <algorithm>
#include <chrono>

namespace {

constexpr int kHelloTimeoutMs   = 10000;  // 等待 HELLO 的超时
constexpr int kIdleTimeoutMs    = 75000;  // 会话空闲超时（心跳每 15s 一次）
constexpr int kHeartbeatSeconds = 15;     // 心跳周期
constexpr int kSendTimeoutMs    = 5000;   // 单次 send 的超时，避免慢客户端拖住广播

// 把消息中的换行替换为空格，保证一条消息显示一行
std::string FlattenNewlines(const std::string& utf8) {
    std::string out = utf8;
    for (char& c : out) {
        if (c == '\r' || c == '\n') {
            c = ' ';
        }
    }
    return out;
}

}  // namespace

ChatServer::ChatServer() = default;

ChatServer::~ChatServer() { Stop(); }

bool ChatServer::Start(const std::string& bindAddr, uint16_t port, std::wstring* err) {
    if (m_running.load()) {
        if (err != nullptr) {
            *err = L"服务器已经在运行";
        }
        return false;
    }

    std::string error;
    if (!net::Startup(&error)) {
        if (err != nullptr) {
            *err = text::Utf8ToWide(error);
        }
        return false;
    }

    // 端口占用检查：Windows 的 SO_REUSEADDR 允许重复 bind，
    {
        std::string probeError;
        const SOCKET probe = net::Connect("127.0.0.1", port, 500, &probeError);
        if (probe != INVALID_SOCKET) {
            net::CloseGraceful(probe);
            net::Cleanup();
            if (err != nullptr) {
                *err = L"端口 " + std::to_wstring(port) +
                       L" 已被占用：本机已经有服务端在监听，请先关闭它或换一个端口。";
            }
            return false;
        }
    }

    m_listen = net::Listen(bindAddr, port, &error);
    if (m_listen == INVALID_SOCKET) {
        net::Cleanup();
        if (err != nullptr) {
            *err = text::Utf8ToWide(error);
        }
        return false;
    }

    m_bindAddr = bindAddr.empty() ? "0.0.0.0" : bindAddr;
    m_port = port;
    m_seq.store(0);
    m_running.store(true);

    m_acceptThread = std::thread([this]() { AcceptLoop(); });
    m_heartbeatThread = std::thread([this]() { HeartbeatLoop(); });

    Log(text::Format(L"[启动] 监听 %s:%u ，等待客户端连接…",
                     text::Utf8ToWide(m_bindAddr).c_str(),
                     static_cast<unsigned>(port)));
    return true;
}

void ChatServer::Stop() {
    if (!m_running.exchange(false)) {
        if (m_listen != INVALID_SOCKET) {
            ::closesocket(m_listen);
            m_listen = INVALID_SOCKET;
        }
        return;
    }

    // (1) 通知在线客户端服务器即将关闭
    {
        std::lock_guard<std::mutex> broadcastLock(m_broadcastMutex);
        std::vector<std::string> fields;
        NextEventHeader(fields);
        fields.push_back(text::WideToUtf8(L"服务器正在关闭，连接即将断开。"));
        BroadcastFrameLocked(scp::m::kSystem, fields);
    }

    // (2) 等待监听线程退出，然后关闭监听套接字（不再接受新连接）
    if (m_acceptThread.joinable()) {
        m_acceptThread.join();
    }
    SOCKET listenSocket = m_listen;
    m_listen = INVALID_SOCKET;
    if (listenSocket != INVALID_SOCKET) {
        ::closesocket(listenSocket);
    }

    // (3) 关闭所有会话套接字，令其接收线程从 recv 返回
    {
        auto sessions = Snapshot(false);
        for (const auto& session : sessions) {
            std::lock_guard<std::mutex> lock(session->sendMutex);
            if (session->sock != INVALID_SOCKET) {
                ::shutdown(session->sock, SD_BOTH);
            }
        }
    }

    // (4) 等待所有会话线程收尾
    {
        std::unique_lock<std::mutex> lock(m_activeMutex);
        m_activeCv.wait_for(lock, std::chrono::seconds(8),
                            [this]() { return m_activeSessions == 0; });
    }

    // (5) 停止心跳线程
    if (m_heartbeatThread.joinable()) {
        m_heartbeatThread.join();
    }

    net::Cleanup();
    Log(L"[停止] 服务器已停止监听。");
}

void ChatServer::AcceptLoop() {
    while (m_running.load()) {
        // 用 select 做 200ms 轮询，使停止服务器时不必依赖 closesocket 唤醒 accept
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(m_listen, &readSet);
        timeval timeout{0, 200000};
        const int ready = ::select(0, &readSet, nullptr, nullptr, &timeout);
        if (!m_running.load()) {
            break;
        }
        if (ready <= 0) {
            continue;
        }

        sockaddr_in peer{};
        int peerLength = static_cast<int>(sizeof(peer));
        const SOCKET client =
            ::accept(m_listen, reinterpret_cast<sockaddr*>(&peer), &peerLength);
        if (client == INVALID_SOCKET) {
            if (m_running.load()) {
                Log(L"[警告] accept 失败：" +
                    text::Utf8ToWide(net::ErrorText(WSAGetLastError())));
            }
            continue;
        }
        if (!m_running.load()) {
            ::closesocket(client);
            break;
        }

        net::SetNoDelay(client);              
        net::SetSendTimeout(client, kSendTimeoutMs);

        auto session = std::make_shared<Session>();
        session->sock = client;
        session->peer = peer;

        {
            std::lock_guard<std::mutex> lock(m_activeMutex);
            ++m_activeSessions;
        }

        Log(text::Format(L"[连接] 新客户端 %s",
                         text::Utf8ToWide(net::PeerText(peer)).c_str()));

        std::thread([this, session]() {
            SessionLoop(session);
            {
                std::lock_guard<std::mutex> lock(m_activeMutex);
                --m_activeSessions;
            }
            m_activeCv.notify_all();
        }).detach();
    }
}

void ChatServer::HeartbeatLoop() {
    while (m_running.load()) {
        for (int i = 0; i < kHeartbeatSeconds * 10 && m_running.load(); ++i) {
            ::Sleep(100);
        }
        if (!m_running.load()) {
            break;
        }
        std::lock_guard<std::mutex> broadcastLock(m_broadcastMutex);
        const std::string now = text::WideToUtf8(text::NowDateTime());
        std::vector<std::string> fields{now};
        BroadcastFrameLocked(scp::m::kPing, fields);
        const int online = static_cast<int>(Snapshot(true).size());
        Log(text::Format(L"[心跳] 已向 %d 位在线用户发送 PING", online));
    }
}

void ChatServer::SessionLoop(SessionPtr session) {
    const std::wstring peerText = text::Utf8ToWide(net::PeerText(session->peer));
    std::string error;
    std::string payload;

    // 阶段 1：等待并校验 HELLO
    net::SetRecvTimeout(session->sock, kHelloTimeoutMs);
    if (!net::RecvFrame(session->sock, payload, &error)) {
        Log(text::Format(L"[断开] %s 未完成登录：%s", peerText.c_str(),
                         text::Utf8ToWide(error).c_str()));
        ::closesocket(session->sock);
        session->sock = INVALID_SOCKET;
        return;
    }
    ++m_statFramesIn;

    scp::Frame hello;
    if (!scp::ParsePayload(payload, hello, &error) || hello.type != scp::m::kHello) {
        SendFrameTo(session, scp::m::kError, {"E01", "首个消息必须是 HELLO"});
        Log(text::Format(L"[断开] %s 首帧不是 HELLO", peerText.c_str()));
        ::closesocket(session->sock);
        session->sock = INVALID_SOCKET;
        return;
    }
    if (hello.fields.size() < 2) {
        SendFrameTo(session, scp::m::kError, {"E02", "HELLO 字段不足"});
        ::closesocket(session->sock);
        session->sock = INVALID_SOCKET;
        return;
    }
    if (hello.fields[0] != scp::kVersion) {
        SendFrameTo(session, scp::m::kError,
                    {"E03", "协议版本不匹配，服务端要求 " + std::string(scp::kVersion)});
        Log(text::Format(L"[断开] %s 协议版本不匹配：%s", peerText.c_str(),
                         text::Utf8ToWide(hello.fields[0]).c_str()));
        ::closesocket(session->sock);
        session->sock = INVALID_SOCKET;
        return;
    }

    const std::wstring nickWide = text::Trim(text::Utf8ToWide(hello.fields[1]));
    const std::string nickUtf8 = text::WideToUtf8(nickWide);
    if (nickWide.empty()) {
        SendFrameTo(session, scp::m::kError, {"E04", "昵称不能为空"});
        ::closesocket(session->sock);
        session->sock = INVALID_SOCKET;
        return;
    }
    if (nickUtf8.size() > scp::kMaxNickBytes) {
        SendFrameTo(session, scp::m::kError, {"E05", "昵称过长（最多 32 个汉字）"});
        ::closesocket(session->sock);
        session->sock = INVALID_SOCKET;
        return;
    }
    if (scp::HasControlChar(nickUtf8)) {
        SendFrameTo(session, scp::m::kError, {"E06", "昵称包含控制字符"});
        ::closesocket(session->sock);
        session->sock = INVALID_SOCKET;
        return;
    }

    // 阶段 2：登录成功，广播 JOIN
    {
        std::lock_guard<std::mutex> broadcastLock(m_broadcastMutex);
        session->id = m_nextId.fetch_add(1) + 1;
        session->nick = AllocNick(nickUtf8);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            session->welcomed = true;
            m_sessions.push_back(session);
        }
        ++m_statLogins;

        const int online = static_cast<int>(Snapshot(true).size());
        SendFrameTo(session, scp::m::kWelcome,
                    {scp::kVersion, scp::kServerName,
                     text::WideToUtf8(text::NowDateTime()),
                     std::to_string(session->id), session->nick,
                     std::to_string(online)});

        std::vector<std::string> joinFields;
        NextEventHeader(joinFields);
        joinFields.push_back(std::to_string(session->id));
        joinFields.push_back(session->nick);
        joinFields.push_back(std::to_string(online));
        BroadcastFrameLocked(scp::m::kJoin, joinFields);
        BroadcastUserListLocked();
    }

    Log(text::Format(L"[登录] %s 以昵称「%s」登录成功（%s，当前在线 %d 人）",
                     peerText.c_str(), text::Utf8ToWide(session->nick).c_str(),
                     text::Utf8ToWide(net::PeerText(session->peer)).c_str(),
                     static_cast<int>(Snapshot(true).size())));

    // 阶段 3：消息循环
    net::SetRecvTimeout(session->sock, kIdleTimeoutMs);
    std::wstring reason = L"连接中断";
    while (m_running.load() && !session->dead.load()) {
        if (!net::RecvFrame(session->sock, payload, &error)) {
            if (!m_running.load()) {
                reason = L"服务器关闭";
            } else if (net::LastErrorCode() == WSAETIMEDOUT) {
                reason = L"心跳超时";
            } else {
                reason = L"连接中断";
            }
            Log(text::Format(L"[断开] %s（%s）：%s",
                             text::Utf8ToWide(session->nick).c_str(), reason.c_str(),
                             text::Utf8ToWide(error).c_str()));
            break;
        }
        ++m_statFramesIn;

        scp::Frame frame;
        if (!scp::ParsePayload(payload, frame, &error)) {
            Log(L"[警告] 收到无法解析的帧，已丢弃：" + text::Utf8ToWide(error));
            continue;
        }
        if (!HandleFrame(session, frame, reason)) {
            break;
        }
    }

    // 阶段 4：退出清理，广播 LEAVE
    {
        std::lock_guard<std::mutex> broadcastLock(m_broadcastMutex);
        bool removed = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto it = std::find(m_sessions.begin(), m_sessions.end(), session);
            if (it != m_sessions.end()) {
                m_sessions.erase(it);
                removed = true;
            }
        }
        if (removed && m_running.load()) {
            std::vector<std::string> leaveFields;
            NextEventHeader(leaveFields);
            leaveFields.push_back(std::to_string(session->id));
            leaveFields.push_back(session->nick);
            leaveFields.push_back(text::WideToUtf8(reason));
            leaveFields.push_back(std::to_string(Snapshot(true).size()));
            BroadcastFrameLocked(scp::m::kLeave, leaveFields);
            BroadcastUserListLocked();
            Log(text::Format(L"[离开] 「%s」退出（%s），当前在线 %d 人",
                             text::Utf8ToWide(session->nick).c_str(), reason.c_str(),
                             static_cast<int>(Snapshot(true).size())));
        }
    }

    net::CloseGraceful(session->sock);
    session->sock = INVALID_SOCKET;
}

bool ChatServer::HandleFrame(const SessionPtr& session, const scp::Frame& frame,
                             std::wstring& reason) {
    if (frame.type == scp::m::kChat) {
        if (frame.fields.size() < 2) {
            SendFrameTo(session, scp::m::kError, {"E10", "CHAT 字段不足"});
            return true;
        }
        const std::wstring content = text::Trim(text::Utf8ToWide(frame.fields[1]));
        const std::string contentUtf8 = text::WideToUtf8(content);
        if (content.empty()) {
            SendFrameTo(session, scp::m::kError, {"E11", "消息内容为空"});
            return true;
        }
        if (contentUtf8.size() > scp::kMaxChatBytes) {
            SendFrameTo(session, scp::m::kError, {"E12", "消息过长，单条不超过 4000 字节"});
            return true;
        }

        const std::string localSeq = frame.fields[0];
        const std::string body = FlattenNewlines(contentUtf8);

        {
            std::lock_guard<std::mutex> broadcastLock(m_broadcastMutex);
            std::vector<std::string> fields;
            NextEventHeader(fields);
            fields.push_back(std::to_string(session->id));
            fields.push_back(session->nick);
            fields.push_back(body);
            BroadcastFrameLocked(scp::m::kChat, fields);
        }
        ++m_statMessages;
        Log(text::Format(L"[聊天] 「%s」(本地序号 %s) 说：%s",
                         text::Utf8ToWide(session->nick).c_str(),
                         text::Utf8ToWide(localSeq).c_str(),
                         content.c_str()));
        return true;
    }

    if (frame.type == scp::m::kPong) {
        return true;  // 心跳应答，仅用于维持连接
    }

    if (frame.type == scp::m::kBye) {
        reason = L"主动退出";
        return false;
    }

    SendFrameTo(session, scp::m::kError, {"E99", "未知消息类型：" + frame.type});
    Log(L"[警告] 未知消息类型：" + text::Utf8ToWide(frame.type));
    return true;
}

void ChatServer::SendFrameTo(const SessionPtr& session, const std::string& type,
                             const std::vector<std::string>& fields) {
    scp::Frame frame;
    frame.type = type;
    frame.fields = fields;
    std::string wire;
    std::string error;
    if (!scp::BuildWire(frame, wire, &error)) {
        Log(L"[错误] 帧编码失败：" + text::Utf8ToWide(error));
        return;
    }
    SendWire(session, wire);
}

void ChatServer::SendWire(const SessionPtr& session, const std::string& wire) {
    if (session->dead.load()) {
        return;
    }
    std::lock_guard<std::mutex> lock(session->sendMutex);
    if (session->sock == INVALID_SOCKET) {
        return;
    }
    std::string error;
    if (!net::SendFrame(session->sock, wire, &error)) {
        ++m_statSendErrors;
        session->dead.store(true);
        ::shutdown(session->sock, SD_BOTH);
        Log(L"[警告] 向客户端发送失败，将断开它：" + text::Utf8ToWide(error));
        return;
    }
    ++m_statFramesOut;
}

void ChatServer::BroadcastFrameLocked(const std::string& type,
                                      const std::vector<std::string>& fields,
                                      const SessionPtr& except) {
    scp::Frame frame;
    frame.type = type;
    frame.fields = fields;
    std::string wire;
    std::string error;
    if (!scp::BuildWire(frame, wire, &error)) {
        Log(L"[错误] 广播帧编码失败：" + text::Utf8ToWide(error));
        return;
    }
    const auto targets = Snapshot(true);
    for (const auto& target : targets) {
        if (target == except || target->dead.load()) {
            continue;
        }
        SendWire(target, wire);
    }
}

void ChatServer::BroadcastUserListLocked() {
    const auto sessions = Snapshot(true);
    std::vector<std::string> fields;
    fields.push_back(std::to_string(sessions.size()));
    std::vector<std::wstring> names;
    names.reserve(sessions.size());
    for (const auto& session : sessions) {
        fields.push_back(std::to_string(session->id));
        fields.push_back(session->nick);
        names.push_back(text::Utf8ToWide(session->nick));
    }
    BroadcastFrameLocked(scp::m::kUserList, fields);
    if (m_userListSink) {
        m_userListSink(names);
    }
}

std::vector<ChatServer::SessionPtr> ChatServer::Snapshot(bool onlyWelcomed) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<SessionPtr> out;
    out.reserve(m_sessions.size());
    for (const auto& session : m_sessions) {
        if (onlyWelcomed && !session->welcomed) {
            continue;
        }
        out.push_back(session);
    }
    return out;
}

std::vector<std::wstring> ChatServer::OnlineUsers() const {
    const auto sessions = Snapshot(true);
    std::vector<std::wstring> names;
    names.reserve(sessions.size());
    for (const auto& session : sessions) {
        names.push_back(text::Utf8ToWide(session->nick));
    }
    return names;
}

ServerStats ChatServer::GetStats() const {
    ServerStats stats;
    stats.online = static_cast<int>(Snapshot(true).size());
    stats.messages = m_statMessages.load();
    stats.logins = m_statLogins.load();
    stats.framesIn = m_statFramesIn.load();
    stats.framesOut = m_statFramesOut.load();
    stats.sendErrors = m_statSendErrors.load();
    return stats;
}

void ChatServer::NextEventHeader(std::vector<std::string>& fields) {
    const unsigned long long seq = m_seq.fetch_add(1) + 1;
    fields.push_back(std::to_string(seq));
    fields.push_back(text::WideToUtf8(text::NowDateTime()));
}

std::string ChatServer::AllocNick(const std::string& desired) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto taken = [this](const std::string& name) {
        for (const auto& session : m_sessions) {
            if (session->nick == name) {
                return true;
            }
        }
        return false;
    };
    if (!taken(desired)) {
        return desired;
    }
    for (int i = 2; i < 1000; ++i) {
        const std::string candidate = desired + "#" + std::to_string(i);
        if (!taken(candidate)) {
            return candidate;
        }
    }
    return desired + "#" + std::to_string(::GetTickCount());
}

void ChatServer::Log(const std::wstring& line) {
    if (m_logSink) {
        m_logSink(line);
    }
}
