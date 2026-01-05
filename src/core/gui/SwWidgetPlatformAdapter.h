#pragma once
/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#include "Sw.h"
#include "SwFont.h"
#include "SwString.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <tuple>
#include <mutex>
#include <unordered_set>
#include <codecvt>
#include <locale>
#include <sstream>

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#endif

struct SwWidgetPlatformHandle {
    void* nativeHandle{nullptr};
    void* nativeDisplay{nullptr};

    explicit operator bool() const { return nativeHandle != nullptr; }
};

class SwWidgetPlatformAdapter {
public:
    static SwWidgetPlatformHandle fromNativeHandle(void* handle, void* display = nullptr) {
        SwWidgetPlatformHandle h;
        h.nativeHandle = handle;
        h.nativeDisplay = display;
        return h;
    }

    template <typename T>
    static T nativeHandleAs(const SwWidgetPlatformHandle& handle) {
        return reinterpret_cast<T>(handle.nativeHandle);
    }

    static void setCursor(CursorType cursor);
    static void invalidateRect(const SwWidgetPlatformHandle& handle, const SwRect& rect);
    static SwRect clientRect(const SwWidgetPlatformHandle& handle);

    static size_t characterIndexAtPosition(const SwWidgetPlatformHandle& handle,
                                           const SwString& text,
                                           SwFont font,
                                           int relativeX,
                                           int defaultWidth);
    static int textWidthUntil(const SwWidgetPlatformHandle& handle,
                              const SwString& text,
                              SwFont font,
                              size_t length,
                              int defaultWidth);

    static bool isBackspaceKey(int keyCode);
    static bool isDeleteKey(int keyCode);
    static bool isLeftArrowKey(int keyCode);
    static bool isRightArrowKey(int keyCode);
    static bool isUpArrowKey(int keyCode);
    static bool isDownArrowKey(int keyCode);
    static bool isHomeKey(int keyCode);
    static bool isEndKey(int keyCode);
    static bool isReturnKey(int keyCode);
    static bool isEscapeKey(int keyCode);
    static bool isCapsLockKey(int keyCode);
    static bool matchesShortcutKey(int keyCode, char letter);
    static bool translateCharacter(int keyCode, bool shiftPressed, bool capsLock, char& outChar);
    static bool isShiftModifierActive();
#if defined(__linux__)
    static bool requestSyntheticExpose(::Window window);
    static void finishSyntheticExpose(::Window window);
#endif

private:
#if defined(__linux__)
    struct LinuxExposeTracker {
        std::mutex mutex;
        std::unordered_set<std::uintptr_t> pending;
    };
    static LinuxExposeTracker& exposeTracker();
#endif
    static size_t approximateIndex(const SwString& text, int relativeX, int defaultWidth);
    static int approximateWidth(const SwString& text, size_t length, int defaultWidth);
};

#if defined(_WIN32)

inline void SwWidgetPlatformAdapter::setCursor(CursorType cursor) {
    LPCTSTR cursorId = IDC_ARROW;
    switch (cursor) {
    case CursorType::Hand: cursorId = IDC_HAND; break;
    case CursorType::IBeam: cursorId = IDC_IBEAM; break;
    case CursorType::Cross: cursorId = IDC_CROSS; break;
    case CursorType::Wait: cursorId = IDC_WAIT; break;
    case CursorType::SizeAll: cursorId = IDC_SIZEALL; break;
    case CursorType::SizeNS: cursorId = IDC_SIZENS; break;
    case CursorType::SizeWE: cursorId = IDC_SIZEWE; break;
    case CursorType::SizeNWSE: cursorId = IDC_SIZENWSE; break;
    case CursorType::SizeNESW: cursorId = IDC_SIZENESW; break;
    default: cursorId = IDC_ARROW; break;
    }

    HCURSOR cursorHandle = LoadCursor(nullptr, cursorId);
    if (cursorHandle) {
        ::SetCursor(cursorHandle);
    }
}

inline void SwWidgetPlatformAdapter::invalidateRect(const SwWidgetPlatformHandle& handle,
                                                    const SwRect& rect) {
    HWND hwnd = nativeHandleAs<HWND>(handle);
    if (!hwnd) {
        return;
    }
    RECT r;
    r.left = rect.x;
    r.top = rect.y;
    r.right = rect.x + rect.width;
    r.bottom = rect.y + rect.height;
    ::InvalidateRect(hwnd, &r, FALSE);
    // Trigger an immediate paint on the GUI thread so frames are not stuck behind idle waits.
    ::PostMessage(hwnd, WM_NULL, 0,0);

}

