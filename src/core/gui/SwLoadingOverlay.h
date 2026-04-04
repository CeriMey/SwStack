#pragma once

#include "SwWidget.h"
#include "SwTimer.h"
#include "core/object/SwPointer.h"

#include <algorithm>
#include <cmath>

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#endif

class SwLoadingOverlay : public SwWidget {
    SW_OBJECT(SwLoadingOverlay, SwWidget)

public:
    explicit SwLoadingOverlay(SwWidget* parent = nullptr);
    ~SwLoadingOverlay() override;

    void start() { setActive(true); }
    void stop() { setActive(false); }
    bool isActive() const { return getActive(); }

    CUSTOM_PROPERTY(bool, Active, false) { syncActiveState_(); }
    CUSTOM_PROPERTY(bool, AutoFillParent, true) { syncToParent_(); }
    CUSTOM_PROPERTY(bool, BlockInput, true) {
        if (getActive() && isVisibleInHierarchy()) {
            if (value) captureFocus_();
            else releaseFocusCapture_();
        }
        update();
    }
    CUSTOM_PROPERTY(bool, ShowBackdrop, true) { update(); }
    CUSTOM_PROPERTY(bool, ShowPanel, true) { update(); }
    CUSTOM_PROPERTY(SwString, Text, "Loading...") { update(); }
    CUSTOM_PROPERTY(SwString, OverlayColor, "rgba(7, 11, 19, 0.42)") { update(); }
    CUSTOM_PROPERTY(SwString, PanelColor, "rgba(15, 23, 42, 0.92)") { update(); }
    CUSTOM_PROPERTY(SwString, PanelBorderColor, "rgba(148, 163, 184, 0.28)") { update(); }
    CUSTOM_PROPERTY(SwString, PanelShadowColor, "rgba(2, 6, 23, 0.40)") { update(); }
    CUSTOM_PROPERTY(SwString, SpinnerColor, "rgb(125, 211, 252)") { update(); }
    CUSTOM_PROPERTY(SwString, SpinnerTrailColor, "rgba(148, 163, 184, 0.18)") { update(); }
    CUSTOM_PROPERTY(SwString, SpinnerGlowColor, "rgba(56, 189, 248, 0.34)") { update(); }
    CUSTOM_PROPERTY(SwString, SpinnerStyle, "segments") { update(); }
    CUSTOM_PROPERTY(SwString, TextColor, "rgb(241, 245, 249)") { update(); }
    CUSTOM_PROPERTY(int, SpinnerSize, 68) { update(); }
    CUSTOM_PROPERTY(int, SpinnerThickness, 6) { update(); }
    CUSTOM_PROPERTY(int, SegmentCount, 12) { update(); }
    CUSTOM_PROPERTY(int, SegmentLength, 18) { update(); }
    CUSTOM_PROPERTY(int, SpinnerSweepDegrees, 220) { update(); }
    CUSTOM_PROPERTY(int, AnimationInterval, 28) { restartAnimation_(); }
    CUSTOM_PROPERTY(int, AnimationStep, 1) { update(); }
    CUSTOM_PROPERTY(int, BoxWidth, 178) { update(); }
    CUSTOM_PROPERTY(int, BoxMinHeight, 154) { update(); }
    CUSTOM_PROPERTY(int, BoxPadding, 26) { update(); }
    CUSTOM_PROPERTY(int, BoxSpacing, 16) { update(); }
    CUSTOM_PROPERTY(int, BoxRadius, 26) { update(); }

protected:
    void newParentEvent(SwObject* parent) override;
    void showEvent(Event* event) override;
    void hideEvent(Event* event) override;
    void resizeEvent(ResizeEvent* event) override;
    void paintEvent(PaintEvent* event) override;
    void mousePressEvent(MouseEvent* event) override;
    void mouseReleaseEvent(MouseEvent* event) override;
    void mouseDoubleClickEvent(MouseEvent* event) override;
    void mouseMoveEvent(MouseEvent* event) override;
    void wheelEvent(WheelEvent* event) override;
    void keyPressEvent(KeyEvent* event) override;
    void keyReleaseEvent(KeyEvent* event) override;

private:
    struct ColorSpec_ {
        SwColor color{0, 0, 0};
        float alpha{1.0f};
    };

    struct LayoutSpec_ {
        SwRect panelRect{0, 0, 0, 0};
        SwRect spinnerRect{0, 0, 0, 0};
        SwRect textRect{0, 0, 0, 0};
        bool hasText{false};
    };

