#include "IpcRingBufferVideoWidget.h"

#include "SwPainter.h"

#include <algorithm>
#include <cstring>

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#endif

IpcRingBufferVideoWidget::IpcRingBufferVideoWidget(SwWidget* parent)
    : SwWidget(parent) {}

void IpcRingBufferVideoWidget::setFrame(SwVideoFrame frame) {
    {
        std::lock_guard<std::mutex> lk(frameMutex_);
        frame_ = std::move(frame);
    }
    update();
}

void IpcRingBufferVideoWidget::paintEvent(PaintEvent* event) {
    if (!event) return;
    SwPainter* painter = event->painter();
    if (!painter) return;

    const SwRect rect = this->rect();
    painter->fillRect(rect, {10, 10, 10}, {10, 10, 10}, 0);

    SwVideoFrame f;
    {
        std::lock_guard<std::mutex> lk(frameMutex_);
        f = frame_;
    }

    if (!f.isValid()) {
        drawPlaceholder_(painter, rect);
        return;
    }

    const SwRect target = computeTargetRect_(rect, f);
    if (!renderBGRA_(painter, f, target)) {
        drawPlaceholder_(painter, rect);
    }
}

SwRect IpcRingBufferVideoWidget::computeTargetRect_(const SwRect& bounds, const SwVideoFrame& frame) const {
    SwRect target = bounds;
    if (scalingMode_ == ScalingMode::Stretch) {
        return target;
    }

    double scale = (std::min)(static_cast<double>(bounds.width) / frame.width(),
                              static_cast<double>(bounds.height) / frame.height());
    const int scaledWidth = static_cast<int>(frame.width() * scale);
    const int scaledHeight = static_cast<int>(frame.height() * scale);
    target.width = (std::min)(bounds.width, scaledWidth);
    target.height = (std::min)(bounds.height, scaledHeight);
    target.x = bounds.x + (bounds.width - target.width) / 2;
    target.y = bounds.y + (bounds.height - target.height) / 2;
    return target;
}

bool IpcRingBufferVideoWidget::renderBGRA_(SwPainter* painter, const SwVideoFrame& frame, const SwRect& targetRect) {
    if (!painter) return false;
    if (!frame.isValid()) return false;
    if (frame.pixelFormat() != SwVideoPixelFormat::BGRA32) return false;
    if (frame.planeStride(0) != frame.width() * 4) return false;

#if defined(_WIN32)
    void* native = painter->nativeHandle();
    if (!native) return false;
    HDC hdc = reinterpret_cast<HDC>(native);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = frame.width();
    bmi.bmiHeader.biHeight = -frame.height();
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biSizeImage = static_cast<DWORD>(frame.width() * frame.height() * 4);

    const uint8_t* data = frame.planeData(0);
    if (!data) return false;

    int previousMode = SetStretchBltMode(hdc, HALFTONE);
    POINT prevOrg{};
    SetBrushOrgEx(hdc, 0, 0, &prevOrg);

    int result = StretchDIBits(hdc,
                               targetRect.x,
                               targetRect.y,
                               targetRect.width,
                               targetRect.height,
                               0,
                               0,
                               frame.width(),
                               frame.height(),
                               data,
                               &bmi,
                               DIB_RGB_COLORS,
                               SRCCOPY);
    if (previousMode != 0) {
        SetStretchBltMode(hdc, previousMode);
    }
    SetBrushOrgEx(hdc, prevOrg.x, prevOrg.y, nullptr);

    return !(result == 0 || result == GDI_ERROR);
#else
    (void)targetRect;
    (void)frame;
    return false;
#endif
}

void IpcRingBufferVideoWidget::drawPlaceholder_(SwPainter* painter, const SwRect& rect) {
    if (!painter) return;
    painter->fillRect(rect, {20, 20, 20}, {60, 60, 60}, 1);
    painter->drawText(rect,
                      SwString("Waiting stream..."),
                      DrawTextFormat::Center | DrawTextFormat::VCenter,
                      {220, 220, 220},
                      SwFont());
}

