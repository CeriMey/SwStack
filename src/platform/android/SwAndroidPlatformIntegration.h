#pragma once

#include "platform/SwPlatformTarget.h"
#include "platform/SwPlatformIntegration.h"

#if SW_PLATFORM_ANDROID

#include "core/gui/graphics/SwImage.h"

#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_window.h>
#if __has_include(<android/native_window_jni.h>)
#include <android/native_window_jni.h>
#endif
#if __has_include(<android/window.h>)
#include <android/window.h>
#endif

#if __has_include(<android_native_app_glue.h>)
#include <android_native_app_glue.h>
#define SW_ANDROID_HAS_NATIVE_APP_GLUE 1
#else
struct android_app;
struct android_poll_source;
#define SW_ANDROID_HAS_NATIVE_APP_GLUE 0
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class SwAndroidPlatformIntegration;

namespace swandroid {

inline void logVerbose(const char* message) {
    __android_log_write(ANDROID_LOG_VERBOSE,
                        "SwAndroidPlatform",
                        message ? message : "null");
}

enum class QueuedEventType {
    SurfaceChanged,
    SurfaceLost,
    TouchDown,
    TouchMove,
    TouchUp,
    KeyDown,
    KeyUp,
    DeleteRequest
};

struct QueuedEvent {
    QueuedEventType type{QueuedEventType::TouchMove};
    int x{0};
    int y{0};
    int width{0};
    int height{0};
    int keyCode{0};
    bool ctrl{false};
    bool shift{false};
    bool alt{false};

    QueuedEvent() = default;
    explicit QueuedEvent(QueuedEventType eventType)
        : type(eventType) {}
};

struct SharedState {
    std::mutex mutex;
    android_app* app{nullptr};
    ANativeWindow* window{nullptr};
    int width{0};
    int height{0};
    bool frameRequested{false};
    bool deleteQueued{false};
    std::deque<QueuedEvent> events;
};

inline SharedState& sharedState() {
    static SharedState state;
    return state;
}

inline void queueEvent(const QueuedEvent& event) {
    SharedState& shared = sharedState();
    std::lock_guard<std::mutex> lock(shared.mutex);
    shared.events.push_back(event);
}

inline std::deque<QueuedEvent> takeEvents() {
    SharedState& shared = sharedState();
    std::deque<QueuedEvent> result;
    std::lock_guard<std::mutex> lock(shared.mutex);
    result.swap(shared.events);
    return result;
}

inline SwPlatformSize currentSurfaceSize() {
    SharedState& shared = sharedState();
    std::lock_guard<std::mutex> lock(shared.mutex);
    return SwPlatformSize{shared.width, shared.height};
}

inline ANativeWindow* acquireNativeWindow() {
    SharedState& shared = sharedState();
    std::lock_guard<std::mutex> lock(shared.mutex);
    if (!shared.window) {
        return nullptr;
    }
    ANativeWindow_acquire(shared.window);
    return shared.window;
}

inline bool consumeFrameRequest() {
    SharedState& shared = sharedState();
    std::lock_guard<std::mutex> lock(shared.mutex);
    const bool requested = shared.frameRequested;
    shared.frameRequested = false;
    return requested;
}

inline void requestFrame() {
    SharedState& shared = sharedState();
    android_app* app = nullptr;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.frameRequested = true;
        app = shared.app;
    }
#if SW_ANDROID_HAS_NATIVE_APP_GLUE
    if (app && app->looper) {
        ALooper_wake(app->looper);
    }
#else
    (void)app;
#endif
}

inline void updateSharedSurface(ANativeWindow* window) {
    SharedState& shared = sharedState();
    QueuedEvent event;
    if (window) {
        ANativeWindow_setBuffersGeometry(window, 0, 0, WINDOW_FORMAT_RGBA_8888);
        event.type = QueuedEventType::SurfaceChanged;
        event.width = std::max(1, ANativeWindow_getWidth(window));
        event.height = std::max(1, ANativeWindow_getHeight(window));
    } else {
        event.type = QueuedEventType::SurfaceLost;
    }

    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        if (shared.window == window) {
            if (shared.window) {
                shared.width = event.width;
                shared.height = event.height;
            }
        } else {
            if (shared.window) {
                ANativeWindow_release(shared.window);
            }
            shared.window = window;
            if (shared.window) {
                ANativeWindow_acquire(shared.window);
                shared.width = event.width;
                shared.height = event.height;
            } else {
                shared.width = 0;
                shared.height = 0;
            }
        }
        shared.events.push_back(event);
        shared.frameRequested = true;
    }
}

inline void clearSharedSurface() {
    updateSharedSurface(nullptr);
}

inline bool ctrlFromMeta(int metaState) {
    return (metaState & (AMETA_CTRL_ON | AMETA_CTRL_LEFT_ON | AMETA_CTRL_RIGHT_ON)) != 0;
}

inline bool shiftFromMeta(int metaState) {
    return (metaState & (AMETA_SHIFT_ON | AMETA_SHIFT_LEFT_ON | AMETA_SHIFT_RIGHT_ON)) != 0;
}

inline bool altFromMeta(int metaState) {
    return (metaState & (AMETA_ALT_ON | AMETA_ALT_LEFT_ON | AMETA_ALT_RIGHT_ON)) != 0;
}

inline void queueTouchEvent(QueuedEventType type,
                            int x,
                            int y,
                            bool ctrl,
                            bool shift,
                            bool alt) {
    QueuedEvent event;
    event.type = type;
    event.x = x;
    event.y = y;
    event.ctrl = ctrl;
    event.shift = shift;
    event.alt = alt;
    queueEvent(event);
}

