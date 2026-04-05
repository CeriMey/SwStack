#include "QtStudioPane.h"

#include <algorithm>

#include <QAbstractButton>
#include <QButtonGroup>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpacerItem>
#include <QVBoxLayout>
#include <QWidget>

#include "SwXmlDocument.h"

#include "../demo/Example42SketchSupport.h"

namespace {

using XmlNode = SwXmlNode;

QString loadSharedStyleSheet_() {
    QFile file(example42SharedQssPathQt());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

SwString childText_(const XmlNode& node, const char* childName) {
    const XmlNode* child = node.firstChild(childName);
    return child ? child->text.trimmed() : SwString();
}

SwString textValue_(const XmlNode& propNode) {
    if (!propNode.children.isEmpty()) {
        for (const auto& child : propNode.children) {
            if (child.name == "string" || child.name == "cstring" || child.name == "number" || child.name == "bool" ||
                child.name == "enum") {
                return child.text.trimmed();
            }
        }
    }
    return propNode.text.trimmed();
}

int toInt_(const SwString& value, int defaultValue = 0) {
    if (value.isEmpty()) {
        return defaultValue;
    }
    bool ok = false;
    const int parsed = value.trimmed().toInt(&ok);
    return ok ? parsed : defaultValue;
}

QSpacerItem* loadQtSpacer_(const XmlNode& spacerNode) {
    int width = 40;
    int height = 20;
    bool vertical = false;
    QSizePolicy::Policy horizontalPolicy = QSizePolicy::Minimum;
    QSizePolicy::Policy verticalPolicy = QSizePolicy::Minimum;

    for (const auto* prop : spacerNode.childrenNamed("property")) {
        if (!prop) {
            continue;
        }

        const SwString rawName = prop->attr("name");
        if (rawName == "orientation") {
            const SwString value = textValue_(*prop).toLower();
            vertical = value.contains("vertical");
        } else if (rawName == "sizeHint") {
            const XmlNode* size = prop->firstChild("size");
            if (!size) {
                continue;
            }
            width = toInt_(childText_(*size, "width"), width);
            height = toInt_(childText_(*size, "height"), height);
        } else if (rawName == "sizeType") {
            const SwString value = textValue_(*prop);
            const bool expanding = value.contains("Expanding");
            if (vertical) {
                verticalPolicy = expanding ? QSizePolicy::Expanding : QSizePolicy::Minimum;
            } else {
                horizontalPolicy = expanding ? QSizePolicy::Expanding : QSizePolicy::Minimum;
            }
        }
    }

    return new QSpacerItem(width, height, horizontalPolicy, verticalPolicy);
}

QLayout* createQtLayout_(const SwString& className, QString& error) {
    if (className == "QVBoxLayout") {
        return new QVBoxLayout();
    }
    if (className == "QHBoxLayout") {
        return new QHBoxLayout();
    }

    error = QStringLiteral("Unsupported layout class: ") + toQString(className);
    return nullptr;
}

QWidget* createQtWidget_(const SwString& className, QWidget* parent, QString& error) {
    if (className == "QWidget") {
        return new QWidget(parent);
    }
    if (className == "QLabel") {
        return new QLabel(parent);
    }
    if (className == "QPushButton") {
        return new QPushButton(parent);
    }
    if (className == "QLineEdit") {
        return new QLineEdit(parent);
    }

    error = QStringLiteral("Unsupported widget class: ") + toQString(className);
    return nullptr;
}

void applyQtProperty_(QWidget* widget, const SwString& rawName, const XmlNode& propNode) {
    if (!widget || rawName.isEmpty()) {
        return;
    }

    if (rawName == "minimumSize") {
        const XmlNode* size = propNode.firstChild("size");
        if (!size) {
            return;
        }
        widget->setMinimumSize(toInt_(childText_(*size, "width")), toInt_(childText_(*size, "height")));
        return;
    }

    if (rawName == "maximumSize") {
        const XmlNode* size = propNode.firstChild("size");
        if (!size) {
            return;
        }
        widget->setMaximumSize(toInt_(childText_(*size, "width")), toInt_(childText_(*size, "height")));
        return;
    }

    if (rawName == "text") {
        const QString text = toQString(textValue_(propNode));
        if (QLabel* label = qobject_cast<QLabel*>(widget)) {
            label->setText(text);
            return;
        }
        if (QPushButton* button = qobject_cast<QPushButton*>(widget)) {
            button->setText(text);
            return;
        }
    }

    if (rawName == "placeholderText") {
        if (QLineEdit* lineEdit = qobject_cast<QLineEdit*>(widget)) {
            lineEdit->setPlaceholderText(toQString(textValue_(propNode)));
        }
        return;
    }
}

QWidget* loadQtWidget_(const XmlNode& widgetNode, QWidget* parent, QString& error);

bool applyQtLayout_(QWidget* parentWidget, const XmlNode& widgetNode, QString& error) {
    const XmlNode* layoutNode = widgetNode.firstChild("layout");
    if (!layoutNode) {
        return true;
    }

    QLayout* layout = createQtLayout_(layoutNode->attr("class"), error);
    if (!layout) {
        return false;
    }

    for (const auto* prop : layoutNode->childrenNamed("property")) {
        if (!prop) {
            continue;
        }
        const SwString rawName = prop->attr("name");
        if (rawName == "spacing") {
            layout->setSpacing(toInt_(textValue_(*prop), layout->spacing()));
        } else if (rawName == "leftMargin" || rawName == "topMargin" || rawName == "rightMargin" || rawName == "bottomMargin") {
            int left = 0;
            int top = 0;
            int right = 0;
            int bottom = 0;
            layout->getContentsMargins(&left, &top, &right, &bottom);
            const int value = toInt_(textValue_(*prop), 0);
            if (rawName == "leftMargin") left = value;
            if (rawName == "topMargin") top = value;
            if (rawName == "rightMargin") right = value;
            if (rawName == "bottomMargin") bottom = value;
            layout->setContentsMargins(left, top, right, bottom);
        } else if (rawName == "margin") {
            const int value = toInt_(textValue_(*prop), 0);
            layout->setContentsMargins(value, value, value, value);
        }
    }

    for (const auto* item : layoutNode->childrenNamed("item")) {
        if (!item) {
            continue;
        }

        if (const XmlNode* childWidgetNode = item->firstChild("widget")) {
            QWidget* child = loadQtWidget_(*childWidgetNode, parentWidget, error);
            if (!child) {
                delete layout;
                return false;
            }
            layout->addWidget(child);
            continue;
        }

        if (const XmlNode* spacerNode = item->firstChild("spacer")) {
            QSpacerItem* spacer = loadQtSpacer_(*spacerNode);
            if (!spacer) {
                delete layout;
                error = QStringLiteral("Failed to create spacer");
                return false;
            }
            layout->addItem(spacer);
        }
    }

    parentWidget->setLayout(layout);
    return true;
}

QWidget* loadQtWidget_(const XmlNode& widgetNode, QWidget* parent, QString& error) {
    QWidget* widget = createQtWidget_(widgetNode.attr("class"), parent, error);
    if (!widget) {
        return nullptr;
    }

    const SwString objectName = widgetNode.attr("name");
    if (!objectName.isEmpty()) {
        widget->setObjectName(toQString(objectName));
    }

    for (const auto* prop : widgetNode.childrenNamed("property")) {
        if (!prop) {
            continue;
        }
        const SwString rawName = prop->attr("name");
        if (rawName == "geometry") {
            continue;
        }
        applyQtProperty_(widget, rawName, *prop);
    }

    if (!applyQtLayout_(widget, widgetNode, error)) {
        delete widget;
        return nullptr;
    }

    if (!widgetNode.firstChild("layout")) {
        for (const auto* childWidgetNode : widgetNode.childrenNamed("widget")) {
            if (!childWidgetNode) {
                continue;
            }
            QWidget* child = loadQtWidget_(*childWidgetNode, widget, error);
            if (!child) {
                delete widget;
                return nullptr;
            }
            child->setParent(widget);
        }
    }

    return widget;
}

QWidget* loadQtCentralWidgetFromUi_(const QString& filePath, QWidget* parent, QString& error) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error = QStringLiteral("Failed to open UI file: ") + filePath;
        return nullptr;
    }

