// =============================================================================
//  console_main.cpp  --  数据包捕获（命令行交互与结果显示）
// =============================================================================
#include "capture/capture.h"
#include "common/platform.h"
#include "common/text.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};

// 通用的类型字段名称提示
const wchar_t* TypeName(unsigned int type) {
    switch (type) {
        case 0x0800: return L"IPv4";
        case 0x0806: return L"ARP";
        case 0x86DD: return L"IPv6";
        case 0x8100: return L"VLAN";
        default:     return L"其他";
    }
}

// 把一整行写到标准输出；控制台已设为 UTF-8，重定向到文件也是 UTF-8
void Out(const std::wstring& line) {
    const std::string utf8 = text::WideToUtf8(line);
    std::fwrite(utf8.data(), 1, utf8.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

// Ctrl+C 关闭窗口时把标志置位，主循环会很快退出
BOOL WINAPI ConsoleHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        g_stop.store(true);
        return TRUE;      // 告诉系统已处理
    }
    return FALSE;
}

void PrintUsage() {
    Out(L"用法：");
    Out(L"  CaptureConsole.exe            列出网卡并按提示选择，开始捕获");
    Out(L"  CaptureConsole.exe -l         只列出网卡");
    Out(L"  CaptureConsole.exe -i 序号    直接捕获指定序号的网卡");
    Out(L"  CaptureConsole.exe -t 秒数    捕获指定秒数后自动暂停");
    Out(L"");
    Out(L"捕获中：按 Ctrl+C 暂停（结果留在屏幕上）");
    Out(L"暂停后：按回车退出，输入 c 再抓一轮");
}

// 把一帧格式化成几行：第一行是序号和时间，下面每行一个字段
std::wstring FormatPacket(const capture::Packet& packet) {
    const std::wstring firstByte = (packet.firstByte >= 0)
                                       ? text::Format(L"0x%02X", packet.firstByte)
                                       : std::wstring(L"----");
    const std::wstring typeText =
        text::Format(L"0x%04X (%s)", packet.type, TypeName(packet.type));

    std::wstring out;
    out += text::Format(L"#%06llu  %s", packet.seq, packet.time.c_str());
    out += L"\n   源MAC地址：";
    out += packet.srcMac;
    out += L"\n   目的MAC地址：";
    out += packet.dstMac;
    out += L"\n   类型字段的值：";
    out += typeText;
    out += L"\n   类型字段之后第一个字节的值：";
    out += firstByte;
    out += text::Format(L"\n   帧长：%u 字节", packet.wireLen);
    out += L"\n";     
    return out;
}

