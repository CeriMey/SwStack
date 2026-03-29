#include "QtStudioPane.h"

#include <algorithm>

#include <QAbstractButton>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QVector>

#include "../demo/Example42SketchSupport.h"

namespace {

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

} // namespace

class QtStudioPane::Impl {
public:
    explicit Impl(QtStudioPane* owner)
        : owner_(owner) {
        titleLabel_ = new QLabel(QStringLiteral("Qt Sketch Studio"), owner_);
        subtitleLabel_ = new QLabel(QStringLiteral("Qt Widgets + QPainter"), owner_);
        statusLabel_ = new QLabel(QStringLiteral("Qt side ready"), owner_);
        runtimeLabel_ = new QLabel(QStringLiteral("SwThread idle"), owner_);
        bridgeButton_ = new QPushButton(QStringLiteral("Send to Sw"), owner_);
        fiberButton_ = new QPushButton(QStringLiteral("Run SwThread Fiber"), owner_);
        lineEdit_ = new QLineEdit(owner_);
        paletteLabel_ = new QLabel(QStringLiteral("Palette"), owner_);
        currentInkLabel_ = new QLabel(inkLabelText(0), owner_);
        clearButton_ = new QPushButton(QStringLiteral("Clear canvas"), owner_);
        canvas_ = new QtSketchCanvas(owner_);

        titleLabel_->setStyleSheet(QStringLiteral("QLabel { color: rgb(15, 23, 42); font-size: 20px; font-weight: 700; }"));
        subtitleLabel_->setStyleSheet(QStringLiteral("QLabel { color: rgb(100, 116, 139); font-size: 12px; }"));
        statusLabel_->setStyleSheet(QStringLiteral("QLabel { color: rgb(37, 99, 235); font-size: 13px; }"));
        runtimeLabel_->setStyleSheet(QStringLiteral("QLabel { color: rgb(71, 85, 105); font-size: 12px; }"));
        paletteLabel_->setStyleSheet(QStringLiteral("QLabel { color: rgb(71, 85, 105); font-size: 12px; font-weight: 600; }"));
        currentInkLabel_->setStyleSheet(QStringLiteral("QLabel { color: rgb(100, 116, 139); font-size: 12px; }"));
        bridgeButton_->setStyleSheet(qtPrimaryButtonStyleSheet());
        fiberButton_->setStyleSheet(qtSecondaryButtonStyleSheet());
        clearButton_->setStyleSheet(qtSecondaryButtonStyleSheet());
        lineEdit_->setStyleSheet(qtLineEditStyleSheet());
        lineEdit_->setPlaceholderText(QStringLiteral("Write a message for Sw"));

        for (int index = 0; index < kInkPaletteCount; ++index) {
            QtColorChipButton* swatch = new QtColorChipButton(index, owner_);
            owner_->connect(swatch, &QAbstractButton::clicked, owner_, [this, index]() {
                selectInk(index);
                statusLabel_->setText(QStringLiteral("Qt ink -> %1").arg(inkName(index)));
            });
            swatches_.append(swatch);
        }

        owner_->connect(clearButton_, &QPushButton::clicked, owner_, [this]() {
            canvas_->clearSketch();
            statusLabel_->setText(QStringLiteral("Qt canvas cleared"));
        });

        selectInk(0);
    }

    QPushButton* bridgeButton() const {
        return bridgeButton_;
    }

    QPushButton* fiberButton() const {
        return fiberButton_;
    }

    QString messageText() const {
        return lineEdit_->text().trimmed();
    }

    void setStatusText(const QString& text) {
        statusLabel_->setText(text);
    }

    void setRuntimeStatusText(const QString& text) {
        runtimeLabel_->setText(text);
    }

    void layoutWidgets() {
        const int outerMargin = 24;
        const int spacing = 12;
        const int availableWidth = std::max(180, owner_->width() - outerMargin * 2);
        int y = outerMargin;

        titleLabel_->setGeometry(outerMargin, y, availableWidth, 28);
        y += 28;

        subtitleLabel_->setGeometry(outerMargin, y, availableWidth, 20);
        y += 20 + 6;

        statusLabel_->setGeometry(outerMargin, y, availableWidth, 22);
        y += 22 + spacing;

        runtimeLabel_->setGeometry(outerMargin, y, availableWidth, 20);
        y += 20 + spacing;

        bridgeButton_->setGeometry(outerMargin, y, availableWidth, 42);
        y += 42 + spacing;

        fiberButton_->setGeometry(outerMargin, y, availableWidth, 34);
        y += 34 + spacing;

        lineEdit_->setGeometry(outerMargin, y, availableWidth, 38);
        y += 38 + spacing;

        paletteLabel_->setGeometry(outerMargin, y, 100, 22);
        currentInkLabel_->setGeometry(owner_->width() - outerMargin - 160, y, 160, 22);
        y += 22 + 10;

        int x = outerMargin;
        for (QtColorChipButton* swatch : swatches_) {
            swatch->setGeometry(x, y, 30, 30);
            x += 38;
        }

        clearButton_->setGeometry(owner_->width() - outerMargin - 124, y - 2, 124, 34);
        y += 30 + spacing;

        canvas_->setGeometry(outerMargin,
                             y,
                             availableWidth,
                             std::max(220, owner_->height() - y - outerMargin));
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

    QtStudioPane* owner_{nullptr};
    QLabel* titleLabel_{nullptr};
    QLabel* subtitleLabel_{nullptr};
    QLabel* statusLabel_{nullptr};
    QLabel* runtimeLabel_{nullptr};
    QPushButton* bridgeButton_{nullptr};
    QPushButton* fiberButton_{nullptr};
    QLineEdit* lineEdit_{nullptr};
    QLabel* paletteLabel_{nullptr};
    QLabel* currentInkLabel_{nullptr};
    QPushButton* clearButton_{nullptr};
    QtSketchCanvas* canvas_{nullptr};
    QList<QtColorChipButton*> swatches_;
};

QtStudioPane::QtStudioPane(QWidget* parent)
    : QWidget(parent)
    , impl_(new Impl(this)) {
    setAutoFillBackground(true);
}

QtStudioPane::~QtStudioPane() = default;

QPushButton* QtStudioPane::bridgeButton() const {
    return impl_->bridgeButton();
}

QPushButton* QtStudioPane::fiberButton() const {
    return impl_->fiberButton();
}

QString QtStudioPane::messageText() const {
    return impl_->messageText();
}

void QtStudioPane::setStatusText(const QString& text) {
    impl_->setStatusText(text);
}

void QtStudioPane::setRuntimeStatusText(const QString& text) {
    impl_->setRuntimeStatusText(text);
}

void QtStudioPane::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    impl_->layoutWidgets();
}

void QtStudioPane::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(244, 247, 251));
}
