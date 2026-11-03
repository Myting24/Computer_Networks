// =============================================================================
//  main.cpp  --  服务端图形界面（Win32，无第三方界面库）
// =============================================================================
#include "../common/platform.h"
#include "../common/logger.h"
#include "../common/text.h"
#include "../ui/theme.h"
#include "../ui/ui_channel.h"
#include "server.h"

#include <string>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ws2_32.lib")

namespace {

constexpr UINT WM_APP_AUTOSTART = WM_APP + 2;

enum ControlId : int {
    IDC_BIND = 1001,
    IDC_PORT,
    IDC_START,
    IDC_STOP,
    IDC_CLEAR,
    IDC_BIND_LABEL,
    IDC_PORT_LABEL,
    IDC_USERS_LABEL,
    IDC_USERS,
    IDC_LOG_LABEL,
    IDC_LOG,
};

struct AppState {
    HWND hwnd = nullptr;
    HWND bindLabel = nullptr;
    HWND bindEdit = nullptr;
    HWND portLabel = nullptr;
    HWND portEdit = nullptr;
    HWND startButton = nullptr;
    HWND stopButton = nullptr;
    HWND clearButton = nullptr;
    HWND usersLabel = nullptr;
    HWND usersList = nullptr;
    HWND logLabel = nullptr;
    HWND logEdit = nullptr;

    ChatServer server;
    common::UiChannel channel;
    common::FileLogger logger;

    std::wstring logBuffer;
    std::wstring statusLeft;
    std::wstring statusRight;
    std::wstring activeBind;
    unsigned     activePort = 0;

