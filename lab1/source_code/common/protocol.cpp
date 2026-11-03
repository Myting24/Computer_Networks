// =============================================================================
//  protocol.cpp  --  聊天协议 Ting/1.0 的编解码实现
// =============================================================================
#include "protocol.h"

#include <cstdio>

namespace scp {

namespace {

void SetErr(std::string* err, const char* text) {
    if (err != nullptr) {
        *err = text;
    }
}

bool IsUpperType(const std::string& type) {
    if (type.empty() || type.size() > 32) {
        return false;
    }
    for (char c : type) {
        if (c < 'A' || c > 'Z') {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string EscapeField(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            case '\n': out += "\\n";  break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

bool UnescapeField(const std::string& in, std::string& out) {
    out.clear();
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (++i >= in.size()) {
            return false;  // 末尾出现孤立的转义符
        }
        switch (in[i]) {
            case '\\': out.push_back('\\'); break;
            case 't':  out.push_back('\t'); break;
            case 'r':  out.push_back('\r'); break;
            case 'n':  out.push_back('\n'); break;
            default:   return false;        // 非法转义序列
        }
    }
    return true;
}

bool BuildPayload(const Frame& f, std::string& payload, std::string* err) {
    if (!IsUpperType(f.type)) {
        SetErr(err, "消息类型非法（必须为 1~32 个大写字母）");
        return false;
    }
    std::string out = f.type;
    for (const std::string& field : f.fields) {
        out.push_back('\t');
        out += EscapeField(field);
    }
    if (out.size() > kMaxPayload) {
        SetErr(err, "消息长度超过单帧上限");
        return false;
    }
    payload.swap(out);
    return true;
}

bool BuildWire(const Frame& f, std::string& wire, std::string* err) {
    std::string payload;
    if (!BuildPayload(f, payload, err)) {
        return false;
    }
    const uint32_t len = static_cast<uint32_t>(payload.size());
    std::string out;
    out.reserve(4 + payload.size());
    out.push_back(static_cast<char>((len >> 24) & 0xFF));  // 大端帧头
    out.push_back(static_cast<char>((len >> 16) & 0xFF));
    out.push_back(static_cast<char>((len >> 8) & 0xFF));
    out.push_back(static_cast<char>(len & 0xFF));
    out += payload;
    wire.swap(out);
    return true;
}

bool ParsePayload(const std::string& payload, Frame& f, std::string* err) {
    f.type.clear();
    f.fields.clear();
    if (payload.empty()) {
        SetErr(err, "负载为空");
        return false;
    }
    const size_t firstTab = payload.find('\t');
    const std::string head =
        (firstTab == std::string::npos) ? payload : payload.substr(0, firstTab);
    if (!IsUpperType(head)) {
        SetErr(err, "消息类型非法");
        return false;
    }
    f.type = head;

    size_t pos = firstTab;
    while (pos != std::string::npos) {
        const size_t next = payload.find('\t', pos + 1);
        const std::string raw = (next == std::string::npos)
                                    ? payload.substr(pos + 1)
                                    : payload.substr(pos + 1, next - pos - 1);
        std::string value;
        if (!UnescapeField(raw, value)) {
            SetErr(err, "字段转义非法");
            return false;
        }
        f.fields.push_back(std::move(value));
        pos = next;
    }
    return true;
}

bool HasControlChar(const std::string& s) {
    for (unsigned char c : s) {
        if (c < 0x20 || c == 0x7F) {
            return true;
        }
    }
    return false;
}

}  // namespace scp
