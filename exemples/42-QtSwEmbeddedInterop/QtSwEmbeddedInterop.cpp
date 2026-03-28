#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>

#include <QtGlobal>
#include <QAbstractButton>
#include <QApplication>
#include <QByteArray>
#include <QFont>
#include <QLabel>
#include <QList>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMainWindow>
#include <QMouseEvent>
#include <QCoreApplication>
#include <QPaintEngine>
#include <QPaintEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

#include "SwGuiApplication.h"
#include "SwLabel.h"
#include "SwList.h"
#include "SwLineEdit.h"
#include "SwPushButton.h"
#include "SwShortcut.h"
#include "SwSlider.h"
#include "SwString.h"
#include "SwTimer.h"
#include "SwToolTip.h"
#include "SwWidget.h"
#include "SwWidgetPlatformAdapter.h"
#include "platform/win/SwWin32Painter.h"

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#endif

namespace {

static SwString toSwString(const QString& value) {
    return SwString::fromUtf8(value.toUtf8().constData());
}

static QString toQString(const SwString& value) {
    return QString::fromUtf8(value.toStdString().c_str());
}

static QString sliderText(int value) {
    return QStringLiteral("Slider: %1").arg(value);
}

static QString qtButtonStyleSheet() {
    return QStringLiteral(
        "QPushButton {"
        " background-color: rgb(236, 236, 236);"
        " border: 1px solid rgb(172, 172, 172);"
        " color: rgb(30, 30, 30);"
        " border-radius: 6px;"
        " padding: 8px 14px;"
        " font-size: 14px;"
        "}"
        "QPushButton:hover {"
        " background-color: rgb(224, 224, 224);"
        " border-color: rgb(160, 160, 160);"
        "}"
        "QPushButton:pressed {"
        " background-color: rgb(210, 210, 210);"
        " border-color: rgb(140, 140, 140);"
        " color: rgb(20, 20, 20);"
        "}");
}

static QString qtLineEditStyleSheet() {
    return QStringLiteral(
        "QLineEdit {"
        " background-color: rgb(255, 255, 255);"
        " border: 1px solid rgb(220, 224, 232);"
        " border-radius: 12px;"
        " padding: 6px 10px;"
        " color: rgb(24, 28, 36);"
        " font-size: 14px;"
        "}");
}

static QString qtSliderStyleSheet() {
    return QStringLiteral(
        "QSlider {"
        " background-color: rgb(28, 32, 48);"
        " border: 1px solid rgb(18, 20, 30);"
        " border-radius: 8px;"
        " padding: 0px 18px;"
        "}"
        "QSlider::groove:horizontal {"
        " background: rgb(54, 62, 86);"
        " height: 10px;"
        " border-radius: 5px;"
        "}"
        "QSlider::handle:horizontal {"
        " background: rgb(88, 140, 255);"
        " width: 26px;"
        " margin: -8px 0px;"
        " border-radius: 13px;"
        "}");
}

struct InkColorDef {
    const char* name;
    int red;
    int green;
    int blue;
};

static constexpr InkColorDef kInkPalette[] = {
    {"Azure", 44, 124, 255},
    {"Coral", 255, 121, 85},
    {"Mint", 48, 196, 141},
    {"Amber", 255, 202, 88},
    {"Violet", 161, 122, 255}
};

static constexpr int kInkPaletteCount = static_cast<int>(sizeof(kInkPalette) / sizeof(kInkPalette[0]));

static int clampInkIndex(int index) {
    return std::max(0, std::min(index, kInkPaletteCount - 1));
}

static QColor inkQColor(int index) {
    const InkColorDef& ink = kInkPalette[clampInkIndex(index)];
    return QColor(ink.red, ink.green, ink.blue);
}

static SwColor inkSwColor(int index) {
    const InkColorDef& ink = kInkPalette[clampInkIndex(index)];
    return SwColor{ink.red, ink.green, ink.blue};
}

static QString inkName(int index) {
    return QString::fromLatin1(kInkPalette[clampInkIndex(index)].name);
}

static SwString inkNameSw(int index) {
    return SwString(kInkPalette[clampInkIndex(index)].name);
}

static QString inkLabelText(int index) {
    return QStringLiteral("Ink: %1").arg(inkName(index));
}

static SwString inkLabelTextSw(int index) {
    return SwString("Ink: ") + inkNameSw(index);
}

static QString qtPrimaryButtonStyleSheet() {
    return QStringLiteral(
        "QPushButton {"
        " background-color: rgb(30, 102, 255);"
        " border: 1px solid rgb(22, 84, 220);"
        " color: rgb(255, 255, 255);"
        " border-radius: 12px;"
        " padding: 10px 14px;"
        " font-size: 14px;"
        " font-weight: 600;"
        "}"
        "QPushButton:hover {"
        " background-color: rgb(44, 114, 255);"
        " border-color: rgb(30, 102, 255);"
        "}"
        "QPushButton:pressed {"
        " background-color: rgb(20, 76, 206);"
        " border-color: rgb(15, 62, 176);"
        "}");
}

static QString qtSecondaryButtonStyleSheet() {
    return QStringLiteral(
        "QPushButton {"
        " background-color: rgb(255, 255, 255);"
        " border: 1px solid rgb(220, 224, 232);"
        " color: rgb(24, 28, 36);"
        " border-radius: 12px;"
        " padding: 9px 14px;"
        " font-size: 13px;"
        "}"
        "QPushButton:hover {"
        " background-color: rgb(248, 250, 252);"
        " border-color: rgb(203, 213, 225);"
        "}"
        "QPushButton:pressed {"
        " background-color: rgb(241, 245, 249);"
        "}");
}

static SwString swPrimaryButtonStyleSheet() {
    return SwString(R"(
        SwPushButton {
            background-color: rgb(30, 102, 255);
            border-color: rgb(22, 84, 220);
            color: rgb(255, 255, 255);
            border-radius: 12px;
            padding: 10px 14px;
            border-width: 1px;
            font-size: 14px;
        }
        SwPushButton:hover {
            background-color: rgb(44, 114, 255);
            border-color: rgb(30, 102, 255);
        }
        SwPushButton:pressed {
            background-color: rgb(20, 76, 206);
            border-color: rgb(15, 62, 176);
        }
    )");
}

static SwString swSecondaryButtonStyleSheet() {
    return SwString(R"(
        SwPushButton {
            background-color: rgb(255, 255, 255);
            border-color: rgb(220, 224, 232);
            color: rgb(24, 28, 36);
            border-radius: 12px;
            padding: 9px 14px;
            border-width: 1px;
            font-size: 13px;
        }
        SwPushButton:hover {
            background-color: rgb(248, 250, 252);
            border-color: rgb(203, 213, 225);
        }
        SwPushButton:pressed {
            background-color: rgb(241, 245, 249);
            border-color: rgb(203, 213, 225);
        }
    )");
}

class ScopedSwGuiApplication final : public SwGuiApplication {
public:
    ~ScopedSwGuiApplication() override {
        instance(false) = nullptr;
    }
};

class QtAnimatedPanel final : public QWidget {
public:
    explicit QtAnimatedPanel(QWidget* parent = nullptr)
        : QWidget(parent) {
        setMinimumHeight(170);
        timer_.setInterval(33);
        connect(&timer_, &QTimer::timeout, this, [this]() {
            phase_ += 0.06;
            update();
        });
        timer_.start();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF bounds = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setBrush(QColor(16, 22, 38));
        painter.setPen(QPen(QColor(42, 58, 92), 1));
        painter.drawRoundedRect(bounds, 16, 16);

        const qreal wave = std::sin(phase_) * 18.0;
        const QRectF bandRect(18.0,
                              28.0 + wave,
                              std::max(40.0, bounds.width() - 36.0),
                              26.0);
        painter.setBrush(QColor(44, 124, 255));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(bandRect, 13, 13);

        const QPointF orbCenter(bounds.center().x() + std::cos(phase_ * 1.5) * 54.0,
                                bounds.center().y() + std::sin(phase_ * 0.8) * 24.0);
        painter.setBrush(QColor(255, 202, 88));
        painter.drawEllipse(orbCenter, 16, 16);

        painter.setPen(QColor(255, 255, 255));
        QFont titleFont = font();
        titleFont.setPointSize(11);
        titleFont.setBold(true);
        painter.setFont(titleFont);
        painter.drawText(bounds.adjusted(18, 14, -18, -14),
                         Qt::AlignTop | Qt::AlignLeft,
                         QStringLiteral("Qt paint panel"));
    }

private:
    QTimer timer_;
    qreal phase_{0.0};
};

class SwAnimatedWidget final : public SwWidget {
    SW_OBJECT(SwAnimatedWidget, SwWidget)

public:
    explicit SwAnimatedWidget(SwWidget* parent = nullptr)
        : SwWidget(parent)
        , timer_(new SwTimer(33, this)) {
        resize(260, 150);
        setStyleSheet("SwAnimatedWidget { background-color: rgb(19, 26, 44); border-width: 0px; }");
        SwObject::connect(timer_, &SwTimer::timeout, this, [this]() {
            phase_ += 0.08;
            update();
        });
        timer_->start();
    }