    HBRUSH bgBrush = nullptr;
    HBRUSH editBrush = nullptr;
};

AppState* g_app = nullptr;

std::wstring GetText(HWND control) {
    const int length = ::GetWindowTextLengthW(control);
    if (length <= 0) {
        return std::wstring();
    }
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    ::GetWindowTextW(control, &text[0], length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

void UpdateButtons(AppState& app) {
    const bool running = app.server.IsRunning();
    ::EnableWindow(app.startButton, running ? FALSE : TRUE);
    ::EnableWindow(app.stopButton, running ? TRUE : FALSE);
    ::EnableWindow(app.bindEdit, running ? FALSE : TRUE);
    ::EnableWindow(app.portEdit, running ? FALSE : TRUE);
}

void RefreshUsers(AppState& app, const std::vector<std::wstring>& names) {
    ::SendMessageW(app.usersList, LB_RESETCONTENT, 0, 0);
    for (const std::wstring& name : names) {
        ::SendMessageW(app.usersList, LB_ADDSTRING, 0,
                       reinterpret_cast<LPARAM>(name.c_str()));
    }
}

void Relayout(AppState& app) {
    RECT client{};
    ::GetClientRect(app.hwnd, &client);
    const int width = ui::UnD(client.right);
    const int height = ui::UnD(client.bottom);
    if (width < 200 || height < 200) {
        return;
    }

    const int margin = 16;
    const int gap = 12;
    const int toolbarY = ui::HeaderHeight() + 12;
    const int toolbarH = 30;
    const int listWidth = 200;
    const int labelY = toolbarY + toolbarH + 16;
    const int contentTop = labelY + 22;
    const int contentBottom = height - ui::StatusHeight() - 12;

    ui::Place(app.bindLabel, margin, toolbarY, 64, toolbarH);
    ui::Place(app.bindEdit, margin + 68, toolbarY, 128, toolbarH);
    ui::Place(app.portLabel, margin + 204, toolbarY, 36, toolbarH);
    ui::Place(app.portEdit, margin + 244, toolbarY, 64, toolbarH);
    ui::Place(app.startButton, margin + 324, toolbarY, 84, toolbarH);
    ui::Place(app.stopButton, margin + 416, toolbarY, 84, toolbarH);
    ui::Place(app.clearButton, margin + 508, toolbarY, 100, toolbarH);

    ui::Place(app.usersLabel, margin, labelY, listWidth, 22);
    ui::Place(app.logLabel, margin + listWidth + gap, labelY,
              width - margin * 2 - listWidth - gap, 22);
    ui::Place(app.usersList, margin, contentTop, listWidth,
              contentBottom - contentTop);
    ui::Place(app.logEdit, margin + listWidth + gap, contentTop,
              width - margin * 2 - listWidth - gap, contentBottom - contentTop);
}

void StartServer(AppState& app) {
    const std::wstring bindText = text::Trim(GetText(app.bindEdit));
    const std::wstring portText = text::Trim(GetText(app.portEdit));
    const int port = _wtoi(portText.c_str());
    if (port <= 0 || port > 65535) {
        ::MessageBoxW(app.hwnd, L"端口号必须是 1~65535 之间的整数。", L"参数错误",
                      MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring error;
    if (!app.server.Start(text::WideToUtf8(bindText), static_cast<uint16_t>(port),
                          &error)) {
        const std::wstring message = L"启动失败：\n" + error +
                                     L"\n\n请检查端口是否已被占用，或换一个端口。";
        ::MessageBoxW(app.hwnd, message.c_str(), L"启动失败", MB_OK | MB_ICONERROR);
        app.statusLeft = L"未启动";
        return;
    }
    app.activeBind = bindText.empty() ? L"0.0.0.0" : bindText;
    app.activePort = static_cast<unsigned>(port);
    app.statusLeft = text::Format(L"正在监听 %s:%u", app.activeBind.c_str(),
                                 app.activePort);
    UpdateButtons(app);
    // 主动把焦点移开按钮：否则「启动服务」变灰后焦点会落到「停止服务」上，
    ::SetFocus(app.logEdit);
    ::InvalidateRect(app.hwnd, nullptr, TRUE);
}

void StopServer(AppState& app) {
    app.server.Stop();
    app.statusLeft = L"已停止监听";
    UpdateButtons(app);
    ::SetFocus(app.logEdit);
    ::InvalidateRect(app.hwnd, nullptr, TRUE);
}

void DrainEvents(AppState& app) {
    std::vector<common::UiEvent> events;
    app.channel.Drain(events);

    // 一次把这一批日志拼起来再写进编辑框：
    std::wstring batch;
    for (const common::UiEvent& event : events) {
        if (event.kind == common::UiEvent::Kind::LogLine) {
            batch += event.text;
            batch += L"\r\n";
        } else if (event.kind == common::UiEvent::Kind::UserList) {
            RefreshUsers(app, event.items);
        }
    }
    if (!batch.empty()) {
        ui::AppendText(app.logEdit, app.logBuffer, batch, 150000);
    }
}

void UpdateStatusBar(AppState& app) {
    const ServerStats stats = app.server.GetStats();
    app.statusRight = text::Format(
        L"在线 %d 人   |   聊天 %llu 条   |   登录 %llu 人次   |   帧 RX %llu / TX %llu",
        stats.online, stats.messages, stats.logins, stats.framesIn, stats.framesOut);
    RECT client{};
    ::GetClientRect(app.hwnd, &client);
    RECT bar = client;
    bar.top = bar.bottom - ui::StatusHeight();
    ::InvalidateRect(app.hwnd, &bar, FALSE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    AppState* app = g_app;
    if (app == nullptr) {
        return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }

    switch (message) {
        case WM_CREATE: {
            app->hwnd = hwnd;
            ui::InitDpi(hwnd);
            ui::InitFonts();
            ui::FitWindowToDesignSize(hwnd, 980, 700);
            app->bgBrush = ::CreateSolidBrush(ui::Bg());
            app->editBrush = ::CreateSolidBrush(ui::EditBg());

            app->bindLabel = ui::MakeLabel(hwnd, IDC_BIND_LABEL, L"监听地址");
            app->bindEdit = ui::MakeEdit(hwnd, IDC_BIND, false, false, false);
            app->portLabel = ui::MakeLabel(hwnd, IDC_PORT_LABEL, L"端口");
            app->portEdit = ui::MakeEdit(hwnd, IDC_PORT, false, false, false);
            app->startButton = ui::MakeButton(hwnd, IDC_START, L"启动服务");
            app->stopButton = ui::MakeButton(hwnd, IDC_STOP, L"停止服务");
            app->clearButton = ui::MakeButton(hwnd, IDC_CLEAR, L"清空日志");
            app->usersLabel = ui::MakeLabel(hwnd, IDC_USERS_LABEL, L"在线用户");
            app->usersList = ui::MakeList(hwnd, IDC_USERS);
            app->logLabel = ui::MakeLabel(hwnd, IDC_LOG_LABEL, L"运行日志 / 协议交互");
            app->logEdit = ui::MakeEdit(hwnd, IDC_LOG, true, true, true);

            ::SetWindowTextW(app->bindEdit, L"0.0.0.0");
            ::SetWindowTextW(app->portEdit, L"8888");
            ::SendMessageW(app->bindEdit, EM_SETLIMITTEXT, 32, 0);
            ::SendMessageW(app->portEdit, EM_SETLIMITTEXT, 5, 0);

            // 日志：写到界面 + 文件，且带上时间戳
            app->logger.Open(common::ExeDirectory() + L"\\server_run.log");
            app->server.SetLogSink([app](const std::wstring& line) {
                const std::wstring stamped = text::NowTime() + L"  " + line;
                app->logger.Write(stamped);
                common::UiEvent event;
                event.kind = common::UiEvent::Kind::LogLine;
                event.text = stamped;
                app->channel.Post(std::move(event));
            });
            app->server.SetUserListSink([app](const std::vector<std::wstring>& names) {
                common::UiEvent event;
                event.kind = common::UiEvent::Kind::UserList;
                event.items = names;
                app->channel.Post(std::move(event));
            });

            app->channel.Attach(hwnd);
            app->statusLeft = L"未启动";
            UpdateButtons(*app);
            ::SetTimer(hwnd, 1, 500, nullptr);

            // 等窗口显示后再自动监听，避免弹窗盖在未显示的窗口上
            ::PostMessageW(hwnd, WM_APP_AUTOSTART, 0, 0);
            return 0;
        }

        case WM_APP_AUTOSTART:
            StartServer(*app);
            return 0;

        case WM_SIZE:
            Relayout(*app);
            return 0;

        case WM_TIMER:
            if (wParam == 1) {
                UpdateStatusBar(*app);
            }
            return 0;

        case common::WM_APP_UI_EVENT:
            DrainEvents(*app);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = ::BeginPaint(hwnd, &paint);
            RECT client{};
            ::GetClientRect(hwnd, &client);
            ::FillRect(dc, &client, app->bgBrush);
            ui::DrawHeader(dc, client, L"聊天室 · 服务端", L"");
            ui::DrawStatusBar(dc, client, app->statusLeft, app->statusRight);
            ::EndPaint(hwnd, &paint);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            HWND child = reinterpret_cast<HWND>(lParam);
            if (child == app->logEdit) {
                ::SetBkMode(dc, OPAQUE);
                ::SetBkColor(dc, ui::EditBg());
                ::SetTextColor(dc, ui::Text());
                return reinterpret_cast<LRESULT>(app->editBrush);
            }
            ::SetBkMode(dc, TRANSPARENT);
            ::SetTextColor(dc, ui::Text());
            return reinterpret_cast<LRESULT>(app->bgBrush);
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            ::SetBkMode(dc, OPAQUE);
            ::SetBkColor(dc, ui::EditBg());
            ::SetTextColor(dc, ui::Text());
            return reinterpret_cast<LRESULT>(app->editBrush);
        }

        case WM_DRAWITEM:
            ui::DrawOwnerButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
            return TRUE;

        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == IDC_START) {
                StartServer(*app);
            } else if (id == IDC_STOP) {
                StopServer(*app);
            } else if (id == IDC_CLEAR) {
                app->logBuffer.clear();
                ::SetWindowTextW(app->logEdit, L"");
            }
            return 0;
        }

        case WM_CLOSE:
            ::KillTimer(hwnd, 1);
            app->channel.Attach(nullptr);
            StopServer(*app);
            ::DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (app->bgBrush != nullptr) {
                ::DeleteObject(app->bgBrush);
                app->bgBrush = nullptr;
            }
            if (app->editBrush != nullptr) {
                ::DeleteObject(app->editBrush);
                app->editBrush = nullptr;
            }
            ui::DestroyFonts();
            ::PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCommand) {
    ::SetProcessDPIAware();

    AppState app;
    g_app = &app;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // 背景在 WM_PAINT 中自行绘制
    wc.lpszClassName = L"ChatServerWindow";
    if (::RegisterClassExW(&wc) == 0) {
        return 0;
    }

    const int windowWidth = 980;
    const int windowHeight = 700;
    const int dpi = [] {
        HDC dc = ::GetDC(nullptr);
        const int value = ::GetDeviceCaps(dc, LOGPIXELSX);
        ::ReleaseDC(nullptr, dc);
        return value > 0 ? value : 96;
    }();

    RECT desired{0, 0, ::MulDiv(windowWidth, dpi, 96), ::MulDiv(windowHeight, dpi, 96)};
    ::AdjustWindowRect(&desired, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = ::CreateWindowExW(
        0, wc.lpszClassName, L"聊天室 · 服务端  (Chat Server)",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
        desired.right - desired.left, desired.bottom - desired.top, nullptr, nullptr,
        instance, nullptr);
    if (hwnd == nullptr) {
        return 0;
    }

    ::ShowWindow(hwnd, showCommand);
    ::UpdateWindow(hwnd);

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    app.server.Stop();
    app.logger.Close();
    g_app = nullptr;
    return static_cast<int>(msg.wParam);
}