    void initDefaults_();
    bool shouldBlockInput_() const;
    void syncActiveState_();
    void startAnimation_();
    void stopAnimation_();
    void restartAnimation_();
    void advanceAnimation_();
    void observeParent_(SwWidget* parentWidget);
    void disconnectObservedParent_();
    void syncToParent_();
    void captureFocus_();
    void releaseFocusCapture_();
    SwWidget* findRootWidget_() const;
    LayoutSpec_ layoutSpec_() const;
    SwString styleValue_(const char* propertyName) const;
    SwString firstStyleValue_(const char* nameA, const char* nameB = nullptr, const char* nameC = nullptr) const;
    ColorSpec_ parseColorSpec_(const SwString& text, const ColorSpec_& fallback) const;
    ColorSpec_ resolveColor_(const char* styleName,
                             const SwString& propertyValue,
                             const ColorSpec_& fallback,
                             const char* styleAlias = nullptr) const;
    int resolveMetric_(const char* styleName,
                       int propertyValue,
                       int fallback,
                       const char* styleAlias = nullptr,
                       const char* styleAlias2 = nullptr) const;
    int resolveSpinnerSize_() const;
    int resolveSpinnerThickness_() const;
    int resolveSegmentCount_() const;
    int resolveSegmentLength_() const;
    SwString resolveSpinnerStyle_() const;
    int resolveSpinnerSweepDegrees_() const;
    int resolveBoxWidth_(int spinnerSize, int boxPadding) const;
    int resolveBoxMinHeight_() const;
    int resolveBoxPadding_() const;
    int resolveBoxSpacing_() const;
    int resolveBoxRadius_() const;
    ColorSpec_ overlayColor_() const;
    ColorSpec_ panelColor_() const;
    ColorSpec_ panelBorderColor_() const;
    ColorSpec_ panelShadowColor_() const;
    ColorSpec_ spinnerHeadColor_() const;
    ColorSpec_ spinnerTrailColor_() const;
    ColorSpec_ spinnerGlowColor_() const;
    ColorSpec_ textColor_() const;
    static SwColor mixColor_(const SwColor& a, const SwColor& b, double t);
    static ColorSpec_ mixColorSpec_(const ColorSpec_& a, const ColorSpec_& b, double t);
    static double segmentStrength_(int frame, int index, int count);
    static SwColor approximateAlphaColor_(const ColorSpec_& spec);
    void paintFallback_(SwPainter* painter);
    void paintSpinnerFallback_(SwPainter* painter, const SwRect& spinnerRect);
    void paintRingSpinnerFallback_(SwPainter* painter, const SwRect& spinnerRect);
    void paintText_(SwPainter* painter);

#if defined(_WIN32)
    static BYTE alphaByte_(float alpha);
    static Gdiplus::Color toGdiColor_(const ColorSpec_& spec, float alphaScale = 1.0f);
    static void addRoundedRectPath_(Gdiplus::GraphicsPath& path, const SwRect& rect, int radius);
    bool paintNativeAlpha_(SwPainter* painter);
    void paintPanelNative_(Gdiplus::Graphics& graphics, const SwRect& panelRect, int radius);
    void paintSpinnerNative_(Gdiplus::Graphics& graphics, const SwRect& spinnerRect);
    void paintRingSpinnerNative_(Gdiplus::Graphics& graphics, const SwRect& spinnerRect);
#endif

    SwTimer* m_animationTimer{nullptr};
    int m_animationFrame{0};
    SwPointer<SwWidget> m_observedParent;
    SwPointer<SwWidget> m_previousFocus;
};

inline SwLoadingOverlay::SwLoadingOverlay(SwWidget* parent)
    : SwWidget(parent) {
    initDefaults_();
}

inline SwLoadingOverlay::~SwLoadingOverlay() {
    stopAnimation_();
    disconnectObservedParent_();
    releaseFocusCapture_();
}

inline void SwLoadingOverlay::newParentEvent(SwObject* parent) {
    SwWidget::newParentEvent(parent);
    observeParent_(dynamic_cast<SwWidget*>(parent));
    syncToParent_();
}

inline void SwLoadingOverlay::showEvent(Event* event) {
    SwWidget::showEvent(event);
    if (getAutoFillParent()) {
        syncToParent_();
    }
    if (getActive()) {
        startAnimation_();
        if (getBlockInput()) {
            captureFocus_();
        }
    }
}

