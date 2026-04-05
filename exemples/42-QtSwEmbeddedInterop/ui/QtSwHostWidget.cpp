#include "QtSwHostWidget.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include <QByteArray>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPaintEngine>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>

#include "SwLabel.h"
#include "SwLayout.h"
#include "SwLineEdit.h"
#include "SwList.h"
#include "SwPushButton.h"
#include "SwUiLoader.h"
#include "SwWidget.h"
#include "SwWidgetSnapshot.h"
#include "core/io/SwFile.h"
#include "gui/qtbinding/SwQtBindingWin32WidgetHost.h"

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#endif

#include "../demo/Example42SketchSupport.h"

namespace {

SwString loadSharedStyleSheet_() {
    SwFile file(example42SharedQssPathSw());
    if (!file.open(SwFile::Read)) {
        return SwString();
    }
    const SwString content = file.readAll();
    file.close();
    return content;
}

void applyZeroMarginLayout_(SwWidget* host, SwAbstractLayout* layout) {
    if (!host || !layout) {
        return;
    }
    layout->setMargin(0);
    layout->setSpacing(0);
    host->setLayout(layout);
}

class SwColorChipButton final : public SwWidget {
    SW_OBJECT(SwColorChipButton, SwWidget)

public:
    explicit SwColorChipButton(int paletteIndex, SwWidget* parent = nullptr)
        : SwWidget(parent)
        , paletteIndex_(clampInkIndex(paletteIndex))
        , color_(inkSwColor(paletteIndex_)) {
        const int swatchSize = example42SwatchSize();
        setCursor(CursorType::Hand);
        resize(swatchSize, swatchSize);
        setMinimumSize(swatchSize, swatchSize);
        setMaximumSize(swatchSize, swatchSize);
        setProperty("Checked", SwAny(false));
        setProperty("Pressed", SwAny(false));
    }

    int paletteIndex() const {
        return paletteIndex_;
    }

    void setSelected(bool selected) {
        if (selected_ == selected) {
            return;
        }

        selected_ = selected;
        setProperty("Checked", SwAny(selected_));
        update();
    }

signals:
    DECLARE_SIGNAL(activated, int);

protected:
    void setGeometry(int newX, int newY, int newWidth, int newHeight) override {
        const int swatchSize = example42SwatchSize();
        const int centeredX = newX + std::max(0, (newWidth - swatchSize) / 2);
        const int centeredY = newY + std::max(0, (newHeight - swatchSize) / 2);
        SwWidget::setGeometry(centeredX, centeredY, swatchSize, swatchSize);
    }

    SwSize sizeHint() const override {
        const int swatchSize = example42SwatchSize();
        return SwSize{swatchSize, swatchSize};
    }

    SwSize minimumSizeHint() const override {
        return sizeHint();
    }

    void paintEvent(PaintEvent* event) override {
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect bounds = rect();
        const SwColor ringFill = SwColor{255, 255, 255};
        const SwColor ringBorder = selected_ ? SwColor{15, 23, 42} : SwColor{203, 213, 225};
        const SwRect outer{1, 1, std::max(0, bounds.width - 2), std::max(0, bounds.height - 2)};
        const SwRect inner{5, 5, std::max(0, bounds.width - 10), std::max(0, bounds.height - 10)};
        painter->fillEllipse(outer, ringFill, ringBorder, selected_ ? 2 : 1);
        painter->fillEllipse(inner, color_, color_, 0);
    }

    void mousePressEvent(MouseEvent* event) override {
        if (event && event->button() == SwMouseButton::Left && isPointInside(event->x(), event->y())) {
            pressed_ = true;
            setProperty("Pressed", SwAny(true));
            event->accept();
            update();
            return;
        }

        SwWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(MouseEvent* event) override {
        const bool shouldActivate = pressed_ && event && event->button() == SwMouseButton::Left && isPointInside(event->x(), event->y());
        pressed_ = false;
        setProperty("Pressed", SwAny(false));
        update();

        if (shouldActivate) {
            activated(paletteIndex_);
            event->accept();
            return;
        }

        SwWidget::mouseReleaseEvent(event);
    }

private:
    int paletteIndex_{0};
    SwColor color_{inkSwColor(0)};
    bool selected_{false};
    bool pressed_{false};
};

class SwSketchCanvas final : public SwWidget {
    SW_OBJECT(SwSketchCanvas, SwWidget)

public:
    explicit SwSketchCanvas(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        setObjectName("canvasSurface");
        setCursor(CursorType::Cross);
        resize(320, 260);
        setMinimumSize(example42CanvasMinimumWidth(), example42CanvasMinimumHeight());
    }

