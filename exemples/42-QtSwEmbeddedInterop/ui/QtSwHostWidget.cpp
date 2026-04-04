#include "QtSwHostWidget.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include <QByteArray>
#include <QMouseEvent>
#include <QPaintEngine>
#include <QResizeEvent>
#include <QSize>
#include <QShowEvent>

#include "SwLabel.h"
#include "SwLineEdit.h"
#include "SwList.h"
#include "SwPushButton.h"
#include "SwWidget.h"
#include "SwWidgetSnapshot.h"
#include "gui/qtbinding/SwQtBindingWin32WidgetHost.h"

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#endif

#include "../demo/Example42SketchSupport.h"

namespace {

class SwColorChipButton final : public SwWidget {
    SW_OBJECT(SwColorChipButton, SwWidget)

public:
    explicit SwColorChipButton(int paletteIndex, SwWidget* parent = nullptr)
        : SwWidget(parent)
        , paletteIndex_(clampInkIndex(paletteIndex))
        , color_(inkSwColor(paletteIndex_)) {
        setCursor(CursorType::Hand);
        resize(30, 30);
        setMinimumSize(30, 30);
    }

    int paletteIndex() const {
        return paletteIndex_;
    }

    void setSelected(bool selected) {
        if (selected_ == selected) {
            return;
        }

        selected_ = selected;
        update();
    }

signals:
    DECLARE_SIGNAL(activated, int);

protected:
    void paintEvent(PaintEvent* event) override {
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect bounds = rect();
        const SwColor ringFill = selected_ ? SwColor{15, 23, 42} : SwColor{255, 255, 255};
        const SwColor ringBorder = selected_ ? SwColor{15, 23, 42} : SwColor{203, 213, 225};
        const SwRect outer{1, 1, std::max(0, bounds.width - 2), std::max(0, bounds.height - 2)};
        const SwRect inner{5, 5, std::max(0, bounds.width - 10), std::max(0, bounds.height - 10)};
        painter->fillEllipse(outer, ringFill, ringBorder, selected_ ? 2 : 1);
        painter->fillEllipse(inner,
                             color_,
                             color_,
                             0);
    }

    void mousePressEvent(MouseEvent* event) override {
        if (event && event->button() == SwMouseButton::Left && isPointInside(event->x(), event->y())) {
            pressed_ = true;
            event->accept();
            update();
            return;
        }

        SwWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(MouseEvent* event) override {
        const bool shouldActivate = pressed_ && event && event->button() == SwMouseButton::Left && isPointInside(event->x(), event->y());
        pressed_ = false;
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
        setCursor(CursorType::Cross);
        resize(320, 260);
        setMinimumSize(example42CanvasMinimumWidth(), example42CanvasMinimumHeight());
        setStyleSheet("SwSketchCanvas { background-color: rgb(255, 255, 255); border-color: rgb(220, 224, 232); border-width: 1px; border-radius: 18px; }");
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
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect bounds = rect();
        painter->fillRoundedRect(bounds, 18, SwColor{255, 255, 255}, SwColor{220, 224, 232}, 1);

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
                          getFont());

        if (strokes_.size() == 0) {
            painter->drawText(SwRect{24, 0, bounds.width - 48, bounds.height},
                              example42TextSw(example42PaneTexts().emptyCanvasHint),
                              DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              SwColor{148, 163, 184},
                              getFont());
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
        const Example42PaneTextSet& texts = example42PaneTexts();
        setStyleSheet("EmbeddedSwRoot { background-color: rgb(244, 247, 251); border-width: 0px; }");

        titleLabel_ = new SwLabel(example42TextSw(texts.title), this);
        titleLabel_->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); color: rgb(15, 23, 42); font-size: 20px; border-width: 0px; }");
        titleLabel_->setMinimumSize(example42ContentMinimumWidth(), example42TitleHeight());

