// =============================================================================
//  proto_test.cpp  --  丢包自测工具（命令行）
//
//  用途：连接服务端后连续发送 N 条带序号的消息，再统计：
//    - 自己发出的 N 条消息回来了几条（回显条数）
//    - 服务端广播的全局序号有没有跳变
//  用数据说明「消息有没有丢失」，对应实验要求里的「观察是否有数据包的丢失」。
//
//  用法：
//    ChatTest.exe --name 测试A --count 200
//    可选：--host <地址> --port <端口> --quiet
//
//  注意：它是客户端，运行前必须先启动 Server.exe。
// =============================================================================
#include "../common/net.h"
#include "../common/platform.h"
#include "../common/protocol.h"
#include "../common/text.h"

#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace {

struct Options {
    std::wstring host = L"127.0.0.1";
    uint16_t     port = 8888;
    std::wstring name = L"自动测试";
    int          count = 200;
    bool         quiet = false;
};

void Out(const std::wstring& text) {
    const std::string utf8 = text::WideToUtf8(text);
    std::fwrite(utf8.data(), 1, utf8.size(), stdout);
    std::fflush(stdout);
}

void OutLine(const std::wstring& text) { Out(text + L"\n"); }

bool SendFrame(SOCKET sock, const std::string& type,
               const std::vector<std::string>& fields) {
    scp::Frame frame;
    frame.type = type;
    frame.fields = fields;
    std::string wire;
    std::string error;
    if (!scp::BuildWire(frame, wire, &error)) {
        return false;
    }
    return net::SendFrame(sock, wire, &error);
}

