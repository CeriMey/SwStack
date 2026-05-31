#pragma once

/**
 * @file src/core/gui/SwSystemTrayIcon.h
 * @brief System tray icon with context menu, tooltip, and click signals.
 *
 * Usage:
 * @code
 *   SwSystemTrayIcon tray(&window);
 *   tray.setToolTip("My App");
 *   tray.addAction("Show",  [&]() { window.show(); });
 *   tray.addSeparator();
 *   tray.addAction("Quit",  [&]() { app.quit(); });
 *   tray.show();
 * @endcode
 */

#include "SwObject.h"
#include "SwString.h"
#include "SwList.h"
#include "SwIcon.h"
#include <functional>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#define SW_TRAY_MSG (WM_USER + 200)
#ifndef NIN_SELECT
#define NIN_SELECT (WM_USER + 0)
#endif
#ifndef NIN_KEYSELECT
#define NIN_KEYSELECT (WM_USER + 1)
#endif
#ifndef NIIF_NONE
#define NIIF_NONE 0x00000000
#endif
#ifndef NIIF_INFO
#define NIIF_INFO 0x00000001
#endif
#ifndef NIIF_WARNING
#define NIIF_WARNING 0x00000002
#endif
#ifndef NIIF_ERROR
#define NIIF_ERROR 0x00000003
#endif
#endif

class SwSystemTrayIcon : public SwObject {
    SW_OBJECT(SwSystemTrayIcon, SwObject)

public:
    enum class MessageIcon {
        NoIcon,
        Information,
        Warning,
        Critical
    };

    explicit SwSystemTrayIcon(SwObject* parent = nullptr)
        : SwObject(parent) {}

    ~SwSystemTrayIcon() override {
        hide();
    }

    // -- Configuration --

    void setToolTip(const SwString& tip) { m_tooltip = tip; updateTip_(); }
    SwString toolTip() const { return m_tooltip; }

    /** @brief Set icon from a SwIcon object. */
    void setIcon(const SwIcon& icon) { m_icon = icon; m_iconPath = icon.filePath(); updateIcon_(); }

    /** @brief Set icon from a file path (.ico, .bmp). */
    void setIconPath(const SwString& iconPath) { m_iconPath = iconPath; m_icon.load(iconPath); updateIcon_(); }

    const SwIcon& icon() const { return m_icon; }

    // -- Menu actions --

    struct Action {
        SwString text;
        std::function<void()> callback;
        bool separator = false;
    };

    void addAction(const SwString& text, const std::function<void()>& callback) {
        Action a; a.text = text; a.callback = callback;
        m_actions.append(a);
    }

    void addSeparator() {
        Action a; a.separator = true;
        m_actions.append(a);
    }

    void clearActions() { m_actions.clear(); }

    // -- Show / Hide --

    void show() {
#ifdef _WIN32
        if (m_visible) return;
        m_hwnd = createMessageWindow_();
        if (!m_hwnd) return;

        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = SW_TRAY_MSG;
        nid.hIcon = loadIcon_();
        if (!m_tooltip.isEmpty()) {
            const std::wstring tip = toWide_(m_tooltip);
            wcsncpy_s(nid.szTip, tip.c_str(), 127);
        }
        Shell_NotifyIconW(NIM_ADD, &nid);
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
        m_visible = true;
#endif
    }

    void hide() {
#ifdef _WIN32
        if (!m_visible || !m_hwnd) return;
        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd;
        nid.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        destroyMessageWindow_();
        m_visible = false;
#endif
    }

    bool isVisible() const { return m_visible; }

    void showMessage(const SwString& title,
                     const SwString& message,
                     MessageIcon icon = MessageIcon::Information,
                     int timeoutMs = 10000) {
#ifdef _WIN32
        if (!m_visible) {
            show();
        }
        if (!m_visible || !m_hwnd || message.isEmpty()) {
            return;
        }

        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_INFO;
        nid.dwInfoFlags = messageIconFlags_(icon);
        if (timeoutMs > 0) {
            nid.uTimeout = static_cast<UINT>(timeoutMs);
        }

        const std::wstring titleText = toWide_(title.isEmpty() ? m_tooltip : title);
        const std::wstring messageText = toWide_(message);
        wcsncpy_s(nid.szInfoTitle, titleText.empty() ? L"" : titleText.c_str(), static_cast<size_t>(-1));
        wcsncpy_s(nid.szInfo, messageText.c_str(), static_cast<size_t>(-1));
        Shell_NotifyIconW(NIM_MODIFY, &nid);
#else
        (void)title;
        (void)message;
        (void)icon;
        (void)timeoutMs;
#endif
    }

signals:
    DECLARE_SIGNAL_VOID(activated)
    DECLARE_SIGNAL_VOID(doubleClicked)

private:
    SwString m_tooltip;
    SwString m_iconPath;
    SwIcon m_icon;
    SwList<Action> m_actions;
    bool m_visible = false;

#ifndef _WIN32
    void updateIcon_() {}
    void updateTip_() {}
#endif

#ifdef _WIN32
    HWND m_hwnd = nullptr;

