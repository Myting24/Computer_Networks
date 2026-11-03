// =============================================================================
//  theme.h  --  界面公共部分：配色、字体、DPI 缩放、控件创建、绘制
// =============================================================================
#pragma once

#include "../common/platform.h"

#include <string>

namespace ui {

// ---- 配色 ------------------------------------------------------------------
COLORREF Bg();          // 窗口背景：米黄
COLORREF Panel();       // 面板 / 状态栏：浅黄
COLORREF Header();      // 标题栏：深黄
COLORREF Accent();      // 边框 / 强调：金褐
COLORREF BtnFill();     // 按钮底色
COLORREF BtnFillDown(); // 按钮按下
COLORREF BtnDisabled(); // 按钮禁用
COLORREF Text();        // 正文
COLORREF SubText();     // 次要文字
COLORREF EditBg();      // 输入 / 列表背景
COLORREF ChatBg();      // 聊天区背景

// ---- DPI 缩放 --------------------------------------------------------------
void InitDpi(HWND hwnd);      // 依据主显示器 DPI 初始化缩放比例
int  D(int px);               // 按 DPI 缩放像素值
int  UnD(int px);             // 物理像素 -> 逻辑像素（布局计算用）
int  HeaderHeight();          // 标题栏高度
int  StatusHeight();          // 状态栏高度

// 按当前 DPI 把窗口客户区调整成“设计尺寸”，保证布局与字体使用同一比例
void FitWindowToDesignSize(HWND hwnd, int designWidth, int designHeight);

// ---- 字体 ------------------------------------------------------------------
void   InitFonts();
void   DestroyFonts();
HFONT  TitleFont();
HFONT  BodyFont();
HFONT  SmallFont();
HFONT  BoldFont();

// ---- 控件创建（内部按 D() 缩放） -------------------------------------------
HWND MakeLabel(HWND parent, int id, const wchar_t* text);
HWND MakeEdit(HWND parent, int id, bool readOnly, bool multiLine, bool withScroll);
HWND MakeButton(HWND parent, int id, const wchar_t* text);
HWND MakeList(HWND parent, int id);
void SetFont(HWND control, HFONT font);
void Place(HWND control, int x, int y, int w, int h);

// ---- 绘制 ------------------------------------------------------------------
void DrawHeader(HDC dc, const RECT& client, const wchar_t* title,
                const wchar_t* subtitle);
void DrawStatusBar(HDC dc, const RECT& client, const std::wstring& left,
                   const std::wstring& right);
void DrawOwnerButton(const DRAWITEMSTRUCT* item);

// 向多行只读编辑框追加文本（text 需自带换行），追加后滚动到底部并限制总长度
void AppendText(HWND edit, std::wstring& buffer, const std::wstring& text,
                size_t maxChars = 200000);

// 追加一行（自动补换行）
void AppendLine(HWND edit, std::wstring& buffer, const std::wstring& line,
                size_t maxChars = 200000);

}  // namespace ui