    const SwString xml = SwString::fromUtf8(file.readAll().constData());
    const auto parsed = SwXmlDocument::parse(xml);
    if (!parsed.ok) {
        error = toQString(parsed.error.isEmpty() ? SwString("XML parse error") : parsed.error);
        return nullptr;
    }

    const XmlNode* uiRoot = &parsed.root;
    if (uiRoot->name != "ui") {
        error = QStringLiteral("UI root element must be <ui>");
        return nullptr;
    }

    const XmlNode* mainWidget = uiRoot->firstChild("widget");
    if (!mainWidget || mainWidget->attr("class") != "QMainWindow") {
        error = QStringLiteral("UI root widget must be QMainWindow");
        return nullptr;
    }

    for (const auto* childWidgetNode : mainWidget->childrenNamed("widget")) {
        if (!childWidgetNode) {
            continue;
        }
        if (childWidgetNode->attr("name") == "centralWidget") {
            return loadQtWidget_(*childWidgetNode, parent, error);
        }
    }

    error = QStringLiteral("QMainWindow has no centralWidget");
    return nullptr;
}

template<typename T>
T* findChildByObjectName_(QObject* root, const QString& name) {
    if (!root) {
        return nullptr;
    }

    const QObjectList directChildren = root->children();
    for (QObject* child : directChildren) {
        if (!child) {
            continue;
        }

        if (T* typed = qobject_cast<T*>(child)) {
            if (typed->objectName().compare(name, Qt::CaseSensitive) == 0) {
                return typed;
            }
        }

        if (T* nested = findChildByObjectName_<T>(child, name)) {
            return nested;
        }
    }

    return nullptr;
}