    void setInkColor(int paletteIndex) {
        activeInkIndex_ = clampInkIndex(paletteIndex);
        activeColor_ = inkSwColor(activeInkIndex_);
        update();
    }

    void clearSketch() {
        strokes_.clear();
        drawing_ = false;
        update();
    }

protected:
    void paintEvent(PaintEvent* event) override {
        SwWidget::paintEvent(event);

        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect bounds = rect();
        const SwFont font = resolvedStyledFont_(getToolSheet());
        for (int x = 24; x < bounds.width; x += 28) {
            painter->drawLine(x, 10, x, bounds.height - 10, SwColor{236, 240, 247}, 1);
        }
        for (int y = 24; y < bounds.height; y += 28) {
            painter->drawLine(10, y, bounds.width - 10, y, SwColor{236, 240, 247}, 1);
        }

        for (const SwStroke& stroke : strokes_) {
            if (stroke.points.size() == 0) {
                continue;
            }

            if (stroke.points.size() == 1) {
                const SwPoint point = stroke.points[0];
                painter->fillEllipse(SwRect{point.x - 2, point.y - 2, 4, 4}, stroke.color, stroke.color, 0);
                continue;
            }

            for (size_t i = 1; i < stroke.points.size(); ++i) {
                const SwPoint& from = stroke.points[i - 1];
                const SwPoint& to = stroke.points[i];
                painter->drawLine(from.x, from.y, to.x, to.y, stroke.color, 4);
            }
        }

        painter->drawText(SwRect{18, 14, bounds.width - 36, 20},
                          inkLabelTextSw(activeInkIndex_),
                          DrawTextFormats(DrawTextFormat::Right | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          SwColor{100, 116, 139},
                          font);

        if (strokes_.size() == 0) {
            painter->drawText(SwRect{24, 0, bounds.width - 48, bounds.height},
                              example42TextSw(example42PaneTexts().emptyCanvasHint),
                              DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              SwColor{148, 163, 184},
                              font);
        }
    }

    void mousePressEvent(MouseEvent* event) override {
        if (!event || event->button() != SwMouseButton::Left) {
            SwWidget::mousePressEvent(event);
            return;
        }

        drawing_ = true;
        SwStroke stroke;
        stroke.color = activeColor_;
        stroke.points.append(clampPoint_(event->pos()));
        strokes_.append(stroke);
        event->accept();
        update();
    }

    void mouseMoveEvent(MouseEvent* event) override {
        if (!drawing_ || !event || strokes_.size() == 0) {
            SwWidget::mouseMoveEvent(event);
            return;
        }

        appendPoint_(strokes_[strokes_.size() - 1].points, clampPoint_(event->pos()));
        event->accept();
        update();
    }

    void mouseReleaseEvent(MouseEvent* event) override {
        if (!drawing_ || !event || event->button() != SwMouseButton::Left || strokes_.size() == 0) {
            SwWidget::mouseReleaseEvent(event);
            return;
        }

        appendPoint_(strokes_[strokes_.size() - 1].points, clampPoint_(event->pos()));
        drawing_ = false;
        event->accept();
        update();
    }

private:
    struct SwStroke {
        SwColor color;
        SwList<SwPoint> points;
    };

    static void appendPoint_(SwList<SwPoint>& points, const SwPoint& point) {
        if (points.size() == 0 || points[points.size() - 1].x != point.x || points[points.size() - 1].y != point.y) {
            points.append(point);
        }
    }

    SwPoint clampPoint_(const SwPoint& point) const {
        return SwPoint{
            std::max(14, std::min(point.x, width() - 14)),
            std::max(14, std::min(point.y, height() - 14))
        };
    }

    SwList<SwStroke> strokes_;
    SwColor activeColor_{inkSwColor(0)};
    int activeInkIndex_{0};
    bool drawing_{false};
};

class EmbeddedSwRoot final : public SwWidget {
    SW_OBJECT(EmbeddedSwRoot, SwWidget)

public:
    EmbeddedSwRoot(QtSwHostWidget::MessageSink onSendToQt,
                   QtSwHostWidget::MessageSink onWorkerFiberRequested,
                   SwWidget* parent = nullptr)
        : SwWidget(parent)
        , onSendToQt_(std::move(onSendToQt))
        , onWorkerFiberRequested_(std::move(onWorkerFiberRequested)) {
        example42Trace("sw root: ctor start");
        setStyleSheet(loadSharedStyleSheet_());
        example42Trace("sw root: stylesheet applied");

        const swui::UiLoader::LoadResult loaded = swui::UiLoader::loadFromFile(example42StudioUiPathSw(), this);
        example42Trace(loaded.ok ? "sw root: ui loaded" : "sw root: ui load failed");
        if (loaded.ok && loaded.root) {
            contentRoot_ = loaded.root;
        } else {
            contentRoot_ = new SwWidget(this);
            contentRoot_->setObjectName("centralWidget");
            contentRoot_->setMinimumSize(example42PaneMinimumWidth(), example42PaneMinimumHeight());
        }

        SwVerticalLayout* hostLayout = new SwVerticalLayout(this);
        hostLayout->setMargin(0);
        hostLayout->setSpacing(0);
        hostLayout->addWidget(contentRoot_, 1);
        setLayout(hostLayout);

        titleLabel_ = contentRoot_->findChild<SwLabel>("titleLabel");
        subtitleLabel_ = contentRoot_->findChild<SwLabel>("subtitleLabel");
        statusLabel_ = contentRoot_->findChild<SwLabel>("statusLabel");
        runtimeLabel_ = contentRoot_->findChild<SwLabel>("runtimeLabel");
        button_ = contentRoot_->findChild<SwPushButton>("bridgeButton");
        fiberButton_ = contentRoot_->findChild<SwPushButton>("fiberButton");
        lineEdit_ = contentRoot_->findChild<SwLineEdit>("messageEdit");
        paletteLabel_ = contentRoot_->findChild<SwLabel>("paletteLabel");
        currentInkLabel_ = contentRoot_->findChild<SwLabel>("currentInkLabel");
        clearButton_ = contentRoot_->findChild<SwPushButton>("clearButton");
        swatchStrip_ = contentRoot_->findChild<SwWidget>("swatchStrip");
        canvasHost_ = contentRoot_->findChild<SwWidget>("canvasHost");

        if (auto* contentLayout = dynamic_cast<SwVerticalLayout*>(contentRoot_->layout())) {
            if (canvasHost_) {
                contentLayout->setStretchFactor(canvasHost_, 1);
            }
        }

        if (currentInkLabel_) {
            currentInkLabel_->setAlignment(DrawTextFormats(DrawTextFormat::Right | DrawTextFormat::VCenter | DrawTextFormat::SingleLine));
        }

        if (swatchStrip_) {
            SwHorizontalLayout* swatchLayout = new SwHorizontalLayout(swatchStrip_);
            swatchLayout->setMargin(0);
            swatchLayout->setSpacing(example42SwatchStep() - example42SwatchSize());
            swatchStrip_->setLayout(swatchLayout);

            for (int index = 0; index < kInkPaletteCount; ++index) {
                SwColorChipButton* swatch = new SwColorChipButton(index, swatchStrip_);
                swatchLayout->addWidget(swatch);
                swatches_.append(swatch);

                SwObject::connect(swatch, &SwColorChipButton::activated, this, [this](int paletteIndex) {
                    selectInk_(paletteIndex);
                    if (statusLabel_) {
                        statusLabel_->setText(SwString("Sw ink -> ") + inkNameSw(paletteIndex));
                    }
                });
            }
            swatchLayout->addStretch(1);
        }
        example42Trace("sw root: swatches ready");

        if (canvasHost_) {
            SwVerticalLayout* canvasLayout = new SwVerticalLayout(canvasHost_);
            applyZeroMarginLayout_(canvasHost_, canvasLayout);
            canvas_ = new SwSketchCanvas(canvasHost_);
            canvasLayout->addWidget(canvas_, 1);
        }
        example42Trace("sw root: canvas ready");

        if (button_) {
            SwObject::connect(button_, &SwPushButton::clicked, this, [this]() {
                const SwString text = (lineEdit_ && !lineEdit_->getText().isEmpty()) ? lineEdit_->getText() : SwString("hello from Sw");
                if (onSendToQt_) {
                    onSendToQt_(text);
                }
                if (statusLabel_) {
                    statusLabel_->setText(SwString("Sw -> Qt: ") + text);
                }
            });
        }

        if (fiberButton_) {
            SwObject::connect(fiberButton_, &SwPushButton::clicked, this, [this]() {
                const SwString text = (lineEdit_ && !lineEdit_->getText().isEmpty()) ? lineEdit_->getText() : SwString("hello from Sw");
                if (onWorkerFiberRequested_) {
                    onWorkerFiberRequested_(text);
                }
                if (statusLabel_) {
                    statusLabel_->setText(SwString("Sw -> SwThread: ") + text);
                }
            });
        }

        if (clearButton_) {
            SwObject::connect(clearButton_, &SwPushButton::clicked, this, [this]() {
                if (canvas_) {
                    canvas_->clearSketch();
                }
                if (statusLabel_) {
                    statusLabel_->setText("Sw canvas cleared");
                }
            });
        }

        selectInk_(0);
        const SwSize minHint = contentRoot_->minimumSizeHint();
        setMinimumSize(minHint.width, minHint.height);
        invalidateSizeHintCache_();
        example42Trace("sw root: ctor done");
    }

