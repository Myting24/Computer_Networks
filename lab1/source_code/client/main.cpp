// =============================================================================
//  main.cpp  --  客户端图形界面（Win32，无第三方界面库）
// =============================================================================
#include "../common/platform.h"
#include "../common/net.h"
#include "../common/text.h"
#include "../ui/theme.h"
#include "../ui/ui_channel.h"
#include "client.h"

#include <commctrl.h>
#include <imm.h>

#include <string>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "imm32.lib")

namespace {

enum ControlId : int {
    IDC_HOST = 2001,
    IDC_PORT,
    IDC_NICK,
    IDC_CONNECT,
    IDC_DISCONNECT,
    IDC_CHAT,
    IDC_USERS,
    IDC_INPUT,
    IDC_SEND,
    IDC_HOST_LABEL,
    IDC_PORT_LABEL,
    IDC_NICK_LABEL,
    IDC_USERS_LABEL,
};

struct AppState {
    HWND hwnd = nullptr;
    HWND hostLabel = nullptr;
    HWND hostEdit = nullptr;
    HWND portLabel = nullptr;
    HWND portEdit = nullptr;
    HWND nickLabel = nullptr;
    HWND nickEdit = nullptr;
    HWND connectButton = nullptr;
    HWND disconnectButton = nullptr;
    HWND chatEdit = nullptr;
    HWND usersLabel = nullptr;
    HWND usersList = nullptr;
    HWND inputEdit = nullptr;
    HWND sendButton = nullptr;

    ChatClient client;
    common::UiChannel channel;

    std::wstring chatBuffer;
    std::wstring statusLeft;
    std::wstring statusRight;