inline void SwLoadingOverlay::hideEvent(Event* event) {
    SwWidget::hideEvent(event);
    stopAnimation_();
    releaseFocusCapture_();
}

inline void SwLoadingOverlay::resizeEvent(ResizeEvent* event) {
    SwWidget::resizeEvent(event);
    update();
}

inline void SwLoadingOverlay::paintEvent(PaintEvent* event) {
    if (!event) {
        return;
    }

    SwPainter* painter = event->painter();
    if (!painter) {
        return;
    }

#if defined(_WIN32)
    if (paintNativeAlpha_(painter)) {
        paintText_(painter);
        return;
    }
#endif

    paintFallback_(painter);
    paintText_(painter);
}

inline void SwLoadingOverlay::mousePressEvent(MouseEvent* event) {
    if (shouldBlockInput_() && event) {
        event->accept();
        return;
    }
    SwWidget::mousePressEvent(event);
}

inline void SwLoadingOverlay::mouseReleaseEvent(MouseEvent* event) {
    if (shouldBlockInput_() && event) {
        event->accept();
        return;
    }
    SwWidget::mouseReleaseEvent(event);
}

inline void SwLoadingOverlay::mouseDoubleClickEvent(MouseEvent* event) {
    if (shouldBlockInput_() && event) {
        event->accept();
        return;
    }
    SwWidget::mouseDoubleClickEvent(event);
}

inline void SwLoadingOverlay::mouseMoveEvent(MouseEvent* event) {
    if (shouldBlockInput_() && event) {
        setHover(isPointInside(event->x(), event->y()));
        event->accept();
        return;
    }
    SwWidget::mouseMoveEvent(event);
}

inline void SwLoadingOverlay::wheelEvent(WheelEvent* event) {
    if (shouldBlockInput_() && event) {
        event->accept();
        return;
    }
    SwWidget::wheelEvent(event);
}

inline void SwLoadingOverlay::keyPressEvent(KeyEvent* event) {
    if (shouldBlockInput_() && event) {
        event->accept();
        return;
    }
    SwWidget::keyPressEvent(event);
}

inline void SwLoadingOverlay::keyReleaseEvent(KeyEvent* event) {
    if (shouldBlockInput_() && event) {
        event->accept();
        return;
    }
    SwWidget::keyReleaseEvent(event);
}

inline void SwLoadingOverlay::initDefaults_() {
    resize(180, 160);
    setFocusPolicy(FocusPolicyEnum::Accept);
    setCursor(CursorType::Arrow);
    setFont(SwFont(L"Segoe UI", 10, Medium));
    setStyleSheet(R"(
        SwLoadingOverlay {
            background-color: rgba(7, 11, 19, 0.42);
            panel-background-color: rgba(15, 23, 42, 0.92);
            panel-border-color: rgba(148, 163, 184, 0.28);
            panel-shadow-color: rgba(2, 6, 23, 0.40);
            color: rgb(241, 245, 249);
            spinner-color: rgb(125, 211, 252);
            spinner-trail-color: rgba(148, 163, 184, 0.18);
            spinner-glow-color: rgba(56, 189, 248, 0.34);
            spinner-style: segments;
            spinner-size: 68px;
            spinner-thickness: 6px;
            spinner-segment-count: 12;
            spinner-segment-length: 18px;
            spinner-sweep: 220;
            box-width: 178px;
            box-min-height: 154px;
            box-padding: 26px;
            box-spacing: 16px;
            box-radius: 26px;
        }
    )");

    m_animationTimer = new SwTimer(getAnimationInterval(), this);
    SwObject::connect(m_animationTimer, &SwTimer::timeout, this, [this]() {
        advanceAnimation_();
    });

    hide();
}

inline bool SwLoadingOverlay::shouldBlockInput_() const {
    return getActive() && getBlockInput() && isVisibleInHierarchy();
}

inline void SwLoadingOverlay::syncActiveState_() {
    if (getActive()) {
        if (getAutoFillParent()) {
            syncToParent_();
        }
        show();
        if (getBlockInput()) {
            captureFocus_();
        }
        startAnimation_();
    } else {
        stopAnimation_();
        releaseFocusCapture_();
        hide();
    }
    update();
}

inline void SwLoadingOverlay::startAnimation_() {
    if (!m_animationTimer || !getActive() || !isVisibleInHierarchy()) {
        return;
    }
    m_animationTimer->setInterval(clampInt(getAnimationInterval(), 12, 1000));
    if (!m_animationTimer->isActive()) {
        m_animationTimer->start();
    }
}

