// =============================================================================
//  net.h  --  基本 Socket 函数的使用封装
// =============================================================================
#pragma once

#include "platform.h"

#include <string>

namespace net {

// 进程级 Winsock 初始化 / 清理
bool Startup(std::string* err);
void Cleanup();

// 创建监听套接字
SOCKET Listen(const std::string& bindAddr, uint16_t port, std::string* err);

// 创建并连接套接字
SOCKET Connect(const std::string& host, uint16_t port, int timeoutMs, std::string* err);

// 循环发送直到 len 字节全部写完
bool SendAll(SOCKET s, const char* data, size_t len, std::string* err = nullptr);

// 接收 len 字节
bool RecvExact(SOCKET s, char* buf, size_t len, std::string* err = nullptr);

// 发送一帧：data 为已编码好的完整帧字节
bool SendFrame(SOCKET s, const std::string& wire, std::string* err = nullptr);

// 接收一帧：先读 4 字节帧头，再读负载
bool RecvFrame(SOCKET s, std::string& payload, std::string* err);

// 半关闭后关闭套接字
void CloseGraceful(SOCKET s);

bool SetRecvTimeout(SOCKET s, int ms);
bool SetSendTimeout(SOCKET s, int ms);
bool SetNoDelay(SOCKET s);

// 错误码到可读文本
std::string ErrorText(int code);

// 本线程最近一次 send/recv 失败的 Winsock 错误码（0 表示无错误），用于区分心跳超时 WSAETIMEDOUT与正常关闭
int LastErrorCode();

// "127.0.0.1:52344"
std::string PeerText(const sockaddr_in& addr);

}  // namespace net