    HBRUSH bgBrush = nullptr;
    HBRUSH editBrush = nullptr;
    HBRUSH chatBrush = nullptr;
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

void AppendChat(AppState& app, const std::wstring& line) {
    ui::AppendLine(app.chatEdit, app.chatBuffer, line, 300000);
}

void UpdateButtons(AppState& app) {
    const ChatClient::State state = app.client.GetState();
    const bool idle = (state == ChatClient::State::Idle);
    const bool online = (state == ChatClient::State::Online);
    const bool connecting =
        (state == ChatClient::State::Connecting || state == ChatClient::State::LoggingIn);

    ::EnableWindow(app.connectButton, idle ? TRUE : FALSE);
    ::EnableWindow(app.disconnectButton, (idle || connecting) ? FALSE : TRUE);
    ::EnableWindow(app.hostEdit, idle ? TRUE : FALSE);
    ::EnableWindow(app.portEdit, idle ? TRUE : FALSE);
    ::EnableWindow(app.nickEdit, idle ? TRUE : FALSE);
    ::EnableWindow(app.inputEdit, online ? TRUE : FALSE);
    ::EnableWindow(app.sendButton, online ? TRUE : FALSE);
}

void RefreshUsers(AppState& app, const std::vector<std::wstring>& names) {
    ::SendMessageW(app.usersList, LB_RESETCONTENT, 0, 0);
    for (const std::wstring& name : names) {
        ::SendMessageW(app.usersList, LB_ADDSTRING, 0,
                       reinterpret_cast<LPARAM>(name.c_str()));
    }
}

void SendCurrentInput(AppState& app) {
    if (app.client.GetState() != ChatClient::State::Online) {
        AppendChat(app, L"【提示】尚未连接服务器，无法发送消息。");
        return;
    }
    const std::wstring text = GetText(app.inputEdit);
    if (text::Trim(text).empty()) {
        return;
    }
    std::wstring error;
    if (!app.client.SendChat(text, &error)) {
        AppendChat(app, L"【提示】发送失败：" + error);
        return;
    }
    ::SetWindowTextW(app.inputEdit, L"");
}

void OnConnect(AppState& app) {
    const std::wstring host = text::Trim(GetText(app.hostEdit));
    const int port = _wtoi(text::Trim(GetText(app.portEdit)).c_str());
    std::wstring nick = text::Trim(GetText(app.nickEdit));

    if (host.empty()) {
        ::MessageBoxW(app.hwnd, L"请填写服务器地址，例如 127.0.0.1。", L"提示",
                      MB_OK | MB_ICONWARNING);
        return;
    }
    if (port <= 0 || port > 65535) {
        ::MessageBoxW(app.hwnd, L"端口号必须是 1~65535 之间的整数。", L"提示",
                      MB_OK | MB_ICONWARNING);
        return;
    }
    if (nick.empty()) {
        nick = text::RandomNick();
        ::SetWindowTextW(app.nickEdit, nick.c_str());
    }

    std::wstring error;
    if (!app.client.BeginConnect(host, static_cast<uint16_t>(port), nick, &error)) {
        ::MessageBoxW(app.hwnd, error.c_str(), L"无法连接", MB_OK | MB_ICONERROR);
        return;
    }
    UpdateButtons(app);
}

void OnDisconnect(AppState& app) {
    // 先记下断开前的状态：在线时断开提示"退出聊天室"，连接中途断开提示"取消连接"
    const ChatClient::State before = app.client.GetState();
    app.client.Disconnect();

    if (before == ChatClient::State::Online) {
        // 自己退出时，服务端只会通知别人；自己这边补一条提示，
        AppendChat(app, L"[" + text::NowTime() + L"] —— 你已退出聊天室 ——");
    } else if (before == ChatClient::State::Connecting ||
               before == ChatClient::State::LoggingIn) {
        AppendChat(app, L"[" + text::NowTime() + L"] —— 已取消连接 ——");
    }

    UpdateButtons(app);
    ::SetFocus(app.inputEdit);
}

LRESULT CALLBACK InputEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                               UINT_PTR, DWORD_PTR) {
    auto imeComposing = [&]() {
        HIMC context = ::ImmGetContext(hwnd);
        if (context == nullptr) {
            return false;
        }
        const LONG length = ::ImmGetCompositionStringW(context, GCS_COMPSTR, nullptr, 0);
        ::ImmReleaseContext(hwnd, context);
        return length > 0;
    };

    if (message == WM_KEYDOWN && wParam == VK_RETURN) {
        if (imeComposing()) {
            return ::DefSubclassProc(hwnd, message, wParam, lParam);
        }
        if (g_app != nullptr) {
            SendCurrentInput(*g_app);
        }
        return 0;
    }
    if (message == WM_CHAR && wParam == VK_RETURN) {
        return 0;  
    }
    return ::DefSubclassProc(hwnd, message, wParam, lParam);
}

void Relayout(AppState& app) {
    RECT client{};
    ::GetClientRect(app.hwnd, &client);
    const int width = ui::UnD(client.right);
    const int height = ui::UnD(client.bottom);
    if (width < 300 || height < 300) {
        return;
    }

    const int margin = 16;
    const int gap = 12;
    const int rightWidth = 200;
    const int toolbarY = ui::HeaderHeight() + 12;
    const int toolbarH = 30;
    const int labelY = toolbarY + toolbarH + 16;
    const int contentTop = labelY + 22;
    const int inputH = 38;
    const int inputY = height - ui::StatusHeight() - gap - inputH;
    const int chatWidth = width - margin * 2 - gap - rightWidth;
    const int chatHeight = inputY - gap - contentTop;
    const int usersX = width - margin - rightWidth;

    ui::Place(app.hostLabel, margin, toolbarY, 40, toolbarH);
    ui::Place(app.hostEdit, margin + 44, toolbarY, 140, toolbarH);
    ui::Place(app.portLabel, margin + 192, toolbarY, 32, toolbarH);
    ui::Place(app.portEdit, margin + 228, toolbarY, 62, toolbarH);
    ui::Place(app.nickLabel, margin + 300, toolbarY, 32, toolbarH);
    ui::Place(app.nickEdit, margin + 336, toolbarY, 164, toolbarH);
    ui::Place(app.connectButton, margin + 512, toolbarY, 88, toolbarH);
    ui::Place(app.disconnectButton, margin + 608, toolbarY, 88, toolbarH);

    ui::Place(app.chatEdit, margin, contentTop, chatWidth, chatHeight);
    ui::Place(app.usersLabel, usersX, labelY, rightWidth, 22);
    ui::Place(app.usersList, usersX, contentTop, rightWidth,
              height - ui::StatusHeight() - gap - contentTop);

    ui::Place(app.inputEdit, margin, inputY, chatWidth - gap - 96, inputH);
    ui::Place(app.sendButton, margin + chatWidth - 96, inputY, 96, inputH);
}

void DrainEvents(AppState& app) {
    std::vector<common::UiEvent> events;
    app.channel.Drain(events);

    // 同一批到达的消息先拼成一个字符串再写入
    std::wstring batch;
    for (const common::UiEvent& event : events) {
        switch (event.kind) {
            case common::UiEvent::Kind::ChatLine:
                batch += event.text;
                batch += L"\r\n";
                break;
            case common::UiEvent::Kind::UserList:
                RefreshUsers(app, event.items);
                break;
            case common::UiEvent::Kind::State:
                app.statusLeft = L"状态：" + event.text;
                UpdateButtons(app);
                if (app.client.GetState() == ChatClient::State::Online) {
                    ::SetFocus(app.inputEdit);
                }
                ::InvalidateRect(app.hwnd, nullptr, TRUE);
                break;
            default:
                break;
        }
    }
    if (!batch.empty()) {
        ui::AppendText(app.chatEdit, app.chatBuffer, batch, 150000);
    }
}

void UpdateStatusBar(AppState& app) {
    const int online = static_cast<int>(
        ::SendMessageW(app.usersList, LB_GETCOUNT, 0, 0));
    app.statusRight = text::Format(
        L"在线 %d 人   |   收 %llu 帧 / 发 %llu 帧   |   序号跳跃 %llu 次   |   Enter 发送",
        online, app.client.RxFrames(), app.client.TxFrames(), app.client.GapCount());
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
            ui::FitWindowToDesignSize(hwnd, 1020, 760);
            app->bgBrush = ::CreateSolidBrush(ui::Bg());
            app->editBrush = ::CreateSolidBrush(ui::EditBg());
            app->chatBrush = ::CreateSolidBrush(ui::ChatBg());

            app->hostLabel = ui::MakeLabel(hwnd, IDC_HOST_LABEL, L"服务器");
            app->hostEdit = ui::MakeEdit(hwnd, IDC_HOST, false, false, false);
            app->portLabel = ui::MakeLabel(hwnd, IDC_PORT_LABEL, L"端口");
            app->portEdit = ui::MakeEdit(hwnd, IDC_PORT, false, false, false);
            app->nickLabel = ui::MakeLabel(hwnd, IDC_NICK_LABEL, L"昵称");
            app->nickEdit = ui::MakeEdit(hwnd, IDC_NICK, false, false, false);
            app->connectButton = ui::MakeButton(hwnd, IDC_CONNECT, L"连接");
            app->disconnectButton = ui::MakeButton(hwnd, IDC_DISCONNECT, L"断开");
            app->chatEdit = ui::MakeEdit(hwnd, IDC_CHAT, true, true, true);
            app->usersLabel = ui::MakeLabel(hwnd, IDC_USERS_LABEL, L"在线用户");
            app->usersList = ui::MakeList(hwnd, IDC_USERS);
            app->inputEdit = ui::MakeEdit(hwnd, IDC_INPUT, false, false, false);
            app->sendButton = ui::MakeButton(hwnd, IDC_SEND, L"发送");

            ::SetWindowTextW(app->hostEdit, L"127.0.0.1");
            ::SetWindowTextW(app->portEdit, L"8888");
            ::SetWindowTextW(app->nickEdit, text::RandomNick().c_str());
            ::SendMessageW(app->hostEdit, EM_SETLIMITTEXT, 64, 0);
            ::SendMessageW(app->portEdit, EM_SETLIMITTEXT, 5, 0);
            ::SendMessageW(app->nickEdit, EM_SETLIMITTEXT, 32, 0);
            ::SendMessageW(app->inputEdit, EM_SETLIMITTEXT, 2000, 0);

            ::SetWindowSubclass(app->inputEdit, InputEditProc, 1, 0);

            app->client.SetLineSink([app](const std::wstring& line) {
                common::UiEvent event;
                event.kind = common::UiEvent::Kind::ChatLine;
                event.text = line;
                app->channel.Post(std::move(event));
            });
            app->client.SetListSink([app](const std::vector<std::wstring>& names) {
                common::UiEvent event;
                event.kind = common::UiEvent::Kind::UserList;
                event.items = names;
                app->channel.Post(std::move(event));
            });
            app->client.SetStateSink(
                [app](ChatClient::State, const std::wstring& text) {
                    common::UiEvent event;
                    event.kind = common::UiEvent::Kind::State;
                    event.text = text;
                    app->channel.Post(std::move(event));
                });

            app->channel.Attach(hwnd);
            app->statusLeft = L"状态：未连接";
            UpdateButtons(*app);
            ::SetTimer(hwnd, 1, 500, nullptr);
            return 0;
        }

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
            ui::DrawHeader(dc, client, L"聊天室 · 客户端", L"");
            ui::DrawStatusBar(dc, client, app->statusLeft, app->statusRight);
            ::EndPaint(hwnd, &paint);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            HWND child = reinterpret_cast<HWND>(lParam);
            if (child == app->chatEdit) {
                // 聊天区是只读编辑框
                ::SetBkMode(dc, OPAQUE);
                ::SetBkColor(dc, ui::ChatBg());
                ::SetTextColor(dc, ui::Text());
                return reinterpret_cast<LRESULT>(app->chatBrush);
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
            if (id == IDC_CONNECT) {
                OnConnect(*app);
            } else if (id == IDC_DISCONNECT) {
                OnDisconnect(*app);
            } else if (id == IDC_SEND) {
                SendCurrentInput(*app);
                ::SetFocus(app->inputEdit);
            }
            return 0;
        }