    void paintEvent(PaintEvent* event) override {
        SwPainter* painter = event->painter();
        if (!painter) {
            return;
        }

        const SwRect bounds = rect();
        painter->fillRoundedRect(bounds, 16, SwColor{16, 22, 38}, SwColor{42, 58, 92}, 1);

        const int bandY = 28 + static_cast<int>(std::sin(phase_) * 18.0);
        painter->fillRoundedRect(SwRect{18, bandY, std::max(40, bounds.width - 36), 26},
                                 13,
                                 SwColor{44, 124, 255},
                                 SwColor{44, 124, 255},
                                 0);

        const int orbX = bounds.width / 2 + static_cast<int>(std::cos(phase_ * 1.5) * 54.0);
        const int orbY = bounds.height / 2 + static_cast<int>(std::sin(phase_ * 0.8) * 24.0);
        painter->fillEllipse(SwRect{orbX - 16, orbY - 16, 32, 32},
                             SwColor{255, 202, 88},
                             SwColor{255, 238, 180},
                             1);

        painter->drawText(SwRect{18, 14, bounds.width - 36, 24},
                          "Sw paint panel",
                          DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          SwColor{255, 255, 255},
                          getFont());
    }

private:
    SwTimer* timer_{nullptr};
    double phase_{0.0};
};

class QtColorChipButton final : public QAbstractButton {
public:
    QtColorChipButton(int paletteIndex, QWidget* parent = nullptr)
        : QAbstractButton(parent)
        , paletteIndex_(clampInkIndex(paletteIndex))
        , color_(inkQColor(paletteIndex_)) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(30, 30);
    }

