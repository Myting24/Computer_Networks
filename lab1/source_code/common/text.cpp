// =============================================================================
//  text.cpp  --  字符编码与时间格式化实现
// =============================================================================
#include "text.h"

#include <cstdarg>
#include <cwchar>
#include <random>

namespace text {

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) {
        return std::wstring();
    }
    int n = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                  static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) {
        // 含非法字节时退化为宽松转换，保证界面能显示而不是整条消息丢弃
        n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                  nullptr, 0);
    }
    if (n <= 0) {
        return std::wstring();
    }
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) {
        return std::string();
    }
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                        nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
        return std::string();
    }
    std::string out(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n,
                          nullptr, nullptr);
    return out;
}

std::wstring NowDateTime() {
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    return Format(L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond);
}

std::wstring NowTime() {
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    return Format(L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
}

std::wstring ShortTime(const std::wstring& dateTime) {
    // 期望形如 "2026-10-20 18:30:05"，时间部分从第 11 个字符开始
    if (dateTime.size() >= 19 && dateTime[10] == L' ') {
        return dateTime.substr(11, 8);
    }
    return dateTime;
}

std::wstring Trim(const std::wstring& s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && (s[begin] == L' ' || s[begin] == L'\t' || s[begin] == L'\r' ||
                           s[begin] == L'\n' || s[begin] == L'\u3000')) {
        ++begin;
    }
    while (end > begin && (s[end - 1] == L' ' || s[end - 1] == L'\t' ||
                           s[end - 1] == L'\r' || s[end - 1] == L'\n' ||
                           s[end - 1] == L'\u3000')) {
        --end;
    }
    return s.substr(begin, end - begin);
}

std::wstring ToLowerAscii(const std::wstring& s) {
    std::wstring out = s;
    for (wchar_t& c : out) {
        if (c >= L'A' && c <= L'Z') {
            c = static_cast<wchar_t>(c - L'A' + L'a');
        }
    }
    return out;
}

std::wstring RandomNick() {
    static std::mt19937 rng(static_cast<unsigned>(::GetTickCount64() ^
                                                  (::GetCurrentProcessId() << 16)));
    std::uniform_int_distribution<int> dist(1000, 9999);
    return Format(L"访客%d", dist(rng));
}

std::wstring Format(const wchar_t* fmt, ...) {
    wchar_t buffer[1024] = {0};
    va_list args;
    va_start(args, fmt);
    const int n = _vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, fmt, args);
    va_end(args);
    if (n < 0) {
        return std::wstring(buffer);  // 被截断时的部分内容
    }
    return std::wstring(buffer, static_cast<size_t>(n));
}

int DisplayWidth(const std::wstring& s) {
    int width = 0;
    for (wchar_t c : s) {
        if (c < 0x80) {
            ++width;
        } else if (c >= 0x1100) {
            width += 2;  // 中日韩字符按双宽估算
        } else {
            ++width;
        }
    }
    return width;
}

}  // namespace text