class QtColorChipButton final : public QAbstractButton {
public:
    QtColorChipButton(int paletteIndex, QWidget* parent = nullptr)
        : QAbstractButton(parent)
        , paletteIndex_(clampInkIndex(paletteIndex))
        , color_(inkQColor(paletteIndex_)) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(example42SwatchSize(), example42SwatchSize());
        setFocusPolicy(Qt::NoFocus);
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
        setObjectName(QStringLiteral("canvasSurface"));
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        setMinimumSize(example42CanvasMinimumWidth(), example42CanvasMinimumHeight());
        setAttribute(Qt::WA_StyledBackground, true);
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
        if (!event || event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }

        drawing_ = true;
        strokes_.append(QtStroke{activeColor_, QVector<QPoint>{clampPoint_(event->pos())}});
        event->accept();
        update();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (drawing_ && event && (event->buttons() & Qt::LeftButton) && !strokes_.isEmpty()) {
            appendPoint_(strokes_.last().points, clampPoint_(event->pos()));
            event->accept();
            update();
            return;
        }

        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (!drawing_ || !event || event->button() != Qt::LeftButton || strokes_.isEmpty()) {
            QWidget::mouseReleaseEvent(event);
            return;
        }

        appendPoint_(strokes_.last().points, clampPoint_(event->pos()));
        drawing_ = false;
        event->accept();
        update();
    }

    void paintEvent(QPaintEvent* event) override {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF bounds = rect().adjusted(0.5, 0.5, -0.5, -0.5);
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
                             example42TextQt(example42PaneTexts().emptyCanvasHint));
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
        example42Trace("qt pane: ctor start");

        QString uiError;
        contentRoot_ = loadQtCentralWidgetFromUi_(example42StudioUiPathQt(), owner_, uiError);
        example42Trace(contentRoot_ ? "qt pane: ui loaded" : "qt pane: ui load failed");
        if (!contentRoot_) {
            contentRoot_ = new QWidget(owner_);
            contentRoot_->setObjectName(QStringLiteral("centralWidget"));
            contentRoot_->setMinimumSize(example42PaneMinimumWidth(), example42PaneMinimumHeight());
        }

        contentRoot_->setAttribute(Qt::WA_StyledBackground, true);

        QVBoxLayout* hostLayout = new QVBoxLayout(owner_);
        hostLayout->setContentsMargins(0, 0, 0, 0);
        hostLayout->setSpacing(0);
        hostLayout->addWidget(contentRoot_);

        titleLabel_ = findChildByObjectName_<QLabel>(contentRoot_, QString::fromLatin1("titleLabel"));
        subtitleLabel_ = findChildByObjectName_<QLabel>(contentRoot_, QString::fromLatin1("subtitleLabel"));
        statusLabel_ = findChildByObjectName_<QLabel>(contentRoot_, QString::fromLatin1("statusLabel"));
        runtimeLabel_ = findChildByObjectName_<QLabel>(contentRoot_, QString::fromLatin1("runtimeLabel"));
        bridgeButton_ = findChildByObjectName_<QPushButton>(contentRoot_, QString::fromLatin1("bridgeButton"));
        fiberButton_ = findChildByObjectName_<QPushButton>(contentRoot_, QString::fromLatin1("fiberButton"));
        lineEdit_ = findChildByObjectName_<QLineEdit>(contentRoot_, QString::fromLatin1("messageEdit"));
        paletteLabel_ = findChildByObjectName_<QLabel>(contentRoot_, QString::fromLatin1("paletteLabel"));
        currentInkLabel_ = findChildByObjectName_<QLabel>(contentRoot_, QString::fromLatin1("currentInkLabel"));
        clearButton_ = findChildByObjectName_<QPushButton>(contentRoot_, QString::fromLatin1("clearButton"));
        swatchStrip_ = findChildByObjectName_<QWidget>(contentRoot_, QString::fromLatin1("swatchStrip"));
        canvasHost_ = findChildByObjectName_<QWidget>(contentRoot_, QString::fromLatin1("canvasHost"));
        rootLayout_ = qobject_cast<QVBoxLayout*>(contentRoot_->layout());

        if (currentInkLabel_) {
            currentInkLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        }

        owner_->setStyleSheet(loadSharedStyleSheet_());
        example42Trace("qt pane: stylesheet applied");

        if (swatchStrip_) {
            QHBoxLayout* swatchLayout = new QHBoxLayout();
            swatchLayout->setContentsMargins(0, 0, 0, 0);
            swatchLayout->setSpacing(example42SwatchStep() - example42SwatchSize());
            swatchStrip_->setLayout(swatchLayout);

            swatchGroup_ = new QButtonGroup(owner_);
            swatchGroup_->setExclusive(true);

            for (int index = 0; index < kInkPaletteCount; ++index) {
                QtColorChipButton* swatch = new QtColorChipButton(index, swatchStrip_);
                swatchGroup_->addButton(swatch, index);
                swatchLayout->addWidget(swatch);
                swatches_.append(swatch);

                owner_->connect(swatch, &QAbstractButton::clicked, owner_, [this, index]() {
                    selectInk(index);
                    if (statusLabel_) {
                        statusLabel_->setText(QStringLiteral("Qt ink -> ") + inkName(index));
                    }
                });
            }
            swatchLayout->addStretch(1);
        }

        if (canvasHost_) {
            QVBoxLayout* canvasLayout = new QVBoxLayout();
            canvasLayout->setContentsMargins(0, 0, 0, 0);
            canvasLayout->setSpacing(0);
            canvasHost_->setLayout(canvasLayout);

            canvas_ = new QtSketchCanvas(canvasHost_);
            canvasLayout->addWidget(canvas_);
            example42Trace("qt pane: canvas created");
        }

        if (rootLayout_ && canvasHost_) {
            rootLayout_->setStretchFactor(canvasHost_, 1);
        }

        if (clearButton_) {
            owner_->connect(clearButton_, &QPushButton::clicked, owner_, [this]() {
                if (canvas_) {
                    canvas_->clearSketch();
                }
                if (statusLabel_) {
                    statusLabel_->setText(QStringLiteral("Qt canvas cleared"));
                }
            });
        }

        selectInk(0);
        owner_->setMinimumSize(contentRoot_->minimumSizeHint());
        example42Trace("qt pane: ctor done");
    }

    QPushButton* bridgeButton() const {
        return bridgeButton_;
    }

    QPushButton* fiberButton() const {
        return fiberButton_;
    }

    QString messageText() const {
        return lineEdit_ ? lineEdit_->text().trimmed() : QString();
    }

    void setStatusText(const QString& text) {
        if (statusLabel_) {
            statusLabel_->setText(text);
        }
    }

    void setRuntimeStatusText(const QString& text) {
        if (runtimeLabel_) {
            runtimeLabel_->setText(text);
        }
    }

    QSize minimumSizeHint() const {
        return contentRoot_ ? contentRoot_->minimumSizeHint()
                            : QSize(example42PaneMinimumWidth(), example42PaneMinimumHeight());
    }

    QSize sizeHint() const {
        if (!contentRoot_) {
            return QSize(example42PanePreferredWidth(), example42PanePreferredHeight());
        }
        return contentRoot_->sizeHint().expandedTo(contentRoot_->minimumSizeHint());
    }

    QString debugGeometryReport() const {
        QString report;
        auto appendWidget = [&report](const QString& label, const QWidget* widget) {
            report += label;
            if (!widget) {
                report += QStringLiteral(": <null>\n");
                return;
            }

            const QRect g = widget->geometry();
            const QSize min = widget->minimumSizeHint();
            const QSize pref = widget->sizeHint();
            report += QStringLiteral(": geom=%1,%2 %3x%4 min=%5x%6 pref=%7x%8 object=%9\n")
                          .arg(g.x())
                          .arg(g.y())
                          .arg(g.width())
                          .arg(g.height())
                          .arg(min.width())
                          .arg(min.height())
                          .arg(pref.width())
                          .arg(pref.height())
                          .arg(widget->objectName());
        };

        appendWidget(QStringLiteral("owner"), owner_);
        appendWidget(QStringLiteral("contentRoot"), contentRoot_);
        appendWidget(QStringLiteral("titleLabel"), titleLabel_);
        appendWidget(QStringLiteral("subtitleLabel"), subtitleLabel_);
        appendWidget(QStringLiteral("statusLabel"), statusLabel_);
        appendWidget(QStringLiteral("runtimeLabel"), runtimeLabel_);
        appendWidget(QStringLiteral("bridgeButton"), bridgeButton_);
        appendWidget(QStringLiteral("fiberButton"), fiberButton_);
        appendWidget(QStringLiteral("messageEdit"), lineEdit_);
        appendWidget(QStringLiteral("paletteLabel"), paletteLabel_);
        appendWidget(QStringLiteral("currentInkLabel"), currentInkLabel_);
        appendWidget(QStringLiteral("clearButton"), clearButton_);
        appendWidget(QStringLiteral("swatchStrip"), swatchStrip_);
        appendWidget(QStringLiteral("canvasHost"), canvasHost_);
        appendWidget(QStringLiteral("canvasSurface"), canvas_);
        for (int i = 0; i < swatches_.size(); ++i) {
            appendWidget(QStringLiteral("swatch[%1]").arg(i), swatches_.at(i));
        }
        return report;
    }

