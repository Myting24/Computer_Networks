// =============================================================================
//  theme.cpp  --  界面公共部分实现
// =============================================================================
#include "theme.h"

#include <cstdio>

namespace ui {

namespace {

int  g_dpi = 96;
HFONT g_title = nullptr;
HFONT g_body = nullptr;
HFONT g_small = nullptr;
HFONT g_bold = nullptr;

HFONT CreateUiFont(int pointSize, int weight) {
    LOGFONTW lf{};
    lf.lfHeight = -::MulDiv(pointSize, g_dpi, 72);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    // 微软雅黑：中英文都清晰，Windows 7 以后系统自带
    wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
    HFONT font = ::CreateFontIndirectW(&lf);
    if (font == nullptr) {
        lf.lfCharSet = GB2312_CHARSET;
        wcscpy_s(lf.lfFaceName, L"SimSun");
        font = ::CreateFontIndirectW(&lf);
    }
    return font;
}

}  // namespace

COLORREF Bg()          { return RGB(0xFF, 0xFA, 0xEC); }  // 米黄
COLORREF Panel()       { return RGB(0xFF, 0xF3, 0xC4); }  // 浅黄
COLORREF Header()      { return RGB(0xDF, 0xA5, 0x12); }  // 深黄
COLORREF Accent()      { return RGB(0xB8, 0x86, 0x0B); }  // 金褐
COLORREF BtnFill()     { return RGB(0xEC, 0xBF, 0x45); }
COLORREF BtnFillDown() { return RGB(0xC9, 0x94, 0x14); }
COLORREF BtnDisabled() { return RGB(0xEA, 0xE1, 0xC6); }
COLORREF Text()        { return RGB(0x3A, 0x2E, 0x00); }
COLORREF SubText()     { return RGB(0x7A, 0x63, 0x1E); }
COLORREF EditBg()      { return RGB(0xFF, 0xFD, 0xF5); }
COLORREF ChatBg()      { return RGB(0xFF, 0xFF, 0xFC); }

void InitDpi(HWND hwnd) {
    HDC dc = ::GetDC(hwnd);
    int dpi = 96;
    if (dc != nullptr) {
        dpi = ::GetDeviceCaps(dc, LOGPIXELSY);
        ::ReleaseDC(hwnd, dc);
    }
    if (dpi <= 0) {
        dpi = 96;
    }
    // 限制在合理范围，避免异常 DPI 导致布局完全乱掉
    if (dpi < 96) dpi = 96;
    if (dpi > 384) dpi = 384;
    g_dpi = dpi;
}

int D(int px) {
    if (g_dpi == 96) {
        return px;
    }
    return ::MulDiv(px, g_dpi, 96);
}

int UnD(int px) {
    if (g_dpi == 96) {
        return px;
    }
    return ::MulDiv(px, 96, g_dpi);
}

int HeaderHeight() { return D(56); }
int StatusHeight() { return D(34); }

void FitWindowToDesignSize(HWND hwnd, int designWidth, int designHeight) {
    if (hwnd == nullptr) {
        return;
    }
    int width = D(designWidth);
    int height = D(designHeight);

    // 兜底：投影 / 小屏笔记本上工作区可能放不下设计尺寸，按工作区等比收缩，
    // 布局本身按实际客户区计算，因此缩小后仍然可用。
    RECT work{};
    if (::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        const int maxWidth = (work.right - work.left) - D(16);
        const int maxHeight = (work.bottom - work.top) - D(16);
        if (maxWidth > D(320) && width > maxWidth) {
            width = maxWidth;
        }
        if (maxHeight > D(240) && height > maxHeight) {
            height = maxHeight;
        }
    }

    RECT target{0, 0, width, height};
    ::AdjustWindowRect(&target, WS_OVERLAPPEDWINDOW, FALSE);
    RECT current{};
    ::GetWindowRect(hwnd, &current);
    ::SetWindowPos(hwnd, nullptr, current.left, current.top,
                   target.right - target.left, target.bottom - target.top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
}

void InitFonts() {
    DestroyFonts();
    g_title = CreateUiFont(15, FW_BOLD);
    g_body  = CreateUiFont(10, FW_NORMAL);
    g_small = CreateUiFont(9, FW_NORMAL);
    g_bold  = CreateUiFont(10, FW_SEMIBOLD);
}

void DestroyFonts() {
    if (g_title != nullptr) { ::DeleteObject(g_title); g_title = nullptr; }
    if (g_body != nullptr)  { ::DeleteObject(g_body);  g_body = nullptr; }
    if (g_small != nullptr) { ::DeleteObject(g_small); g_small = nullptr; }
    if (g_bold != nullptr)  { ::DeleteObject(g_bold);  g_bold = nullptr; }
}

HFONT TitleFont() { return g_title; }
HFONT BodyFont()  { return g_body; }
HFONT SmallFont() { return g_small; }
HFONT BoldFont()  { return g_bold; }

void SetFont(HWND control, HFONT font) {
    if (control != nullptr && font != nullptr) {
        ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

HWND MakeLabel(HWND parent, int id, const wchar_t* text) {
    HWND h = ::CreateWindowExW(0, L"STATIC", text,
                               WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                               0, 0, 10, 10, parent,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               ::GetModuleHandleW(nullptr), nullptr);
    SetFont(h, BodyFont());
    return h;
}

HWND MakeEdit(HWND parent, int id, bool readOnly, bool multiLine, bool withScroll) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    if (multiLine) {
        style |= ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL;
        if (readOnly) {
            style |= ES_READONLY;
        }
        if (withScroll) {
            style |= WS_VSCROLL;
        }
    } else {
        style |= ES_AUTOHSCROLL;
        if (readOnly) {
            style |= ES_READONLY;
        }
    }
    HWND h = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", style, 0, 0, 10, 10,
                               parent,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               ::GetModuleHandleW(nullptr), nullptr);
    SetFont(h, BodyFont());
    return h;
}

HWND MakeButton(HWND parent, int id, const wchar_t* text) {
    HWND h = ::CreateWindowExW(0, L"BUTTON", text,
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                               0, 0, 10, 10, parent,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               ::GetModuleHandleW(nullptr), nullptr);
    SetFont(h, BodyFont());
    return h;
}

HWND MakeList(HWND parent, int id) {
    HWND h = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY |
                                   LBS_NOINTEGRALHEIGHT | WS_TABSTOP,
                               0, 0, 10, 10, parent,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               ::GetModuleHandleW(nullptr), nullptr);
    SetFont(h, BodyFont());
    return h;
}

void Place(HWND control, int x, int y, int w, int h) {
    if (control == nullptr) {
        return;
    }
    ::MoveWindow(control, D(x), D(y), D(w) < 1 ? 1 : D(w), D(h) < 1 ? 1 : D(h), TRUE);
}

void DrawHeader(HDC dc, const RECT& client, const wchar_t* title,
                const wchar_t* subtitle) {
    RECT bar = client;
    bar.bottom = bar.top + HeaderHeight();
    HBRUSH brush = ::CreateSolidBrush(Header());
    ::FillRect(dc, &bar, brush);
    ::DeleteObject(brush);

    // 标题栏底部的深色分隔线
    RECT line = bar;
    line.top = bar.bottom - D(3);
    HBRUSH lineBrush = ::CreateSolidBrush(Accent());
    ::FillRect(dc, &line, lineBrush);
    ::DeleteObject(lineBrush);

    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, RGB(0xFF, 0xFF, 0xFF));
    HFONT oldFont = static_cast<HFONT>(::SelectObject(dc, TitleFont()));
    RECT titleRect = bar;
    titleRect.left += D(18);
    titleRect.right -= D(18);
    ::DrawTextW(dc, title, -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (subtitle != nullptr && subtitle[0] != L'\0') {
        ::SelectObject(dc, SmallFont());
        ::SetTextColor(dc, RGB(0xFF, 0xF6, 0xD8));
        RECT subRect = bar;
        subRect.right -= D(18);
        ::DrawTextW(dc, subtitle, -1, &subRect,
                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    ::SelectObject(dc, oldFont);
}

void DrawStatusBar(HDC dc, const RECT& client, const std::wstring& left,
                   const std::wstring& right) {
    RECT bar = client;
    bar.top = bar.bottom - StatusHeight();
    HBRUSH brush = ::CreateSolidBrush(Panel());
    ::FillRect(dc, &bar, brush);
    ::DeleteObject(brush);

    RECT line = bar;
    line.bottom = bar.top + 1;
    HBRUSH lineBrush = ::CreateSolidBrush(Accent());
    ::FillRect(dc, &line, lineBrush);
    ::DeleteObject(lineBrush);

    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, SubText());
    HFONT oldFont = static_cast<HFONT>(::SelectObject(dc, SmallFont()));
    RECT textRect = bar;
    textRect.left += D(18);
    textRect.right -= D(18);
    ::DrawTextW(dc, left.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (!right.empty()) {
        ::DrawTextW(dc, right.c_str(), -1, &textRect,
                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    ::SelectObject(dc, oldFont);
}

void DrawOwnerButton(const DRAWITEMSTRUCT* item) {
    if (item == nullptr) {
        return;
    }
    HDC dc = item->hDC;
    RECT rc = item->rcItem;
    const bool disabled = (item->itemState & ODS_DISABLED) != 0;
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;

    COLORREF fill = disabled ? BtnDisabled() : (pressed ? BtnFillDown() : BtnFill());
    COLORREF color = disabled ? RGB(0x9C, 0x91, 0x70)
                              : (pressed ? RGB(0xFF, 0xFF, 0xFF) : Text());

    HBRUSH brush = ::CreateSolidBrush(fill);
    HPEN pen = ::CreatePen(PS_SOLID, D(1) < 1 ? 1 : D(1), Accent());
    HBRUSH oldBrush = static_cast<HBRUSH>(::SelectObject(dc, brush));
    HPEN oldPen = static_cast<HPEN>(::SelectObject(dc, pen));
    ::RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, D(10), D(10));
    ::SelectObject(dc, oldBrush);
    ::SelectObject(dc, oldPen);
    ::DeleteObject(brush);
    ::DeleteObject(pen);

    wchar_t text[256] = {0};
    ::GetWindowTextW(item->hwndItem, text, 256);
    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, color);
    HFONT font = reinterpret_cast<HFONT>(::SendMessageW(item->hwndItem, WM_GETFONT, 0, 0));
    HFONT oldFont = font != nullptr ? static_cast<HFONT>(::SelectObject(dc, font)) : nullptr;
    RECT textRect = rc;
    ::DrawTextW(dc, text, -1, &textRect,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (oldFont != nullptr) {
        ::SelectObject(dc, oldFont);
    }
    if ((item->itemState & ODS_FOCUS) != 0) {
        RECT focus = rc;
        ::InflateRect(&focus, -D(4), -D(4));
        ::DrawFocusRect(dc, &focus);
    }
}

void AppendLine(HWND edit, std::wstring& buffer, const std::wstring& line,
                size_t maxChars) {
    std::wstring text = line;
    text += L"\r\n";
    AppendText(edit, buffer, text, maxChars);
}

void AppendText(HWND edit, std::wstring& buffer, const std::wstring& text,
                size_t maxChars) {
    if (edit == nullptr || text.empty()) {
        return;
    }
    buffer += text;
    if (buffer.size() > maxChars) {
        const size_t overflow = buffer.size() - maxChars;
        const size_t cut = buffer.find(L"\r\n", overflow);
        if (cut != std::wstring::npos) {
            buffer.erase(0, cut + 2);
        }
    }

    ::SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
    ::SetWindowTextW(edit, buffer.c_str());
    ::SendMessageW(edit, WM_SETREDRAW, TRUE, 0);

    // 先让插入点停在末尾，再直接把视图滚到最后一行。
    // 只用 SetWindowText 的话，编辑框会把滚动条复位到顶部，
    // 日志一多就"看着像卡在开头"，所以这里必须显式滚到底。
    const int len = static_cast<int>(buffer.size());
    ::SendMessageW(edit, EM_SETSEL, len, len);
    ::SendMessageW(edit, EM_SCROLLCARET, 0, 0);
    const int lineCount = static_cast<int>(::SendMessageW(edit, EM_GETLINECOUNT, 0, 0));
    if (lineCount > 0) {
        // 向下滚动足够多行，编辑框会自动停在最后一行
        ::SendMessageW(edit, EM_LINESCROLL, 0, lineCount);
    }
    ::InvalidateRect(edit, nullptr, TRUE);
}

}  // namespace ui
