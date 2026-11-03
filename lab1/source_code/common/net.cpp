// =============================================================================
//  net.cpp  --  基本 Socket 函数
// =============================================================================
#include "net.h"
#include "protocol.h"

#include <cstdio>

namespace net {

namespace {

thread_local int g_lastError = 0;

std::string WsaErr(int code, const char* stage) {
    std::string text;
    if (stage != nullptr) {
        text = stage;
        text += "：";
    }
    text += ErrorText(code);
    return text;
}

}  // namespace

bool Startup(std::string* err) {
    WSADATA data{};
    const int ret = WSAStartup(MAKEWORD(2, 2), &data);
    if (ret != 0) {
        if (err != nullptr) {
            *err = WsaErr(ret, "WSAStartup 失败");
        }
        return false;
    }
    return true;
}

void Cleanup() { WSACleanup(); }

std::string ErrorText(int code) {
    // 用宽字符版本取系统错误描述，再统一转成 UTF-8，
    wchar_t buffer[512] = {0};
    const DWORD n = FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
        static_cast<DWORD>(code), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        buffer, static_cast<DWORD>(_countof(buffer) - 1), nullptr);
    std::wstring wide;
    if (n > 0) {
        wide.assign(buffer, n);
        while (!wide.empty() && (wide.back() == L'\r' || wide.back() == L'\n' ||
                                 wide.back() == L' ' || wide.back() == L'.')) {
            wide.pop_back();
        }
    } else {
        wide = L"未知错误";
    }
    char codeText[32] = {0};
    std::snprintf(codeText, sizeof(codeText), " (错误码 %d)", code);

    std::string utf8;
    if (!wide.empty()) {
        const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                                static_cast<int>(wide.size()),
                                                nullptr, 0, nullptr, nullptr);
        if (bytes > 0) {
            utf8.resize(static_cast<size_t>(bytes));
            ::WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                  static_cast<int>(wide.size()), &utf8[0], bytes,
                                  nullptr, nullptr);
        }
    }
    if (utf8.empty()) {
        utf8 = "未知错误";
    }
    utf8 += codeText;
    return utf8;
}

int LastErrorCode() { return g_lastError; }

SOCKET Listen(const std::string& bindAddr, uint16_t port, std::string* err) {
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        if (err != nullptr) *err = WsaErr(WSAGetLastError(), "创建套接字失败");
        return INVALID_SOCKET;
    }

    // 允许服务端停止后立即重新启动
    BOOL reuse = TRUE;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
                 sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    const std::string addrText = bindAddr.empty() ? "0.0.0.0" : bindAddr;
    if (::inet_pton(AF_INET, addrText.c_str(), &addr.sin_addr) != 1) {
        if (err != nullptr) *err = "监听地址格式非法：" + addrText;
        ::closesocket(s);
        return INVALID_SOCKET;
    }

    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        if (err != nullptr) *err = WsaErr(WSAGetLastError(), "bind 失败");
        ::closesocket(s);
        return INVALID_SOCKET;
    }

    if (::listen(s, SOMAXCONN) == SOCKET_ERROR) {
        if (err != nullptr) *err = WsaErr(WSAGetLastError(), "listen 失败");
        ::closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

SOCKET Connect(const std::string& host, uint16_t port, int timeoutMs, std::string* err) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char portText[16] = {0};
    std::snprintf(portText, sizeof(portText), "%u", static_cast<unsigned>(port));

    addrinfo* result = nullptr;
    const int gai = ::getaddrinfo(host.c_str(), portText, &hints, &result);
    if (gai != 0 || result == nullptr) {
        if (err != nullptr) *err = "无法解析服务器地址：" + host;
        return INVALID_SOCKET;
    }

    SOCKET s = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (s == INVALID_SOCKET) {
        if (err != nullptr) *err = WsaErr(WSAGetLastError(), "创建套接字失败");
        ::freeaddrinfo(result);
        return INVALID_SOCKET;
    }

    // 非阻塞 connect + select，保证连接失败时不会长时间卡住界面
    u_long nonBlocking = 1;
    ::ioctlsocket(s, FIONBIO, &nonBlocking);
    const int rc = ::connect(s, result->ai_addr, static_cast<int>(result->ai_addrlen));
    if (rc == SOCKET_ERROR) {
        const int code = WSAGetLastError();
        if (code != WSAEWOULDBLOCK && code != WSAEINPROGRESS) {
            if (err != nullptr) *err = WsaErr(code, "connect 失败");
            ::closesocket(s);
            ::freeaddrinfo(result);
            return INVALID_SOCKET;
        }
        fd_set writeSet;
        fd_set exceptSet;
        FD_ZERO(&writeSet);
        FD_ZERO(&exceptSet);
        FD_SET(s, &writeSet);
        FD_SET(s, &exceptSet);  // 连接被拒绝时 Windows 只在 except 集合里置位
        timeval tv{};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        const int sel = ::select(0, nullptr, &writeSet, &exceptSet, &tv);
        if (sel <= 0) {
            if (err != nullptr) {
                *err = (sel == 0) ? "连接超时：目标主机无响应" : "select 失败";
            }
            ::closesocket(s);
            ::freeaddrinfo(result);
            return INVALID_SOCKET;
        }
        int soError = 0;
        int len = static_cast<int>(sizeof(soError));
        ::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &len);
        if (soError != 0) {
            if (err != nullptr) *err = WsaErr(soError, "connect 失败");
            ::closesocket(s);
            ::freeaddrinfo(result);
            return INVALID_SOCKET;
        }
    }

    nonBlocking = 0;
    ::ioctlsocket(s, FIONBIO, &nonBlocking);
    ::freeaddrinfo(result);
    return s;
}