inline void SwLoadingOverlay::stopAnimation_() {
    if (m_animationTimer && m_animationTimer->isActive()) {
        m_animationTimer->stop();
    }
}

inline void SwLoadingOverlay::restartAnimation_() {
    if (!m_animationTimer) {
        return;
    }
    m_animationTimer->setInterval(clampInt(getAnimationInterval(), 12, 1000));
    if (getActive() && isVisibleInHierarchy()) {
        m_animationTimer->stop();
        m_animationTimer->start();
    }
    update();
}

inline void SwLoadingOverlay::advanceAnimation_() {
    const int count = resolveSegmentCount_();
    const int step = clampInt(getAnimationStep(), 1, count);
    m_animationFrame = (m_animationFrame + step) % count;
    update();
}

inline void SwLoadingOverlay::observeParent_(SwWidget* parentWidget) {
    if (m_observedParent.data() == parentWidget) {
        return;
    }

    disconnectObservedParent_();
    m_observedParent = parentWidget;
    if (!parentWidget) {
        return;
    }

    SwWidget* expectedParent = parentWidget;
    SwObject::connect(parentWidget, &SwWidget::resized, this, [this, expectedParent](int, int) {
        if (m_observedParent.data() != expectedParent) {
            return;
        }
        syncToParent_();
    });
}

inline void SwLoadingOverlay::disconnectObservedParent_() {
    if (m_observedParent) {
        SwObject::disconnect(m_observedParent.data(), this);
        m_observedParent.clear();
    }
}

inline void SwLoadingOverlay::syncToParent_() {
    if (!getAutoFillParent()) {
        return;
    }
    SwWidget* parentWidget = dynamic_cast<SwWidget*>(parent());
    if (!parentWidget) {
        return;
    }
    move(0, 0);
    resize(parentWidget->width(), parentWidget->height());
}

inline void SwLoadingOverlay::captureFocus_() {
    if (!getBlockInput()) {
        return;
    }

    SwWidget* root = findRootWidget_();
    if (!root) {
        return;
    }

    SwWidget* currentFocused = root->focusedWidgetInHierarchy();
    if (currentFocused && currentFocused != this) {
        m_previousFocus = currentFocused;
    }
    setFocus(true);
}

inline void SwLoadingOverlay::releaseFocusCapture_() {
    if (getFocus()) {
        setFocus(false);
    }
    if (m_previousFocus && m_previousFocus.data() != this && m_previousFocus->isVisibleInHierarchy()) {
        m_previousFocus->setFocus(true);
    }
    m_previousFocus.clear();
}

inline SwWidget* SwLoadingOverlay::findRootWidget_() const {
    SwWidget* current = const_cast<SwLoadingOverlay*>(this);
    while (current) {
        SwWidget* parentWidget = dynamic_cast<SwWidget*>(current->parent());
        if (!parentWidget) {
            return current;
        }
        current = parentWidget;
    }
    return nullptr;
}

inline SwLoadingOverlay::LayoutSpec_ SwLoadingOverlay::layoutSpec_() const {
    LayoutSpec_ spec;

    const SwString text = getText().trimmed();
    const int spinnerSize = resolveSpinnerSize_();
    const int boxPadding = resolveBoxPadding_();
    const int boxSpacing = resolveBoxSpacing_();
    const int boxWidth = resolveBoxWidth_(spinnerSize, boxPadding);
    const int boxMinHeight = resolveBoxMinHeight_();
    const bool hasText = !text.isEmpty();
    const int textHeight = hasText ? 38 : 0;
    const int contentHeight = spinnerSize + (hasText ? boxSpacing + textHeight : 0);
    const int panelHeight = std::max(boxMinHeight, contentHeight + boxPadding * 2);

    const SwRect bounds = rect();
    const bool showPanel = getShowPanel();
    spec.panelRect = showPanel
        ? SwRect{
              bounds.x + std::max(0, (bounds.width - boxWidth) / 2),
              bounds.y + std::max(0, (bounds.height - panelHeight) / 2),
              std::min(boxWidth, bounds.width),
              std::min(panelHeight, bounds.height)}
        : bounds;

    const int layoutWidth = showPanel ? spec.panelRect.width : bounds.width;
    const int layoutHeight = showPanel ? spec.panelRect.height : bounds.height;
    const int layoutX = showPanel ? spec.panelRect.x : bounds.x;
    const int layoutY = showPanel ? spec.panelRect.y : bounds.y;
    const int startY = layoutY + std::max(boxPadding, (layoutHeight - contentHeight) / 2);

    spec.spinnerRect = SwRect{
        layoutX + std::max(0, (layoutWidth - spinnerSize) / 2),
        startY,
        spinnerSize,
        spinnerSize};

    if (hasText) {
        const int textWidth = std::max(0, layoutWidth - boxPadding * 2);
        spec.textRect = SwRect{
            layoutX + std::max(boxPadding, (layoutWidth - textWidth) / 2),
            spec.spinnerRect.y + spec.spinnerRect.height + boxSpacing,
            textWidth,
            textHeight};
        spec.hasText = true;
    }

    return spec;
}