        subtitleLabel_ = new SwLabel(example42TextSw(texts.subtitle), this);
        subtitleLabel_->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); color: rgb(100, 116, 139); font-size: 12px; border-width: 0px; }");
        subtitleLabel_->setMinimumSize(example42ContentMinimumWidth(), example42SubtitleHeight());

        statusLabel_ = new SwLabel(example42TextSw(texts.readyStatus), this);
        statusLabel_->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); color: rgb(37, 99, 235); font-size: 13px; border-width: 0px; }");
        statusLabel_->setMinimumSize(example42ContentMinimumWidth(), example42StatusHeight());

        runtimeLabel_ = new SwLabel(example42TextSw(texts.runtimeIdle), this);
        runtimeLabel_->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); color: rgb(71, 85, 105); font-size: 12px; border-width: 0px; }");
        runtimeLabel_->setMinimumSize(example42ContentMinimumWidth(), example42RuntimeHeight());

        button_ = new SwPushButton(example42TextSw(texts.bridgeButton), this);
        button_->setStyleSheet(swPrimaryButtonStyleSheet());
        button_->setMinimumSize(example42ContentMinimumWidth(), example42BridgeButtonHeight());

        fiberButton_ = new SwPushButton(example42TextSw(texts.fiberButton), this);
        fiberButton_->setStyleSheet(swSecondaryButtonStyleSheet());
        fiberButton_->setMinimumSize(example42ContentMinimumWidth(), example42FiberButtonHeight());

        lineEdit_ = new SwLineEdit(example42TextSw(texts.placeholder), this);
        lineEdit_->setStyleSheet(swLineEditStyleSheet());
        lineEdit_->setMinimumSize(example42ContentMinimumWidth(), example42LineEditHeight());

        paletteLabel_ = new SwLabel(example42TextSw(texts.paletteLabel), this);
        paletteLabel_->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); color: rgb(71, 85, 105); font-size: 12px; border-width: 0px; }");
        paletteLabel_->setMinimumSize(example42PaletteLabelWidth(), example42PaletteRowHeight());

        currentInkLabel_ = new SwLabel(inkLabelTextSw(0), this);
        currentInkLabel_->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); color: rgb(100, 116, 139); font-size: 12px; border-width: 0px; }");
        currentInkLabel_->setMinimumSize(example42CurrentInkLabelWidth(), example42PaletteRowHeight());

        clearButton_ = new SwPushButton(example42TextSw(texts.clearButton), this);
        clearButton_->setStyleSheet(swSecondaryButtonStyleSheet());
        clearButton_->setMinimumSize(example42ClearButtonWidth(), example42ClearButtonHeight());

        canvas_ = new SwSketchCanvas(this);

        for (int index = 0; index < kInkPaletteCount; ++index) {
            SwColorChipButton* swatch = new SwColorChipButton(index, this);
            SwObject::connect(swatch, &SwColorChipButton::activated, this, [this](int paletteIndex) {
                selectInk_(paletteIndex);
                statusLabel_->setText(SwString("Sw ink -> ") + inkNameSw(paletteIndex));
            });
            swatches_.append(swatch);
        }

        SwObject::connect(button_, &SwPushButton::clicked, this, [this]() {
            const SwString text = lineEdit_->getText().isEmpty() ? SwString("hello from Sw") : lineEdit_->getText();
            if (onSendToQt_) {
                onSendToQt_(text);
            }
            statusLabel_->setText(SwString("Sw -> Qt: ") + text);
        });

        SwObject::connect(fiberButton_, &SwPushButton::clicked, this, [this]() {
            const SwString text = lineEdit_->getText().isEmpty() ? SwString("hello from Sw") : lineEdit_->getText();
            if (onWorkerFiberRequested_) {
                onWorkerFiberRequested_(text);
            }
            statusLabel_->setText(SwString("Sw -> SwThread: ") + text);
        });

        SwObject::connect(clearButton_, &SwPushButton::clicked, this, [this]() {
            canvas_->clearSketch();
            statusLabel_->setText("Sw canvas cleared");
        });

        selectInk_(0);
        setMinimumSize(example42PaneMinimumWidth(), example42PaneMinimumHeight());
    }

    SwSize minimumSizeHint() const override {
        return SwSize{example42PaneMinimumWidth(), example42PaneMinimumHeight()};
    }

    SwSize sizeHint() const override {
        return SwSize{example42PanePreferredWidth(), example42PanePreferredHeight()};
    }

    void resizeEvent(ResizeEvent* event) override {
        SwWidget::resizeEvent(event);

        const Example42PaneLayout layout = computeExample42PaneLayout(event->width(), event->height());

        titleLabel_->setGeometry(layout.title.x, layout.title.y, layout.title.width, layout.title.height);
        subtitleLabel_->setGeometry(layout.subtitle.x, layout.subtitle.y, layout.subtitle.width, layout.subtitle.height);
        statusLabel_->setGeometry(layout.status.x, layout.status.y, layout.status.width, layout.status.height);
        runtimeLabel_->setGeometry(layout.runtime.x, layout.runtime.y, layout.runtime.width, layout.runtime.height);
        button_->setGeometry(layout.bridgeButton.x, layout.bridgeButton.y, layout.bridgeButton.width, layout.bridgeButton.height);
        fiberButton_->setGeometry(layout.fiberButton.x, layout.fiberButton.y, layout.fiberButton.width, layout.fiberButton.height);
        lineEdit_->setGeometry(layout.lineEdit.x, layout.lineEdit.y, layout.lineEdit.width, layout.lineEdit.height);
        paletteLabel_->setGeometry(layout.paletteLabel.x, layout.paletteLabel.y, layout.paletteLabel.width, layout.paletteLabel.height);
        currentInkLabel_->setGeometry(layout.currentInkLabel.x,
                                      layout.currentInkLabel.y,
                                      layout.currentInkLabel.width,
                                      layout.currentInkLabel.height);

        int x = layout.paletteLabel.x;
        for (SwColorChipButton* swatch : swatches_) {
            swatch->setGeometry(x, layout.clearButton.y + 2, example42SwatchSize(), example42SwatchSize());
            x += example42SwatchStep();
        }

        clearButton_->setGeometry(layout.clearButton.x, layout.clearButton.y, layout.clearButton.width, layout.clearButton.height);
        canvas_->setGeometry(layout.canvas.x, layout.canvas.y, layout.canvas.width, layout.canvas.height);
    }

    void setIncomingMessage(const SwString& text) {
        statusLabel_->setText(text);
    }

    void setRuntimeStatus(const SwString& text) {
        runtimeLabel_->setText(text);
    }

