// =============================================================================
//  capture.h  --  网卡枚举与数据包捕获
// =============================================================================
#pragma once

#include "../common/platform.h"

#include <pcap.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace capture {

// 一块网卡
struct Device {
    std::string  name;         // 设备名，打开网卡时传给 pcap_open
    std::string  description;  // 可读描述，界面上显示
    std::wstring display;      // 列表里显示的文字
};

// 一个捕获到的以太网帧中，显示的字段
struct Packet {
    unsigned long long seq = 0;   // 序号
    std::wstring time;            // 捕获时间 时:分:秒.毫秒
    std::wstring srcMac;          // 源 MAC 地址
    std::wstring dstMac;          // 目的 MAC 地址
    unsigned int type = 0;        // 类型字段的值（16 位，大端）
    int          firstByte = -1;  // 类型字段之后第一个字节；-1 表示帧太短
    unsigned int wireLen = 0;     // 帧在网络上原本的长度（pcap_pkthdr::len）
};

// 枚举本机网卡，失败时返回 false，err 里是原因
bool ListDevices(std::vector<Device>& devices, std::wstring* err);

// 捕获器：Start 之后在后台线程里持续收包，每收到一个包回调一次 sink
class Capture {
public:
    using Sink = std::function<void(const Packet&)>;

    Capture() = default;
    ~Capture();

    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;

    void SetSink(Sink sink) { m_sink = std::move(sink); }

    bool Start(const std::string& deviceName, std::wstring* err);
    void Stop();
    bool IsRunning() const { return m_running.load(); }

    // 捕获线程出错时的原因（线程已经退出后读取）
    std::wstring LastError() const;

private:
    void Loop();

    pcap_t*           m_handle = nullptr;
    std::thread       m_thread;
    std::atomic<bool> m_running{false};
    Sink              m_sink;
    std::wstring      m_lastError;
};

}  // namespace capture