// 捕获一轮，直到用户按 Ctrl+C 或到达指定时长，返回 false 表示打开网卡失败
bool CaptureOnce(const capture::Device& device, int durationSeconds,
                 unsigned long long* outCount) {
    capture::Capture capturer;
    std::mutex queueMutex;
    std::vector<std::wstring> queue;

    capturer.SetSink([&](const capture::Packet& packet) {
        std::lock_guard<std::mutex> lock(queueMutex);
        queue.push_back(FormatPacket(packet));
    });

    std::wstring error;
    if (!capturer.Start(device.name, &error)) {
        Out(L"【错误】" + error);
        return false;
    }

    g_stop.store(false);
    if (durationSeconds > 0) {
        Out(text::Format(L"开始捕获（%d 秒后自动暂停，也可以按 Ctrl+C 提前暂停）…", durationSeconds));
    } else {
        Out(L"开始捕获（按 Ctrl+C 暂停）…");
    }
    Out(L"");

    unsigned long long printed = 0;
    const ULONGLONG startTick = ::GetTickCount64();

    auto emit = [&](const std::wstring& block) {
        Out(block);
        ++printed;
    };

    while (!g_stop.load()) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            for (const std::wstring& line : queue) {
                emit(line);
            }
            queue.clear();
        }

        if (durationSeconds > 0 &&
            (::GetTickCount64() - startTick) >= static_cast<ULONGLONG>(durationSeconds) * 1000ULL) {
            break;
        }
        ::Sleep(100);
    }

    capturer.Stop();

    // 把停止前最后几行也打出来
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        for (const std::wstring& line : queue) {
            emit(line);
        }
        queue.clear();
    }

    const std::wstring lastError = capturer.LastError();
    if (!lastError.empty()) {
        Out(L"");
        Out(L"【错误】捕获过程中出错：" + lastError);
    }

    Out(std::wstring(114, L'-'));
    Out(text::Format(L"已暂停，本次共捕获 %llu 个数据包。", printed));
    if (outCount != nullptr) {
        *outCount = printed;
    }
    return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    // wpcap.dll 采用延迟加载，第一次调用前先把 Npcap 的目录加入 DLL 搜索路径
    wchar_t systemDirectory[MAX_PATH] = {0};
    if (::GetSystemDirectoryW(systemDirectory, MAX_PATH) > 0) {
        const std::wstring npcapDirectory = std::wstring(systemDirectory) + L"\\Npcap";
        ::SetDllDirectoryW(npcapDirectory.c_str());
    }

    int  deviceIndex = -1;      // -1 表示需要用户选择
    int  durationSeconds = 0;   // 0 表示一直抓到按键为止
    bool listOnly = false;

    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"-l") == 0 || std::wcscmp(argv[i], L"--list") == 0) {
            listOnly = true;
        } else if (std::wcscmp(argv[i], L"-i") == 0 && i + 1 < argc) {
            deviceIndex = _wtoi(argv[++i]);
        } else if (std::wcscmp(argv[i], L"-t") == 0 && i + 1 < argc) {
            durationSeconds = _wtoi(argv[++i]);
        } else if (std::wcscmp(argv[i], L"-h") == 0 || std::wcscmp(argv[i], L"--help") == 0) {
            PrintUsage();
            return 0;
        } else {
            Out(L"无法识别的参数，使用 -h 查看用法");
            return 1;
        }
    }

    const std::wstring bannerTitle = L" 数据包捕获程序";
    const std::wstring bannerDesc =
        L" 每个数据包显示：源MAC地址、目的MAC地址、类型字段的值、"
        L"类型字段之后第一个字节的值，以及该帧的长度";
    const int bannerWidth = text::DisplayWidth(bannerDesc);

    Out(std::wstring(static_cast<size_t>(bannerWidth), L'='));
    Out(bannerTitle);
    Out(bannerDesc);
    Out(std::wstring(static_cast<size_t>(bannerWidth), L'='));
    Out(L"");

    // 1. 获取设备列表
    std::vector<capture::Device> devices;
    std::wstring error;
    if (!capture::ListDevices(devices, &error)) {
        Out(L"【错误】" + error);
        Out(L"");
        Out(L"按回车键退出 …");
        std::cin.get();
        return 1;
    }

    Out(text::Format(L"本机共有 %d 块网络接口：", static_cast<int>(devices.size())));
    for (size_t i = 0; i < devices.size(); ++i) {
        Out(text::Format(L"  [%d] %s", static_cast<int>(i), devices[i].display.c_str()));
    }
    Out(L"");

    if (listOnly) {
        Out(L"按回车键退出 …");
        std::cin.get();
        return 0;
    }

    // 2. 选择网卡
    if (deviceIndex < 0) {
        Out(L"请输入要捕获的网卡序号（直接回车则选第 0 块）：");

        std::string input;
        std::getline(std::cin, input);
        deviceIndex = input.empty() ? 0 : std::atoi(input.c_str());
    }
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(devices.size())) {
        Out(L"【错误】网卡序号超出范围");
        Out(L"");
        Out(L"按回车键退出 …");
        std::cin.get();
        return 1;
    }

    const capture::Device& device = devices[deviceIndex];
    Out(L"选中：" + device.display);
    Out(L"");

    // 3. 捕获 -> 暂停 -> 可选择继续
    while (true) {
        unsigned long long count = 0;
        if (!CaptureOnce(device, durationSeconds, &count)) {
            Out(L"");
            Out(L"按回车键退出 …");
            std::cin.get();
            return 1;
        }

        Out(L"");
        Out(L"按回车键退出；输入 c 再抓一轮：");
        ::FlushConsoleInputBuffer(::GetStdHandle(STD_INPUT_HANDLE));

        std::string answer;
        if (!std::getline(std::cin, answer)) {
            break;                      // 输入流结束，直接退出
        }
        if (answer != "c" && answer != "C") {
            break;
        }
        Out(L"");
        Out(L"--------------------------------------------------------------");
        Out(L"");
    }

    return 0;
}