bool ParseOptions(int argc, wchar_t** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        auto next = [&](std::wstring& target) {
            if (i + 1 < argc) {
                target = argv[++i];
            }
        };
        if (arg == L"--host") {
            next(options.host);
        } else if (arg == L"--port") {
            std::wstring value;
            next(value);
            options.port = static_cast<uint16_t>(_wtoi(value.c_str()));
        } else if (arg == L"--name") {
            next(options.name);
        } else if (arg == L"--count") {
            std::wstring value;
            next(value);
            options.count = _wtoi(value.c_str());
            if (options.count <= 0) {
                options.count = 1;
            }
        } else if (arg == L"--quiet") {
            options.quiet = true;
        } else {
            // 不认识的参数（包括已经删掉的 --listen）一律打印用法，避免误解
            return false;
        }
    }
    return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleTitleW(L"聊天室 · 丢包自测工具");

    Options options;
    if (!ParseOptions(argc, argv, options)) {
        OutLine(L"用法：ChatTest.exe [--host 127.0.0.1] [--port 8888] "
                L"[--name 昵称] [--count 200] [--quiet]");
        return 2;
    }

    std::string error;
    if (!net::Startup(&error)) {
        OutLine(L"Winsock 初始化失败：" + text::Utf8ToWide(error));
        return 1;
    }

    SOCKET sock = net::Connect(text::WideToUtf8(options.host), options.port, 5000, &error);
    if (sock == INVALID_SOCKET) {
        OutLine(L"连接服务器失败：" + text::Utf8ToWide(error));
        // 10061 = 该端口上没有程序在监听，最常见的原因就是服务端没启动
        if (error.find("10061") != std::string::npos) {
            OutLine(L"提示：这个端口上没有服务端在监听。请先启动 Server.exe，"
                    L"并确认它底部状态栏显示「正在监听 0.0.0.0:8888」；"
                    L"如果服务端换了端口，这里要用 --port 指定同一个端口。");
        }
        net::Cleanup();
        return 1;
    }
    net::SetNoDelay(sock);

    if (!SendFrame(sock, scp::m::kHello,
                   {scp::kVersion, text::WideToUtf8(options.name)})) {
        OutLine(L"发送 HELLO 失败");
        net::CloseGraceful(sock);
        net::Cleanup();
        return 1;
    }

    // ---- 等待 WELCOME，记录服务端分配的昵称与 id ----
    unsigned long long myId = 0;
    std::wstring assignedNick = options.name;
    bool welcomed = false;
    unsigned long long expectedSeq = 0;
    unsigned long long received = 0;
    unsigned long long gaps = 0;

    while (!welcomed) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(sock, &readSet);
        timeval timeout{5, 0};
        if (::select(0, &readSet, nullptr, nullptr, &timeout) <= 0) {
            OutLine(L"登录超时：服务端没有返回 WELCOME");
            net::CloseGraceful(sock);
            net::Cleanup();
            return 1;
        }
        std::string payload;
        if (!net::RecvFrame(sock, payload, &error)) {
            OutLine(L"接收 WELCOME 失败：" + text::Utf8ToWide(error));
            net::CloseGraceful(sock);
            net::Cleanup();
            return 1;
        }
        scp::Frame frame;
        if (!scp::ParsePayload(payload, frame, &error)) {
            continue;
        }
        if (frame.type == scp::m::kWelcome) {
            welcomed = true;
            if (frame.fields.size() > 4) {
                assignedNick = text::Utf8ToWide(frame.fields[4]);
            }
            if (frame.fields.size() > 3) {
                try {
                    myId = std::stoull(frame.fields[3]);
                } catch (...) {
                    myId = 0;
                }
            }
            if (!options.quiet) {
                OutLine(L"登录成功：服务端分配的昵称 = " + assignedNick +
                        L"，客户端编号 = " + std::to_wstring(myId));
            }
        } else if (frame.type == scp::m::kError) {
            OutLine(L"服务端返回错误：" + text::Utf8ToWide(
                                            frame.fields.size() > 1 ? frame.fields[1]
                                                                    : std::string()));
            net::CloseGraceful(sock);
            net::Cleanup();
            return 1;
        }
    }

    // ---- 发送 count 条消息，统计回显 ----
    const int total = options.count;
    for (int i = 1; i <= total; ++i) {
        const std::wstring content = text::Format(
            L"顺序测试 #%d／%d  中文 English mixed 载荷", i, total);
        if (!SendFrame(sock, scp::m::kChat,
                       {std::to_string(i), text::WideToUtf8(content)})) {
            OutLine(L"第 " + std::to_wstring(i) + L" 条发送失败");
            net::CloseGraceful(sock);
            net::Cleanup();
            return 1;
        }
        if (!options.quiet && (i % 50 == 0)) {
            Out(L".");
        }
    }
    if (!options.quiet) {
        OutLine(L"");
        OutLine(L"已发送 " + std::to_wstring(total) + L" 条消息，等待回显……");
    }

    // ---- 收集回显：直到收满 total 条或 15 秒内没有新消息 ----
    unsigned long long echoes = 0;
    unsigned long long lastProgress = ::GetTickCount64();
    while (echoes < static_cast<unsigned long long>(total)) {
        if (::GetTickCount64() - lastProgress > 15000) {
            break;
        }
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(sock, &readSet);
        timeval timeout{0, 200000};
        if (::select(0, &readSet, nullptr, nullptr, &timeout) <= 0) {
            continue;
        }
        std::string payload;
        if (!net::RecvFrame(sock, payload, &error)) {
            OutLine(L"接收中断：" + text::Utf8ToWide(error));
            break;
        }
        lastProgress = ::GetTickCount64();

        scp::Frame frame;
        if (!scp::ParsePayload(payload, frame, &error)) {
            continue;
        }
        if (frame.type == scp::m::kPing) {
            SendFrame(sock, scp::m::kPong,
                      {frame.fields.empty() ? std::string() : frame.fields[0]});
            continue;
        }
        if (frame.type == scp::m::kChat || frame.type == scp::m::kJoin ||
            frame.type == scp::m::kLeave || frame.type == scp::m::kSystem) {
            if (!frame.fields.empty()) {
                try {
                    const unsigned long long seq = std::stoull(frame.fields[0]);
                    if (expectedSeq != 0 && seq != expectedSeq) {
                        ++gaps;
                        if (!options.quiet) {
                            OutLine(text::Format(
                                L"【警告】序号不连续：期望 %llu，收到 %llu",
                                expectedSeq, seq));
                        }
                    }
                    expectedSeq = seq + 1;
                    ++received;
                } catch (...) {
                }
            }
        }
        if (frame.type == scp::m::kChat && frame.fields.size() > 2) {
            unsigned long long senderId = 0;
            try {
                senderId = std::stoull(frame.fields[2]);
            } catch (...) {
                senderId = 0;
            }
            if (senderId == myId) {
                ++echoes;
                if (!options.quiet && (echoes % 50 == 0)) {
                    Out(L"#");
                }
            }
        }
    }
    if (!options.quiet) {
        OutLine(L"");
    }

    SendFrame(sock, scp::m::kBye, {});
    ::Sleep(100);
    net::CloseGraceful(sock);
    net::Cleanup();

    const unsigned long long missing = static_cast<unsigned long long>(total) - echoes;
    OutLine(L"----------------------------------------------------------");
    OutLine(L"发送消息        ：" + std::to_wstring(total) + L" 条");
    OutLine(L"收到自身回显    ：" + std::to_wstring(echoes) + L" 条");
    OutLine(L"缺失            ：" + std::to_wstring(missing) + L" 条");
    OutLine(L"收到广播帧总数  ：" + std::to_wstring(received) + L" 帧");
    OutLine(L"全局序号跳跃    ：" + std::to_wstring(gaps) + L" 次");
    OutLine(L"结论            ：" +
            std::wstring((missing == 0 && gaps == 0) ? L"通过，未发现消息丢失"
                                                     : L"未通过，请检查"));
    // 便于脚本自动判定的纯 ASCII 摘要行
    OutLine(L"SUMMARY sent=" + std::to_wstring(total) +
            L" echoes=" + std::to_wstring(echoes) +
            L" missing=" + std::to_wstring(missing) +
            L" frames=" + std::to_wstring(received) +
            L" gaps=" + std::to_wstring(gaps) +
            L" result=" + std::wstring((missing == 0 && gaps == 0) ? L"PASS" : L"FAIL"));
    OutLine(L"----------------------------------------------------------");
    return (missing == 0 && gaps == 0) ? 0 : 1;
}
