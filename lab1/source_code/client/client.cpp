// =============================================================================
//  client.cpp  --  聊天客户端实现
// =============================================================================
#include "client.h"

#include "../common/net.h"
#include "../common/text.h"

#include <algorithm>

namespace {

// 从字段中取出时间戳并转成 "HH:MM:SS"；字段缺失时用当前时间兜底
std::wstring FieldTime(const scp::Frame& frame, size_t index) {
    if (frame.fields.size() > index && !frame.fields[index].empty()) {
        return text::ShortTime(text::Utf8ToWide(frame.fields[index]));
    }
    return text::NowTime();
}

std::wstring FieldText(const scp::Frame& frame, size_t index) {
    if (frame.fields.size() > index) {
        return text::Utf8ToWide(frame.fields[index]);
    }
    return std::wstring();
}

int FieldInt(const scp::Frame& frame, size_t index, int fallback) {
    if (frame.fields.size() > index) {
        try {
            return std::stoi(frame.fields[index]);
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

}  // namespace

ChatClient::ChatClient() = default;

ChatClient::~ChatClient() {
    Disconnect();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void ChatClient::EmitLine(const std::wstring& line) {
    if (m_lineSink) {
        m_lineSink(line);
    }
}

void ChatClient::EmitList(const std::vector<std::wstring>& names) {
    if (m_listSink) {
        m_listSink(names);
    }
}

void ChatClient::EmitState(State state, const std::wstring& text) {
    if (m_stateSink) {
        m_stateSink(state, text);
    }
}

bool ChatClient::BeginConnect(const std::wstring& host, uint16_t port,
                              const std::wstring& nick, std::wstring* err) {
    if (m_state.load() != State::Idle) {
        if (err != nullptr) {
            *err = L"当前已处于连接状态";
        }
        return false;
    }
    if (host.empty()) {
        if (err != nullptr) {
            *err = L"服务器地址不能为空";
        }
        return false;
    }
    if (m_thread.joinable()) {
        m_thread.join();  // 回收上一次连接留下的线程
    }

    m_expectedSeq = 0;
    m_localSeq = 0;
    m_assignedId = 0;
    m_received.store(0);
    m_gaps.store(0);
    m_rxFrames.store(0);
    m_txFrames.store(0);
    m_nick.clear();
    m_state.store(State::Connecting);

    m_thread = std::thread([this, host, port, nick]() {
        WorkerMain(host, port, nick);
    });
    return true;
}

void ChatClient::Disconnect() {
    if (m_state.load() == State::Idle) {
        if (m_thread.joinable()) {
            m_thread.join();
        }
        return;
    }

    if (m_state.load() == State::Online) {
        SendFrame(scp::m::kBye, {}, nullptr);  
    }
    m_state.store(State::Closing);

    {
        std::lock_guard<std::mutex> lock(m_ioMutex);
        if (m_sock != INVALID_SOCKET) {
            ::shutdown(m_sock, SD_BOTH);  // 唤醒阻塞中的 recv 的接收线程
        }
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_state.store(State::Idle);
}

void ChatClient::WorkerMain(std::wstring host, uint16_t port, std::wstring nick) {
    EmitState(State::Connecting,
              L"正在连接 " + host + L":" + std::to_wstring(port) + L" …");

    std::string error;
    SOCKET sock = net::Connect(text::WideToUtf8(host), port, 5000, &error);
    if (sock == INVALID_SOCKET) {
        EmitLine(L"【错误】连接服务器失败：" + text::Utf8ToWide(error));
        if (error.find("10061") != std::string::npos) {
            EmitLine(L"【提示】这个端口上没有服务端在监听：请先打开 Server.exe，"
                     L"并检查这里的端口号是否和服务端一致（默认 8888）。");
        }
        m_state.store(State::Idle);
        EmitState(State::Idle, L"未连接");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_ioMutex);
        m_sock = sock;
    }
    net::SetNoDelay(sock);
    net::SetSendTimeout(sock, 3000);

    EmitState(State::LoggingIn, L"TCP 连接已建立，正在发送 HELLO 登录 …");
    std::wstring sendError;
    if (!SendFrame(scp::m::kHello, {scp::kVersion, text::WideToUtf8(nick)}, &sendError)) {
        EmitLine(L"【错误】发送 HELLO 失败：" + sendError);
        std::lock_guard<std::mutex> lock(m_ioMutex);
        net::CloseGraceful(m_sock);
        m_sock = INVALID_SOCKET;
        m_state.store(State::Idle);
        EmitState(State::Idle, L"未连接");
        return;
    }

    RecvLoop(sock);

    {
        std::lock_guard<std::mutex> lock(m_ioMutex);
        if (m_sock != INVALID_SOCKET) {
            net::CloseGraceful(m_sock);
            m_sock = INVALID_SOCKET;
        }
    }
    m_state.store(State::Idle);
    EmitState(State::Idle, L"未连接");
    EmitList({});
}

void ChatClient::RecvLoop(SOCKET sock) {
    while (m_state.load() != State::Closing) {
        std::string payload;
        std::string error;
        if (!net::RecvFrame(sock, payload, &error)) {
            if (m_state.load() != State::Closing) {
                if (net::LastErrorCode() == WSAETIMEDOUT) {
                    EmitLine(L"【提示】等待服务器数据超时，连接已断开。");
                } else {
                    EmitLine(L"【提示】与服务器的连接已断开：" +
                             text::Utf8ToWide(error));
                }
            }
            return;
        }
        ++m_rxFrames;

        scp::Frame frame;
        if (!scp::ParsePayload(payload, frame, &error)) {
            EmitLine(L"【提示】收到无法解析的数据帧，已忽略。");
            continue;
        }
        if (!Dispatch(frame)) {
            return;
        }
    }
}

bool ChatClient::Dispatch(const scp::Frame& frame) {
    if (frame.type == scp::m::kWelcome) {
        m_assignedId = 0;
        if (frame.fields.size() > 3) {
            try {
                m_assignedId = std::stoull(frame.fields[3]);
            } catch (...) {
                m_assignedId = 0;
            }
        }
        {
            std::lock_guard<std::mutex> lock(m_nickMutex);
            m_nick = FieldText(frame, 4);
        }
        const std::wstring server = FieldText(frame, 1);
        m_expectedSeq = 0;
        m_state.store(State::Online);
        EmitState(State::Online, L"在线（昵称：" + FieldText(frame, 4) + L"）");
        EmitLine(text::Format(L"【系统】已连接服务器 %s，你的昵称是「%s」，当前在线 %d 人。",
                              server.c_str(), FieldText(frame, 4).c_str(),
                              FieldInt(frame, 5, 1)));
        return true;
    }

    if (frame.type == scp::m::kJoin) {
        CheckSequence(frame.fields.size() > 0 ? frame.fields[0] : std::string());
        EmitLine(text::Format(L"[%s] —— 「%s」加入了聊天室，当前在线 %d 人 ——",
                              FieldTime(frame, 1).c_str(), FieldText(frame, 3).c_str(),
                              FieldInt(frame, 4, 0)));
        return true;
    }

    if (frame.type == scp::m::kLeave) {
        CheckSequence(frame.fields.size() > 0 ? frame.fields[0] : std::string());
        EmitLine(text::Format(L"[%s] —— 「%s」离开了聊天室（%s），当前在线 %d 人 ——",
                              FieldTime(frame, 1).c_str(), FieldText(frame, 3).c_str(),
                              FieldText(frame, 4).c_str(), FieldInt(frame, 5, 0)));
        return true;
    }

    if (frame.type == scp::m::kChat) {
        CheckSequence(frame.fields.size() > 0 ? frame.fields[0] : std::string());
        EmitLine(text::Format(L"[%s] %s：%s", FieldTime(frame, 1).c_str(),
                              FieldText(frame, 3).c_str(), FieldText(frame, 4).c_str()));
        return true;
    }

    if (frame.type == scp::m::kSystem) {
        CheckSequence(frame.fields.size() > 0 ? frame.fields[0] : std::string());
        EmitLine(text::Format(L"[%s] 【系统】%s", FieldTime(frame, 1).c_str(),
                              FieldText(frame, 3).c_str()));
        return true;
    }

    if (frame.type == scp::m::kUserList) {
        std::vector<std::wstring> names;
        const int count = FieldInt(frame, 0, 0);
        for (int i = 0; i < count; ++i) {
            const size_t nameIndex = static_cast<size_t>(1 + i * 2 + 1);
            if (nameIndex < frame.fields.size()) {
                names.push_back(text::Utf8ToWide(frame.fields[nameIndex]));
            }
        }
        EmitList(names);
        return true;
    }

    if (frame.type == scp::m::kPing) {
        SendFrame(scp::m::kPong, {frame.fields.empty() ? std::string() : frame.fields[0]},
                  nullptr);
        return true;
    }

    if (frame.type == scp::m::kError) {
        const std::wstring code = FieldText(frame, 0);
        const std::wstring message = FieldText(frame, 1);
        EmitLine(text::Format(L"【错误】%s（错误码 %s）", message.c_str(), code.c_str()));
        // 登录阶段的错误意味着服务端会立即断开，结束接收循环
        return m_state.load() != State::LoggingIn;
    }

    return true;
}

void ChatClient::CheckSequence(const std::string& seqText) {
    if (seqText.empty()) {
        return;
    }
    unsigned long long seq = 0;
    try {
        seq = std::stoull(seqText);
    } catch (...) {
        return;
    }
    if (m_expectedSeq != 0 && seq != m_expectedSeq) {
        ++m_gaps;
        EmitLine(text::Format(L"【警告】消息序号不连续：期望 %llu，实际收到 %llu（可能存在丢包）",
                              m_expectedSeq, seq));
    }
    m_expectedSeq = seq + 1;
    ++m_received;
}

bool ChatClient::SendChat(const std::wstring& text, std::wstring* err) {
    if (m_state.load() != State::Online) {
        if (err != nullptr) {
            *err = L"尚未连接服务器，无法发送";
        }
        return false;
    }
    const std::wstring content = text::Trim(text);
    if (content.empty()) {
        if (err != nullptr) {
            *err = L"消息内容为空";
        }
        return false;
    }
    const std::string utf8 = text::WideToUtf8(content);
    if (utf8.size() > scp::kMaxChatBytes) {
        if (err != nullptr) {
            *err = L"消息过长（单条最多 4000 字节，约 1300 个汉字）";
        }
        return false;
    }
    ++m_localSeq;
    return SendFrame(scp::m::kChat, {std::to_string(m_localSeq), utf8}, err);
}

bool ChatClient::SendFrame(const std::string& type,
                           const std::vector<std::string>& fields, std::wstring* err) {
    scp::Frame frame;
    frame.type = type;
    frame.fields = fields;
    std::string wire;
    std::string error;
    if (!scp::BuildWire(frame, wire, &error)) {
        if (err != nullptr) {
            *err = text::Utf8ToWide(error);
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(m_ioMutex);
    if (m_sock == INVALID_SOCKET) {
        if (err != nullptr) {
            *err = L"尚未连接服务器";
        }
        return false;
    }
    if (!net::SendFrame(m_sock, wire, &error)) {
        if (err != nullptr) {
            *err = text::Utf8ToWide(error);
        }
        return false;
    }
    ++m_txFrames;
    return true;
}

std::wstring ChatClient::AssignedNick() const {
    std::lock_guard<std::mutex> lock(m_nickMutex);
    return m_nick;
}
