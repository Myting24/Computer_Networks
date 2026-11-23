// =============================================================================
//  capture.cpp  --  网卡枚举、打开网卡、循环收包、解析以太网帧
// =============================================================================
#include "capture.h"

#include "../common/text.h"

#include <cstdio>

namespace capture {

namespace {

// 打开网卡时一次最多抓多少字节
constexpr int kSnapLen = 65536;

// pcap_next_ex 的读取超时（毫秒）：超时返回 0，用来定期检查是否该停止
constexpr int kReadTimeoutMs = 500;

// 把 6 字节 MAC 变成 "AA-BB-CC-DD-EE-FF"
std::wstring MacToString(const unsigned char* mac) {
    wchar_t buffer[32] = {0};
    _snwprintf_s(buffer, _TRUNCATE, L"%02X-%02X-%02X-%02X-%02X-%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::wstring(buffer);
}

// timeval -> "时:分:秒.毫秒"
std::wstring TimeToString(const struct timeval& ts) {
    struct tm localTime{};
    time_t seconds = static_cast<time_t>(ts.tv_sec);
    localtime_s(&localTime, &seconds);
    wchar_t buffer[32] = {0};
    _snwprintf_s(buffer, _TRUNCATE, L"%02d:%02d:%02d.%03d",
                 localTime.tm_hour, localTime.tm_min, localTime.tm_sec,
                 static_cast<int>(ts.tv_usec / 1000));
    return std::wstring(buffer);
}

}  // namespace

bool ListDevices(std::vector<Device>& devices, std::wstring* err) {
    devices.clear();

    char errbuf[PCAP_ERRBUF_SIZE] = {0};
    pcap_if_t* all = nullptr;
    // PCAP_SRC_IF_STRING 即 "rpcap://"，表示本机的接口设备
    if (pcap_findalldevs_ex(PCAP_SRC_IF_STRING, nullptr, &all, errbuf) != 0) {
        if (err != nullptr) {
            *err = text::Utf8ToWide(std::string("获取网卡列表失败：") + errbuf);
        }
        return false;
    }

    for (pcap_if_t* d = all; d != nullptr; d = d->next) {
        if (d->name == nullptr) {
            continue;
        }
        Device device;
        device.name = d->name;
        device.description = (d->description != nullptr) ? d->description : "";
        // 列表里优先显示描述，没有描述就显示设备名
        device.display = device.description.empty()
                             ? text::Utf8ToWide(device.name)
                             : text::Utf8ToWide(device.description);
        devices.push_back(std::move(device));
    }

    pcap_freealldevs(all);

    if (devices.empty()) {
        if (err != nullptr) {
            *err = L"没有找到任何网络接口";
        }
        return false;
    }
    return true;
}

Capture::~Capture() {
    Stop();
}

bool Capture::Start(const std::string& deviceName, std::wstring* err) {
    if (m_running.load()) {
        if (err != nullptr) {
            *err = L"已经在捕获中";
        }
        return false;
    }

    char errbuf[PCAP_ERRBUF_SIZE] = {0};
    // 参数：设备名、抓取长度、混杂模式、读取超时、认证（本地为空）、错误缓冲区
    pcap_t* handle = pcap_open(deviceName.c_str(), kSnapLen, PCAP_OPENFLAG_PROMISCUOUS,
                               kReadTimeoutMs, nullptr, errbuf);
    if (handle == nullptr) {
        if (err != nullptr) {
            std::wstring message = text::Utf8ToWide(std::string("打开网卡失败：") + errbuf);
            message += L"\n（捕获真实网卡需要管理员权限，请右键程序选择“以管理员身份运行”）";
            *err = message;
        }
        return false;
    }

    // 本程序按以太网帧解析 MAC 地址，因此只支持以太网封装的链路
    if (pcap_datalink(handle) != DLT_EN10MB) {
        pcap_close(handle);
        if (err != nullptr) {
            *err = L"该网卡的链路类型不是以太网（DLT_EN10MB），无法按以太网帧解析";
        }
        return false;
    }

    m_handle = handle;
    m_lastError.clear();
    m_running.store(true);
    m_thread = std::thread([this]() { Loop(); });
    return true;
}

void Capture::Stop() {
    if (!m_running.exchange(false)) {
        // 即使没有在运行，也把残留的句柄收掉
        if (m_thread.joinable()) {
            m_thread.join();
        }
        return;
    }
    if (m_thread.joinable()) {
        m_thread.join();     // 线程会在 pcap_next_ex 超时后看到标志位并退出
    }
    if (m_handle != nullptr) {
        pcap_close(m_handle);
        m_handle = nullptr;
    }
}

std::wstring Capture::LastError() const {
    return m_lastError;
}

void Capture::Loop() {
    unsigned long long seq = 0;

    while (m_running.load()) {
        pcap_pkthdr* header = nullptr;
        const u_char* data = nullptr;

        const int result = pcap_next_ex(m_handle, &header, &data);
        if (result == 0) {
            continue;                 // 读取超时，没有数据，回去检查是否要停止
        }
        if (result == -2) {
            break;                    // 读到文件尾
        }
        if (result < 0) {
            // 出错：记下原因，结束循环
            const char* message = pcap_geterr(m_handle);
            m_lastError = text::Utf8ToWide(message != nullptr ? message : "捕获出错");
            break;
        }

        // 一帧以太网数据的结构：[0..5] 目的 MAC  [6..11] 源 MAC  [12..13] 类型  [14..] 数据
        if (header == nullptr || header->caplen < 14) {
            continue;                 // 连帧头都不完整，跳过
        }

        Packet packet;
        packet.seq = ++seq;
        packet.time = TimeToString(header->ts);
        packet.dstMac = MacToString(data + 0);
        packet.srcMac = MacToString(data + 6);
        packet.type = (static_cast<unsigned int>(data[12]) << 8) | data[13];
        packet.firstByte = (header->caplen >= 15) ? static_cast<int>(data[14]) : -1;
        packet.wireLen = header->len;

        if (m_sink) {
            m_sink(packet);
        }
    }

    m_running.store(false);
}

}  // namespace capture