private:
    void selectInk(int index) {
        const int clamped = clampInkIndex(index);
        if (currentInkLabel_) {
            currentInkLabel_->setText(inkLabelText(clamped));
        }
        if (canvas_) {
            canvas_->setInkColor(clamped);
        }
        for (QtColorChipButton* swatch : swatches_) {
            if (swatch) {
                swatch->setChecked(swatch->paletteIndex() == clamped);
                swatch->update();
            }
        }
    }

    QtStudioPane* owner_{nullptr};
    QWidget* contentRoot_{nullptr};
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
    QWidget* swatchStrip_{nullptr};
    QWidget* canvasHost_{nullptr};
    QVBoxLayout* rootLayout_{nullptr};
    QtSketchCanvas* canvas_{nullptr};
    QButtonGroup* swatchGroup_{nullptr};
    QList<QtColorChipButton*> swatches_;
};

QtStudioPane::QtStudioPane(QWidget* parent)
    : QWidget(parent)
    , impl_(new Impl(this)) {
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

QString QtStudioPane::debugGeometryReport() const {
    return impl_->debugGeometryReport();
}

QSize QtStudioPane::minimumSizeHint() const {
    return impl_->minimumSizeHint();
}

QSize QtStudioPane::sizeHint() const {
    return impl_->sizeHint();
}