inline SwRect SwWidgetPlatformAdapter::clientRect(const SwWidgetPlatformHandle& handle) {
    HWND hwnd = nativeHandleAs<HWND>(handle);
    if (!hwnd) {
        return SwRect{0, 0, 0, 0};
    }
    RECT clientRect{};
    if (!::GetClientRect(hwnd, &clientRect)) {
        return SwRect{0, 0, 0, 0};
    }
    const int w = std::max(0, static_cast<int>(clientRect.right - clientRect.left));
    const int h = std::max(0, static_cast<int>(clientRect.bottom - clientRect.top));
    return SwRect{0, 0, w, h};
}


inline size_t SwWidgetPlatformAdapter::characterIndexAtPosition(const SwWidgetPlatformHandle& handle,
                                                                const SwString& text,
                                                                SwFont font,
                                                                int relativeX,
                                                                int defaultWidth) {
    HWND hwnd = nativeHandleAs<HWND>(handle);
    if (!hwnd) {
        return approximateIndex(text, relativeX, defaultWidth);
    }
    HDC hdc = GetDC(hwnd);
    if (!hdc) {
        return approximateIndex(text, relativeX, defaultWidth);
    }

    HFONT hFont = font.handle(hdc);
    HFONT oldFont = nullptr;
    if (hFont) {
        oldFont = (HFONT)SelectObject(hdc, hFont);
    }

    SIZE charSize{};
    int currentX = 0;
    size_t index = 0;
    std::string buffer = text.toStdString();
    for (; index < buffer.size(); ++index) {
        char ch = buffer[index];
        if (!GetTextExtentPoint32A(hdc, &ch, 1, &charSize)) {
            continue;
        }
        if (currentX + charSize.cx / 2 >= relativeX) {
            break;
        }
        currentX += charSize.cx;
    }

    if (oldFont) {
        SelectObject(hdc, oldFont);
    }
    ReleaseDC(hwnd, hdc);
    return index;
}

inline int SwWidgetPlatformAdapter::textWidthUntil(const SwWidgetPlatformHandle& handle,
                                                   const SwString& text,
                                                   SwFont font,
                                                   size_t length,
                                                   int defaultWidth) {
    HWND hwnd = nativeHandleAs<HWND>(handle);
    if (!hwnd) {
        return approximateWidth(text, length, defaultWidth);
    }
    HDC hdc = GetDC(hwnd);
    if (!hdc) {
        return approximateWidth(text, length, defaultWidth);
    }

    HFONT hFont = font.handle(hdc);
    HFONT oldFont = nullptr;
    if (hFont) {
        oldFont = (HFONT)SelectObject(hdc, hFont);
    }

    SIZE textSize{};
    SwString segment = text.substr(0, length);
    std::string utf8 = segment.toStdString();
    if (!utf8.empty()) {
        GetTextExtentPoint32A(hdc, utf8.c_str(), static_cast<int>(utf8.size()), &textSize);
    }

    if (oldFont) {
        SelectObject(hdc, oldFont);
    }
    ReleaseDC(hwnd, hdc);
    return textSize.cx;
}

inline bool SwWidgetPlatformAdapter::isBackspaceKey(int keyCode) {
    return keyCode == VK_BACK;
}

inline bool SwWidgetPlatformAdapter::isDeleteKey(int keyCode) {
    return keyCode == VK_DELETE;
}

inline bool SwWidgetPlatformAdapter::isLeftArrowKey(int keyCode) {
    return keyCode == VK_LEFT;
}

inline bool SwWidgetPlatformAdapter::isRightArrowKey(int keyCode) {
    return keyCode == VK_RIGHT;
}

inline bool SwWidgetPlatformAdapter::isUpArrowKey(int keyCode) {
    return keyCode == VK_UP;
}

inline bool SwWidgetPlatformAdapter::isDownArrowKey(int keyCode) {
    return keyCode == VK_DOWN;
}

inline bool SwWidgetPlatformAdapter::isHomeKey(int keyCode) {
    return keyCode == VK_HOME;
}

inline bool SwWidgetPlatformAdapter::isEndKey(int keyCode) {
    return keyCode == VK_END;
}