inline SwString SwLoadingOverlay::styleValue_(const char* propertyName) const {
    if (!propertyName) {
        return {};
    }

    const StyleSheet* sheet = const_cast<SwLoadingOverlay*>(this)->getToolSheet();
    if (!sheet) {
        return {};
    }

    auto selectors = classHierarchy();
    if (!selectors.contains("SwLoadingOverlay")) {
        selectors.append("SwLoadingOverlay");
    }
    if (!selectors.contains("SwWidget")) {
        selectors.append("SwWidget");
    }

    for (int i = static_cast<int>(selectors.size()) - 1; i >= 0; --i) {
        const SwString& selector = selectors[static_cast<size_t>(i)];
        if (selector.isEmpty()) {
            continue;
        }
        const SwString value = sheet->getStyleProperty(selector, propertyName);
        if (!value.isEmpty()) {
            return value;
        }
    }

    return {};
}

inline SwString SwLoadingOverlay::firstStyleValue_(const char* nameA,
                                                   const char* nameB,
                                                   const char* nameC) const {
    SwString value = styleValue_(nameA);
    if (!value.isEmpty()) {
        return value;
    }
    value = styleValue_(nameB);
    if (!value.isEmpty()) {
        return value;
    }
    return styleValue_(nameC);
}

inline SwLoadingOverlay::ColorSpec_ SwLoadingOverlay::parseColorSpec_(const SwString& text,
                                                                      const ColorSpec_& fallback) const {
    if (text.isEmpty()) {
        return fallback;
    }

    StyleSheet* sheet = const_cast<SwLoadingOverlay*>(this)->getToolSheet();
    if (!sheet) {
        return fallback;
    }

    try {
        float alpha = fallback.alpha;
        const SwColor color = clampColor(sheet->parseColor(text, &alpha));
        return ColorSpec_{color, static_cast<float>(clampDouble(alpha, 0.0, 1.0))};
    } catch (...) {
        return fallback;
    }
}

inline SwLoadingOverlay::ColorSpec_ SwLoadingOverlay::resolveColor_(const char* styleName,
                                                                    const SwString& propertyValue,
                                                                    const ColorSpec_& fallback,
                                                                    const char* styleAlias) const {
    const SwString styled = firstStyleValue_(styleName, styleAlias);
    if (!styled.isEmpty()) {
        return parseColorSpec_(styled, fallback);
    }
    return parseColorSpec_(propertyValue, fallback);
}

inline int SwLoadingOverlay::resolveMetric_(const char* styleName,
                                            int propertyValue,
                                            int fallback,
                                            const char* styleAlias,
                                            const char* styleAlias2) const {
    int value = propertyValue;
    const SwString styled = firstStyleValue_(styleName, styleAlias, styleAlias2);
    if (!styled.isEmpty()) {
        value = parsePixelValue(styled, value);
    }
    if (value == 0 && propertyValue == 0) {
        return fallback;
    }
    return value;
}

inline int SwLoadingOverlay::resolveSpinnerSize_() const {
    return clampInt(resolveMetric_("spinner-size", getSpinnerSize(), 68), 18, 320);
}

inline int SwLoadingOverlay::resolveSpinnerThickness_() const {
    return clampInt(resolveMetric_("spinner-thickness", getSpinnerThickness(), 6), 2, 24);
}

inline int SwLoadingOverlay::resolveSegmentCount_() const {
    return clampInt(resolveMetric_("spinner-segment-count", getSegmentCount(), 12), 6, 32);
}

inline int SwLoadingOverlay::resolveSegmentLength_() const {
    return clampInt(resolveMetric_("spinner-segment-length", getSegmentLength(), 18), 6, 96);
}

