// =============================================================================
//  server.h  --  聊天服务端（多线程、流式套接字）
//
//  线程模型：
//    界面线程 : Win32 窗口与控件
//    监听线程 x1 : select + accept，为每个新连接创建会话线程
//    会话线程 xN : 每个客户端一个，负责收消息、解析、广播
//    心跳线程 x1 : 周期性发送 PING，探测死连接
//
//  共享数据保护：
//    m_mutex : 保护在线会话表
//    m_broadcastMutex : 串行化「广播」这一动作，保证所有客户端看到的消息顺序与序号完全一致（先广播者先入表）
//    每个会话的 sendMutex : 同一连接上的 send 不能并发，否则字节流会交错
// =============================================================================
#pragma once

#include "../common/platform.h"
#include "../common/protocol.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ServerStats {
    int                online = 0;
    unsigned long long messages = 0;   // 已广播的聊天消息条数
    unsigned long long logins = 0;     // 累计登录人数
    unsigned long long framesIn = 0;   // 收到的帧
    unsigned long long framesOut = 0;  // 发出的帧
    unsigned long long sendErrors = 0; // 发送失败次数
};

class ChatServer {
public:
    using LogSink      = std::function<void(const std::wstring&)>;
    using UserListSink = std::function<void(const std::vector<std::wstring>&)>;

    ChatServer();
    ~ChatServer();

    ChatServer(const ChatServer&) = delete;
    ChatServer& operator=(const ChatServer&) = delete;

    bool Start(const std::string& bindAddr, uint16_t port, std::wstring* err);
    void Stop();
    bool IsRunning() const { return m_running.load(); }

    void SetLogSink(LogSink sink) { m_logSink = std::move(sink); }
    void SetUserListSink(UserListSink sink) { m_userListSink = std::move(sink); }

    ServerStats GetStats() const;
    std::vector<std::wstring> OnlineUsers() const;

private:
    struct Session {
        SOCKET              sock = INVALID_SOCKET;
        sockaddr_in         peer{};
        unsigned long long  id = 0;
        std::string         nick;              // UTF-8
        bool                welcomed = false;  // 是否已完成 WELCOME
        std::atomic<bool>   dead{false};
        std::mutex          sendMutex;
    };
    using SessionPtr = std::shared_ptr<Session>;

    void AcceptLoop();
    void HeartbeatLoop();
    void SessionLoop(SessionPtr session);

    bool HandleFrame(const SessionPtr& session, const scp::Frame& frame,
                     std::wstring& reason);

    void SendFrameTo(const SessionPtr& session, const std::string& type,
                     const std::vector<std::string>& fields);
    void BroadcastFrameLocked(const std::string& type,
                              const std::vector<std::string>& fields,
                              const SessionPtr& except = nullptr);
    void BroadcastUserListLocked();
    void SendWire(const SessionPtr& session, const std::string& wire);

    std::vector<SessionPtr> Snapshot(bool onlyWelcomed) const;
    void NotifyUserList();

    void Log(const std::wstring& line);

    // 生成 "序号 | 时间" 两个公共字段
    void NextEventHeader(std::vector<std::string>& fields);
    std::string AllocNick(const std::string& desired);

    std::string m_bindAddr;
    uint16_t    m_port = 0;
    SOCKET      m_listen = INVALID_SOCKET;

    mutable std::mutex         m_mutex;            // 保护 m_sessions
    std::vector<SessionPtr>    m_sessions;
    std::mutex                 m_broadcastMutex;   // 串行化广播

    std::atomic<bool>          m_running{false};
    std::atomic<unsigned long long> m_seq{0};
    std::atomic<unsigned long long> m_nextId{0};

    std::atomic<unsigned long long> m_statMessages{0};
    std::atomic<unsigned long long> m_statLogins{0};
    std::atomic<unsigned long long> m_statFramesIn{0};
    std::atomic<unsigned long long> m_statFramesOut{0};
    std::atomic<unsigned long long> m_statSendErrors{0};

    std::thread            m_acceptThread;
    std::thread            m_heartbeatThread;
    int                    m_activeSessions = 0;
    std::mutex             m_activeMutex;
    std::condition_variable m_activeCv;

    LogSink      m_logSink;
    UserListSink m_userListSink;
};
