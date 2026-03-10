#pragma once

/**
 * @file src/core/gui/SwWidgetPlatformAdapter.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwWidgetPlatformAdapter in the CoreSw GUI
 * layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the widget platform adapter interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are SwWidgetPlatformHandle and SwWidgetPlatformAdapter.
 *
 * Widget-oriented declarations here usually capture persistent UI state, input handling, layout
 * participation, and paint-time behavior while keeping platform-specific rendering details behind
 * lower layers.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */

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
#include "platform/SwPlatformTarget.h"

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

#if SW_PLATFORM_WIN32
#include "platform/win/SwWindows.h"
#elif SW_PLATFORM_ANDROID
#include "platform/android/SwAndroidPlatformIntegration.h"
#elif SW_PLATFORM_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#endif

struct SwWidgetPlatformHandle {
    void* nativeHandle{nullptr};
    void* nativeDisplay{nullptr};

    /**
     * @brief Returns the current bool.
     * @return The current bool.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    explicit operator bool() const { return nativeHandle != nullptr; }
};

class SwWidgetPlatformAdapter {
public:
    /**
     * @brief Performs the `fromNativeHandle` operation.
     * @param handle Value passed to the method.
     * @param display Value passed to the method.
     * @return The requested from Native Handle.
     */
    static SwWidgetPlatformHandle fromNativeHandle(void* handle, void* display = nullptr) {
        SwWidgetPlatformHandle h;
        h.nativeHandle = handle;
        h.nativeDisplay = display;
        return h;
    }

    template <typename T>
    /**
     * @brief Performs the `nativeHandleAs` operation.
     * @param handle Value passed to the method.
     * @return The requested native Handle As.
     */
    static T nativeHandleAs(const SwWidgetPlatformHandle& handle) {
        return reinterpret_cast<T>(handle.nativeHandle);
    }

    /**
     * @brief Sets the cursor.
     * @param cursor Value passed to the method.
     * @return The requested cursor.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    static void setCursor(CursorType cursor);
    /**
     * @brief Performs the `invalidateRect` operation.
     * @param handle Value passed to the method.
     * @param rect Rectangle used by the operation.
     * @return The requested invalidate Rect.
     */
    static void invalidateRect(const SwWidgetPlatformHandle& handle, const SwRect& rect);
    static void flushDamage();
    /**
     * @brief Performs the `clientRect` operation.
     * @param handle Value passed to the method.
     * @return The requested client Rect.
     */
    static SwRect clientRect(const SwWidgetPlatformHandle& handle);
    /**
     * @brief Returns the client-area origin in screen coordinates.
     * @param handle Value passed to the method.
     * @return The requested client origin on screen.
     */
    static SwPoint clientOriginOnScreen(const SwWidgetPlatformHandle& handle);
    /**
     * @brief Returns the native window frame rectangle in screen coordinates.
     * @param handle Value passed to the method.
     * @return The requested window frame rect on screen.
     */
    static SwRect windowFrameRect(const SwWidgetPlatformHandle& handle);

    /**
     * @brief Performs the `characterIndexAtPosition` operation.
     * @param handle Value passed to the method.
     * @param text Value passed to the method.
     * @param font Font value used by the operation.
     * @param relativeX Value passed to the method.
     * @param defaultWidth Value passed to the method.
     * @return The requested character Index At Position.
     */
    static size_t characterIndexAtPosition(const SwWidgetPlatformHandle& handle,
                                           const SwString& text,
                                           SwFont font,
                                           int relativeX,
                                           int defaultWidth);
    /**
     * @brief Performs the `textWidthUntil` operation.
     * @param handle Value passed to the method.
     * @param text Value passed to the method.
     * @param font Font value used by the operation.
     * @param length Value passed to the method.
     * @param defaultWidth Value passed to the method.
     * @return The requested text Width Until.
     */
    static int textWidthUntil(const SwWidgetPlatformHandle& handle,
                              const SwString& text,
                              SwFont font,
                              size_t length,
                              int defaultWidth);