    int paletteIndex() const {
        return paletteIndex_;
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF outer = rect().adjusted(1.0, 1.0, -1.0, -1.0);
        painter.setPen(QPen(isChecked() ? QColor(15, 23, 42) : QColor(203, 213, 225), isChecked() ? 2.0 : 1.0));
        painter.setBrush(QColor(255, 255, 255));
        painter.drawEllipse(outer);

        const QRectF inner = outer.adjusted(4.0, 4.0, -4.0, -4.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color_);
        painter.drawEllipse(inner);
    }

private:
    int paletteIndex_{0};
    QColor color_;
};

class QtSketchCanvas final : public QWidget {
public:
    explicit QtSketchCanvas(QWidget* parent = nullptr)
        : QWidget(parent) {
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        setMinimumHeight(260);
    }

    void setInkColor(int paletteIndex) {
        activeInkIndex_ = clampInkIndex(paletteIndex);
        activeColor_ = inkQColor(activeInkIndex_);
        update();
    }

    void clearSketch() {
        strokes_.clear();
        drawing_ = false;
        update();
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }

        drawing_ = true;
        strokes_.append(QtStroke{activeColor_, QVector<QPoint>{clampPoint_(event->pos())}});
        event->accept();
        update();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (drawing_ && (event->buttons() & Qt::LeftButton) && !strokes_.isEmpty()) {
            appendPoint_(strokes_.last().points, clampPoint_(event->pos()));
            event->accept();
            update();
            return;
        }

        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (!drawing_ || event->button() != Qt::LeftButton || strokes_.isEmpty()) {
            QWidget::mouseReleaseEvent(event);
            return;
        }

        appendPoint_(strokes_.last().points, clampPoint_(event->pos()));
        drawing_ = false;
        event->accept();
        update();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF bounds = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setBrush(QColor(255, 255, 255));
        painter.setPen(QPen(QColor(220, 224, 232), 1));
        painter.drawRoundedRect(bounds, 18, 18);

        painter.save();
        painter.setClipRect(bounds.adjusted(6.0, 6.0, -6.0, -6.0));
        painter.setPen(QPen(QColor(236, 240, 247), 1));
        for (int x = 24; x < width(); x += 28) {
            painter.drawLine(QPointF(x, 10), QPointF(x, height() - 10));
        }
        for (int y = 24; y < height(); y += 28) {
            painter.drawLine(QPointF(10, y), QPointF(width() - 10, y));
        }
        painter.restore();

