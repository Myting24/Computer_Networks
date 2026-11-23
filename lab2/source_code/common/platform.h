// =============================================================================
//  platform.h  --  统一的 Windows / Winsock 头文件入口
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