        case WM_CLOSE:
            ::KillTimer(hwnd, 1);
            app->channel.Attach(nullptr);
            app->client.Disconnect();
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
            if (app->chatBrush != nullptr) {
                ::DeleteObject(app->chatBrush);
                app->chatBrush = nullptr;
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

    // Winsock 在进程启动时初始化一次，整个生命周期内不再清理
    std::string netError;
    if (!net::Startup(&netError)) {
        ::MessageBoxW(nullptr, L"网络环境初始化失败，程序无法启动。", L"错误",
                      MB_OK | MB_ICONERROR);
        return 1;
    }

    AppState app;
    g_app = &app;

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    ::InitCommonControlsEx(&controls);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"ChatClientWindow";
    if (::RegisterClassExW(&wc) == 0) {
        return 0;
    }

    const int windowWidth = 1020;
    const int windowHeight = 760;
    const int dpi = [] {
        HDC dc = ::GetDC(nullptr);
        const int value = ::GetDeviceCaps(dc, LOGPIXELSX);
        ::ReleaseDC(nullptr, dc);
        return value > 0 ? value : 96;
    }();
    RECT desired{0, 0, ::MulDiv(windowWidth, dpi, 96), ::MulDiv(windowHeight, dpi, 96)};
    ::AdjustWindowRect(&desired, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = ::CreateWindowExW(
        0, wc.lpszClassName, L"聊天室 · 客户端  (Chat Client)",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
        desired.right - desired.left, desired.bottom - desired.top, nullptr, nullptr,
        instance, nullptr);
    if (hwnd == nullptr) {
        return 0;
    }

    ::ShowWindow(hwnd, showCommand);
    ::UpdateWindow(hwnd);
    ::SetFocus(app.inputEdit);

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    app.client.Disconnect();
    g_app = nullptr;
    return static_cast<int>(msg.wParam);
}