inline bool SwWidgetPlatformAdapter::isReturnKey(int keyCode) {
    return keyCode == VK_RETURN;
}

inline bool SwWidgetPlatformAdapter::isEscapeKey(int keyCode) {
    return keyCode == VK_ESCAPE;
}

inline bool SwWidgetPlatformAdapter::isCapsLockKey(int keyCode) {
    return keyCode == VK_CAPITAL;
}

inline bool SwWidgetPlatformAdapter::matchesShortcutKey(int keyCode, char letter) {
    char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
    return keyCode == upper;
}

inline bool SwWidgetPlatformAdapter::translateCharacter(int keyCode,
                                                        bool shiftPressed,
                                                        bool capsLock,
                                                        char& outChar) {
    if (keyCode >= 'A' && keyCode <= 'Z') {
        bool upper = shiftPressed ^ capsLock;
        outChar = upper ? static_cast<char>(keyCode) : static_cast<char>(keyCode + ('a' - 'A'));
        return true;
    }

    if (keyCode >= '0' && keyCode <= '9') {
        static const char shifted[] = {')', '!', '@', '#', '$', '%', '^', '&', '*', '('};
        outChar = shiftPressed ? shifted[keyCode - '0'] : static_cast<char>(keyCode);
        return true;
    }

    switch (keyCode) {
    case VK_SPACE: outChar = ' '; return true;
    case VK_OEM_MINUS: outChar = shiftPressed ? '_' : '-'; return true;
    case VK_OEM_PLUS: outChar = shiftPressed ? '+' : '='; return true;
    case VK_OEM_4: outChar = shiftPressed ? '{' : '['; return true;
    case VK_OEM_6: outChar = shiftPressed ? '}' : ']'; return true;
    case VK_OEM_1: outChar = shiftPressed ? ':' : ';'; return true;
    case VK_OEM_7: outChar = shiftPressed ? '"' : '\''; return true;
    case VK_OEM_COMMA: outChar = shiftPressed ? '<' : ','; return true;
    case VK_OEM_PERIOD: outChar = shiftPressed ? '>' : '.'; return true;
    case VK_OEM_2: outChar = shiftPressed ? '?' : '/'; return true;
    case VK_OEM_5: outChar = shiftPressed ? '|' : '\\'; return true;
    case VK_OEM_3: outChar = shiftPressed ? '~' : '`'; return true;
    default: break;
    }
    return false;
}

inline bool SwWidgetPlatformAdapter::isShiftModifierActive() {
    return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
}

#elif defined(__linux__)

inline SwWidgetPlatformAdapter::LinuxExposeTracker& SwWidgetPlatformAdapter::exposeTracker() {
    static LinuxExposeTracker tracker;
    return tracker;
}

inline bool SwWidgetPlatformAdapter::requestSyntheticExpose(::Window window) {
    if (!window) {
        return false;
    }
    auto& tracker = exposeTracker();
    std::lock_guard<std::mutex> lock(tracker.mutex);
    const auto key = reinterpret_cast<std::uintptr_t>(window);
    return tracker.pending.insert(key).second;
}

inline void SwWidgetPlatformAdapter::finishSyntheticExpose(::Window window) {
    if (!window) {
        return;
    }
    auto& tracker = exposeTracker();
    std::lock_guard<std::mutex> lock(tracker.mutex);
    tracker.pending.erase(reinterpret_cast<std::uintptr_t>(window));
}

namespace SwLinuxFontCache {
struct Key {
    std::string family;
    int pixelSize{12};
    bool bold{false};
    bool italic{false};
    bool underline{false};