inline void queueKeyEvent(QueuedEventType type,
                          int keyCode,
                          bool ctrl,
                          bool shift,
                          bool alt) {
    QueuedEvent event;
    event.type = type;
    event.keyCode = keyCode;
    event.ctrl = ctrl;
    event.shift = shift;
    event.alt = alt;
    queueEvent(event);
}

#if SW_ANDROID_HAS_NATIVE_APP_GLUE

inline int32_t handleInputEvent(android_app*, AInputEvent* event) {
    if (!event) {
        return 0;
    }

    const int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        const int32_t action = AMotionEvent_getAction(event);
        const int32_t actionMasked = action & AMOTION_EVENT_ACTION_MASK;
        const size_t pointerIndex = static_cast<size_t>((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                                                        >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        const size_t pointerCount = static_cast<size_t>(AMotionEvent_getPointerCount(event));
        if (pointerCount == 0) {
            return 0;
        }

        const size_t index = std::min(pointerIndex, pointerCount - 1);
        const int x = static_cast<int>(AMotionEvent_getX(event, index));
        const int y = static_cast<int>(AMotionEvent_getY(event, index));
        const int metaState = AMotionEvent_getMetaState(event);
        const bool ctrl = ctrlFromMeta(metaState);
        const bool shift = shiftFromMeta(metaState);
        const bool alt = altFromMeta(metaState);

        switch (actionMasked) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            queueTouchEvent(QueuedEventType::TouchDown, x, y, ctrl, shift, alt);
            requestFrame();
            return 1;
        case AMOTION_EVENT_ACTION_MOVE:
            queueTouchEvent(QueuedEventType::TouchMove, x, y, ctrl, shift, alt);
            requestFrame();
            return 1;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
        case AMOTION_EVENT_ACTION_CANCEL:
            queueTouchEvent(QueuedEventType::TouchUp, x, y, ctrl, shift, alt);
            requestFrame();
            return 1;
        default:
            return 0;
        }
    }

    if (type == AINPUT_EVENT_TYPE_KEY) {
        const int32_t action = AKeyEvent_getAction(event);
        const int metaState = AKeyEvent_getMetaState(event);
        const bool ctrl = ctrlFromMeta(metaState);
        const bool shift = shiftFromMeta(metaState);
        const bool alt = altFromMeta(metaState);
        const int keyCode = static_cast<int>(AKeyEvent_getKeyCode(event));

        if (action == AKEY_EVENT_ACTION_DOWN) {
            queueKeyEvent(QueuedEventType::KeyDown, keyCode, ctrl, shift, alt);
            requestFrame();
            return 1;
        }
        if (action == AKEY_EVENT_ACTION_UP) {
            queueKeyEvent(QueuedEventType::KeyUp, keyCode, ctrl, shift, alt);
            requestFrame();
            return 1;
        }
    }

    return 0;
}

inline void handleAppCommand(android_app*, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW: {
        ANativeWindow* window = nullptr;
        {
            SharedState& shared = sharedState();
            std::lock_guard<std::mutex> lock(shared.mutex);
            window = shared.app ? shared.app->window : nullptr;
        }
        updateSharedSurface(window);
        break;
    }
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONTENT_RECT_CHANGED: {
        ANativeWindow* window = nullptr;
        {
            SharedState& shared = sharedState();
            std::lock_guard<std::mutex> lock(shared.mutex);
            window = shared.app ? shared.app->window : nullptr;
        }
        updateSharedSurface(window);
        break;
    }
    case APP_CMD_TERM_WINDOW:
        clearSharedSurface();
        break;
    case APP_CMD_DESTROY: {
        SharedState& shared = sharedState();
        std::lock_guard<std::mutex> lock(shared.mutex);
        if (!shared.deleteQueued) {
            shared.deleteQueued = true;
            shared.events.push_back(QueuedEvent{QueuedEventType::DeleteRequest});
        }
        break;
    }
    case APP_CMD_GAINED_FOCUS:
        requestFrame();
        break;
    default:
        break;
    }
}

inline void bindApp(android_app* app) {
    SharedState& shared = sharedState();
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.app = app;
        shared.deleteQueued = false;
    }
    if (!app) {
        return;
    }
    app->onAppCmd = &handleAppCommand;
    app->onInputEvent = &handleInputEvent;
}

inline void processGlueEvents() {
    SharedState& shared = sharedState();
    android_app* app = nullptr;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        app = shared.app;
    }
    if (!app || !app->looper) {
        return;
    }

    while (true) {
        int events = 0;
        android_poll_source* source = nullptr;
        const int ident = ALooper_pollOnce(0, nullptr, &events, reinterpret_cast<void**>(&source));
        if (ident < 0) {
            break;
        }
        if (source) {
            source->process(app, source);
        }
    }

    if (app->destroyRequested) {
        std::lock_guard<std::mutex> lock(shared.mutex);
        if (!shared.deleteQueued) {
            shared.deleteQueued = true;
            shared.events.push_back(QueuedEvent{QueuedEventType::DeleteRequest});
        }
    }
}

#else

inline void bindApp(android_app*) {}
inline void processGlueEvents() {}

#endif

inline int clampByte(int value) {
    return std::max(0, std::min(255, value));
}

inline std::uint32_t toArgb32(const SwColor& color, int alpha = 255) {
    const std::uint32_t a = static_cast<std::uint32_t>(clampByte(alpha));
    const std::uint32_t r = static_cast<std::uint32_t>(clampByte(color.r));
    const std::uint32_t g = static_cast<std::uint32_t>(clampByte(color.g));
    const std::uint32_t b = static_cast<std::uint32_t>(clampByte(color.b));
    return (a << 24) | (r << 16) | (g << 8) | b;
}

