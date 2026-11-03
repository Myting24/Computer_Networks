// =============================================================================
//  text.h  --  字符编码与时间格式化工具
//  程序内部一律使用 UTF-16（Windows 宽字符），网络上一律使用 UTF-8，这一层负责两者之间的转换。
// =============================================================================
#pragma once

#include "platform.h"

#include <string>
#include <vector>

namespace text {

std::wstring Utf8ToWide(const std::string& s);
std::string  WideToUtf8(const std::wstring& s);

// "2026-10-20 18:30:05"
std::wstring NowDateTime();
// "18:30:05"
std::wstring NowTime();

// 从 "2026-10-20 18:30:05" 中截取 "18:30:05"，格式不符时原样返回
std::wstring ShortTime(const std::wstring& dateTime);

std::wstring Trim(const std::wstring& s);
std::wstring ToLowerAscii(const std::wstring& s);

// 生成默认昵称
std::wstring RandomNick();

// 简单格式化（内部用 vswprintf）
std::wstring Format(const wchar_t* fmt, ...);

// 由 UTF-8 字符串长度估算其显示宽度（中文按 2 个宽度计算）
int DisplayWidth(const std::wstring& s);

}  // namespace text