    bool operator<(const Key& other) const {
        return std::tie(family, pixelSize, bold, italic, underline) <
               std::tie(other.family, other.pixelSize, other.bold, other.italic, other.underline);
    }
};

inline std::string toUtf8(const std::wstring& str) {
    if (str.empty()) {
        return {};
    }
    try {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
        return conv.to_bytes(str);
    } catch (...) {
        std::string fallback;
        fallback.reserve(str.size());
        for (wchar_t ch : str) {
            fallback.push_back(static_cast<char>((ch >= 0 && ch <= 0x7F) ? ch : '?'));
        }
        return fallback;
    }
}

inline std::string sanitizeFamily(const std::wstring& family) {
    std::string utf8 = toUtf8(family);
    if (utf8.empty()) {
        utf8 = "sans";
    }
    std::string lower;
    lower.reserve(utf8.size());
    for (char c : utf8) {
        char normalized = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (normalized == '_' || normalized == ' ') {
            normalized = '-';
        }
        lower.push_back(normalized);
    }
    if (lower == "segoe-ui" || lower == "segoeui") {
        lower = "dejavu-sans";
    }
    return lower;
}

inline int toPixelSize(const SwFont& font) {
    const int point = font.getPointSize() > 0 ? font.getPointSize() : 9;
    constexpr double dpi = 96.0;
    return static_cast<int>(point * dpi / 72.0 + 0.5);
}

inline std::string buildPattern(const Key& key) {
    std::string family = key.family.empty() ? "sans" : key.family;
    std::string weight = key.bold ? "bold" : "medium";
    std::string slant = key.italic ? "i" : "r";
    std::ostringstream oss;
    oss << "-*-" << family << "-" << weight << "-" << slant << "-*-"
        << key.pixelSize << "-*-*-*-*-*-*-*";
    return oss.str();
}

inline XFontStruct* tryLoad(Display* display, const std::string& pattern) {
    if (!display) {
        return nullptr;
    }
    if (pattern.empty()) {
        return nullptr;
    }
    return XLoadQueryFont(display, pattern.c_str());
}

inline XFontStruct* acquire(Display* display, const SwFont& font) {
    if (!display) {
        return nullptr;
    }

    static std::map<Key, XFontStruct*> cache;
    static std::mutex cacheMutex;

    Key key;
    key.family = sanitizeFamily(font.getFamily());
    key.pixelSize = toPixelSize(font);
    key.bold = font.getWeight() >= FontWeight::SemiBold;
    key.italic = font.isItalic();
    key.underline = font.isUnderline();

    {
        std::lock_guard<std::mutex> guard(cacheMutex);
        auto it = cache.find(key);
        if (it != cache.end()) {
            return it->second;
        }
    }

    XFontStruct* resolved = nullptr;

    const std::string pattern = buildPattern(key);
    resolved = tryLoad(display, pattern);

    if (!resolved) {
        resolved = tryLoad(display, key.family.c_str());
    }

    if (!resolved) {
        Key fallbackKey = key;
        fallbackKey.family = "dejavu-sans";
        resolved = tryLoad(display, buildPattern(fallbackKey));
        if (!resolved) {
            resolved = tryLoad(display, "dejavu sans");
        }
        if (resolved) {
            key = fallbackKey;
        }
    }

    if (!resolved) {
        resolved = tryLoad(display, "-*-sans-*-r-*-*-12-*-*-*-*-*-*-*");
    }

    if (!resolved) {
        resolved = tryLoad(display, "fixed");
    }

    {
        std::lock_guard<std::mutex> guard(cacheMutex);
        cache[key] = resolved;
    }
    return resolved;
}
} // namespace SwLinuxFontCache

inline void SwWidgetPlatformAdapter::setCursor(CursorType) {}

inline void SwWidgetPlatformAdapter::invalidateRect(const SwWidgetPlatformHandle& handle,
                                                    const SwRect& rect) {
    Display* display = reinterpret_cast<Display*>(handle.nativeDisplay);
    ::Window window = reinterpret_cast<::Window>(handle.nativeHandle);
    if (!display || window == 0) {
        return;
    }
    if (!requestSyntheticExpose(window)) {
        return;
    }

    XEvent exposeEvent{};
    exposeEvent.type = Expose;
    exposeEvent.xexpose.window = window;
    exposeEvent.xexpose.x = rect.x;
    exposeEvent.xexpose.y = rect.y;
    exposeEvent.xexpose.width = rect.width > 0 ? rect.width : 1;
    exposeEvent.xexpose.height = rect.height > 0 ? rect.height : 1;
    exposeEvent.xexpose.count = 0;

    XSendEvent(display, window, False, ExposureMask, &exposeEvent);
    XFlush(display);
}

inline SwRect SwWidgetPlatformAdapter::clientRect(const SwWidgetPlatformHandle& handle) {
    Display* display = reinterpret_cast<Display*>(handle.nativeDisplay);
    ::Window window = reinterpret_cast<::Window>(handle.nativeHandle);
    if (!display || window == 0) {
        return SwRect{0, 0, 0, 0};
    }

    XWindowAttributes attrs{};
    if (!XGetWindowAttributes(display, window, &attrs)) {
        return SwRect{0, 0, 0, 0};
    }

    return SwRect{0, 0, std::max(0, attrs.width), std::max(0, attrs.height)};
}