inline std::uint8_t alphaOf(std::uint32_t argb) {
    return static_cast<std::uint8_t>((argb >> 24) & 0xFFu);
}

inline std::uint8_t redOf(std::uint32_t argb) {
    return static_cast<std::uint8_t>((argb >> 16) & 0xFFu);
}

inline std::uint8_t greenOf(std::uint32_t argb) {
    return static_cast<std::uint8_t>((argb >> 8) & 0xFFu);
}

inline std::uint8_t blueOf(std::uint32_t argb) {
    return static_cast<std::uint8_t>(argb & 0xFFu);
}

inline std::uint32_t blendOver(std::uint32_t dst, std::uint32_t src) {
    const std::uint32_t srcA = alphaOf(src);
    if (srcA >= 255u) {
        return src;
    }
    if (srcA == 0u) {
        return dst;
    }

    const std::uint32_t invA = 255u - srcA;
    const std::uint32_t outA = srcA + (alphaOf(dst) * invA + 127u) / 255u;
    const std::uint32_t outR = (redOf(src) * srcA + redOf(dst) * invA + 127u) / 255u;
    const std::uint32_t outG = (greenOf(src) * srcA + greenOf(dst) * invA + 127u) / 255u;
    const std::uint32_t outB = (blueOf(src) * srcA + blueOf(dst) * invA + 127u) / 255u;
    return (outA << 24) | (outR << 16) | (outG << 8) | outB;
}

inline int charAdvanceForFont(const SwFont& font) {
    return std::max(6, font.getPointSize() <= 0 ? 9 : font.getPointSize());
}

inline int lineHeightForFont(const SwFont& font) {
    return std::max(8, charAdvanceForFont(font) + 2);
}

inline int glyphScaleForFont(const SwFont& font) {
    return std::max(1, (charAdvanceForFont(font) + 5) / 6);
}

struct GlyphRows {
    std::array<std::uint8_t, 7> rows{};

    GlyphRows() = default;
    GlyphRows(std::initializer_list<std::uint8_t> values) {
        std::size_t index = 0;
        for (std::initializer_list<std::uint8_t>::const_iterator it = values.begin();
             it != values.end() && index < rows.size();
             ++it, ++index) {
            rows[index] = *it;
        }
    }
};