    /**
     * @brief Returns whether the object reports backspace Key.
     * @param keyCode Value passed to the method.
     * @return The requested backspace Key.
     *
     * @details This query does not modify the object state.
     */
    static bool isBackspaceKey(int keyCode);
    /**
     * @brief Returns whether the object reports delete Key.
     * @param keyCode Value passed to the method.
     * @return The requested delete Key.
     *
     * @details This query does not modify the object state.
     */
    static bool isDeleteKey(int keyCode);
    /**
     * @brief Returns whether the object reports left Arrow Key.
     * @param keyCode Value passed to the method.
     * @return The requested left Arrow Key.
     *
     * @details This query does not modify the object state.
     */
    static bool isLeftArrowKey(int keyCode);
    /**
     * @brief Returns whether the object reports right Arrow Key.
     * @param keyCode Value passed to the method.
     * @return The requested right Arrow Key.
     *
     * @details This query does not modify the object state.
     */
    static bool isRightArrowKey(int keyCode);
    /**
     * @brief Returns whether the object reports up Arrow Key.
     * @param keyCode Value passed to the method.
     * @return The requested up Arrow Key.
     *
     * @details This query does not modify the object state.
     */
    static bool isUpArrowKey(int keyCode);
    /**
     * @brief Returns whether the object reports down Arrow Key.
     * @param keyCode Value passed to the method.
     * @return The requested down Arrow Key.
     *
     * @details This query does not modify the object state.
     */
    static bool isDownArrowKey(int keyCode);
    /**
     * @brief Returns whether the object reports home Key.
     * @param keyCode Value passed to the method.
     * @return The requested home Key.
     *
     * @details This query does not modify the object state.
     */
    static bool isHomeKey(int keyCode);
    /**
     * @brief Returns whether the object reports end Key.
     * @param keyCode Value passed to the method.
     * @return The requested end Key.
     *
     * @details This query does not modify the object state.
     */
    static bool isEndKey(int keyCode);
    /**
     * @brief Returns whether the object reports return Key.
     * @param keyCode Value passed to the method.
     * @return The requested return Key.
     *
     * @details This query does not modify the object state.
     */
    static bool isReturnKey(int keyCode);
    /**
     * @brief Returns whether the object reports escape Key.
     * @param keyCode Value passed to the method.
     * @return The requested escape Key.
     *
     * @details This query does not modify the object state.
     */
    static bool isEscapeKey(int keyCode);
    /**
     * @brief Returns whether the object reports caps Lock Key.
     * @param keyCode Value passed to the method.
     * @return The requested caps Lock Key.
     *
     * @details This query does not modify the object state.
     */
    static bool isCapsLockKey(int keyCode);
    /**
     * @brief Performs the `matchesShortcutKey` operation.
     * @param keyCode Value passed to the method.
     * @param letter Value passed to the method.
     * @return The requested matches Shortcut Key.
     */
    static bool matchesShortcutKey(int keyCode, char letter);
    /**
     * @brief Performs the `translateCharacter` operation.
     * @param keyCode Value passed to the method.
     * @param shiftPressed Value passed to the method.
     * @param capsLock Value passed to the method.
     * @param outChar Output value filled by the method.
     * @return The requested translate Character.
     */
    static bool translateCharacter(int keyCode, bool shiftPressed, bool capsLock, char& outChar);
    /**
     * @brief Returns whether the object reports shift Modifier Active.
     * @return The current shift Modifier Active.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static bool isShiftModifierActive();
#if SW_PLATFORM_X11
    /**
     * @brief Performs the `requestSyntheticExpose` operation.
     * @param window Value passed to the method.
     * @return The requested request Synthetic Expose.
     */
    static bool requestSyntheticExpose(::Window window);
    /**
     * @brief Performs the `finishSyntheticExpose` operation.
     * @param window Value passed to the method.
     * @return The requested finish Synthetic Expose.
     */
    static void finishSyntheticExpose(::Window window);
#endif

private:
#if SW_PLATFORM_X11
    struct LinuxExposeTracker {
        std::mutex mutex;
        std::unordered_set<std::uintptr_t> pending;
    };
    static LinuxExposeTracker& exposeTracker();
#endif
    static size_t approximateIndex(const SwString& text, int relativeX, int defaultWidth);
    static int approximateWidth(const SwString& text, size_t length, int defaultWidth);
};

inline bool swWidgetPlatformIsUtf8ContinuationByte_(unsigned char byte) {
    return (byte & 0xC0U) == 0x80U;
}