        for (const QtStroke& stroke : strokes_) {
            if (stroke.points.isEmpty()) {
                continue;
            }

            painter.setPen(QPen(stroke.color, 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            if (stroke.points.size() == 1) {
                painter.drawPoint(stroke.points.first());
                continue;
            }

            for (int i = 1; i < stroke.points.size(); ++i) {
                painter.drawLine(stroke.points[i - 1], stroke.points[i]);
            }
        }

        painter.setPen(QColor(100, 116, 139));
        painter.drawText(bounds.adjusted(18, 14, -18, -14),
                         Qt::AlignTop | Qt::AlignRight,
                         inkLabelText(activeInkIndex_));

        if (strokes_.isEmpty()) {
            painter.setPen(QColor(148, 163, 184));
            painter.drawText(bounds.adjusted(22, 0, -22, -20),
                             Qt::AlignCenter,
                             QStringLiteral("Hold the mouse and sketch here"));
        }
    }

private:
    struct QtStroke {
        QColor color;
        QVector<QPoint> points;
    };

    static void appendPoint_(QVector<QPoint>& points, const QPoint& point) {
        if (points.isEmpty() || points.last() != point) {
            points.append(point);
        }
    }

    QPoint clampPoint_(const QPoint& point) const {
        return QPoint(std::max(14, std::min(point.x(), width() - 14)),
                      std::max(14, std::min(point.y(), height() - 14)));
    }

    QVector<QtStroke> strokes_;
    QColor activeColor_{inkQColor(0)};
    int activeInkIndex_{0};
    bool drawing_{false};
};

class QtStudioPane final : public QWidget {
public:
    explicit QtStudioPane(QWidget* parent = nullptr)
        : QWidget(parent) {
        setAutoFillBackground(true);

        titleLabel_ = new QLabel(QStringLiteral("Qt Sketch Studio"), this);
        subtitleLabel_ = new QLabel(QStringLiteral("Qt Widgets + QPainter"), this);
        statusLabel_ = new QLabel(QStringLiteral("Qt side ready"), this);
        bridgeButton_ = new QPushButton(QStringLiteral("Send to Sw"), this);
        lineEdit_ = new QLineEdit(this);
        paletteLabel_ = new QLabel(QStringLiteral("Palette"), this);
        currentInkLabel_ = new QLabel(inkLabelText(0), this);
        clearButton_ = new QPushButton(QStringLiteral("Clear canvas"), this);
        canvas_ = new QtSketchCanvas(this);

        titleLabel_->setStyleSheet(QStringLiteral("QLabel { color: rgb(15, 23, 42); font-size: 20px; font-weight: 700; }"));
        subtitleLabel_->setStyleSheet(QStringLiteral("QLabel { color: rgb(100, 116, 139); font-size: 12px; }"));
        statusLabel_->setStyleSheet(QStringLiteral("QLabel { color: rgb(37, 99, 235); font-size: 13px; }"));
        paletteLabel_->setStyleSheet(QStringLiteral("QLabel { color: rgb(71, 85, 105); font-size: 12px; font-weight: 600; }"));
        currentInkLabel_->setStyleSheet(QStringLiteral("QLabel { color: rgb(100, 116, 139); font-size: 12px; }"));
        bridgeButton_->setStyleSheet(qtPrimaryButtonStyleSheet());
        clearButton_->setStyleSheet(qtSecondaryButtonStyleSheet());
        lineEdit_->setStyleSheet(qtLineEditStyleSheet());
        lineEdit_->setPlaceholderText(QStringLiteral("Write a message for Sw"));

        for (int index = 0; index < kInkPaletteCount; ++index) {
            QtColorChipButton* swatch = new QtColorChipButton(index, this);
            connect(swatch, &QAbstractButton::clicked, this, [this, index]() {
                selectInk(index);
                setStatusText(QStringLiteral("Qt ink -> %1").arg(inkName(index)));
            });
            swatches_.append(swatch);
        }

        connect(clearButton_, &QPushButton::clicked, this, [this]() {
            canvas_->clearSketch();
            setStatusText(QStringLiteral("Qt canvas cleared"));
        });

        selectInk(0);
    }

    QPushButton* bridgeButton() const {
        return bridgeButton_;
    }

    QString messageText() const {
        return lineEdit_->text().trimmed();
    }

    void setStatusText(const QString& text) {
        statusLabel_->setText(text);
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);

        const int outerMargin = 24;
        const int spacing = 12;
        const int availableWidth = std::max(180, width() - outerMargin * 2);
        int y = outerMargin;

        titleLabel_->setGeometry(outerMargin, y, availableWidth, 28);
        y += 28;

        subtitleLabel_->setGeometry(outerMargin, y, availableWidth, 20);
        y += 20 + 6;

        statusLabel_->setGeometry(outerMargin, y, availableWidth, 22);
        y += 22 + spacing;

        bridgeButton_->setGeometry(outerMargin, y, availableWidth, 42);
        y += 42 + spacing;

        lineEdit_->setGeometry(outerMargin, y, availableWidth, 38);
        y += 38 + spacing;

        paletteLabel_->setGeometry(outerMargin, y, 100, 22);
        currentInkLabel_->setGeometry(width() - outerMargin - 160, y, 160, 22);
        y += 22 + 10;

        int x = outerMargin;
        for (QtColorChipButton* swatch : swatches_) {
            swatch->setGeometry(x, y, 30, 30);
            x += 38;
        }
        clearButton_->setGeometry(width() - outerMargin - 124, y - 2, 124, 34);
        y += 30 + spacing;

        canvas_->setGeometry(outerMargin,
                             y,
                             availableWidth,
                             std::max(220, height() - y - outerMargin));
    }

    void paintEvent(QPaintEvent* event) override {
        QWidget::paintEvent(event);
        QPainter painter(this);
        painter.fillRect(rect(), QColor(244, 247, 251));
    }

private:
    void selectInk(int index) {
        const int clamped = clampInkIndex(index);
        currentInkLabel_->setText(inkLabelText(clamped));
        canvas_->setInkColor(clamped);
        for (QtColorChipButton* swatch : swatches_) {
            swatch->setChecked(swatch->paletteIndex() == clamped);
            swatch->update();
        }
    }

    QLabel* titleLabel_{nullptr};
    QLabel* subtitleLabel_{nullptr};
    QLabel* statusLabel_{nullptr};
    QPushButton* bridgeButton_{nullptr};
    QLineEdit* lineEdit_{nullptr};
    QLabel* paletteLabel_{nullptr};
    QLabel* currentInkLabel_{nullptr};
    QPushButton* clearButton_{nullptr};
    QtSketchCanvas* canvas_{nullptr};
    QList<QtColorChipButton*> swatches_;
};