    SwSize minimumSizeHint() const override {
        ensureSizeHintCache_();
        return cachedMinimumSizeHint_;
    }

    SwSize sizeHint() const override {
        ensureSizeHintCache_();
        return cachedSizeHint_;
    }

    void setIncomingMessage(const SwString& text) {
        if (statusLabel_) {
            statusLabel_->setText(text);
        }
        invalidateSizeHintCache_();
    }

    void setRuntimeStatus(const SwString& text) {
        if (runtimeLabel_) {
            runtimeLabel_->setText(text);
        }
        invalidateSizeHintCache_();
    }

    QString debugGeometryReport() const {
        QString report;
        auto appendWidget = [&report](const QString& label, const SwWidget* widget) {
            report += label;
            if (!widget) {
                report += QStringLiteral(": <null>\n");
                return;
            }

            const SwRect g = widget->geometry();
            const SwSize min = widget->minimumSizeHint();
            const SwSize pref = widget->sizeHint();
            report += QStringLiteral(": geom=%1,%2 %3x%4 min=%5x%6 pref=%7x%8 object=%9 class=%10\n")
                          .arg(g.x)
                          .arg(g.y)
                          .arg(g.width)
                          .arg(g.height)
                          .arg(min.width)
                          .arg(min.height)
                          .arg(pref.width)
                          .arg(pref.height)
                          .arg(toQString(widget->getObjectName()))
                          .arg(toQString(widget->className()));
        };

        appendWidget(QStringLiteral("owner"), this);
        appendWidget(QStringLiteral("contentRoot"), contentRoot_);
        appendWidget(QStringLiteral("titleLabel"), titleLabel_);
        appendWidget(QStringLiteral("subtitleLabel"), subtitleLabel_);
        appendWidget(QStringLiteral("statusLabel"), statusLabel_);
        appendWidget(QStringLiteral("runtimeLabel"), runtimeLabel_);
        appendWidget(QStringLiteral("bridgeButton"), button_);
        appendWidget(QStringLiteral("fiberButton"), fiberButton_);
        appendWidget(QStringLiteral("messageEdit"), lineEdit_);
        appendWidget(QStringLiteral("paletteLabel"), paletteLabel_);
        appendWidget(QStringLiteral("currentInkLabel"), currentInkLabel_);
        appendWidget(QStringLiteral("clearButton"), clearButton_);
        appendWidget(QStringLiteral("swatchStrip"), swatchStrip_);
        appendWidget(QStringLiteral("canvasHost"), canvasHost_);
        appendWidget(QStringLiteral("canvasSurface"), canvas_);
        for (size_t i = 0; i < swatches_.size(); ++i) {
            appendWidget(QStringLiteral("swatch[%1]").arg(static_cast<int>(i)), swatches_[i]);
        }
        return report;
    }

private:
    void ensureSizeHintCache_() const {
        if (!sizeHintCacheDirty_) {
            return;
        }

        if (!contentRoot_) {
            cachedMinimumSizeHint_ = SwSize{example42PaneMinimumWidth(), example42PaneMinimumHeight()};
            cachedSizeHint_ = SwSize{example42PanePreferredWidth(), example42PanePreferredHeight()};
            sizeHintCacheDirty_ = false;
            return;
        }

        cachedMinimumSizeHint_ = contentRoot_->minimumSizeHint();
        const SwSize preferred = contentRoot_->sizeHint();
        cachedSizeHint_ = SwSize{std::max(preferred.width, cachedMinimumSizeHint_.width),
                                 std::max(preferred.height, cachedMinimumSizeHint_.height)};
        sizeHintCacheDirty_ = false;
    }