inline SwString SwLoadingOverlay::resolveSpinnerStyle_() const {
    SwString value = firstStyleValue_("spinner-style", "spinner-variant");
    if (value.isEmpty()) {
        value = getSpinnerStyle();
    }
    value = value.trimmed().toLower();
    if (value.isEmpty()) {
        return "segments";
    }
    if (value.contains("minimal") || value.contains("sob") || value.contains("subtle")) {
        return "minimal-ring";
    }
    if (value.contains("ring") || value.contains("arc") || value.contains("trail")) {
        return "ring";
    }
    return "segments";
}

inline int SwLoadingOverlay::resolveSpinnerSweepDegrees_() const {
    return clampInt(resolveMetric_("spinner-sweep",
                                   getSpinnerSweepDegrees(),
                                   220,
                                   "spinner-sweep-degrees"),
                    40,
                    330);
}

inline int SwLoadingOverlay::resolveBoxWidth_(int spinnerSize, int boxPadding) const {
    const int fallback = spinnerSize + boxPadding * 2 + 18;
    return clampInt(resolveMetric_("box-width", getBoxWidth(), fallback), spinnerSize + boxPadding * 2, 640);
}

inline int SwLoadingOverlay::resolveBoxMinHeight_() const {
    return clampInt(resolveMetric_("box-min-height", getBoxMinHeight(), 154), 72, 640);
}

inline int SwLoadingOverlay::resolveBoxPadding_() const {
    return clampInt(resolveMetric_("box-padding", getBoxPadding(), 26), 8, 80);
}

inline int SwLoadingOverlay::resolveBoxSpacing_() const {
    return clampInt(resolveMetric_("box-spacing", getBoxSpacing(), 16), 4, 48);
}

inline int SwLoadingOverlay::resolveBoxRadius_() const {
    int value = getBoxRadius();
    const SwString styled = firstStyleValue_("box-radius", "panel-radius", "border-radius");
    if (!styled.isEmpty()) {
        value = parsePixelValue(styled, value);
    }
    return clampInt(value, 0, 80);
}

inline SwLoadingOverlay::ColorSpec_ SwLoadingOverlay::overlayColor_() const {
    return resolveColor_("background-color", getOverlayColor(), ColorSpec_{SwColor{7, 11, 19}, 0.42f}, "overlay-color");
}

inline SwLoadingOverlay::ColorSpec_ SwLoadingOverlay::panelColor_() const {
    return resolveColor_("panel-background-color", getPanelColor(), ColorSpec_{SwColor{15, 23, 42}, 0.92f});
}

inline SwLoadingOverlay::ColorSpec_ SwLoadingOverlay::panelBorderColor_() const {
    return resolveColor_("panel-border-color",
                         getPanelBorderColor(),
                         ColorSpec_{SwColor{148, 163, 184}, 0.28f},
                         "border-color");
}

inline SwLoadingOverlay::ColorSpec_ SwLoadingOverlay::panelShadowColor_() const {
    return resolveColor_("panel-shadow-color",
                         getPanelShadowColor(),
                         ColorSpec_{SwColor{2, 6, 23}, 0.40f},
                         "shadow-color");
}

inline SwLoadingOverlay::ColorSpec_ SwLoadingOverlay::spinnerHeadColor_() const {
    return resolveColor_("spinner-color", getSpinnerColor(), ColorSpec_{SwColor{125, 211, 252}, 1.0f}, "accent-color");
}

inline SwLoadingOverlay::ColorSpec_ SwLoadingOverlay::spinnerTrailColor_() const {
    return resolveColor_("spinner-trail-color",
                         getSpinnerTrailColor(),
                         ColorSpec_{SwColor{148, 163, 184}, 0.18f});
}

inline SwLoadingOverlay::ColorSpec_ SwLoadingOverlay::spinnerGlowColor_() const {
    return resolveColor_("spinner-glow-color",
                         getSpinnerGlowColor(),
                         ColorSpec_{SwColor{56, 189, 248}, 0.34f});
}

inline SwLoadingOverlay::ColorSpec_ SwLoadingOverlay::textColor_() const {
    return resolveColor_("color", getTextColor(), ColorSpec_{SwColor{241, 245, 249}, 1.0f});
}

inline SwColor SwLoadingOverlay::mixColor_(const SwColor& a, const SwColor& b, double t) {
    t = clampDouble(t, 0.0, 1.0);
    const double omt = 1.0 - t;
    return SwColor{
        clampInt(static_cast<int>(std::round(a.r * omt + b.r * t)), 0, 255),
        clampInt(static_cast<int>(std::round(a.g * omt + b.g * t)), 0, 255),
        clampInt(static_cast<int>(std::round(a.b * omt + b.b * t)), 0, 255)};
}