inline GlyphRows glyphForChar(char ch) {
    const unsigned char uchar = static_cast<unsigned char>(ch);
    const char upper = static_cast<char>(std::toupper(uchar));

    switch (upper) {
    case 'A': return {{0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
    case 'B': return {{0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}};
    case 'C': return {{0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}};
    case 'D': return {{0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}};
    case 'E': return {{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}};
    case 'F': return {{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}};
    case 'G': return {{0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}};
    case 'H': return {{0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
    case 'I': return {{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}};
    case 'J': return {{0x1F, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C}};
    case 'K': return {{0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}};
    case 'L': return {{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}};
    case 'M': return {{0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}};
    case 'N': return {{0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}};
    case 'O': return {{0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
    case 'P': return {{0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}};
    case 'Q': return {{0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}};
    case 'R': return {{0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}};
    case 'S': return {{0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}};
    case 'T': return {{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}};
    case 'U': return {{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
    case 'V': return {{0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}};
    case 'W': return {{0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}};
    case 'X': return {{0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}};
    case 'Y': return {{0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}};
    case 'Z': return {{0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}};
    case '0': return {{0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}};
    case '1': return {{0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F}};
    case '2': return {{0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}};
    case '3': return {{0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}};
    case '4': return {{0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}};
    case '5': return {{0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}};
    case '6': return {{0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}};
    case '7': return {{0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}};
    case '8': return {{0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}};
    case '9': return {{0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}};
    case ' ': return {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
    case '.': return {{0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}};
    case ',': return {{0x00, 0x00, 0x00, 0x00, 0x06, 0x04, 0x08}};
    case ':': return {{0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00}};
    case ';': return {{0x00, 0x06, 0x06, 0x00, 0x06, 0x04, 0x08}};
    case '!': return {{0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}};
    case '?': return {{0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}};
    case '-': return {{0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}};
    case '_': return {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}};
    case '+': return {{0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}};
    case '/': return {{0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}};
    case '\\': return {{0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01}};
    case '(': return {{0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}};
    case ')': return {{0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}};
    case '[': return {{0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}};
    case ']': return {{0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}};
    case '=': return {{0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}};
    case '\'': return {{0x06, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00}};
    case '"': return {{0x0A, 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00}};
    case '#': return {{0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}};
    case '&': return {{0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D}};
    case '%': return {{0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13}};
    case '*': return {{0x00, 0x15, 0x0E, 0x1F, 0x0E, 0x15, 0x00}};
    default: return {{0x1F, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}};
    }
}

inline SwRect intersectRects(const SwRect& a, const SwRect& b) {
    const int x1 = std::max(a.x, b.x);
    const int y1 = std::max(a.y, b.y);
    const int x2 = std::min(a.x + a.width, b.x + b.width);
    const int y2 = std::min(a.y + a.height, b.y + b.height);
    return SwRect{x1, y1, std::max(0, x2 - x1), std::max(0, y2 - y1)};
}

inline bool isInsideRoundedRect(const SwRect& rect, int radius, int x, int y) {
    if (rect.width <= 0 || rect.height <= 0) {
        return false;
    }

    const int clampedRadius = std::max(0, std::min(radius, std::min(rect.width, rect.height) / 2));
    if (clampedRadius <= 0) {
        return x >= rect.x && y >= rect.y &&
               x < rect.x + rect.width &&
               y < rect.y + rect.height;
    }

    const int left = rect.x;
    const int top = rect.y;
    const int right = rect.x + rect.width - 1;
    const int bottom = rect.y + rect.height - 1;
    if (x < left || x > right || y < top || y > bottom) {
        return false;
    }

    const int innerLeft = left + clampedRadius;
    const int innerRight = right - clampedRadius;
    if (x >= innerLeft && x <= innerRight) {
        return true;
    }

    const int innerTop = top + clampedRadius;
    const int innerBottom = bottom - clampedRadius;
    if (y >= innerTop && y <= innerBottom) {
        return true;
    }

    const auto insideCorner = [x, y, clampedRadius](int cx, int cy) {
        const int dx = x - cx;
        const int dy = y - cy;
        return dx * dx + dy * dy <= clampedRadius * clampedRadius;
    };

    if (x < innerLeft && y < innerTop) {
        return insideCorner(innerLeft, innerTop);
    }
    if (x > innerRight && y < innerTop) {
        return insideCorner(innerRight, innerTop);
    }
    if (x < innerLeft && y > innerBottom) {
        return insideCorner(innerLeft, innerBottom);
    }
    if (x > innerRight && y > innerBottom) {
        return insideCorner(innerRight, innerBottom);
    }
    return true;
}

} // namespace swandroid

class SwAndroidPlatformImage : public SwPlatformImage {
public:
    SwAndroidPlatformImage(const SwPlatformSize& size, SwPixelFormat format)
        : m_size(size),
          m_format(format == SwPixelFormat::Unknown ? SwPixelFormat::ARGB32 : format) {
        const int bytesPerPixel =
            (m_format == SwPixelFormat::RGB24 || m_format == SwPixelFormat::BGR24) ? 3 : 4;
        m_pitch = std::max(0, m_size.width) * bytesPerPixel;
        m_pixels.resize(static_cast<std::size_t>(m_pitch) * std::max(0, m_size.height));
    }

    SwPlatformSize size() const override { return m_size; }
    SwPixelFormat format() const override { return m_format; }
    int pitch() const override { return m_pitch; }
    std::uint8_t* pixels() override { return m_pixels.empty() ? nullptr : m_pixels.data(); }
    const std::uint8_t* pixels() const override { return m_pixels.empty() ? nullptr : m_pixels.data(); }

    void clear(std::uint32_t argb) override {
        if (m_pixels.empty()) {
            return;
        }
        const std::uint8_t a = static_cast<std::uint8_t>((argb >> 24) & 0xFFu);
        const std::uint8_t r = static_cast<std::uint8_t>((argb >> 16) & 0xFFu);
        const std::uint8_t g = static_cast<std::uint8_t>((argb >> 8) & 0xFFu);
        const std::uint8_t b = static_cast<std::uint8_t>(argb & 0xFFu);
        const int bytesPerPixel =
            (m_format == SwPixelFormat::RGB24 || m_format == SwPixelFormat::BGR24) ? 3 : 4;

        for (int y = 0; y < m_size.height; ++y) {
            std::uint8_t* row = m_pixels.data() + static_cast<std::size_t>(y) * m_pitch;
            for (int x = 0; x < m_size.width; ++x) {
                std::uint8_t* pixel = row + x * bytesPerPixel;
                if (m_format == SwPixelFormat::RGB24) {
                    pixel[0] = r;
                    pixel[1] = g;
                    pixel[2] = b;
                } else if (m_format == SwPixelFormat::BGR24) {
                    pixel[0] = b;
                    pixel[1] = g;
                    pixel[2] = r;
                } else if (m_format == SwPixelFormat::ABGR32) {
                    pixel[0] = a;
                    pixel[1] = b;
                    pixel[2] = g;
                    pixel[3] = r;
                } else {
                    pixel[0] = r;
                    pixel[1] = g;
                    pixel[2] = b;
                    pixel[3] = a;
                }
            }
        }
    }

private:
    SwPlatformSize m_size{};
    SwPixelFormat m_format{SwPixelFormat::ARGB32};
    int m_pitch{0};
    std::vector<std::uint8_t> m_pixels;
};

class SwAndroidPainter : public SwPlatformPainter {
public:
    SwAndroidPainter() = default;
    ~SwAndroidPainter() override { end(); }

    void begin(const SwPlatformPaintEvent& event) override {
        end();

        m_window = static_cast<ANativeWindow*>(event.nativePaintDevice);
        if (!m_window) {
            return;
        }

        m_width = std::max(1, event.surfaceSize.width);
        m_height = std::max(1, event.surfaceSize.height);
        m_buffer.assign(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height),
                        0xFFF9F9F9u);
        m_finalizeRequested = false;
        m_presented = false;
        m_clipStack.clear();
    }

    void end() override {
        m_finalizeRequested = true;
        presentIfNeeded_();
        if (m_window) {
            ANativeWindow_release(m_window);
            m_window = nullptr;
        }
        m_buffer.clear();
        m_clipStack.clear();
        m_width = 0;
        m_height = 0;
        m_finalizeRequested = false;
        m_presented = false;
    }

    void flush() override {
        m_finalizeRequested = true;
        presentIfNeeded_();
    }

    void clear(const SwColor& color) override {
        if (m_buffer.empty()) {
            return;
        }
        std::fill(m_buffer.begin(), m_buffer.end(), swandroid::toArgb32(color));
    }

    void fillRect(const SwRect& rect,
                  const SwColor& fillColor,
                  const SwColor& borderColor,
                  int borderWidth) override {
        fillRectInternal_(rect, swandroid::toArgb32(fillColor));
        if (borderWidth > 0) {
            drawRect(rect, borderColor, borderWidth);
        }
    }

    void fillRoundedRect(const SwRect& rect,
                         int radius,
                         const SwColor& fillColor,
                         const SwColor& borderColor,
                         int borderWidth) override {
        const int clampedRadius = std::max(0, radius);
        if (clampedRadius <= 0) {
            fillRect(rect, fillColor, borderColor, borderWidth);
            return;
        }

        if (borderWidth > 0) {
            fillRoundedRectInternal_(rect, clampedRadius, swandroid::toArgb32(borderColor));
            const SwRect inner{
                rect.x + borderWidth,
                rect.y + borderWidth,
                std::max(0, rect.width - borderWidth * 2),
                std::max(0, rect.height - borderWidth * 2)
            };
            fillRoundedRectInternal_(inner, std::max(0, clampedRadius - borderWidth),
                                     swandroid::toArgb32(fillColor));
            return;
        }

        fillRoundedRectInternal_(rect, clampedRadius, swandroid::toArgb32(fillColor));
    }

    void drawRect(const SwRect& rect,
                  const SwColor& borderColor,
                  int borderWidth) override {
        if (borderWidth <= 0) {
            return;
        }
        const std::uint32_t color = swandroid::toArgb32(borderColor);
        for (int i = 0; i < borderWidth; ++i) {
            drawHorizontalLine_(rect.x + i, rect.x + rect.width - 1 - i, rect.y + i, color);
            drawHorizontalLine_(rect.x + i, rect.x + rect.width - 1 - i,
                                rect.y + rect.height - 1 - i, color);
            drawVerticalLine_(rect.x + i, rect.y + i, rect.y + rect.height - 1 - i, color);
            drawVerticalLine_(rect.x + rect.width - 1 - i, rect.y + i,
                              rect.y + rect.height - 1 - i, color);
        }
    }

    void drawLine(int x1,
                  int y1,
                  int x2,
                  int y2,
                  const SwColor& color,
                  int width) override {
        if (m_buffer.empty()) {
            return;
        }

        const std::uint32_t argb = swandroid::toArgb32(color);
        const int stroke = std::max(1, width);
        int dx = std::abs(x2 - x1);
        const int sx = x1 < x2 ? 1 : -1;
        int dy = -std::abs(y2 - y1);
        const int sy = y1 < y2 ? 1 : -1;
        int err = dx + dy;

        while (true) {
            fillRectInternal_(SwRect{x1 - stroke / 2, y1 - stroke / 2, stroke, stroke}, argb);
            if (x1 == x2 && y1 == y2) {
                break;
            }
            const int e2 = err * 2;
            if (e2 >= dy) {
                err += dy;
                x1 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y1 += sy;
            }
        }
    }

    void pushClipRect(const SwRect& rect) override {
        const SwRect current = currentClip_();
        m_clipStack.push_back(swandroid::intersectRects(current, rect));
    }

    void popClipRect() override {
        if (!m_clipStack.empty()) {
            m_clipStack.pop_back();
        }
    }

    void drawImage(const SwRect& targetRect,
                   const SwImage& image,
                   const SwRect* sourceRect = nullptr) override {
        if (image.isNull() || image.format() != SwImage::Format_ARGB32 || m_buffer.empty()) {
            return;
        }

        SwRect src = sourceRect ? *sourceRect : SwRect{0, 0, image.width(), image.height()};
        src.x = std::max(0, src.x);
        src.y = std::max(0, src.y);
        src.width = std::min(src.width, image.width() - src.x);
        src.height = std::min(src.height, image.height() - src.y);
        if (src.width <= 0 || src.height <= 0 || targetRect.width <= 0 || targetRect.height <= 0) {
            return;
        }

        const SwRect clip = currentClip_();
        const std::uint32_t* bits = image.constBits();
        if (!bits) {
            return;
        }

        for (int y = 0; y < targetRect.height; ++y) {
            const int dstY = targetRect.y + y;
            if (dstY < clip.y || dstY >= clip.y + clip.height) {
                continue;
            }

            const int srcY = src.y + (y * src.height) / std::max(1, targetRect.height);
            for (int x = 0; x < targetRect.width; ++x) {
                const int dstX = targetRect.x + x;
                if (dstX < clip.x || dstX >= clip.x + clip.width) {
                    continue;
                }
                const int srcX = src.x + (x * src.width) / std::max(1, targetRect.width);
                const std::uint32_t pixel =
                    bits[static_cast<std::size_t>(srcY) * static_cast<std::size_t>(image.width()) +
                         static_cast<std::size_t>(srcX)];
                blendPixel_(dstX, dstY, pixel);
            }
        }
    }

    void drawText(const SwRect& rect,
                  const SwString& text,
                  DrawTextFormats alignment,
                  const SwColor& color,
                  const SwFont& font) override {
        const std::string utf8 = text.toStdString();
        if (utf8.empty() || rect.width <= 0 || rect.height <= 0) {
            return;
        }

        const int scale = swandroid::glyphScaleForFont(font);
        const int advance = 6 * scale;
        const int lineHeight = std::max(swandroid::lineHeightForFont(font), 8 * scale);
        const std::uint32_t argb = swandroid::toArgb32(color);

        std::vector<std::string> lines;
        lines.push_back(std::string());

        const bool wordBreak = alignment.testFlag(DrawTextFormat::WordBreak);
        const int maxCharsPerLine = std::max(1, rect.width / std::max(1, advance));

        for (std::size_t i = 0; i < utf8.size(); ++i) {
            const char ch = utf8[i];
            if (ch == '\n') {
                lines.push_back(std::string());
                continue;
            }
            if (wordBreak && static_cast<int>(lines.back().size()) >= maxCharsPerLine) {
                lines.push_back(std::string());
            }
            lines.back().push_back(ch);
        }

        const int totalHeight = static_cast<int>(lines.size()) * lineHeight;
        int startY = rect.y;
        if (alignment.testFlag(DrawTextFormat::VCenter)) {
            startY = rect.y + std::max(0, (rect.height - totalHeight) / 2);
        } else if (alignment.testFlag(DrawTextFormat::Bottom)) {
            startY = rect.y + std::max(0, rect.height - totalHeight);
        }

        for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
            const std::string& line = lines[lineIndex];
            const int lineWidth = static_cast<int>(line.size()) * advance;
            int cursorX = rect.x;
            if (alignment.testFlag(DrawTextFormat::Center)) {
                cursorX = rect.x + std::max(0, (rect.width - lineWidth) / 2);
            } else if (alignment.testFlag(DrawTextFormat::Right)) {
                cursorX = rect.x + std::max(0, rect.width - lineWidth);
            }

            const int cursorY = startY + static_cast<int>(lineIndex) * lineHeight;
            for (std::size_t charIndex = 0; charIndex < line.size(); ++charIndex) {
                const swandroid::GlyphRows glyph = swandroid::glyphForChar(line[charIndex]);
                for (int gy = 0; gy < 7; ++gy) {
                    const std::uint8_t row = glyph.rows[static_cast<std::size_t>(gy)];
                    for (int gx = 0; gx < 5; ++gx) {
                        if ((row & (1u << (4 - gx))) == 0u) {
                            continue;
                        }
                        const SwRect pixelRect{
                            cursorX + static_cast<int>(charIndex) * advance + gx * scale,
                            cursorY + gy * scale,
                            scale,
                            scale
                        };
                        fillRectInternal_(pixelRect, argb);
                    }
                }
            }
        }
    }

    void finalize() override {
        m_finalizeRequested = true;
    }

    void* nativeHandle() override {
        return m_window;
    }

private:
    SwRect currentClip_() const {
        if (m_clipStack.empty()) {
            return SwRect{0, 0, m_width, m_height};
        }
        return m_clipStack.back();
    }

    void blendPixel_(int x, int y, std::uint32_t argb) {
        if (x < 0 || y < 0 || x >= m_width || y >= m_height) {
            return;
        }

        const SwRect clip = currentClip_();
        if (x < clip.x || y < clip.y ||
            x >= clip.x + clip.width || y >= clip.y + clip.height) {
            return;
        }

        const std::size_t index =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) +
            static_cast<std::size_t>(x);
        m_buffer[index] = swandroid::blendOver(m_buffer[index], argb);
    }

    void fillRectInternal_(const SwRect& rect, std::uint32_t argb) {
        if (m_buffer.empty() || rect.width <= 0 || rect.height <= 0) {
            return;
        }
        const SwRect clip = currentClip_();
        const SwRect bounds = swandroid::intersectRects(rect, clip);
        if (bounds.width <= 0 || bounds.height <= 0) {
            return;
        }
        for (int y = bounds.y; y < bounds.y + bounds.height; ++y) {
            for (int x = bounds.x; x < bounds.x + bounds.width; ++x) {
                blendPixel_(x, y, argb);
            }
        }
    }

    void fillRoundedRectInternal_(const SwRect& rect, int radius, std::uint32_t argb) {
        if (m_buffer.empty() || rect.width <= 0 || rect.height <= 0) {
            return;
        }
        const SwRect clip = currentClip_();
        const SwRect bounds = swandroid::intersectRects(rect, clip);
        if (bounds.width <= 0 || bounds.height <= 0) {
            return;
        }
        for (int y = bounds.y; y < bounds.y + bounds.height; ++y) {
            for (int x = bounds.x; x < bounds.x + bounds.width; ++x) {
                if (swandroid::isInsideRoundedRect(rect, radius, x, y)) {
                    blendPixel_(x, y, argb);
                }
            }
        }
    }

    void drawHorizontalLine_(int x1, int x2, int y, std::uint32_t argb) {
        if (y < 0 || y >= m_height) {
            return;
        }
        if (x1 > x2) {
            std::swap(x1, x2);
        }
        fillRectInternal_(SwRect{x1, y, x2 - x1 + 1, 1}, argb);
    }

    void drawVerticalLine_(int x, int y1, int y2, std::uint32_t argb) {
        if (x < 0 || x >= m_width) {
            return;
        }
        if (y1 > y2) {
            std::swap(y1, y2);
        }
        fillRectInternal_(SwRect{x, y1, 1, y2 - y1 + 1}, argb);
    }

    void presentIfNeeded_() {
        if (!m_window || !m_finalizeRequested || m_presented || m_buffer.empty()) {
            return;
        }

        ANativeWindow_Buffer nativeBuffer{};
        if (ANativeWindow_lock(m_window, &nativeBuffer, nullptr) != 0) {
            return;
        }

        const int dstWidth = std::min(m_width, nativeBuffer.width);
        const int dstHeight = std::min(m_height, nativeBuffer.height);
        std::uint8_t* dstBase = static_cast<std::uint8_t*>(nativeBuffer.bits);
        for (int y = 0; y < dstHeight; ++y) {
            std::uint8_t* dstRow = dstBase + static_cast<std::size_t>(y) *
                                             static_cast<std::size_t>(nativeBuffer.stride) * 4u;
            for (int x = 0; x < dstWidth; ++x) {
                const std::uint32_t pixel =
                    m_buffer[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) +
                             static_cast<std::size_t>(x)];
                dstRow[x * 4 + 0] = swandroid::redOf(pixel);
                dstRow[x * 4 + 1] = swandroid::greenOf(pixel);
                dstRow[x * 4 + 2] = swandroid::blueOf(pixel);
                dstRow[x * 4 + 3] = swandroid::alphaOf(pixel);
            }
        }

        ANativeWindow_unlockAndPost(m_window);
        m_presented = true;
    }

    ANativeWindow* m_window{nullptr};
    int m_width{0};
    int m_height{0};
    bool m_finalizeRequested{false};
    bool m_presented{false};
    std::vector<std::uint32_t> m_buffer;
    std::vector<SwRect> m_clipStack;
};

class SwAndroidPlatformWindow : public SwPlatformWindow {
public:
    SwAndroidPlatformWindow(SwAndroidPlatformIntegration* integration,
                            const std::string& title,
                            int width,
                            int height,
                            const SwWindowCallbacks& callbacks,
                            const SwPlatformWindowOptions& options)
        : m_integration(integration),
          m_title(title),
          m_callbacks(callbacks),
          m_options(options),
          m_desiredSize{std::max(1, width), std::max(1, height)},
          m_surfaceSize(swandroid::currentSurfaceSize()) {
        if (m_surfaceSize.width <= 0 || m_surfaceSize.height <= 0) {
            m_surfaceSize = m_desiredSize;
        }
    }

    ~SwAndroidPlatformWindow() override;

    void show() override {
        m_visible = true;
        requestUpdate();
    }

    void hide() override {
        m_visible = false;
    }

    void setTitle(const std::string& title) override {
        m_title = title;
    }

    void resize(int width, int height) override {
        m_desiredSize.width = std::max(1, width);
        m_desiredSize.height = std::max(1, height);
        requestUpdate();
    }

    void move(int, int) override {}

    void requestUpdate() override;

    void* nativeHandle() const override {
        return const_cast<SwAndroidPlatformWindow*>(this);
    }

    void* nativeDisplay() const override {
        return nullptr;
    }

    bool isVisible() const { return m_visible; }
    const SwWindowCallbacks& callbacks() const { return m_callbacks; }
    SwPlatformSize surfaceSize() const {
        if (m_surfaceSize.width > 0 && m_surfaceSize.height > 0) {
            return m_surfaceSize;
        }
        return m_desiredSize;
    }

    SwRect clientRect() const {
        const SwPlatformSize size = surfaceSize();
        return SwRect{0, 0, size.width, size.height};
    }

    bool deleteAlreadyDispatched() const { return m_deleteDispatched; }
    void markDeleteDispatched() { m_deleteDispatched = true; }

    void handleSurfaceChanged(const SwPlatformSize& size) {
        const SwPlatformSize normalized{std::max(1, size.width), std::max(1, size.height)};
        const bool changed = normalized.width != m_surfaceSize.width ||
                             normalized.height != m_surfaceSize.height;
        m_surfaceSize = normalized;
        if (changed && m_callbacks.resizeHandler) {
            m_callbacks.resizeHandler(m_surfaceSize);
        }
        requestUpdate();
    }

    void handleSurfaceLost() {
        m_surfaceSize = SwPlatformSize{};
    }

private:
    SwAndroidPlatformIntegration* m_integration{nullptr};
    std::string m_title;
    SwWindowCallbacks m_callbacks;
    SwPlatformWindowOptions m_options;
    SwPlatformSize m_desiredSize{};
    SwPlatformSize m_surfaceSize{};
    bool m_visible{false};
    bool m_deleteDispatched{false};
};

class SwAndroidPlatformIntegration : public SwPlatformIntegration {
public:
    SwAndroidPlatformIntegration() = default;
    ~SwAndroidPlatformIntegration() override { shutdown(); }

    static void bindAndroidApp(android_app* app) {
        swandroid::bindApp(app);
    }

    void initialize(SwGuiApplication* app) override {
        m_application = app;
        swandroid::requestFrame();
    }

    void shutdown() override {
        m_windows.clear();
        m_application = nullptr;
    }

    std::unique_ptr<SwPlatformWindow> createWindow(const std::string& title,
                                                   int width,
                                                   int height,
                                                   const SwWindowCallbacks& callbacks,
                                                   const SwPlatformWindowOptions& options = {}) override {
        std::unique_ptr<SwAndroidPlatformWindow> window(
            new SwAndroidPlatformWindow(this, title, width, height, callbacks, options));
        m_windows.push_back(window.get());
        swandroid::requestFrame();
        return std::unique_ptr<SwPlatformWindow>(window.release());
    }

    std::unique_ptr<SwPlatformPainter> createPainter() override {
        return std::unique_ptr<SwPlatformPainter>(new SwAndroidPainter());
    }

    std::unique_ptr<SwPlatformImage> createImage(const SwPlatformSize& size,
                                                 SwPixelFormat format) override {
        const SwPixelFormat resolved =
            format == SwPixelFormat::Unknown ? SwPixelFormat::ARGB32 : format;
        return std::unique_ptr<SwPlatformImage>(new SwAndroidPlatformImage(size, resolved));
    }

    void processPlatformEvents() override {
        swandroid::processGlueEvents();

        std::deque<swandroid::QueuedEvent> events = swandroid::takeEvents();
        while (!events.empty()) {
            const swandroid::QueuedEvent event = events.front();
            events.pop_front();
            dispatchQueuedEvent_(event);
        }

        if (swandroid::consumeFrameRequest()) {
            dispatchPaint_();
        }
    }

    void wakeUpGuiThread() override {
        swandroid::requestFrame();
    }

    std::vector<std::string> availableScreens() const override {
        const SwPlatformSize size = swandroid::currentSurfaceSize();
        if (size.width <= 0 || size.height <= 0) {
            return {"Android default display"};
        }
        return {std::string("Android display (") +
                std::to_string(size.width) +
                "x" +
                std::to_string(size.height) +
                ")"};
    }

    SwString clipboardText() override {
        return m_clipboard;
    }

    void setClipboardText(const SwString& text) override {
        m_clipboard = text;
    }

    const char* name() const override { return "android"; }

    void requestFrameFromWindow(SwAndroidPlatformWindow*) {
        swandroid::requestFrame();
    }

    void unregisterWindow(SwAndroidPlatformWindow* window) {
        auto it = std::find(m_windows.begin(), m_windows.end(), window);
        if (it != m_windows.end()) {
            m_windows.erase(it);
        }
    }

private:
    SwAndroidPlatformWindow* primaryWindow_() const {
        for (std::size_t i = 0; i < m_windows.size(); ++i) {
            SwAndroidPlatformWindow* window = m_windows[i];
            if (window && window->isVisible()) {
                return window;
            }
        }
        return m_windows.empty() ? nullptr : m_windows.front();
    }

    void dispatchQueuedEvent_(const swandroid::QueuedEvent& event) {
        SwAndroidPlatformWindow* window = primaryWindow_();
        if (!window) {
            return;
        }

        switch (event.type) {
        case swandroid::QueuedEventType::SurfaceChanged:
            window->handleSurfaceChanged(SwPlatformSize{
                std::max(1, event.width),
                std::max(1, event.height)
            });
            break;
        case swandroid::QueuedEventType::SurfaceLost:
            window->handleSurfaceLost();
            break;
        case swandroid::QueuedEventType::TouchDown:
            if (window->callbacks().mousePressHandler) {
                SwMouseEvent mouseEvent;
                mouseEvent.position = SwPlatformPoint{event.x, event.y};
                mouseEvent.button = SwMouseButton::Left;
                mouseEvent.ctrl = event.ctrl;
                mouseEvent.shift = event.shift;
                mouseEvent.alt = event.alt;
                mouseEvent.clickCount = 1;
                window->callbacks().mousePressHandler(mouseEvent);
            }
            break;
        case swandroid::QueuedEventType::TouchMove:
            if (window->callbacks().mouseMoveHandler) {
                SwMouseEvent mouseEvent;
                mouseEvent.position = SwPlatformPoint{event.x, event.y};
                mouseEvent.button = SwMouseButton::NoButton;
                mouseEvent.ctrl = event.ctrl;
                mouseEvent.shift = event.shift;
                mouseEvent.alt = event.alt;
                window->callbacks().mouseMoveHandler(mouseEvent);
            }
            break;
        case swandroid::QueuedEventType::TouchUp:
            if (window->callbacks().mouseReleaseHandler) {
                SwMouseEvent mouseEvent;
                mouseEvent.position = SwPlatformPoint{event.x, event.y};
                mouseEvent.button = SwMouseButton::Left;
                mouseEvent.ctrl = event.ctrl;
                mouseEvent.shift = event.shift;
                mouseEvent.alt = event.alt;
                window->callbacks().mouseReleaseHandler(mouseEvent);
            }
            break;
        case swandroid::QueuedEventType::KeyDown:
            if (window->callbacks().keyPressHandler) {
                SwKeyEvent keyEvent;
                keyEvent.keyCode = event.keyCode;
                keyEvent.ctrl = event.ctrl;
                keyEvent.shift = event.shift;
                keyEvent.alt = event.alt;
                window->callbacks().keyPressHandler(keyEvent);
            }
            break;
        case swandroid::QueuedEventType::KeyUp:
            if (window->callbacks().keyReleaseHandler) {
                SwKeyEvent keyEvent;
                keyEvent.keyCode = event.keyCode;
                keyEvent.ctrl = event.ctrl;
                keyEvent.shift = event.shift;
                keyEvent.alt = event.alt;
                window->callbacks().keyReleaseHandler(keyEvent);
            }
            break;
        case swandroid::QueuedEventType::DeleteRequest:
            if (!window->deleteAlreadyDispatched() && window->callbacks().deleteHandler) {
                window->markDeleteDispatched();
                window->callbacks().deleteHandler();
            }
            break;
        }
    }

    void dispatchPaint_() {
        SwAndroidPlatformWindow* window = primaryWindow_();
        if (!window || !window->isVisible() || !window->callbacks().paintRequestHandler) {
            return;
        }

        ANativeWindow* nativeWindow = swandroid::acquireNativeWindow();
        if (!nativeWindow) {
            return;
        }

        const SwPlatformSize size = swandroid::currentSurfaceSize();
        const SwPlatformSize resolvedSize{
            std::max(1, size.width > 0 ? size.width : window->surfaceSize().width),
            std::max(1, size.height > 0 ? size.height : window->surfaceSize().height)
        };

        SwPlatformPaintEvent event;
        event.surfaceSize = resolvedSize;
        event.dirtyRect = SwPlatformRect{0, 0, resolvedSize.width, resolvedSize.height};
        event.nativePaintDevice = nativeWindow;
        event.nativeWindowHandle = window->nativeHandle();
        event.nativeDisplay = nullptr;
        window->callbacks().paintRequestHandler(event);
    }

    SwGuiApplication* m_application{nullptr};
    std::vector<SwAndroidPlatformWindow*> m_windows;
    SwString m_clipboard;
};

inline SwAndroidPlatformWindow::~SwAndroidPlatformWindow() {
    if (m_integration) {
        m_integration->unregisterWindow(this);
    }
}

inline void SwAndroidPlatformWindow::requestUpdate() {
    if (m_integration) {
        m_integration->requestFrameFromWindow(this);
    } else {
        swandroid::requestFrame();
    }
}

inline std::unique_ptr<SwPlatformIntegration> SwCreateAndroidPlatformIntegration() {
    return std::unique_ptr<SwPlatformIntegration>(new SwAndroidPlatformIntegration());
}

#endif // SW_PLATFORM_ANDROID