    void invalidateSizeHintCache_() const {
        sizeHintCacheDirty_ = true;
    }

    void selectInk_(int paletteIndex) {
        const int clamped = clampInkIndex(paletteIndex);
        if (currentInkLabel_) {
            currentInkLabel_->setText(inkLabelTextSw(clamped));
        }
        if (canvas_) {
            canvas_->setInkColor(clamped);
        }
        for (SwColorChipButton* swatch : swatches_) {
            if (swatch) {
                swatch->setSelected(swatch->paletteIndex() == clamped);
            }
        }
        invalidateSizeHintCache_();
    }

    QtSwHostWidget::MessageSink onSendToQt_;
    QtSwHostWidget::MessageSink onWorkerFiberRequested_;
    SwWidget* contentRoot_{nullptr};
    SwLabel* titleLabel_{nullptr};
    SwLabel* subtitleLabel_{nullptr};
    SwLabel* statusLabel_{nullptr};
    SwLabel* runtimeLabel_{nullptr};
    SwPushButton* button_{nullptr};
    SwPushButton* fiberButton_{nullptr};
    SwLineEdit* lineEdit_{nullptr};
    SwLabel* paletteLabel_{nullptr};
    SwLabel* currentInkLabel_{nullptr};
    SwPushButton* clearButton_{nullptr};
    SwWidget* swatchStrip_{nullptr};
    SwWidget* canvasHost_{nullptr};
    SwSketchCanvas* canvas_{nullptr};
    SwList<SwColorChipButton*> swatches_;
    mutable bool sizeHintCacheDirty_{true};
    mutable SwSize cachedMinimumSizeHint_{example42PaneMinimumWidth(), example42PaneMinimumHeight()};
    mutable SwSize cachedSizeHint_{example42PanePreferredWidth(), example42PanePreferredHeight()};
};

} // namespace

class QtSwHostWidget::Impl {
public:
    explicit Impl(QtSwHostWidget* owner)
        : owner_(owner) {
        resizeSyncTimer_.setSingleShot(true);
        resizeSyncTimer_.setTimerType(Qt::PreciseTimer);
        QObject::connect(&resizeSyncTimer_, &QTimer::timeout, owner_, [this]() {
            flushPendingResizeSync_();
        });
        resizeIdleTimer_.setSingleShot(true);
        resizeIdleTimer_.setTimerType(Qt::PreciseTimer);
        QObject::connect(&resizeIdleTimer_, &QTimer::timeout, owner_, [this]() {
            setInteractiveResizeActive_(false);
        });
    }