inline size_t swWidgetPlatformClampUtf8Boundary_(const std::string& utf8, size_t offset) {
    if (offset >= utf8.size()) {
        return utf8.size();
    }
    while (offset > 0 && swWidgetPlatformIsUtf8ContinuationByte_(static_cast<unsigned char>(utf8[offset]))) {
        --offset;
    }
    return offset;
}

inline size_t swWidgetPlatformNextUtf8Boundary_(const std::string& utf8, size_t offset) {
    if (offset >= utf8.size()) {
        return utf8.size();
    }
    size_t next = swWidgetPlatformClampUtf8Boundary_(utf8, offset) + 1;
    while (next < utf8.size() && swWidgetPlatformIsUtf8ContinuationByte_(static_cast<unsigned char>(utf8[next]))) {
        ++next;
    }
    return next;
}

#if SW_PLATFORM_WIN32

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

// ── Damage-region accumulator ────────────────────────────────────────
// Instead of issuing one InvalidateRect per widget, we accumulate dirty
// rects into a single union per HWND.  flushDamage() issues one
// InvalidateRect call with the accumulated region.  This turns O(N)
// syscalls per update() tree walk into O(1).
inline std::map<HWND, RECT>& pendingDamage_() {
    static std::map<HWND, RECT> s_damage;
    return s_damage;
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

    auto& damage = pendingDamage_();
    auto it = damage.find(hwnd);
    if (it == damage.end()) {
        damage[hwnd] = r;
    } else {
        // Union of existing damage and new rect
        RECT& d = it->second;
        if (r.left   < d.left)   d.left   = r.left;
        if (r.top    < d.top)    d.top    = r.top;
        if (r.right  > d.right)  d.right  = r.right;
        if (r.bottom > d.bottom) d.bottom = r.bottom;
    }
}