class SwColorChipButton final : public SwWidget {
    SW_OBJECT(SwColorChipButton, SwWidget)

public:
    SwColorChipButton(int paletteIndex, SwWidget* parent = nullptr)
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
        painter->fillEllipse(bounds, ringFill, ringBorder, selected_ ? 2 : 1);
        painter->fillEllipse(SwRect{4, 4, std::max(0, bounds.width - 8), std::max(0, bounds.height - 8)},
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
        setMinimumSize(220, 220);
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
                              "Hold the mouse and sketch here",
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
    explicit EmbeddedSwRoot(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        setStyleSheet("EmbeddedSwRoot { background-color: rgb(244, 247, 251); border-width: 0px; }");

        titleLabel_ = new SwLabel("Sw Sketch Studio", this);
        titleLabel_->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); color: rgb(15, 23, 42); font-size: 20px; border-width: 0px; }");

        subtitleLabel_ = new SwLabel("Sw widgets + SwPainter", this);
        subtitleLabel_->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); color: rgb(100, 116, 139); font-size: 12px; border-width: 0px; }");

        statusLabel_ = new SwLabel("Sw side ready", this);
        statusLabel_->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); color: rgb(37, 99, 235); font-size: 13px; border-width: 0px; }");

        button_ = new SwPushButton("Send to Qt", this);
        button_->setStyleSheet(swPrimaryButtonStyleSheet());

        lineEdit_ = new SwLineEdit("Write a message for Qt", this);

        paletteLabel_ = new SwLabel("Palette", this);
        paletteLabel_->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); color: rgb(71, 85, 105); font-size: 12px; border-width: 0px; }");

        currentInkLabel_ = new SwLabel(inkLabelTextSw(0), this);
        currentInkLabel_->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); color: rgb(100, 116, 139); font-size: 12px; border-width: 0px; }");

        clearButton_ = new SwPushButton("Clear canvas", this);
        clearButton_->setStyleSheet(swSecondaryButtonStyleSheet());

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
            swSendRequested(text);
            statusLabel_->setText(SwString("Sw -> Qt: ") + text);
        });
        SwObject::connect(clearButton_, &SwPushButton::clicked, this, [this]() {
            canvas_->clearSketch();
            statusLabel_->setText("Sw canvas cleared");
        });

        selectInk_(0);
    }

    void attachToHostHandle(const SwWidgetPlatformHandle& handle) {
        setNativeWindowHandleRecursive(handle);
        update();
    }

    void resizeEvent(ResizeEvent* event) override {
        SwWidget::resizeEvent(event);

        const int outerMargin = 24;
        const int spacing = 12;
        const int availableWidth = std::max(180, event->width() - outerMargin * 2);
        int y = outerMargin;

        titleLabel_->setGeometry(outerMargin, y, availableWidth, 28);
        y += 28;

        subtitleLabel_->setGeometry(outerMargin, y, availableWidth, 20);
        y += 20 + 6;

        statusLabel_->setGeometry(outerMargin, y, availableWidth, 22);
        y += 22 + spacing;

        button_->setGeometry(outerMargin, y, availableWidth, 42);
        y += 42 + spacing;

        lineEdit_->setGeometry(outerMargin, y, availableWidth, 38);
        y += 38 + spacing;

        paletteLabel_->setGeometry(outerMargin, y, 100, 22);
        currentInkLabel_->setGeometry(event->width() - outerMargin - 160, y, 160, 22);
        y += 22 + 10;

        int x = outerMargin;
        for (SwColorChipButton* swatch : swatches_) {
            swatch->setGeometry(x, y, 30, 30);
            x += 38;
        }
        clearButton_->setGeometry(event->width() - outerMargin - 124, y - 2, 124, 34);
        y += 30 + spacing;

        canvas_->setGeometry(outerMargin,
                             y,
                             availableWidth,
                             std::max(220, event->height() - y - outerMargin));
    }

    void setIncomingMessage(const SwString& text) {
        statusLabel_->setText(text);
    }

signals:
    DECLARE_SIGNAL(swSendRequested, const SwString&);

private:
    void selectInk_(int paletteIndex) {
        const int clamped = clampInkIndex(paletteIndex);
        currentInkLabel_->setText(inkLabelTextSw(clamped));
        canvas_->setInkColor(clamped);
        for (SwColorChipButton* swatch : swatches_) {
            swatch->setSelected(swatch->paletteIndex() == clamped);
        }
    }

    SwLabel* titleLabel_{nullptr};
    SwLabel* subtitleLabel_{nullptr};
    SwLabel* statusLabel_{nullptr};
    SwPushButton* button_{nullptr};
    SwLineEdit* lineEdit_{nullptr};
    SwLabel* paletteLabel_{nullptr};
    SwLabel* currentInkLabel_{nullptr};
    SwPushButton* clearButton_{nullptr};
    SwSketchCanvas* canvas_{nullptr};
    SwList<SwColorChipButton*> swatches_;
};