    void initializeSw(MessageSink onSendToQt, MessageSink onWorkerFiberRequested) {
        if (root_) {
            return;
        }

        root_ = new EmbeddedSwRoot(std::move(onSendToQt), std::move(onWorkerFiberRequested));
        hostBinding_.setRootWidget(root_);
        syncBridgeToNativeHost_();
        hostBinding_.attach();
        requestResizeSync_(owner_->width(), owner_->height(), true);
        root_->update();
    }

    void shutdownSw() {
        if (!root_) {
            return;
        }

        hostBinding_.shutdown();
        resizeSyncTimer_.stop();
        resizeIdleTimer_.stop();
        setInteractiveResizeActive_(false);
        delete root_;
        root_ = nullptr;
#if defined(_WIN32)
        cachedHostHwnd_ = nullptr;
#endif
    }

    void showIncomingMessage(const QString& text) {
        if (root_) {
            root_->setIncomingMessage(toSwString(text));
        }
    }

    void setRuntimeStatusText(const QString& text) {
        if (root_) {
            root_->setRuntimeStatus(toSwString(text));
        }
    }

    EmbeddedSwRoot* root() const {
        return root_;
    }

    bool saveSwRootSnapshot(const QString& filePath) const {
        if (!root_) {
            return false;
        }
        return SwWidgetSnapshot::savePng(root_, toSwString(filePath));
    }

