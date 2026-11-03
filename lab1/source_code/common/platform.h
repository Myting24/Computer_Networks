// =============================================================================
//  platform.h  --  统一的 Windows / Winsock 头文件入口
//  整个工程（服务端、客户端、测试工具）都先包含本文件，保证<WinSock2.h> 一定在 <windows.h> 之前被包含，避免 winsock.h 与 winsock2.h 的宏/结构体重复定义冲突。 
// =============================================================================
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <WinSock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstring>