inline size_t SwWidgetPlatformAdapter::characterIndexAtPosition(const SwWidgetPlatformHandle& handle,
                                                                 const SwString& text,
                                                                 SwFont font,
                                                                 int relativeX,
                                                                int defaultWidth) {
    Display* display = reinterpret_cast<Display*>(handle.nativeDisplay);
    if (!display) {
        return approximateIndex(text, relativeX, defaultWidth);
    }

    XFontStruct* fontInfo = SwLinuxFontCache::acquire(display, font);
    if (!fontInfo) {
        return approximateIndex(text, relativeX, defaultWidth);
    }

    std::string utf8 = text.toStdString();
    if (utf8.empty()) {
        return 0;
    }

    if (relativeX <= 0) {
        return 0;
    }

    int accumulated = 0;
    const size_t charCount = utf8.size();
    for (size_t i = 0; i < charCount; ++i) {
        char ch = utf8[i];
        int charWidth = XTextWidth(fontInfo, &ch, 1);
        if (relativeX < accumulated + charWidth) {
            return i;
        }
        accumulated += charWidth;
    }

    return charCount;
}

inline int SwWidgetPlatformAdapter::textWidthUntil(const SwWidgetPlatformHandle& handle,
                                                   const SwString& text,
                                                   SwFont font,
                                                   size_t length,
                                                   int defaultWidth) {
    Display* display = reinterpret_cast<Display*>(handle.nativeDisplay);
    if (!display) {
        return approximateWidth(text, length, defaultWidth);
    }

    XFontStruct* fontInfo = SwLinuxFontCache::acquire(display, font);
    if (!fontInfo) {
        return approximateWidth(text, length, defaultWidth);
    }

    std::string utf8 = text.toStdString();
    if (utf8.empty()) {
        return 0;
    }

    const size_t clampedLength = std::min(length, utf8.size());
    if (clampedLength == 0) {
        return 0;
    }

    XCharStruct overall{};
    int direction = 0;
    int ascent = 0;
    int descent = 0;
    XTextExtents(fontInfo,
                 utf8.c_str(),
                 static_cast<int>(clampedLength),
                 &direction,
                 &ascent,
                 &descent,
                 &overall);
    return overall.width;
}

inline bool SwWidgetPlatformAdapter::isBackspaceKey(int keyCode) {
    return keyCode == XK_BackSpace;
}

inline bool SwWidgetPlatformAdapter::isDeleteKey(int keyCode) {
    return keyCode == XK_Delete;
}

inline bool SwWidgetPlatformAdapter::isLeftArrowKey(int keyCode) {
    return keyCode == XK_Left;
}

inline bool SwWidgetPlatformAdapter::isRightArrowKey(int keyCode) {
    return keyCode == XK_Right;
}

inline bool SwWidgetPlatformAdapter::isUpArrowKey(int keyCode) {
    return keyCode == XK_Up;
}

inline bool SwWidgetPlatformAdapter::isDownArrowKey(int keyCode) {
    return keyCode == XK_Down;
}

inline bool SwWidgetPlatformAdapter::isHomeKey(int keyCode) {
    return keyCode == XK_Home;
}

inline bool SwWidgetPlatformAdapter::isEndKey(int keyCode) {
    return keyCode == XK_End;
}

inline bool SwWidgetPlatformAdapter::isReturnKey(int keyCode) {
    return keyCode == XK_Return || keyCode == XK_KP_Enter;
}

inline bool SwWidgetPlatformAdapter::isEscapeKey(int keyCode) {
    return keyCode == XK_Escape;
}

inline bool SwWidgetPlatformAdapter::isCapsLockKey(int keyCode) {
    return keyCode == XK_Caps_Lock;
}

inline bool SwWidgetPlatformAdapter::matchesShortcutKey(int keyCode, char letter) {
    char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
    char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    KeySym upperSym = upper - 'A' + XK_A;
    KeySym lowerSym = lower - 'a' + XK_a;
    return keyCode == upperSym || keyCode == lowerSym || keyCode == upper || keyCode == lower;
}