    QString debugGeometryReport() const {
        return root_ ? root_->debugGeometryReport() : QString();
    }

    void showEvent(QShowEvent*) {
        syncBridgeToNativeHost_();
        hostBinding_.attach();
        requestResizeSync_(owner_->width(), owner_->height(), true);
    }

    void resizeEvent(QResizeEvent*) {
        requestResizeSync_(owner_->width(), owner_->height(), false);
    }

#if defined(_WIN32)
    void noteNativeHostSize_(UINT sizeType, int width, int height) {
        if (sizeType == SIZE_MINIMIZED) {
            return;
        }

        requestResizeSync_(width, height, true);
    }
#endif

private:
    static constexpr int kResizeSyncIntervalMs_ = 4;
    static constexpr int kResizeIdleReleaseMs_ = 90;

#if defined(_WIN32)
    bool handleNativeMessage_(MSG* msg, intptr_t* result) {
        return hostBinding_.handleMessage(msg, result);
    }

    void syncBridgeToNativeHost_() {
        cacheHostHwnd_();
        const HWND hwnd = hostHwnd_();
        if (hwnd) {
            if (hostBinding_.hostWindowHandle() != hwnd) {
                hostBinding_.setHostWindowHandle(hwnd);
            }
        } else if (hostBinding_.hostHandle()) {
            hostBinding_.setHostHandle(SwWidgetPlatformHandle{});
        }
    }

    void cacheHostHwnd_() {
        if (cachedHostHwnd_) {
            return;
        }

        cachedHostHwnd_ = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(owner_->winId()));
    }

    HWND hostHwnd_() const {
        return cachedHostHwnd_;
    }
#endif

    void requestResizeSync_(int width, int height, bool immediate) {
        pendingSyncWidth_ = std::max(1, width);
        pendingSyncHeight_ = std::max(1, height);

        if (!root_) {
            return;
        }

        setInteractiveResizeActive_(true);
        resizeIdleTimer_.start(kResizeIdleReleaseMs_);

        if (immediate || !lastResizeSync_.isValid()) {
            flushPendingResizeSync_();
            return;
        }

        const qint64 elapsedMs = lastResizeSync_.elapsed();
        if (elapsedMs >= kResizeSyncIntervalMs_) {
            flushPendingResizeSync_();
            return;
        }

        const int remainingDelay = std::max(1, kResizeSyncIntervalMs_ - static_cast<int>(elapsedMs));
        if (!resizeSyncTimer_.isActive() || resizeSyncTimer_.interval() != remainingDelay) {
            resizeSyncTimer_.start(remainingDelay);
        }
    }

    void flushPendingResizeSync_() {
        resizeSyncTimer_.stop();
        if (!root_) {
            return;
        }

        if (pendingSyncWidth_ == lastCommittedSyncWidth_ && pendingSyncHeight_ == lastCommittedSyncHeight_) {
            return;
        }

        hostBinding_.syncRootGeometryToHostClientRect(pendingSyncWidth_, pendingSyncHeight_);
        SwWidgetPlatformAdapter::flushDamage(true);
#if defined(_WIN32)
        const HWND hwnd = hostHwnd_();
        if (hwnd && ::IsWindow(hwnd) && !::IsIconic(hwnd)) {
            ::UpdateWindow(hwnd);
        }
#endif
        lastCommittedSyncWidth_ = pendingSyncWidth_;
        lastCommittedSyncHeight_ = pendingSyncHeight_;
        lastResizeSync_.restart();
    }

