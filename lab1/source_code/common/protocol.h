// =============================================================================
//  protocol.h  --  聊天协议 Ting/1.0 的编解码模块
//  字段转义：'\' -> "\\"、TAB -> "\t"、CR -> "\r"、LF -> "\n"
// =============================================================================
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace scp {

// ---- 协议常量 ---------------------------------------------------------------
constexpr const char* kVersion    = "1.0";            // 协议版本号
constexpr const char* kServerName = "Ting/1.0";       // 服务端标识
constexpr uint32_t    kMaxPayload = 1024u * 1024u;    // 单帧负载上限 1 MiB
constexpr size_t      kMaxNickBytes = 96;             // 昵称 UTF-8 字节上限
constexpr size_t      kMaxChatBytes = 4000;           // 单条聊天 UTF-8 字节上限

// ---- 消息类型 -------------------------
namespace m {
constexpr const char* kHello    = "HELLO";     // C -> S  登录
constexpr const char* kWelcome  = "WELCOME";   // S -> C  登录成功
constexpr const char* kJoin     = "JOIN";      // S -> C  有人加入
constexpr const char* kLeave    = "LEAVE";     // S -> C  有人离开
constexpr const char* kChat     = "CHAT";      // 双向    聊天正文
constexpr const char* kUserList = "USERLIST";  // S -> C  在线用户快照
constexpr const char* kSystem   = "SYSTEM";    // S -> C  系统公告
constexpr const char* kError    = "ERROR";     // S -> C  错误应答
constexpr const char* kPing     = "PING";      // S -> C  心跳请求
constexpr const char* kPong     = "PONG";      // C -> S  心跳应答
constexpr const char* kBye      = "BYE";       // C -> S  主动退出
}  // namespace m

// 一帧的逻辑表示：类型 + 若干字段（字段为未转义的 UTF-8 文本）
struct Frame {
    std::string              type;
    std::vector<std::string> fields;
};

// 编解码
// 字段转义 / 反转义
std::string EscapeField(const std::string& in);
bool        UnescapeField(const std::string& in, std::string& out);

// 逻辑帧 -> 负载（不含 4 字节帧头）；失败时 err 给出原因
bool BuildPayload(const Frame& f, std::string& payload, std::string* err);

// 逻辑帧 -> 完整线上字节（4 字节帧头 + 负载）
// 逻辑帧 -> 完整线上字节（4 字节帧头 + 负载）
bool BuildWire(const Frame& f, std::string& wire, std::string* err);

// 负载 -> 逻辑帧
bool ParsePayload(const std::string& payload, Frame& f, std::string* err);

// 负载中是否出现控制字符（用于校验昵称等输入）
bool HasControlChar(const std::string& s);

}  // namespace scp