inline SwLoadingOverlay::ColorSpec_ SwLoadingOverlay::mixColorSpec_(const ColorSpec_& a,
                                                                    const ColorSpec_& b,
                                                                    double t) {
    t = clampDouble(t, 0.0, 1.0);
    return ColorSpec_{
        mixColor_(a.color, b.color, t),
        static_cast<float>(clampDouble(a.alpha * (1.0 - t) + b.alpha * t, 0.0, 1.0))};
}

inline double SwLoadingOverlay::segmentStrength_(int frame, int index, int count) {
    const int delta = (frame - index + count) % count;
    const double linear = 1.0 - (static_cast<double>(delta) / static_cast<double>(count));
    return clampDouble(linear * linear * linear, 0.08, 1.0);
}

inline SwColor SwLoadingOverlay::approximateAlphaColor_(const ColorSpec_& spec) {
    const double alpha = clampDouble(spec.alpha, 0.0, 1.0);
    return SwColor{
        clampInt(static_cast<int>(std::round(spec.color.r * alpha)), 0, 255),
        clampInt(static_cast<int>(std::round(spec.color.g * alpha)), 0, 255),
        clampInt(static_cast<int>(std::round(spec.color.b * alpha)), 0, 255)};
}

inline void SwLoadingOverlay::paintFallback_(SwPainter* painter) {
    const SwRect bounds = rect();
    const LayoutSpec_ spec = layoutSpec_();

    if (getShowBackdrop()) {
        const SwColor fill = approximateAlphaColor_(overlayColor_());
        painter->fillRect(bounds, fill, fill, 0);
    }

    if (getShowPanel()) {
        const int radius = resolveBoxRadius_();
        const SwRect shadowRect{spec.panelRect.x + 4, spec.panelRect.y + 8, spec.panelRect.width, spec.panelRect.height};
        const SwColor shadow = approximateAlphaColor_(panelShadowColor_());
        painter->fillRoundedRect(shadowRect, radius, shadow, shadow, 0);

        const SwColor fill = approximateAlphaColor_(panelColor_());
        const SwColor border = approximateAlphaColor_(panelBorderColor_());
        painter->fillRoundedRect(spec.panelRect, radius, fill, border, 1);
    }

    if (resolveSpinnerStyle_() == "segments") {
        paintSpinnerFallback_(painter, spec.spinnerRect);
    } else {
        paintRingSpinnerFallback_(painter, spec.spinnerRect);
    }
}

inline void SwLoadingOverlay::paintSpinnerFallback_(SwPainter* painter, const SwRect& spinnerRect) {
    const int count = resolveSegmentCount_();
    const int thickness = resolveSpinnerThickness_();
    const int segmentLength = std::min(resolveSegmentLength_(), spinnerRect.width / 2);
    const int cx = spinnerRect.x + spinnerRect.width / 2;
    const int cy = spinnerRect.y + spinnerRect.height / 2;
    const double radiusOuter = static_cast<double>(std::min(spinnerRect.width, spinnerRect.height)) * 0.5 - 2.0;
    const double radiusInner = std::max(3.0, radiusOuter - segmentLength);
    const ColorSpec_ head = spinnerHeadColor_();
    const ColorSpec_ trail = spinnerTrailColor_();
    const ColorSpec_ glow = spinnerGlowColor_();

    const int glowDiameter = std::max(10, std::min(spinnerRect.width, spinnerRect.height) + thickness * 2);
    const SwRect outerHalo{cx - glowDiameter / 2, cy - glowDiameter / 2, glowDiameter, glowDiameter};
    const SwRect innerHalo{cx - glowDiameter / 3, cy - glowDiameter / 3, glowDiameter * 2 / 3, glowDiameter * 2 / 3};
    painter->fillEllipse(outerHalo, approximateAlphaColor_(ColorSpec_{glow.color, glow.alpha * 0.24f}), SwColor{}, 0);
    painter->fillEllipse(innerHalo, approximateAlphaColor_(ColorSpec_{glow.color, glow.alpha * 0.38f}), SwColor{}, 0);

    static const double kPi = 3.14159265358979323846;
    for (int i = 0; i < count; ++i) {
        const double strength = segmentStrength_(m_animationFrame, i, count);
        const ColorSpec_ color = mixColorSpec_(trail, head, strength);
        const double angle = (-kPi * 0.5) + (static_cast<double>(i) * 2.0 * kPi / static_cast<double>(count));

        const int x1 = cx + static_cast<int>(std::round(std::cos(angle) * radiusInner));
        const int y1 = cy + static_cast<int>(std::round(std::sin(angle) * radiusInner));
        const int x2 = cx + static_cast<int>(std::round(std::cos(angle) * radiusOuter));
        const int y2 = cy + static_cast<int>(std::round(std::sin(angle) * radiusOuter));
        painter->drawLine(x1, y1, x2, y2, color.color, std::max(1, thickness));
    }

    const int coreSize = std::max(6, thickness * 2);
    painter->fillEllipse(SwRect{cx - coreSize / 2, cy - coreSize / 2, coreSize, coreSize},
                         head.color,
                         trail.color,
                         1);
}