    void setInteractiveResizeActive_(bool active) {
        if (damageThrottleSuppressed_ == active) {
            return;
        }
        damageThrottleSuppressed_ = active;
        SwWidgetPlatformAdapter::setDamageThrottleSuppressed(hostBinding_.hostHandle(), active);
        if (!active) {
            SwWidgetPlatformAdapter::flushDamage(true);
        }
    }

    QtSwHostWidget* owner_{nullptr};
    EmbeddedSwRoot* root_{nullptr};
    SwQtBindingWin32WidgetHost hostBinding_{};
    QTimer resizeSyncTimer_{};
    QTimer resizeIdleTimer_{};
    QElapsedTimer lastResizeSync_{};
    int pendingSyncWidth_{1};
    int pendingSyncHeight_{1};
    int lastCommittedSyncWidth_{-1};
    int lastCommittedSyncHeight_{-1};
    bool damageThrottleSuppressed_{false};
#if defined(_WIN32)
    HWND cachedHostHwnd_{nullptr};
#endif

    friend class QtSwHostWidget;
};

QtSwHostWidget::QtSwHostWidget(QWidget* parent)
    : QWidget(parent)
    , impl_(new Impl(this)) {
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(example42PaneMinimumWidth(), example42PaneMinimumHeight());
}

QtSwHostWidget::~QtSwHostWidget() {
    shutdownSw();
}

void QtSwHostWidget::initializeSw(MessageSink onSendToQt, MessageSink onWorkerFiberRequested) {
    impl_->initializeSw(std::move(onSendToQt), std::move(onWorkerFiberRequested));
}

void QtSwHostWidget::shutdownSw() {
    impl_->shutdownSw();
}

void QtSwHostWidget::showIncomingMessage(const QString& text) {
    impl_->showIncomingMessage(text);
}

void QtSwHostWidget::setRuntimeStatusText(const QString& text) {
    impl_->setRuntimeStatusText(text);
}

bool QtSwHostWidget::saveSwRootSnapshot(const QString& filePath) const {
    return impl_->saveSwRootSnapshot(filePath);
}

QString QtSwHostWidget::debugGeometryReport() const {
    return impl_->debugGeometryReport();
}

QSize QtSwHostWidget::minimumSizeHint() const {
    if (impl_->root()) {
        const SwSize hint = impl_->root()->minimumSizeHint();
        return QSize(hint.width, hint.height);
    }
    return QSize(example42PaneMinimumWidth(), example42PaneMinimumHeight());
}

QSize QtSwHostWidget::sizeHint() const {
    if (impl_->root()) {
        const SwSize hint = impl_->root()->sizeHint();
        return QSize(hint.width, hint.height);
    }
    return QSize(example42PanePreferredWidth(), example42PanePreferredHeight());
}

QPaintEngine* QtSwHostWidget::paintEngine() const {
    return nullptr;
}

void QtSwHostWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
}

void QtSwHostWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    impl_->showEvent(event);
}

void QtSwHostWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    impl_->resizeEvent(event);
}

void QtSwHostWidget::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    QWidget::mousePressEvent(event);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
bool QtSwHostWidget::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
#else
bool QtSwHostWidget::nativeEvent(const QByteArray& eventType, void* message, long* result) {
#endif
    Q_UNUSED(eventType);

    if (!impl_->root() || !message) {
        return QWidget::nativeEvent(eventType, message, result);
    }

#if defined(_WIN32)
    MSG* msg = static_cast<MSG*>(message);
    intptr_t nativeResult = 0;
    const bool handled = impl_->handleNativeMessage_(msg, &nativeResult);
#if defined(_WIN32)
    if (msg->message == WM_SIZE) {
        impl_->noteNativeHostSize_(static_cast<UINT>(msg->wParam),
                                   std::max(1, static_cast<int>(LOWORD(msg->lParam))),
                                   std::max(1, static_cast<int>(HIWORD(msg->lParam))));
    }
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (handled && result) {
        *result = static_cast<qintptr>(nativeResult);
    }
#else
    if (handled && result) {
        *result = static_cast<long>(nativeResult);
    }
#endif
    return handled;
#else
    Q_UNUSED(result);
    return QWidget::nativeEvent(eventType, message, result);
#endif
}