inline void SwWidgetPlatformAdapter::flushDamage() {
    auto& damage = pendingDamage_();
    for (auto& kv : damage) {
        ::InvalidateRect(kv.first, &kv.second, FALSE);
    }
    damage.clear();
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

inline SwPoint SwWidgetPlatformAdapter::clientOriginOnScreen(const SwWidgetPlatformHandle& handle) {
    HWND hwnd = nativeHandleAs<HWND>(handle);
    if (!hwnd) {
        return SwPoint{0, 0};
    }
    POINT pt{};
    if (!::ClientToScreen(hwnd, &pt)) {
        return SwPoint{0, 0};
    }
    return SwPoint{static_cast<int>(pt.x), static_cast<int>(pt.y)};
}

inline SwRect SwWidgetPlatformAdapter::windowFrameRect(const SwWidgetPlatformHandle& handle) {
    HWND hwnd = nativeHandleAs<HWND>(handle);
    if (!hwnd) {
        return SwRect{0, 0, 0, 0};
    }
    RECT windowRect{};
    if (!::GetWindowRect(hwnd, &windowRect)) {
        return SwRect{0, 0, 0, 0};
    }
    return SwRect{
        static_cast<int>(windowRect.left),
        static_cast<int>(windowRect.top),
        std::max(0, static_cast<int>(windowRect.right - windowRect.left)),
        std::max(0, static_cast<int>(windowRect.bottom - windowRect.top))
    };
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
    const std::string utf8 = text.toStdString();
    for (; index < utf8.size();) {
        const size_t next = swWidgetPlatformNextUtf8Boundary_(utf8, index);
        const SwString segment(utf8.data() + index, next - index);
        std::wstring wide;
        try {
            wide = segment.toStdWString();
        } catch (...) {
            index = next;
            continue;
        }
        if (!GetTextExtentPoint32W(hdc, wide.c_str(), static_cast<int>(wide.size()), &charSize)) {
            index = next;
            continue;
        }
        if (currentX + charSize.cx / 2 >= relativeX) {
            break;
        }
        currentX += charSize.cx;
        index = next;
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
    const std::string utf8 = text.toStdString();
    const size_t clampedLength = swWidgetPlatformClampUtf8Boundary_(utf8, length);
    if (clampedLength > 0) {
        const SwString segment(utf8.data(), clampedLength);
        try {
            const std::wstring wide = segment.toStdWString();
            if (!wide.empty()) {
                GetTextExtentPoint32W(hdc, wide.c_str(), static_cast<int>(wide.size()), &textSize);
            }
        } catch (...) {
            textSize.cx = approximateWidth(text, clampedLength, defaultWidth);
        }
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

#elif SW_PLATFORM_ANDROID

inline void SwWidgetPlatformAdapter::setCursor(CursorType) {}

inline void SwWidgetPlatformAdapter::invalidateRect(const SwWidgetPlatformHandle& handle,
                                                    const SwRect&) {
    SwAndroidPlatformWindow* window =
        nativeHandleAs<SwAndroidPlatformWindow*>(handle);
    if (!window) {
        return;
    }
    window->requestUpdate();
}

inline void SwWidgetPlatformAdapter::flushDamage() {}

inline SwRect SwWidgetPlatformAdapter::clientRect(const SwWidgetPlatformHandle& handle) {
    SwAndroidPlatformWindow* window =
        nativeHandleAs<SwAndroidPlatformWindow*>(handle);
    if (!window) {
        return SwRect{0, 0, 0, 0};
    }
    return window->clientRect();
}

inline SwPoint SwWidgetPlatformAdapter::clientOriginOnScreen(const SwWidgetPlatformHandle&) {
    return SwPoint{0, 0};
}

inline SwRect SwWidgetPlatformAdapter::windowFrameRect(const SwWidgetPlatformHandle& handle) {
    return clientRect(handle);
}

inline size_t SwWidgetPlatformAdapter::characterIndexAtPosition(const SwWidgetPlatformHandle&,
                                                                const SwString& text,
                                                                SwFont font,
                                                                int relativeX,
                                                                int defaultWidth) {
    const int advance = std::max(1, swandroid::charAdvanceForFont(font));
    if (advance <= 0) {
        return approximateIndex(text, relativeX, defaultWidth);
    }
    const int clamped = std::max(0, relativeX);
    const size_t approx = static_cast<size_t>(clamped / advance);
    return std::min(approx, text.length());
}

inline int SwWidgetPlatformAdapter::textWidthUntil(const SwWidgetPlatformHandle&,
                                                   const SwString& text,
                                                   SwFont font,
                                                   size_t length,
                                                   int defaultWidth) {
    const size_t clampedLength = std::min(length, text.length());
    const int advance = std::max(1, swandroid::charAdvanceForFont(font));
    if (advance <= 0) {
        return approximateWidth(text, clampedLength, defaultWidth);
    }
    return static_cast<int>(clampedLength) * advance;
}

inline bool SwWidgetPlatformAdapter::isBackspaceKey(int keyCode) {
    return keyCode == AKEYCODE_DEL;
}

inline bool SwWidgetPlatformAdapter::isDeleteKey(int keyCode) {
    return keyCode == AKEYCODE_FORWARD_DEL;
}

inline bool SwWidgetPlatformAdapter::isLeftArrowKey(int keyCode) {
    return keyCode == AKEYCODE_DPAD_LEFT;
}

inline bool SwWidgetPlatformAdapter::isRightArrowKey(int keyCode) {
    return keyCode == AKEYCODE_DPAD_RIGHT;
}

inline bool SwWidgetPlatformAdapter::isUpArrowKey(int keyCode) {
    return keyCode == AKEYCODE_DPAD_UP;
}

inline bool SwWidgetPlatformAdapter::isDownArrowKey(int keyCode) {
    return keyCode == AKEYCODE_DPAD_DOWN;
}

inline bool SwWidgetPlatformAdapter::isHomeKey(int keyCode) {
    return keyCode == AKEYCODE_MOVE_HOME || keyCode == AKEYCODE_HOME;
}

inline bool SwWidgetPlatformAdapter::isEndKey(int keyCode) {
    return keyCode == AKEYCODE_MOVE_END;
}

inline bool SwWidgetPlatformAdapter::isReturnKey(int keyCode) {
    return keyCode == AKEYCODE_ENTER || keyCode == AKEYCODE_NUMPAD_ENTER;
}

inline bool SwWidgetPlatformAdapter::isEscapeKey(int keyCode) {
    return keyCode == AKEYCODE_ESCAPE || keyCode == AKEYCODE_BACK;
}

inline bool SwWidgetPlatformAdapter::isCapsLockKey(int keyCode) {
    return keyCode == AKEYCODE_CAPS_LOCK;
}

inline bool SwWidgetPlatformAdapter::matchesShortcutKey(int keyCode, char letter) {
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
    if (upper < 'A' || upper > 'Z') {
        return false;
    }
    return keyCode == (AKEYCODE_A + (upper - 'A'));
}

inline bool SwWidgetPlatformAdapter::translateCharacter(int keyCode,
                                                        bool shiftPressed,
                                                        bool capsLock,
                                                        char& outChar) {
    if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z) {
        char base = static_cast<char>('a' + (keyCode - AKEYCODE_A));
        const bool upper = shiftPressed ^ capsLock;
        outChar = upper
            ? static_cast<char>(std::toupper(static_cast<unsigned char>(base)))
            : base;
        return true;
    }

    if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9) {
        static const char shifted[] = {')', '!', '@', '#', '$', '%', '^', '&', '*', '('};
        outChar = shiftPressed
            ? shifted[keyCode - AKEYCODE_0]
            : static_cast<char>('0' + (keyCode - AKEYCODE_0));
        return true;
    }

    switch (keyCode) {
    case AKEYCODE_SPACE: outChar = ' '; return true;
    case AKEYCODE_MINUS: outChar = shiftPressed ? '_' : '-'; return true;
    case AKEYCODE_EQUALS: outChar = shiftPressed ? '+' : '='; return true;
    case AKEYCODE_LEFT_BRACKET: outChar = shiftPressed ? '{' : '['; return true;
    case AKEYCODE_RIGHT_BRACKET: outChar = shiftPressed ? '}' : ']'; return true;
    case AKEYCODE_SEMICOLON: outChar = shiftPressed ? ':' : ';'; return true;
    case AKEYCODE_APOSTROPHE: outChar = shiftPressed ? '"' : '\''; return true;
    case AKEYCODE_COMMA: outChar = shiftPressed ? '<' : ','; return true;
    case AKEYCODE_PERIOD: outChar = shiftPressed ? '>' : '.'; return true;
    case AKEYCODE_SLASH: outChar = shiftPressed ? '?' : '/'; return true;
    case AKEYCODE_BACKSLASH: outChar = shiftPressed ? '|' : '\\'; return true;
    case AKEYCODE_GRAVE: outChar = shiftPressed ? '~' : '`'; return true;
    default: break;
    }
    return false;
}

inline bool SwWidgetPlatformAdapter::isShiftModifierActive() {
    return false;
}

#elif SW_PLATFORM_X11

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

    /**
     * @brief Performs the `operator<` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
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

inline void SwWidgetPlatformAdapter::flushDamage() {
    // X11: invalidateRect already sends XSendEvent + XFlush per call,
    // so there is no pending damage to flush.  This is a no-op.
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

inline SwPoint SwWidgetPlatformAdapter::clientOriginOnScreen(const SwWidgetPlatformHandle& handle) {
    Display* display = reinterpret_cast<Display*>(handle.nativeDisplay);
    ::Window window = reinterpret_cast<::Window>(handle.nativeHandle);
    if (!display || window == 0) {
        return SwPoint{0, 0};
    }

    ::Window root = DefaultRootWindow(display);
    ::Window child = 0;
    int rootX = 0;
    int rootY = 0;
    if (!XTranslateCoordinates(display, window, root, 0, 0, &rootX, &rootY, &child)) {
        return SwPoint{0, 0};
    }
    return SwPoint{rootX, rootY};
}

inline SwRect SwWidgetPlatformAdapter::windowFrameRect(const SwWidgetPlatformHandle& handle) {
    const SwPoint origin = clientOriginOnScreen(handle);
    const SwRect client = clientRect(handle);
    return SwRect{origin.x, origin.y, client.width, client.height};
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
inline void SwWidgetPlatformAdapter::flushDamage() {}
inline SwRect SwWidgetPlatformAdapter::clientRect(const SwWidgetPlatformHandle&) { return SwRect{0, 0, 0, 0}; }
inline SwPoint SwWidgetPlatformAdapter::clientOriginOnScreen(const SwWidgetPlatformHandle&) { return SwPoint{0, 0}; }
inline SwRect SwWidgetPlatformAdapter::windowFrameRect(const SwWidgetPlatformHandle&) { return SwRect{0, 0, 0, 0}; }
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