inline bool SwWidgetPlatformAdapter::translateCharacter(int keyCode,
                                                        bool shiftPressed,
                                                        bool capsLock,
                                                        char& outChar) {
    if (keyCode >= XK_space && keyCode <= XK_asciitilde) {
        char ch = static_cast<char>(keyCode);
        if (std::isalpha(static_cast<unsigned char>(ch))) {
            bool upper = shiftPressed ^ capsLock;
            ch = upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch)))
                       : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        } else if (shiftPressed) {
            switch (ch) {
            case '1': ch = '!'; break;
            case '2': ch = '@'; break;
            case '3': ch = '#'; break;
            case '4': ch = '$'; break;
            case '5': ch = '%'; break;
            case '6': ch = '^'; break;
            case '7': ch = '&'; break;
            case '8': ch = '*'; break;
            case '9': ch = '('; break;
            case '0': ch = ')'; break;
            case '-': ch = '_'; break;
            case '=': ch = '+'; break;
            case '[': ch = '{'; break;
            case ']': ch = '}'; break;
            case ';': ch = ':'; break;
            case '\'': ch = '"'; break;
            case ',': ch = '<'; break;
            case '.': ch = '>'; break;
            case '/': ch = '?'; break;
            }
        }
        outChar = ch;
        return true;
    }
    return false;
}

inline bool SwWidgetPlatformAdapter::isShiftModifierActive() {
    return false;
}

#else

inline void SwWidgetPlatformAdapter::setCursor(CursorType) {}
inline void SwWidgetPlatformAdapter::invalidateRect(const SwWidgetPlatformHandle&, const SwRect&) {}
inline SwRect SwWidgetPlatformAdapter::clientRect(const SwWidgetPlatformHandle&) { return SwRect{0, 0, 0, 0}; }
inline size_t SwWidgetPlatformAdapter::characterIndexAtPosition(const SwWidgetPlatformHandle&,
                                                                const SwString&,
                                                                SwFont,
                                                                int,
                                                                int) { return 0; }
inline int SwWidgetPlatformAdapter::textWidthUntil(const SwWidgetPlatformHandle&,
                                                   const SwString&,
                                                   SwFont,
                                                   size_t,
                                                   int) { return 0; }
inline bool SwWidgetPlatformAdapter::isBackspaceKey(int) { return false; }
inline bool SwWidgetPlatformAdapter::isDeleteKey(int) { return false; }
inline bool SwWidgetPlatformAdapter::isLeftArrowKey(int) { return false; }
inline bool SwWidgetPlatformAdapter::isRightArrowKey(int) { return false; }
inline bool SwWidgetPlatformAdapter::isUpArrowKey(int) { return false; }
inline bool SwWidgetPlatformAdapter::isDownArrowKey(int) { return false; }
inline bool SwWidgetPlatformAdapter::isHomeKey(int) { return false; }
inline bool SwWidgetPlatformAdapter::isEndKey(int) { return false; }
inline bool SwWidgetPlatformAdapter::isReturnKey(int) { return false; }
inline bool SwWidgetPlatformAdapter::isEscapeKey(int) { return false; }
inline bool SwWidgetPlatformAdapter::isCapsLockKey(int) { return false; }
inline bool SwWidgetPlatformAdapter::matchesShortcutKey(int, char) { return false; }
inline bool SwWidgetPlatformAdapter::translateCharacter(int, bool, bool, char&) { return false; }
inline bool SwWidgetPlatformAdapter::isShiftModifierActive() { return false; }

#endif

inline size_t SwWidgetPlatformAdapter::approximateIndex(const SwString& text,
                                                        int relativeX,
                                                        int defaultWidth) {
    int availableWidth = std::max(1, defaultWidth);
    size_t length = text.length();
    if (length == 0) {
        return 0;
    }
    int charWidth = availableWidth / static_cast<int>(length);
    charWidth = std::max(1, charWidth);
    int indexApprox = relativeX / charWidth;
    if (indexApprox < 0) {
        indexApprox = 0;
    } else if (indexApprox > static_cast<int>(length)) {
        indexApprox = static_cast<int>(length);
    }
    return static_cast<size_t>(indexApprox);
}

inline int SwWidgetPlatformAdapter::approximateWidth(const SwString& text,
                                                     size_t length,
                                                     int defaultWidth) {
    size_t total = text.length();
    if (total == 0) {
        return 0;
    }
    double ratio = static_cast<double>(length) / static_cast<double>(total);
    return static_cast<int>(ratio * defaultWidth);
}
