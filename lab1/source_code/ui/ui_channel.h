// =============================================================================
//  ui_channel.h  --  工作线程 -> 界面线程 的事件通道
//
//  Win32 的窗口只能在创建它的线程中操作。因此网络线程不直接改控件，
//  而是把事件塞进线程安全队列，再用 PostMessage 唤醒界面线程统一处理。
// =============================================================================
#pragma once

#include "../common/platform.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace common {

constexpr UINT WM_APP_UI_EVENT = WM_APP + 1;

struct UiEvent {
    enum class Kind {
        ChatLine,    // 追加一行到聊天区
        LogLine,     // 追加一行到日志区
        UserList,    // 刷新在线用户列表
        State,       // 连接状态变化
        Notice,      // 简短提示（显示在状态栏）
    };

    Kind                     kind = Kind::ChatLine;
    std::wstring             text;
    std::vector<std::wstring> items;
};

class UiChannel {
public:
    void Attach(HWND hwnd) { m_hwnd.store(hwnd); }

    void Post(UiEvent event) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_events.push_back(std::move(event));
        }
        const HWND hwnd = m_hwnd.load();
        if (hwnd != nullptr) {
            ::PostMessageW(hwnd, WM_APP_UI_EVENT, 0, 0);
        }
    }

    // 取出当前所有事件（由界面线程调用）
    void Drain(std::vector<UiEvent>& out) {
        std::lock_guard<std::mutex> lock(m_mutex);
        out.swap(m_events);
    }

private:
    std::mutex               m_mutex;
    std::vector<UiEvent>     m_events;
    std::atomic<HWND>        m_hwnd{nullptr};
};

}  // namespace common
