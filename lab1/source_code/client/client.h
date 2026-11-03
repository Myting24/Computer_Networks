// =============================================================================
//  client.h  --  聊天客户端（多线程、流式套接字）
//
//  线程模型：
//    界面线程 : Win32 窗口，负责发送消息（send 由 m_ioMutex 保护）
//    接收线程 : 连接成功后循环 RecvFrame -> 解析 -> 回调给界面
//  状态机（「登录 / 加入 / 离开 / 等待」）
// =============================================================================
#pragma once

#include "../common/platform.h"
#include "../common/protocol.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class ChatClient {
public:
    enum class State { Idle, Connecting, LoggingIn, Online, Closing };

    using LineSink  = std::function<void(const std::wstring&)>;             // 聊天区一行
    using ListSink  = std::function<void(const std::vector<std::wstring>&)>; // 在线列表
    using StateSink = std::function<void(State, const std::wstring&)>;      // 状态变化

    ChatClient();
    ~ChatClient();

    ChatClient(const ChatClient&) = delete;
    ChatClient& operator=(const ChatClient&) = delete;

    void SetLineSink(LineSink sink) { m_lineSink = std::move(sink); }
    void SetListSink(ListSink sink) { m_listSink = std::move(sink); }
    void SetStateSink(StateSink sink) { m_stateSink = std::move(sink); }

    bool BeginConnect(const std::wstring& host, uint16_t port, const std::wstring& nick,
                      std::wstring* err);
    void Disconnect();
    bool SendChat(const std::wstring& text, std::wstring* err);

    State GetState() const { return m_state.load(); }
    std::wstring AssignedNick() const;
    unsigned long long ReceivedCount() const { return m_received.load(); }
    unsigned long long GapCount() const { return m_gaps.load(); }
    unsigned long long RxFrames() const { return m_rxFrames.load(); }
    unsigned long long TxFrames() const { return m_txFrames.load(); }

private:
    void WorkerMain(std::wstring host, uint16_t port, std::wstring nick);
    void RecvLoop(SOCKET sock);
    bool Dispatch(const scp::Frame& frame);
    bool SendFrame(const std::string& type, const std::vector<std::string>& fields,
                   std::wstring* err);
    void CheckSequence(const std::string& seqText);

    void EmitLine(const std::wstring& line);
    void EmitList(const std::vector<std::wstring>& names);
    void EmitState(State state, const std::wstring& text);

    std::atomic<State> m_state{State::Idle};
    SOCKET             m_sock = INVALID_SOCKET;
    std::mutex         m_ioMutex;   // 保护 m_sock 与同一条连接上的 send
    std::thread        m_thread;

    LineSink  m_lineSink;
    ListSink  m_listSink;
    StateSink m_stateSink;

    std::wstring m_nick;        // 服务端最终分配的昵称
    mutable std::mutex m_nickMutex;
    unsigned long long m_localSeq = 0;      // 本地发送序号（界面线程使用）
    unsigned long long m_expectedSeq = 0;   // 期望的下一个全局序号（接收线程使用）
    unsigned long long m_assignedId = 0;

    std::atomic<unsigned long long> m_received{0};
    std::atomic<unsigned long long> m_gaps{0};
    std::atomic<unsigned long long> m_rxFrames{0};
    std::atomic<unsigned long long> m_txFrames{0};
};