private:
    void selectInk_(int paletteIndex) {
        const int clamped = clampInkIndex(paletteIndex);
        currentInkLabel_->setText(inkLabelTextSw(clamped));
        canvas_->setInkColor(clamped);
        for (SwColorChipButton* swatch : swatches_) {
            swatch->setSelected(swatch->paletteIndex() == clamped);
        }
    }

    QtSwHostWidget::MessageSink onSendToQt_;
    QtSwHostWidget::MessageSink onWorkerFiberRequested_;
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
    SwSketchCanvas* canvas_{nullptr};
    SwList<SwColorChipButton*> swatches_;
};

} // namespace

class QtSwHostWidget::Impl {
public:
    explicit Impl(QtSwHostWidget* owner)
        : owner_(owner) {
    }

    void initializeSw(MessageSink onSendToQt, MessageSink onWorkerFiberRequested) {
        if (root_) {
            return;
        }

        root_ = new EmbeddedSwRoot(std::move(onSendToQt), std::move(onWorkerFiberRequested));
        hostBinding_.setRootWidget(root_);
        syncBridgeToNativeHost_();
        hostBinding_.attach();
        hostBinding_.syncRootGeometryToHostClientRect(owner_->width(), owner_->height());
        root_->update();
    }

    void shutdownSw() {
        if (!root_) {
            return;
        }

        hostBinding_.shutdown();
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

    void showEvent(QShowEvent*) {
        syncBridgeToNativeHost_();
        hostBinding_.attach();
        hostBinding_.syncRootGeometryToHostClientRect(owner_->width(), owner_->height());
    }

    void resizeEvent(QResizeEvent*) {
        syncBridgeToNativeHost_();
        hostBinding_.syncRootGeometryToHostClientRect(owner_->width(), owner_->height());
    }

private:
#if defined(_WIN32)
    bool handleNativeMessage_(MSG* msg, intptr_t* result) {
        return hostBinding_.handleMessage(msg, result);
    }

    void syncBridgeToNativeHost_() {
        cacheHostHwnd_();
        const HWND hwnd = hostHwnd_();
        if (hwnd) {
            hostBinding_.setHostWindowHandle(hwnd);
        } else {
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

    QtSwHostWidget* owner_{nullptr};
    EmbeddedSwRoot* root_{nullptr};
    SwQtBindingWin32WidgetHost hostBinding_{};
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