class SwHostWidget final : public QWidget {
public:
    explicit SwHostWidget(QWidget* parent = nullptr)
        : QWidget(parent) {
        setAttribute(Qt::WA_NativeWindow, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAutoFillBackground(false);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setMinimumWidth(320);
    }

    ~SwHostWidget() override {
        shutdownSw();
    }

    EmbeddedSwRoot* root() const {
        return root_;
    }

    void initializeSw(QtStudioPane* qtPane) {
        if (root_) {
            return;
        }

        qtPane_ = qtPane;
        root_ = new EmbeddedSwRoot();

        QPointer<QtStudioPane> safePane = qtPane;
        SwObject::connect(root_, &EmbeddedSwRoot::swSendRequested, root_, [safePane](const SwString& text) {
            if (safePane) {
                safePane->setStatusText(QStringLiteral("Sw -> Qt: %1").arg(toQString(text)));
            }
        });

        ensureHostAttached_();
        syncRootGeometryToClient_();
        root_->update();
    }

    void shutdownSw() {
        if (!root_) {
            return;
        }

        SwToolTip::hideText();
#if defined(_WIN32)
        if (GetCapture() == hostHwnd_()) {
            ReleaseCapture();
        }
#endif
        delete root_;
        root_ = nullptr;
        qtPane_.clear();
        lastMoveTime_ = std::chrono::steady_clock::time_point{};
        lastMousePosition_ = SwPoint{0, 0};
    }

    void showIncomingMessage(const QString& text) {
        if (root_) {
            root_->setIncomingMessage(toSwString(text));
        }
    }

    QPaintEngine* paintEngine() const override {
        return nullptr;
    }

protected:
    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        ensureHostAttached_();
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        ensureHostAttached_();
        syncRootGeometryToClient_();
    }