    HICON loadIcon_() {
        if (!m_icon.isNull()) {
            HICON h = m_icon.toHICON(GetSystemMetrics(SM_CXSMICON));
            if (h) return h;
        }
        return LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    }

    void updateIcon_() {
        if (!m_visible || !m_hwnd) return;
        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_ICON;
        nid.hIcon = loadIcon_();
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    void updateTip_() {
        if (!m_visible || !m_hwnd) return;
        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_TIP;
        const std::wstring tip = toWide_(m_tooltip);
        wcsncpy_s(nid.szTip, tip.c_str(), 127);
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    static DWORD messageIconFlags_(MessageIcon icon) {
        switch (icon) {
        case MessageIcon::NoIcon:
            return NIIF_NONE;
        case MessageIcon::Warning:
            return NIIF_WARNING;
        case MessageIcon::Critical:
            return NIIF_ERROR;
        case MessageIcon::Information:
        default:
            return NIIF_INFO;
        }
    }

    static std::wstring toWide_(const SwString& value) {
        const std::string utf8 = value.toStdString();
        if (utf8.empty()) {
            return std::wstring();
        }
        const int required = MultiByteToWideChar(CP_UTF8,
                                                 0,
                                                 utf8.c_str(),
                                                 -1,
                                                 nullptr,
                                                 0);
        if (required <= 0) {
            return std::wstring(utf8.begin(), utf8.end());
        }

        std::wstring wide(static_cast<size_t>(required), L'\0');
        const int written = MultiByteToWideChar(CP_UTF8,
                                                0,
                                                utf8.c_str(),
                                                -1,
                                                &wide[0],
                                                required);
        if (written <= 0) {
            return std::wstring(utf8.begin(), utf8.end());
        }
        if (!wide.empty() && wide.back() == L'\0') {
            wide.pop_back();
        }
        return wide;
    }

    void showContextMenu_() {
        HMENU menu = CreatePopupMenu();
        if (!menu) {
            return;
        }
        for (size_t i = 0; i < m_actions.size(); i++) {
            if (m_actions[i].separator) {
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            } else {
                std::wstring text(m_actions[i].text.toStdString().begin(),
                                  m_actions[i].text.toStdString().end());
                AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(1000 + i), text.c_str());
            }
        }
        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(m_hwnd);
        UINT cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, m_hwnd, nullptr);
        DestroyMenu(menu);

        if (cmd >= 1000) {
            size_t idx = static_cast<size_t>(cmd - 1000);
            if (idx < m_actions.size() && m_actions[idx].callback) {
                m_actions[idx].callback();
            }
        }
    }

    static const wchar_t* trayWindowClassName_() {
        return L"SwSystemTrayIconMessageWindow";
    }

    static void ensureTrayWindowClassRegistered_() {
        static bool registered = false;
        if (registered) {
            return;
        }

        WNDCLASSW wc = {};
        wc.lpfnWndProc = &SwSystemTrayIcon::trayProc_;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = trayWindowClassName_();
        RegisterClassW(&wc);
        registered = true;
    }

    HWND createMessageWindow_() {
        ensureTrayWindowClassRegistered_();
        HWND hwnd = CreateWindowExW(0,
                                    trayWindowClassName_(),
                                    L"SwSystemTrayIcon",
                                    0,
                                    0,
                                    0,
                                    0,
                                    0,
                                    HWND_MESSAGE,
                                    nullptr,
                                    GetModuleHandleW(nullptr),
                                    this);
        return hwnd;
    }

    void destroyMessageWindow_() {
        if (!m_hwnd) {
            return;
        }
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    static UINT trayEvent_(LPARAM lParam) {
        const UINT raw = static_cast<UINT>(lParam);
        const UINT eventLowWord = LOWORD(raw);
        switch (eventLowWord) {
        case WM_CONTEXTMENU:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONUP:
        case WM_RBUTTONDOWN:
        case NIN_SELECT:
        case NIN_KEYSELECT:
            return eventLowWord;
        default:
            return raw;
        }
    }

    static LRESULT CALLBACK trayProc_(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_NCCREATE) {
            CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd,
                              GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create ? create->lpCreateParams : nullptr));
            return TRUE;
        }

        SwSystemTrayIcon* self =
            reinterpret_cast<SwSystemTrayIcon*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self && msg == SW_TRAY_MSG) {
            const UINT trayEvent = trayEvent_(lParam);
            SW_UNUSED(wParam);
            if (trayEvent == WM_LBUTTONDBLCLK) {
                emit self->doubleClicked();
                return 0;
            }
            if (trayEvent == WM_LBUTTONUP || trayEvent == NIN_SELECT) {
                emit self->activated();
                return 0;
            }
            if (trayEvent == WM_CONTEXTMENU ||
                trayEvent == WM_RBUTTONUP ||
                trayEvent == NIN_KEYSELECT) {
                self->showContextMenu_();
                return 0;
            }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
#endif
};