bool SendAll(SOCKET s, const char* data, size_t len, std::string* err) {
    g_lastError = 0;
    size_t sent = 0;
    while (sent < len) {
        const int n = ::send(s, data + sent, static_cast<int>(len - sent), 0);
        if (n == SOCKET_ERROR) {
            g_lastError = WSAGetLastError();
            if (err != nullptr) *err = WsaErr(g_lastError, "send 失败");
            return false;
        }
        if (n == 0) {
            if (err != nullptr) *err = "send 返回 0，连接已失效";
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool RecvExact(SOCKET s, char* buf, size_t len, std::string* err) {
    g_lastError = 0;
    size_t got = 0;
    while (got < len) {
        const int n = ::recv(s, buf + got, static_cast<int>(len - got), 0);
        if (n == SOCKET_ERROR) {
            g_lastError = WSAGetLastError();
            if (err != nullptr) *err = WsaErr(g_lastError, "recv 失败");
            return false;
        }
        if (n == 0) {
            if (err != nullptr) *err = (got == 0) ? "对端已关闭连接" : "连接在传输中途被关闭";
            return false;
        }
        got += static_cast<size_t>(n);
    }
    return true;
}

bool SendFrame(SOCKET s, const std::string& wire, std::string* err) {
    return SendAll(s, wire.data(), wire.size(), err);
}

bool RecvFrame(SOCKET s, std::string& payload, std::string* err) {
    unsigned char header[4] = {0};
    if (!RecvExact(s, reinterpret_cast<char*>(header), sizeof(header), err)) {
        return false;
    }
    const uint32_t len = (static_cast<uint32_t>(header[0]) << 24) |
                         (static_cast<uint32_t>(header[1]) << 16) |
                         (static_cast<uint32_t>(header[2]) << 8) |
                         static_cast<uint32_t>(header[3]);
    if (len == 0 || len > scp::kMaxPayload) {
        if (err != nullptr) *err = "帧长度非法：" + std::to_string(len);
        return false;
    }
    std::string buffer(len, '\0');
    if (!RecvExact(s, &buffer[0], len, err)) {
        return false;
    }
    payload.swap(buffer);
    return true;
}

void CloseGraceful(SOCKET s) {
    if (s == INVALID_SOCKET) {
        return;
    }
    ::shutdown(s, SD_BOTH);
    ::closesocket(s);
}

bool SetRecvTimeout(SOCKET s, int ms) {
    DWORD timeout = static_cast<DWORD>(ms);
    return ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                        reinterpret_cast<const char*>(&timeout), sizeof(timeout)) != SOCKET_ERROR;
}

bool SetSendTimeout(SOCKET s, int ms) {
    DWORD timeout = static_cast<DWORD>(ms);
    return ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO,
                        reinterpret_cast<const char*>(&timeout), sizeof(timeout)) != SOCKET_ERROR;
}

bool SetNoDelay(SOCKET s) {
    BOOL enable = TRUE;
    return ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
                        reinterpret_cast<const char*>(&enable), sizeof(enable)) != SOCKET_ERROR;
}

std::string PeerText(const sockaddr_in& addr) {
    char ip[INET_ADDRSTRLEN] = {0};
    ::inet_ntop(AF_INET, const_cast<in_addr*>(&addr.sin_addr), ip, sizeof(ip));
    char text[64] = {0};
    std::snprintf(text, sizeof(text), "%s:%u", ip, static_cast<unsigned>(ntohs(addr.sin_port)));
    return text;
}

}  // namespace net