    void mousePressEvent(QMouseEvent* event) override {
        setFocus(Qt::MouseFocusReason);
        QWidget::mousePressEvent(event);
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override {
#else
    bool nativeEvent(const QByteArray& eventType, void* message, long* result) override {
#endif
        Q_UNUSED(eventType);
        if (!root_ || !message) {
            return QWidget::nativeEvent(eventType, message, result);
        }

#if defined(_WIN32)
        MSG* msg = static_cast<MSG*>(message);
        return handleNativeMessage_(msg, result);
#else
        Q_UNUSED(result);
        return QWidget::nativeEvent(eventType, message, result);
#endif
    }

private:
#if defined(_WIN32)
    bool handleNativeMessage_(MSG* msg,
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                              qintptr* result
#else
                              long* result
#endif
    ) {
        if (!msg || msg->hwnd != hostHwnd_()) {
            return false;
        }

        switch (msg->message) {
        case WM_ERASEBKGND:
            if (result) {
                *result = 1;
            }
            return true;
        case WM_PAINT:
            paintSwHost_();
            if (result) {
                *result = 0;
            }
            return true;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            if (GetCapture() != msg->hwnd) {
                SetCapture(msg->hwnd);
            }
            SetFocus(msg->hwnd);
            SwToolTip::handleMousePress();
            dispatchMouseButton_(msg, EventType::MousePressEvent);
            if (result) {
                *result = 0;
            }
            return true;
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDBLCLK:
            dispatchMouseButton_(msg, EventType::MouseDoubleClickEvent);
            if (result) {
                *result = 0;
            }
            return true;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            dispatchMouseButton_(msg, EventType::MouseReleaseEvent);
            if ((static_cast<UINT>(msg->wParam) & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) == 0 && GetCapture() == msg->hwnd) {
                ReleaseCapture();
            }
            if (result) {
                *result = 0;
            }
            return true;
        case WM_MOUSEMOVE:
            trackMouseLeave_();
            dispatchMouseMove_(msg);
            if (result) {
                *result = 0;
            }
            return true;
        case WM_MOUSELEAVE:
            dispatchMouseLeave_();
            lastMoveTime_ = std::chrono::steady_clock::time_point{};
            SwToolTip::hideText();
            if (result) {
                *result = 0;
            }
            return true;
        case WM_MOUSEWHEEL:
            dispatchMouseWheel_(msg, false);
            if (result) {
                *result = 0;
            }
            return true;
        case WM_MOUSEHWHEEL:
            dispatchMouseWheel_(msg, true);
            if (result) {
                *result = 0;
            }
            return true;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            dispatchKeyPress_(msg, L'\0', true);
            if (result) {
                *result = 0;
            }
            return true;
        case WM_CHAR:
        case WM_SYSCHAR: {
            const wchar_t ch = static_cast<wchar_t>(msg->wParam);
            if (ch >= 0x20) {
                dispatchKeyPress_(msg, ch, true);
            }
            if (result) {
                *result = 0;
            }
            return true;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP:
            dispatchKeyRelease_(msg);
            if (result) {
                *result = 0;
            }
            return true;
        default:
            return false;
        }
    }

    void ensureHostAttached_() {
        if (!root_) {
            return;
        }
        HWND hwnd = hostHwnd_();
        if (!hwnd) {
            return;
        }
        root_->attachToHostHandle(SwWidgetPlatformAdapter::fromNativeHandle(hwnd));
        syncRootGeometryToClient_();
    }

    void syncRootGeometryToClient_() {
        if (!root_) {
            return;
        }

        HWND hwnd = hostHwnd_();
        if (!hwnd) {
            root_->setGeometry(0, 0, width(), height());
            return;
        }

        const SwRect clientRect = SwWidgetPlatformAdapter::clientRect(
            SwWidgetPlatformAdapter::fromNativeHandle(hwnd));
        root_->setGeometry(0,
                           0,
                           std::max(1, clientRect.width),
                           std::max(1, clientRect.height));
    }

    void paintSwHost_() {
        HWND hwnd = hostHwnd_();
        if (!hwnd) {
            return;
        }

        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        if (!hdc) {
            return;
        }

        const SwRect clientRect = SwWidgetPlatformAdapter::clientRect(
            SwWidgetPlatformAdapter::fromNativeHandle(hwnd));
        SwPlatformPaintEvent platformPaintEvent;
        platformPaintEvent.nativePaintDevice = hdc;
        platformPaintEvent.nativeWindowHandle = hwnd;
        platformPaintEvent.surfaceSize = SwPlatformSize{std::max(1, clientRect.width), std::max(1, clientRect.height)};
        platformPaintEvent.dirtyRect = SwPlatformRect{
            ps.rcPaint.left,
            ps.rcPaint.top,
            std::max(0, static_cast<int>(ps.rcPaint.right - ps.rcPaint.left)),
            std::max(0, static_cast<int>(ps.rcPaint.bottom - ps.rcPaint.top))
        };

        SwWin32Painter painter;
        painter.begin(platformPaintEvent);
        PaintEvent widgetPaintEvent(&painter, SwRect{0, 0, platformPaintEvent.surfaceSize.width, platformPaintEvent.surfaceSize.height});
        SwCoreApplication::sendEvent(root_, &widgetPaintEvent);
        painter.finalize();
        painter.flush();
        EndPaint(hwnd, &ps);
    }

    void dispatchMouseButton_(MSG* msg, EventType type) {
        const int x = GET_X_LPARAM(msg->lParam);
        const int y = GET_Y_LPARAM(msg->lParam);
        const UINT keyState = static_cast<UINT>(msg->wParam);
        const bool ctrlPressed = (keyState & MK_CONTROL) != 0;
        const bool shiftPressed = (keyState & MK_SHIFT) != 0;
        const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;

        SwMouseButton button = SwMouseButton::NoButton;
        if (msg->message == WM_LBUTTONDOWN || msg->message == WM_LBUTTONUP || msg->message == WM_LBUTTONDBLCLK) {
            button = SwMouseButton::Left;
        } else if (msg->message == WM_RBUTTONDOWN || msg->message == WM_RBUTTONUP || msg->message == WM_RBUTTONDBLCLK) {
            button = SwMouseButton::Right;
        } else if (msg->message == WM_MBUTTONDOWN || msg->message == WM_MBUTTONUP || msg->message == WM_MBUTTONDBLCLK) {
            button = SwMouseButton::Middle;
        }

        MouseEvent mouseEvent(type, x, y, button, ctrlPressed, shiftPressed, altPressed);
        mouseEvent.setGlobalPos(mapLocalToGlobal_(x, y));
        root_->dispatchMouseEventFromRoot(mouseEvent);
    }

    void dispatchMouseMove_(MSG* msg) {
        const int x = GET_X_LPARAM(msg->lParam);
        const int y = GET_Y_LPARAM(msg->lParam);
        const UINT keyState = static_cast<UINT>(msg->wParam);
        const bool ctrlPressed = (keyState & MK_CONTROL) != 0;
        const bool shiftPressed = (keyState & MK_SHIFT) != 0;
        const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;

        MouseEvent mouseEvent(EventType::MouseMoveEvent, x, y, SwMouseButton::NoButton, ctrlPressed, shiftPressed, altPressed);
        if (lastMoveTime_.time_since_epoch().count() != 0) {
            const auto now = std::chrono::steady_clock::now();
            const long long durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMoveTime_).count();
            const int deltaX = x - lastMousePosition_.x;
            const int deltaY = y - lastMousePosition_.y;
            mouseEvent.setDeltaX(deltaX);
            mouseEvent.setDeltaY(deltaY);
            if (durationMs > 0) {
                mouseEvent.setSpeedX((static_cast<double>(deltaX) / static_cast<double>(durationMs)) * 1000.0);
                mouseEvent.setSpeedY((static_cast<double>(deltaY) / static_cast<double>(durationMs)) * 1000.0);
            }
            lastMoveTime_ = now;
        } else {
            lastMoveTime_ = std::chrono::steady_clock::now();
        }

        lastMousePosition_ = SwPoint{x, y};
        mouseEvent.setGlobalPos(mapLocalToGlobal_(x, y));
        root_->dispatchMouseEventFromRoot(mouseEvent);
        SwToolTip::handleMouseMove(root_, x, y);
    }

    void dispatchMouseLeave_() {
        MouseEvent mouseEvent(EventType::MouseMoveEvent,
                              -100000,
                              -100000,
                              SwMouseButton::NoButton,
                              false,
                              false,
                              false);
        mouseEvent.setGlobalPos(SwPoint{-100000, -100000});
        root_->dispatchMouseEventFromRoot(mouseEvent);
    }

    void dispatchMouseWheel_(MSG* msg, bool horizontal) {
        POINT point{};
        point.x = GET_X_LPARAM(msg->lParam);
        point.y = GET_Y_LPARAM(msg->lParam);
        ScreenToClient(msg->hwnd, &point);

        const int delta = horizontal ? -static_cast<short>(HIWORD(msg->wParam))
                                     : static_cast<short>(HIWORD(msg->wParam));
        const UINT keyState = LOWORD(msg->wParam);
        const bool ctrlPressed = (keyState & MK_CONTROL) != 0;
        const bool shiftPressed = horizontal ? true : ((keyState & MK_SHIFT) != 0);
        const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;

        WheelEvent wheelEvent(point.x, point.y, delta, ctrlPressed, shiftPressed, altPressed);
        wheelEvent.setGlobalPos(mapLocalToGlobal_(point.x, point.y));
        root_->dispatchWheelEventFromRoot(wheelEvent);
    }

    void dispatchKeyPress_(MSG* msg, wchar_t textChar, bool textProvided) {
        SwToolTip::handleKeyPress();
        const bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
        const int keyCode = (msg->message == WM_CHAR || msg->message == WM_SYSCHAR) ? 0 : static_cast<int>(msg->wParam);

        KeyEvent keyEvent(keyCode, ctrlPressed, shiftPressed, altPressed, textChar, textProvided);
        if (!root_->dispatchKeyPressEventFromRoot(keyEvent)) {
            SwShortcut::dispatch(root_, &keyEvent);
        }
    }

    void dispatchKeyRelease_(MSG* msg) {
        const bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;

        KeyEvent keyEvent(static_cast<int>(msg->wParam),
                          ctrlPressed,
                          shiftPressed,
                          altPressed,
                          L'\0',
                          false,
                          EventType::KeyReleaseEvent);
        root_->dispatchKeyReleaseEventFromRoot(keyEvent);
    }

    void trackMouseLeave_() {
        TRACKMOUSEEVENT tracking{};
        tracking.cbSize = sizeof(TRACKMOUSEEVENT);
        tracking.dwFlags = TME_LEAVE;
        tracking.hwndTrack = hostHwnd_();
        TrackMouseEvent(&tracking);
    }

    SwPoint mapLocalToGlobal_(int x, int y) const {
        POINT point{};
        point.x = x;
        point.y = y;
        ClientToScreen(hostHwnd_(), &point);
        return SwPoint{point.x, point.y};
    }

    HWND hostHwnd_() const {
        return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(winId()));
    }
#endif

    QPointer<QtStudioPane> qtPane_;
    EmbeddedSwRoot* root_{nullptr};
    std::chrono::steady_clock::time_point lastMoveTime_{};
    SwPoint lastMousePosition_{0, 0};
};

} // namespace