inline void SwLoadingOverlay::paintRingSpinnerFallback_(SwPainter* painter, const SwRect& spinnerRect) {
    const bool minimal = resolveSpinnerStyle_() == "minimal-ring";
    const int thickness = resolveSpinnerThickness_();
    const int cx = spinnerRect.x + spinnerRect.width / 2;
    const int cy = spinnerRect.y + spinnerRect.height / 2;
    const double radius = static_cast<double>(std::min(spinnerRect.width, spinnerRect.height)) * 0.5 -
                          std::max(3.0, static_cast<double>(thickness));
    const int trailSteps = std::max(18, resolveSegmentCount_() * 3);
    const double sweep = static_cast<double>(resolveSpinnerSweepDegrees_()) * 3.14159265358979323846 / 180.0;
    const double headAngle =
        (-3.14159265358979323846 * 0.5) +
        (static_cast<double>(m_animationFrame) * 2.0 * 3.14159265358979323846 /
         static_cast<double>(resolveSegmentCount_()));
    const ColorSpec_ head = spinnerHeadColor_();
    const ColorSpec_ trail = spinnerTrailColor_();
    const ColorSpec_ glow = spinnerGlowColor_();

    if (!minimal) {
        const int haloDiameter = std::max(10, std::min(spinnerRect.width, spinnerRect.height) + thickness * 2);
        painter->fillEllipse(SwRect{cx - haloDiameter / 2, cy - haloDiameter / 2, haloDiameter, haloDiameter},
                             approximateAlphaColor_(ColorSpec_{glow.color, glow.alpha * 0.18f}),
                             SwColor{},
                             0);
    }

    for (int i = 0; i < trailSteps; ++i) {
        const double t0 = static_cast<double>(i) / static_cast<double>(trailSteps);
        const double t1 = static_cast<double>(i + 1) / static_cast<double>(trailSteps);
        const double a0 = headAngle - t0 * sweep;
        const double a1 = headAngle - t1 * sweep;
        const double strength = std::pow(1.0 - t0, minimal ? 2.2 : 1.6);
        const ColorSpec_ color = mixColorSpec_(trail, head, strength);
        const int width = std::max(1, static_cast<int>(std::round(thickness * (minimal ? 0.72 + strength * 0.18
                                                                                     : 0.82 + strength * 0.30))));
        painter->drawLine(cx + static_cast<int>(std::round(std::cos(a0) * radius)),
                          cy + static_cast<int>(std::round(std::sin(a0) * radius)),
                          cx + static_cast<int>(std::round(std::cos(a1) * radius)),
                          cy + static_cast<int>(std::round(std::sin(a1) * radius)),
                          approximateAlphaColor_(color),
                          width);
    }

    if (!minimal) {
        const int orbSize = std::max(6, thickness + 2);
        painter->fillEllipse(SwRect{cx + static_cast<int>(std::round(std::cos(headAngle) * radius)) - orbSize / 2,
                                    cy + static_cast<int>(std::round(std::sin(headAngle) * radius)) - orbSize / 2,
                                    orbSize,
                                    orbSize},
                             head.color,
                             trail.color,
                             1);
    }
}

inline void SwLoadingOverlay::paintText_(SwPainter* painter) {
    if (!painter) {
        return;
    }

    const LayoutSpec_ spec = layoutSpec_();
    if (!spec.hasText) {
        return;
    }

    painter->drawText(spec.textRect,
                      getText().trimmed(),
                      DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::Top | DrawTextFormat::WordBreak),
                      textColor_().color,
                      getFont());
}

#if defined(_WIN32)
#include "SwLoadingOverlayWin32.h"
#endif