int main(int argc, char* argv[]) {
    QApplication qtApp(argc, argv);

    int exitCode = 0;
    std::unique_ptr<ScopedSwGuiApplication> swApp;

    {
        QMainWindow window;
        window.setWindowTitle(QStringLiteral("Qt + Sw Embedded Interop"));
        window.resize(1180, 700);

        QSplitter* splitter = new QSplitter(Qt::Horizontal, &window);
        QtStudioPane* qtPane = new QtStudioPane(splitter);

        SwHostWidget* swHost = new SwHostWidget(splitter);

        splitter->addWidget(qtPane);
        splitter->addWidget(swHost);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 1);
        splitter->setSizes(QList<int>() << 590 << 590);
        window.setCentralWidget(splitter);

        QObject::connect(qtPane->bridgeButton(), &QPushButton::clicked, &window, [qtPane, swHost]() {
            const QString typed = qtPane->messageText();
            const QString message = typed.isEmpty() ? QStringLiteral("hello from Qt") : typed;
            swHost->showIncomingMessage(QStringLiteral("Qt -> Sw: %1").arg(message));
            qtPane->setStatusText(QStringLiteral("Qt -> Sw: %1").arg(message));
        });

        window.show();

        QCoreApplication::processEvents();

        swApp = std::make_unique<ScopedSwGuiApplication>();
        swHost->initializeSw(qtPane);

        QTimer swPumpTimer;
        swPumpTimer.setTimerType(Qt::PreciseTimer);
        swPumpTimer.setInterval(4);
        QObject::connect(&swPumpTimer, &QTimer::timeout, &window, [&swApp]() {
            for (int iteration = 0; iteration < 64; ++iteration) {
                const int nextDelayUs = swApp->processEvent(false);
                if (nextDelayUs != 0) {
                    break;
                }
            }
            SwWidgetPlatformAdapter::flushDamage();
        });

        QObject::connect(&qtApp, &QCoreApplication::aboutToQuit, &window, [&swPumpTimer, swHost]() {
            swPumpTimer.stop();
            swHost->shutdownSw();
        });

        swPumpTimer.start();
        exitCode = qtApp.exec();
        swPumpTimer.stop();
        swHost->shutdownSw();
    }

    swApp.reset();
    return exitCode;
}
