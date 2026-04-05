#include <memory>

#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QList>
#include <QLineEdit>
#include <QMainWindow>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPaintEngine>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QPixmap>
#include <QRect>
#include <QRadioButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScreen>
#include <QShowEvent>
#include <QSlider>
#include <QSpacerItem>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSplitter>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QToolBox>
#include <QVBoxLayout>
#include <QWindow>
#include <QWidget>

#include "SwGuiApplication.h"
#include "SwUiLoader.h"
#include "SwWidget.h"
#include "SwWidgetPlatformAdapter.h"
#include "SwWidgetSnapshot.h"
#include "SwXmlDocument.h"
#include "gui/qtbinding/SwQtBindingEventPump.h"
#include "gui/qtbinding/SwQtBindingWin32WidgetHost.h"
#include "demo/Example42SketchSupport.h"
#include "demo/ExampleSwThreadFiberBridge.h"
#include "ui/QtSwHostWidget.h"
#include "ui/QtStudioPane.h"

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#endif

namespace {

class ScopedSwGuiApplication final : public SwGuiApplication {
public:
    ~ScopedSwGuiApplication() override {
        instance(false) = nullptr;
    }
};

struct SnapshotDiffSummary {
    bool comparable;
    int differingPixels;
    int totalPixels;
};

using XmlNode = SwXmlNode;

constexpr int kSwPumpBusyIntervalMs_ = 1;
constexpr int kSwPumpWakeCheckIntervalMs_ = 50;

int swPumpIntervalMsForDelayUs_(int nextDelayUs) {
    if (nextDelayUs == 0) {
        return kSwPumpBusyIntervalMs_;
    }

    if (nextDelayUs < 0) {
        return kSwPumpWakeCheckIntervalMs_;
    }

    const qint64 roundedDelayMs = (static_cast<qint64>(nextDelayUs) + 999) / 1000;
    if (roundedDelayMs <= kSwPumpBusyIntervalMs_) {
        return kSwPumpBusyIntervalMs_;
    }

    if (roundedDelayMs >= kSwPumpWakeCheckIntervalMs_) {
        return kSwPumpWakeCheckIntervalMs_;
    }

    return static_cast<int>(roundedDelayMs);
}

class AdaptiveSwPumpTimer_ final {
public:
    AdaptiveSwPumpTimer_(QObject* context, SwQtBindingEventPump* pump, int maxIterations = 64)
        : pump_(pump)
        , maxIterations_(maxIterations) {
        timer_.setSingleShot(true);
        timer_.setTimerType(Qt::PreciseTimer);
        QObject::connect(&timer_, &QTimer::timeout, context, [this]() {
            tick_();
        });
    }

    void start() {
        if (running_) {
            return;
        }

        running_ = true;
        scheduleNext_(kSwPumpBusyIntervalMs_);
    }

    void stop() {
        running_ = false;
        timer_.stop();
    }

    int drainNow(int maxIterations = -1, bool flushDamage = true) {
        if (!pump_) {
            return -1;
        }

        const int iterationBudget = (maxIterations > 0) ? maxIterations : maxIterations_;
        const int nextDelayUs = pump_->drainPostedWork(iterationBudget, flushDamage);
        if (running_) {
            scheduleNext_(swPumpIntervalMsForDelayUs_(nextDelayUs));
        }
        return nextDelayUs;
    }

private:
    void tick_() {
        if (!running_ || !pump_) {
            return;
        }

        const int nextDelayUs = pump_->drainPostedWork(maxIterations_, true);
        scheduleNext_(swPumpIntervalMsForDelayUs_(nextDelayUs));
    }

    void scheduleNext_(int intervalMs) {
        if (!running_) {
            return;
        }
        timer_.setTimerType(intervalMs <= 8 ? Qt::PreciseTimer : Qt::CoarseTimer);
        timer_.start(intervalMs);
    }

    SwQtBindingEventPump* pump_{nullptr};
    QTimer timer_{};
    int maxIterations_{64};
    bool running_{false};
};

QString optionValueFromArgs_(int argc, char* argv[], const QString& optionName) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (argv[i] && QString::fromLocal8Bit(argv[i]).compare(optionName, Qt::CaseSensitive) == 0) {
            return QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[i + 1]));
        }
    }
    return QString();
}

bool hasFlagInArgs_(int argc, char* argv[], const QString& optionName) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && QString::fromLocal8Bit(argv[i]).compare(optionName, Qt::CaseSensitive) == 0) {
            return true;
        }
    }
    return false;
}

QString snapshotOutputDirFromArgs_(int argc, char* argv[]) {
    return optionValueFromArgs_(argc, argv, QStringLiteral("--snapshot"));
}

QString widgetSuiteOutputDirFromArgs_(int argc, char* argv[]) {
    return optionValueFromArgs_(argc, argv, QStringLiteral("--widget-suite"));
}

QString widgetPreviewFileFromArgs_(int argc, char* argv[]) {
    return optionValueFromArgs_(argc, argv, QStringLiteral("--widget-preview"));
}

bool noStyleSheetFromArgs_(int argc, char* argv[]) {
    return hasFlagInArgs_(argc, argv, QStringLiteral("--no-stylesheet"));
}

QString normalizedWidgetPreviewFileName_(const QString& rawName) {
    QString fileName = rawName.trimmed();
    if (fileName.isEmpty()) {
        return QString();
    }
    if (!fileName.endsWith(QStringLiteral(".ui"), Qt::CaseInsensitive)) {
        fileName += QStringLiteral(".ui");
    }
    return fileName;
}

QString normalizedSnapshotDir_(const QString& rawPath) {
    const QString cleaned = QDir::cleanPath(rawPath);
    if (cleaned.isEmpty()) {
        return QString();
    }

    QString withSeparator = cleaned;
    if (!withSeparator.endsWith(QLatin1Char('/'))) {
        withSeparator += QLatin1Char('/');
    }
    return withSeparator;
}

void clearQtStyleSheetsRecursive_(QWidget* widget) {
    if (!widget) {
        return;
    }

    widget->setStyleSheet(QString());
    const QObjectList directChildren = widget->children();
    for (QObject* childObject : directChildren) {
        QWidget* childWidget = qobject_cast<QWidget*>(childObject);
        if (!childWidget) {
            continue;
        }
        clearQtStyleSheetsRecursive_(childWidget);
    }
}

void clearSwStyleSheetsRecursive_(SwObject* object) {
    if (!object) {
        return;
    }

    if (auto* widget = dynamic_cast<SwWidget*>(object)) {
        widget->setStyleSheet(SwString());
        widget->setDefaultStyleSheet(SwString());
    } else if (object->propertyExist("StyleSheet")) {
        object->setProperty("StyleSheet", SwAny(SwString()));
    }

    const SwList<SwObject*>& directChildren = object->children();
    for (SwObject* childObject : directChildren) {
        clearSwStyleSheetsRecursive_(childObject);
    }
}

bool ensureSnapshotDir_(const QString& dirPath) {
    if (dirPath.isEmpty()) {
        return false;
    }
    QDir dir;
    return dir.mkpath(dirPath);
}

bool savePixmap_(const QPixmap& pixmap, const QString& filePath) {
    if (pixmap.isNull()) {
        return false;
    }
    return pixmap.save(filePath);
}

bool saveTextFile_(const QString& filePath, const QString& content) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    const QByteArray utf8 = content.toUtf8();
    return file.write(utf8) == utf8.size();
}

bool saveWindowRegionSnapshot_(QWidget* topLevelWindow, const QRect& region, const QString& filePath) {
    if (!topLevelWindow || region.width() <= 0 || region.height() <= 0) {
        return false;
    }

    QWindow* nativeWindow = topLevelWindow->windowHandle();
    QScreen* screen = nativeWindow ? nativeWindow->screen() : QApplication::primaryScreen();
    if (!screen) {
        return false;
    }

    const QPixmap pixmap = screen->grabWindow(topLevelWindow->winId(),
                                              region.x(),
                                              region.y(),
                                              region.width(),
                                              region.height());
    return savePixmap_(pixmap, filePath);
}

bool saveWidgetSnapshot_(QWidget* widget, const QString& filePath) {
    if (!widget || widget->width() <= 0 || widget->height() <= 0) {
        return false;
    }
    QPixmap pixmap(widget->size());
    pixmap.fill(Qt::transparent);
    widget->render(&pixmap);
    return savePixmap_(pixmap, filePath);
}

QString loadTextFile_(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

QString dumpQtWidgetTree_(QWidget* widget, int depth = 0) {
    if (!widget) {
        return QString();
    }

    const QString indent(depth * 2, QLatin1Char(' '));
    const QRect geom = widget->geometry();
    const QSize minSize = widget->minimumSize();
    const QSize prefSize = widget->sizeHint();
    QString extraState;
    if (QTabWidget* tabWidget = qobject_cast<QTabWidget*>(widget)) {
        extraState += QStringLiteral(" currentIndex=%1").arg(tabWidget->currentIndex());
    } else if (QToolBox* toolBox = qobject_cast<QToolBox*>(widget)) {
        extraState += QStringLiteral(" currentIndex=%1").arg(toolBox->currentIndex());
    } else if (QStackedWidget* stackedWidget = qobject_cast<QStackedWidget*>(widget)) {
        extraState += QStringLiteral(" currentIndex=%1").arg(stackedWidget->currentIndex());
    } else if (QComboBox* comboBox = qobject_cast<QComboBox*>(widget)) {
        extraState += QStringLiteral(" currentIndex=%1").arg(comboBox->currentIndex());
    }

    QString report = QStringLiteral("%1%2 name=%3 geom=%4,%5 %6x%7 min=%8x%9 pref=%10x%11\n")
                         .arg(indent)
                         .arg(QString::fromLatin1(widget->metaObject()->className()))
                         .arg(widget->objectName())
                         .arg(geom.x())
                         .arg(geom.y())
                         .arg(geom.width())
                         .arg(geom.height())
                         .arg(minSize.width())
                         .arg(minSize.height())
                         .arg(prefSize.width())
                         .arg(prefSize.height());
    report += extraState;
    report += QLatin1Char('\n');

    const QObjectList childObjects = widget->children();
    for (QObject* childObject : childObjects) {
        QWidget* childWidget = qobject_cast<QWidget*>(childObject);
        if (!childWidget) {
            continue;
        }
        if (childWidget->parentWidget() != widget) {
            continue;
        }
        report += dumpQtWidgetTree_(childWidget, depth + 1);
    }

    return report;
}

QString dumpSwWidgetTree_(SwWidget* widget, int depth = 0) {
    if (!widget) {
        return QString();
    }

    const QString indent(depth * 2, QLatin1Char(' '));
    const SwRect geom = widget->geometry();
    const SwSize minSize = widget->minimumSize();
    const SwSize prefSize = widget->sizeHint();
    QString extraState;
    if (SwTabWidget* tabWidget = dynamic_cast<SwTabWidget*>(widget)) {
        extraState += QStringLiteral(" currentIndex=%1").arg(tabWidget->currentIndex());
    } else if (SwToolBox* toolBox = dynamic_cast<SwToolBox*>(widget)) {
        extraState += QStringLiteral(" currentIndex=%1").arg(toolBox->currentIndex());
    } else if (SwStackedWidget* stackedWidget = dynamic_cast<SwStackedWidget*>(widget)) {
        extraState += QStringLiteral(" currentIndex=%1").arg(stackedWidget->currentIndex());
    } else if (SwComboBox* comboBox = dynamic_cast<SwComboBox*>(widget)) {
        extraState += QStringLiteral(" currentIndex=%1").arg(comboBox->currentIndex());
    }

    QString report = QStringLiteral("%1%2 name=%3 geom=%4,%5 %6x%7 min=%8x%9 pref=%10x%11\n")
                         .arg(indent)
                         .arg(toQString(widget->className()))
                         .arg(toQString(widget->getObjectName()))
                         .arg(geom.x)
                         .arg(geom.y)
                         .arg(geom.width)
                         .arg(geom.height)
                         .arg(minSize.width)
                         .arg(minSize.height)
                         .arg(prefSize.width)
                         .arg(prefSize.height);
    report += extraState;
    report += QLatin1Char('\n');

    const auto& childObjects = widget->children();
    for (SwObject* childObject : childObjects) {
        SwWidget* childWidget = dynamic_cast<SwWidget*>(childObject);
        if (!childWidget) {
            continue;
        }
        if (childWidget->parent() != widget) {
            continue;
        }
        report += dumpSwWidgetTree_(childWidget, depth + 1);
    }

    return report;
}

SnapshotDiffSummary saveDiffImage_(const QString& leftPath,
                                   const QString& rightPath,
                                   const QString& diffPath) {
    SnapshotDiffSummary summary{false, 0, 0};

    QImage left(leftPath);
    QImage right(rightPath);
    if (left.isNull() || right.isNull()) {
        return summary;
    }

    left = left.convertToFormat(QImage::Format_ARGB32);
    right = right.convertToFormat(QImage::Format_ARGB32);
    if (left.size() != right.size()) {
        return summary;
    }

    summary.comparable = true;
    summary.totalPixels = left.width() * left.height();

    QImage diff(left.size(), QImage::Format_ARGB32);
    diff.fill(qRgba(255, 255, 255, 255));

    for (int y = 0; y < left.height(); ++y) {
        for (int x = 0; x < left.width(); ++x) {
            const QRgb leftPixel = left.pixel(x, y);
            const QRgb rightPixel = right.pixel(x, y);
            if (leftPixel == rightPixel) {
                const int gray = qGray(leftPixel);
                diff.setPixel(x, y, qRgba(gray, gray, gray, 255));
                continue;
            }

            ++summary.differingPixels;
            diff.setPixel(x, y, qRgba(255, 0, 255, 255));
        }
    }

    diff.save(diffPath);
    return summary;
}

SwString childText_(const XmlNode& node, const char* childName) {
    const XmlNode* child = node.firstChild(childName);
    return child ? child->text.trimmed() : SwString();
}

SwString textValue_(const XmlNode& propNode) {
    if (!propNode.children.isEmpty()) {
        for (const auto& child : propNode.children) {
            if (child.name == "string" || child.name == "cstring" || child.name == "number" ||
                child.name == "double" || child.name == "bool" || child.name == "enum" || child.name == "set") {
                return child.text.trimmed();
            }
        }
    }
    return propNode.text.trimmed();
}

SwString attributeValue_(const XmlNode& widgetNode, const char* attributeName) {
    if (!attributeName) {
        return SwString();
    }
    for (const auto* attributeNode : widgetNode.childrenNamed("attribute")) {
        if (!attributeNode) {
            continue;
        }
        if (attributeNode->attr("name") == attributeName) {
            return textValue_(*attributeNode);
        }
    }
    return SwString();
}

int toInt_(const SwString& value, int defaultValue = 0) {
    if (value.isEmpty()) {
        return defaultValue;
    }
    bool ok = false;
    const int parsed = value.trimmed().toInt(&ok);
    return ok ? parsed : defaultValue;
}

double toDouble_(const SwString& value, double defaultValue = 0.0) {
    if (value.isEmpty()) {
        return defaultValue;
    }
    bool ok = false;
    const double parsed = value.trimmed().toDouble(&ok);
    return ok ? parsed : defaultValue;
}

QSizePolicy::Policy toQSizePolicy_(SwString value, QSizePolicy::Policy fallback = QSizePolicy::Preferred) {
    value = value.trimmed();
    const size_t sep = value.lastIndexOf(':');
    if (sep != static_cast<size_t>(-1)) {
        value = value.mid(static_cast<int>(sep + 1));
    }

    if (value == "Fixed") return QSizePolicy::Fixed;
    if (value == "Minimum") return QSizePolicy::Minimum;
    if (value == "Maximum") return QSizePolicy::Maximum;
    if (value == "Preferred") return QSizePolicy::Preferred;
    if (value == "MinimumExpanding") return QSizePolicy::MinimumExpanding;
    if (value == "Expanding") return QSizePolicy::Expanding;
    if (value == "Ignored") return QSizePolicy::Ignored;
    return fallback;
}

Qt::Alignment toQtAlignment_(SwString value) {
    value = value.trimmed().toLower();

    Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignTop;
    if (value.contains("alignright")) {
        alignment = (alignment & ~Qt::AlignHorizontal_Mask) | Qt::AlignRight;
    } else if (value.contains("alignhcenter") || value.contains("aligncenter")) {
        alignment = (alignment & ~Qt::AlignHorizontal_Mask) | Qt::AlignHCenter;
    } else {
        alignment = (alignment & ~Qt::AlignHorizontal_Mask) | Qt::AlignLeft;
    }

    if (value.contains("alignbottom")) {
        alignment = (alignment & ~Qt::AlignVertical_Mask) | Qt::AlignBottom;
    } else if (value.contains("alignvcenter") || value.contains("aligncenter")) {
        alignment = (alignment & ~Qt::AlignVertical_Mask) | Qt::AlignVCenter;
    } else {
        alignment = (alignment & ~Qt::AlignVertical_Mask) | Qt::AlignTop;
    }

    return alignment;
}

SwString uiRootClassFromFile_(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return SwString();
    }

    const SwString xml = SwString::fromUtf8(file.readAll().constData());
    const auto parsed = SwXmlDocument::parse(xml);
    if (!parsed.ok || parsed.root.name != "ui") {
        return SwString();
    }

    const XmlNode* rootWidgetNode = parsed.root.firstChild("widget");
    return rootWidgetNode ? rootWidgetNode->attr("class") : SwString();
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
    if (className == "QGridLayout") {
        return new QGridLayout();
    }
    if (className == "QFormLayout") {
        return new QFormLayout();
    }

    error = QStringLiteral("Unsupported layout class: ") + toQString(className);
    return nullptr;
}

QWidget* createQtWidget_(const SwString& className, QWidget* parent, QString& error) {
    if (className == "QWidget") {
        return new QWidget(parent);
    }
    if (className == "QFrame") {
        return new QFrame(parent);
    }
    if (className == "QGroupBox") {
        return new QGroupBox(parent);
    }
    if (className == "QLabel") {
        return new QLabel(parent);
    }
    if (className == "QPushButton") {
        return new QPushButton(parent);
    }
    if (className == "QToolButton") {
        return new QToolButton(parent);
    }
    if (className == "QCheckBox") {
        return new QCheckBox(parent);
    }
    if (className == "QRadioButton") {
        return new QRadioButton(parent);
    }
    if (className == "QLineEdit") {
        return new QLineEdit(parent);
    }
    if (className == "QComboBox") {
        return new QComboBox(parent);
    }
    if (className == "QPlainTextEdit") {
        return new QPlainTextEdit(parent);
    }
    if (className == "QTextEdit") {
        return new QTextEdit(parent);
    }
    if (className == "QProgressBar") {
        return new QProgressBar(parent);
    }
    if (className == "QSlider") {
        return new QSlider(parent);
    }
    if (className == "QSpinBox") {
        return new QSpinBox(parent);
    }
    if (className == "QDoubleSpinBox") {
        return new QDoubleSpinBox(parent);
    }
    if (className == "QTabWidget") {
        return new QTabWidget(parent);
    }
    if (className == "QToolBox") {
        return new QToolBox(parent);
    }
    if (className == "QStackedWidget") {
        return new QStackedWidget(parent);
    }
    if (className == "QScrollArea") {
        return new QScrollArea(parent);
    }
    if (className == "QSplitter") {
        return new QSplitter(parent);
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

    if (rawName == "sizePolicy") {
        const XmlNode* sizePolicyNode = propNode.firstChild("sizepolicy");
        if (!sizePolicyNode) {
            return;
        }
        QSizePolicy sizePolicy(widget->sizePolicy());
        sizePolicy.setHorizontalPolicy(toQSizePolicy_(sizePolicyNode->attr("hsizetype"), sizePolicy.horizontalPolicy()));
        sizePolicy.setVerticalPolicy(toQSizePolicy_(sizePolicyNode->attr("vsizetype"), sizePolicy.verticalPolicy()));
        widget->setSizePolicy(sizePolicy);
        return;
    }

    if (rawName == "enabled") {
        widget->setEnabled(textValue_(propNode).trimmed().toLower() == "true" || textValue_(propNode).trimmed() == "1");
        return;
    }

    if (rawName == "visible") {
        widget->setVisible(textValue_(propNode).trimmed().toLower() == "true" || textValue_(propNode).trimmed() == "1");
        return;
    }

    if (rawName == "text") {
        const QString text = toQString(textValue_(propNode));
        if (QGroupBox* groupBox = qobject_cast<QGroupBox*>(widget)) {
            groupBox->setTitle(text);
            return;
        }
        if (QLabel* label = qobject_cast<QLabel*>(widget)) {
            label->setText(text);
            return;
        }
        if (QPushButton* button = qobject_cast<QPushButton*>(widget)) {
            button->setText(text);
            return;
        }
        if (QToolButton* toolButton = qobject_cast<QToolButton*>(widget)) {
            toolButton->setText(text);
            return;
        }
        if (QCheckBox* checkBox = qobject_cast<QCheckBox*>(widget)) {
            checkBox->setText(text);
            return;
        }
        if (QRadioButton* radioButton = qobject_cast<QRadioButton*>(widget)) {
            radioButton->setText(text);
            return;
        }
        if (QLineEdit* lineEdit = qobject_cast<QLineEdit*>(widget)) {
            lineEdit->setText(text);
            return;
        }
    }

    if (rawName == "plainText") {
        const QString text = toQString(textValue_(propNode));
        if (QPlainTextEdit* plainTextEdit = qobject_cast<QPlainTextEdit*>(widget)) {
            plainTextEdit->setPlainText(text);
            return;
        }
        if (QTextEdit* textEdit = qobject_cast<QTextEdit*>(widget)) {
            textEdit->setPlainText(text);
            return;
        }
        return;
    }

    if (rawName == "html") {
        if (QTextEdit* textEdit = qobject_cast<QTextEdit*>(widget)) {
            textEdit->setHtml(toQString(textValue_(propNode)));
        }
        return;
    }

    if (rawName == "title") {
        if (QGroupBox* groupBox = qobject_cast<QGroupBox*>(widget)) {
            groupBox->setTitle(toQString(textValue_(propNode)));
        }
        return;
    }

    if (rawName == "alignment") {
        const Qt::Alignment alignment = toQtAlignment_(textValue_(propNode));
        if (QLabel* label = qobject_cast<QLabel*>(widget)) {
            label->setAlignment(alignment);
            return;
        }
        if (QLineEdit* lineEdit = qobject_cast<QLineEdit*>(widget)) {
            lineEdit->setAlignment(alignment);
            return;
        }
        return;
    }

    if (rawName == "placeholderText") {
        if (QLineEdit* lineEdit = qobject_cast<QLineEdit*>(widget)) {
            lineEdit->setPlaceholderText(toQString(textValue_(propNode)));
        }
        return;
    }

    if (rawName == "readOnly") {
        const bool readOnly = textValue_(propNode).trimmed().toLower() == "true" || textValue_(propNode).trimmed() == "1";
        if (QLineEdit* lineEdit = qobject_cast<QLineEdit*>(widget)) {
            lineEdit->setReadOnly(readOnly);
            return;
        }
        if (QPlainTextEdit* plainTextEdit = qobject_cast<QPlainTextEdit*>(widget)) {
            plainTextEdit->setReadOnly(readOnly);
            return;
        }
        if (QTextEdit* textEdit = qobject_cast<QTextEdit*>(widget)) {
            textEdit->setReadOnly(readOnly);
            return;
        }
        return;
    }

    if (rawName == "checkable") {
        const bool checkable = textValue_(propNode).trimmed().toLower() == "true" || textValue_(propNode).trimmed() == "1";
        if (QGroupBox* groupBox = qobject_cast<QGroupBox*>(widget)) {
            groupBox->setCheckable(checkable);
            return;
        }
        if (QToolButton* toolButton = qobject_cast<QToolButton*>(widget)) {
            toolButton->setCheckable(checkable);
            return;
        }
        if (QPushButton* button = qobject_cast<QPushButton*>(widget)) {
            button->setCheckable(checkable);
            return;
        }
        return;
    }

    if (rawName == "checked") {
        const bool checked = textValue_(propNode).trimmed().toLower() == "true" || textValue_(propNode).trimmed() == "1";
        if (QGroupBox* groupBox = qobject_cast<QGroupBox*>(widget)) {
            groupBox->setChecked(checked);
            return;
        }
        if (QCheckBox* checkBox = qobject_cast<QCheckBox*>(widget)) {
            checkBox->setChecked(checked);
            return;
        }
        if (QRadioButton* radioButton = qobject_cast<QRadioButton*>(widget)) {
            radioButton->setChecked(checked);
            return;
        }
        if (QToolButton* toolButton = qobject_cast<QToolButton*>(widget)) {
            toolButton->setCheckable(true);
            toolButton->setChecked(checked);
            return;
        }
        if (QPushButton* button = qobject_cast<QPushButton*>(widget)) {
            button->setCheckable(true);
            button->setChecked(checked);
            return;
        }
        return;
    }

    if (rawName == "orientation") {
        const QString value = toQString(textValue_(propNode)).trimmed().toLower();
        const Qt::Orientation orientation = value.contains(QStringLiteral("vertical")) ? Qt::Vertical : Qt::Horizontal;
        if (QSlider* slider = qobject_cast<QSlider*>(widget)) {
            slider->setOrientation(orientation);
            return;
        }
        if (QProgressBar* progressBar = qobject_cast<QProgressBar*>(widget)) {
            progressBar->setOrientation(orientation);
            return;
        }
        if (QSplitter* splitter = qobject_cast<QSplitter*>(widget)) {
            splitter->setOrientation(orientation);
            return;
        }
        return;
    }

    if (rawName == "widgetResizable") {
        if (QScrollArea* scrollArea = qobject_cast<QScrollArea*>(widget)) {
            const bool widgetResizable = textValue_(propNode).trimmed().toLower() == "true" || textValue_(propNode).trimmed() == "1";
            scrollArea->setWidgetResizable(widgetResizable);
        }
        return;
    }

    if (rawName == "handleWidth") {
        if (QSplitter* splitter = qobject_cast<QSplitter*>(widget)) {
            splitter->setHandleWidth(toInt_(textValue_(propNode), splitter->handleWidth()));
        }
        return;
    }

    if (rawName == "minimum") {
        const int value = toInt_(textValue_(propNode), 0);
        if (QProgressBar* progressBar = qobject_cast<QProgressBar*>(widget)) {
            progressBar->setMinimum(value);
            return;
        }
        if (QSlider* slider = qobject_cast<QSlider*>(widget)) {
            slider->setMinimum(value);
            return;
        }
        if (QSpinBox* spinBox = qobject_cast<QSpinBox*>(widget)) {
            spinBox->setMinimum(value);
            return;
        }
        if (QDoubleSpinBox* doubleSpinBox = qobject_cast<QDoubleSpinBox*>(widget)) {
            doubleSpinBox->setMinimum(toDouble_(textValue_(propNode), doubleSpinBox->minimum()));
            return;
        }
        return;
    }

    if (rawName == "maximum") {
        const int value = toInt_(textValue_(propNode), 0);
        if (QProgressBar* progressBar = qobject_cast<QProgressBar*>(widget)) {
            progressBar->setMaximum(value);
            return;
        }
        if (QSlider* slider = qobject_cast<QSlider*>(widget)) {
            slider->setMaximum(value);
            return;
        }
        if (QSpinBox* spinBox = qobject_cast<QSpinBox*>(widget)) {
            spinBox->setMaximum(value);
            return;
        }
        if (QDoubleSpinBox* doubleSpinBox = qobject_cast<QDoubleSpinBox*>(widget)) {
            doubleSpinBox->setMaximum(toDouble_(textValue_(propNode), doubleSpinBox->maximum()));
            return;
        }
        return;
    }

    if (rawName == "value") {
        const int value = toInt_(textValue_(propNode), 0);
        if (QProgressBar* progressBar = qobject_cast<QProgressBar*>(widget)) {
            progressBar->setValue(value);
            return;
        }
        if (QSlider* slider = qobject_cast<QSlider*>(widget)) {
            slider->setValue(value);
            return;
        }
        if (QSpinBox* spinBox = qobject_cast<QSpinBox*>(widget)) {
            spinBox->setValue(value);
            return;
        }
        if (QDoubleSpinBox* doubleSpinBox = qobject_cast<QDoubleSpinBox*>(widget)) {
            doubleSpinBox->setValue(toDouble_(textValue_(propNode), doubleSpinBox->value()));
            return;
        }
        return;
    }

    if (rawName == "decimals") {
        if (QDoubleSpinBox* doubleSpinBox = qobject_cast<QDoubleSpinBox*>(widget)) {
            doubleSpinBox->setDecimals(toInt_(textValue_(propNode), doubleSpinBox->decimals()));
        }
        return;
    }

    if (rawName == "singleStep") {
        if (QSpinBox* spinBox = qobject_cast<QSpinBox*>(widget)) {
            spinBox->setSingleStep(toInt_(textValue_(propNode), spinBox->singleStep()));
            return;
        }
        if (QDoubleSpinBox* doubleSpinBox = qobject_cast<QDoubleSpinBox*>(widget)) {
            doubleSpinBox->setSingleStep(toDouble_(textValue_(propNode), doubleSpinBox->singleStep()));
            return;
        }
    }
}

void applyQtComboItems_(QWidget* widget, const XmlNode& widgetNode) {
    QComboBox* comboBox = qobject_cast<QComboBox*>(widget);
    if (!comboBox) {
        return;
    }

    for (const auto* itemNode : widgetNode.childrenNamed("item")) {
        if (!itemNode) {
            continue;
        }
        QString text;
        for (const auto* prop : itemNode->childrenNamed("property")) {
            if (!prop || prop->attr("name") != "text") {
                continue;
            }
            text = toQString(textValue_(*prop));
            break;
        }
        comboBox->addItem(text);
    }
}

bool attachQtChildToContainer_(QWidget* parentWidget, QWidget* child, const XmlNode& childWidgetNode, QString& error) {
    if (!parentWidget || !child) {
        return true;
    }

    if (QTabWidget* tabWidget = qobject_cast<QTabWidget*>(parentWidget)) {
        QString label = toQString(attributeValue_(childWidgetNode, "title"));
        if (label.isEmpty()) label = toQString(attributeValue_(childWidgetNode, "label"));
        if (label.isEmpty()) label = toQString(childWidgetNode.attr("name"));
        if (label.isEmpty()) label = child->objectName();
        if (label.isEmpty()) label = child->metaObject()->className();
        tabWidget->addTab(child, label);
        return true;
    }

    if (QToolBox* toolBox = qobject_cast<QToolBox*>(parentWidget)) {
        QString label = toQString(attributeValue_(childWidgetNode, "label"));
        if (label.isEmpty()) label = toQString(attributeValue_(childWidgetNode, "title"));
        if (label.isEmpty()) label = toQString(childWidgetNode.attr("name"));
        if (label.isEmpty()) label = child->objectName();
        if (label.isEmpty()) label = child->metaObject()->className();
        toolBox->addItem(child, label);
        return true;
    }

    if (QStackedWidget* stackedWidget = qobject_cast<QStackedWidget*>(parentWidget)) {
        stackedWidget->addWidget(child);
        return true;
    }

    if (QSplitter* splitter = qobject_cast<QSplitter*>(parentWidget)) {
        child->setParent(splitter);
        splitter->addWidget(child);
        return true;
    }

    if (QScrollArea* scrollArea = qobject_cast<QScrollArea*>(parentWidget)) {
        if (scrollArea->widget() && scrollArea->widget() != child) {
            error = QStringLiteral("QScrollArea can only have one content widget");
            return false;
        }
        child->setParent(scrollArea);
        scrollArea->setWidget(child);
        return true;
    }

    child->setParent(parentWidget);
    return true;
}

QWidget* loadQtWidgetNode_(const XmlNode& widgetNode, QWidget* parent, QString& error);

void applyQtDeferredCurrentIndices_(QWidget* widget) {
    if (!widget) {
        return;
    }

    const QVariant deferred = widget->property("_swDeferredCurrentIndex");
    if (deferred.isValid()) {
        const int deferredCurrentIndex = deferred.toInt();
        if (QComboBox* comboBox = qobject_cast<QComboBox*>(widget)) {
            if (comboBox->count() > 0) {
                comboBox->setCurrentIndex(std::max(0, std::min(deferredCurrentIndex, comboBox->count() - 1)));
            }
        } else if (QTabWidget* tabWidget = qobject_cast<QTabWidget*>(widget)) {
            if (tabWidget->count() > 0) {
                tabWidget->setCurrentIndex(std::max(0, std::min(deferredCurrentIndex, tabWidget->count() - 1)));
            }
        } else if (QToolBox* toolBox = qobject_cast<QToolBox*>(widget)) {
            if (toolBox->count() > 0) {
                toolBox->setCurrentIndex(std::max(0, std::min(deferredCurrentIndex, toolBox->count() - 1)));
            }
        } else if (QStackedWidget* stackedWidget = qobject_cast<QStackedWidget*>(widget)) {
            if (stackedWidget->count() > 0) {
                stackedWidget->setCurrentIndex(std::max(0, std::min(deferredCurrentIndex, stackedWidget->count() - 1)));
            }
        }
        widget->setProperty("_swDeferredCurrentIndex", QVariant());
    }

    const QObjectList children = widget->children();
    for (QObject* childObject : children) {
        QWidget* childWidget = qobject_cast<QWidget*>(childObject);
        if (!childWidget) {
            continue;
        }
        applyQtDeferredCurrentIndices_(childWidget);
    }
}

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
        } else if (rawName == "leftMargin" || rawName == "topMargin" || rawName == "rightMargin" ||
                   rawName == "bottomMargin") {
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
            QWidget* child = loadQtWidgetNode_(*childWidgetNode, parentWidget, error);
            if (!child) {
                delete layout;
                return false;
            }
            if (QGridLayout* gridLayout = qobject_cast<QGridLayout*>(layout)) {
                const int row = toInt_(item->attr("row", "0"), 0);
                const int column = toInt_(item->attr("column", "0"), 0);
                const int rowSpan = toInt_(item->attr("rowspan", "1"), 1);
                const int columnSpan = toInt_(item->attr("colspan", "1"), 1);
                gridLayout->addWidget(child, row, column, rowSpan, columnSpan);
            } else if (QFormLayout* formLayout = qobject_cast<QFormLayout*>(layout)) {
                const bool hasRow = item->attributes.find("row") != item->attributes.end();
                const bool hasColumn = item->attributes.find("column") != item->attributes.end();
                if (hasRow || hasColumn) {
                    const int row = toInt_(item->attr("row", "0"), 0);
                    const int column = toInt_(item->attr("column", "0"), 0);
                    const QFormLayout::ItemRole role = (column <= 0) ? QFormLayout::LabelRole
                                                                     : (column == 1 ? QFormLayout::FieldRole
                                                                                    : QFormLayout::SpanningRole);
                    formLayout->setWidget(row, role, child);
                } else {
                    formLayout->addRow(child);
                }
            } else {
                layout->addWidget(child);
            }
            continue;
        }

        if (const XmlNode* spacerNode = item->firstChild("spacer")) {
            QSpacerItem* spacer = loadQtSpacer_(*spacerNode);
            if (!spacer) {
                delete layout;
                error = QStringLiteral("Failed to create spacer");
                return false;
            }
            if (QGridLayout* gridLayout = qobject_cast<QGridLayout*>(layout)) {
                const int row = toInt_(item->attr("row", "0"), 0);
                const int column = toInt_(item->attr("column", "0"), 0);
                const int rowSpan = toInt_(item->attr("rowspan", "1"), 1);
                const int columnSpan = toInt_(item->attr("colspan", "1"), 1);
                gridLayout->addItem(spacer, row, column, rowSpan, columnSpan);
            } else if (QFormLayout* formLayout = qobject_cast<QFormLayout*>(layout)) {
                const bool hasRow = item->attributes.find("row") != item->attributes.end();
                const bool hasColumn = item->attributes.find("column") != item->attributes.end();
                if (hasRow || hasColumn) {
                    const int row = toInt_(item->attr("row", "0"), 0);
                    const int column = toInt_(item->attr("column", "0"), 0);
                    const QFormLayout::ItemRole role = (column <= 0) ? QFormLayout::LabelRole
                                                                     : (column == 1 ? QFormLayout::FieldRole
                                                                                    : QFormLayout::SpanningRole);
                    formLayout->setItem(row, role, spacer);
                } else {
                    formLayout->addItem(spacer);
                }
            } else {
                layout->addItem(spacer);
            }
        }
    }

    parentWidget->setLayout(layout);
    return true;
}

QWidget* loadQtWidgetNode_(const XmlNode& widgetNode, QWidget* parent, QString& error) {
    QWidget* widget = createQtWidget_(widgetNode.attr("class"), parent, error);
    if (!widget) {
        return nullptr;
    }

    const SwString objectName = widgetNode.attr("name");
    if (!objectName.isEmpty()) {
        widget->setObjectName(toQString(objectName));
    }

    int deferredCurrentIndex = -1;
    for (const auto* prop : widgetNode.childrenNamed("property")) {
        if (!prop) {
            continue;
        }
        const SwString rawName = prop->attr("name");
        if (rawName == "geometry") {
            continue;
        }
        if (rawName == "currentIndex") {
            if (qobject_cast<QComboBox*>(widget) ||
                qobject_cast<QTabWidget*>(widget) ||
                qobject_cast<QToolBox*>(widget) ||
                qobject_cast<QStackedWidget*>(widget)) {
                deferredCurrentIndex = toInt_(textValue_(*prop), -1);
                continue;
            }
        }
        applyQtProperty_(widget, rawName, *prop);
    }

    applyQtComboItems_(widget, widgetNode);

    if (!applyQtLayout_(widget, widgetNode, error)) {
        delete widget;
        return nullptr;
    }

    if (!widgetNode.firstChild("layout")) {
        for (const auto* childWidgetNode : widgetNode.childrenNamed("widget")) {
            if (!childWidgetNode) {
                continue;
            }
            QWidget* child = loadQtWidgetNode_(*childWidgetNode, widget, error);
            if (!child) {
                delete widget;
                return nullptr;
            }
            if (!attachQtChildToContainer_(widget, child, *childWidgetNode, error)) {
                delete widget;
                return nullptr;
            }
        }
    }

    if (deferredCurrentIndex >= 0) {
        widget->setProperty("_swDeferredCurrentIndex", deferredCurrentIndex);
    }

    return widget;
}

QWidget* loadQtRootWidgetFromUi_(const QString& filePath, QString& error) {
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

    const XmlNode* rootWidgetNode = uiRoot->firstChild("widget");
    if (!rootWidgetNode) {
        error = QStringLiteral("UI has no root widget");
        return nullptr;
    }

    if (rootWidgetNode->attr("class") == "QMainWindow") {
        for (const auto* childWidgetNode : rootWidgetNode->childrenNamed("widget")) {
            if (!childWidgetNode) {
                continue;
            }
            if (childWidgetNode->attr("name") == "centralWidget") {
                QWidget* loaded = loadQtWidgetNode_(*childWidgetNode, nullptr, error);
                applyQtDeferredCurrentIndices_(loaded);
                return loaded;
            }
        }
        error = QStringLiteral("QMainWindow has no centralWidget");
        return nullptr;
    }

    QWidget* loaded = loadQtWidgetNode_(*rootWidgetNode, nullptr, error);
    applyQtDeferredCurrentIndices_(loaded);
    return loaded;
}

struct UiSuiteCase_ {
    QString displayName;
    QString relativeUiPath;
    QString absoluteUiPath;
    QString absoluteQssPath;
    bool embedSwRoot{false};
};

struct LoadedSwUi_ {
    std::unique_ptr<SwWidget> root{};
    std::unique_ptr<SwWidget> owner{};
    SwWidget* effectiveRoot{nullptr};
};

QString example42UiSuiteDirQt_() {
    return QDir::cleanPath(example42SourceDirQt() + QStringLiteral("/ui"));
}

QString relativeUiPathForCase_(const QString& absoluteUiPath) {
    const QDir uiDir(example42UiSuiteDirQt_());
    return QDir::fromNativeSeparators(uiDir.relativeFilePath(QDir::cleanPath(absoluteUiPath)));
}

QString qssPathForUiFile_(const QString& absoluteUiPath) {
    const QString relativeUiPath = relativeUiPathForCase_(absoluteUiPath);
    if (relativeUiPath.startsWith(QStringLiteral("widget_parity/"), Qt::CaseInsensitive)) {
        return example42WidgetParityQssPathQt();
    }
    return example42SharedQssPathQt();
}

QString resolveUiPreviewPath_(const QString& rawName) {
    const QString normalizedName = normalizedWidgetPreviewFileName_(rawName);
    if (normalizedName.isEmpty()) {
        return QString();
    }

    const QFileInfo directInfo(normalizedName);
    if (directInfo.isAbsolute() && directInfo.exists()) {
        return QDir::cleanPath(directInfo.absoluteFilePath());
    }

    const QString underUi = QDir::cleanPath(example42UiSuiteDirQt_() + QLatin1Char('/') + normalizedName);
    if (QFileInfo::exists(underUi)) {
        return underUi;
    }

    const QString targetFileName = QFileInfo(normalizedName).fileName();
    QString firstBasenameMatch;
    QDirIterator it(example42UiSuiteDirQt_(),
                    QStringList() << QStringLiteral("*.ui"),
                    QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString candidate = QDir::cleanPath(it.next());
        const QString relativePath = relativeUiPathForCase_(candidate);
        if (relativePath.compare(normalizedName, Qt::CaseInsensitive) == 0) {
            return candidate;
        }
        if (QFileInfo(candidate).fileName().compare(targetFileName, Qt::CaseInsensitive) == 0 && firstBasenameMatch.isEmpty()) {
            firstBasenameMatch = candidate;
        }
    }

    return firstBasenameMatch;
}

QList<UiSuiteCase_> discoverUiSuiteCases_() {
    QList<UiSuiteCase_> cases;
    QDirIterator it(example42UiSuiteDirQt_(),
                    QStringList() << QStringLiteral("*.ui"),
                    QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString absoluteUiPath = QDir::cleanPath(it.next());
        const QString relativeUiPath = relativeUiPathForCase_(absoluteUiPath);
        const SwString rootClass = uiRootClassFromFile_(absoluteUiPath);
        UiSuiteCase_ testCase;
        testCase.displayName = relativeUiPath;
        testCase.relativeUiPath = relativeUiPath;
        testCase.absoluteUiPath = absoluteUiPath;
        testCase.absoluteQssPath = qssPathForUiFile_(absoluteUiPath);
        testCase.embedSwRoot = (rootClass == "QMainWindow" || rootClass == "SwMainWindow");
        cases.append(testCase);
    }

    std::sort(cases.begin(), cases.end(), [](const UiSuiteCase_& a, const UiSuiteCase_& b) {
        return a.relativeUiPath.compare(b.relativeUiPath, Qt::CaseInsensitive) < 0;
    });
    return cases;
}

QString suiteCaseOutputDir_(const QString& suiteOutputDir, const UiSuiteCase_& testCase) {
    QString relativeStem = testCase.relativeUiPath;
    if (relativeStem.endsWith(QStringLiteral(".ui"), Qt::CaseInsensitive)) {
        relativeStem.chop(3);
    }
    return normalizedSnapshotDir_(suiteOutputDir + relativeStem);
}

bool loadSwUiForParity_(const UiSuiteCase_& testCase, LoadedSwUi_& loaded, QString& error) {
    if (testCase.embedSwRoot) {
        loaded.owner = std::make_unique<SwWidget>();
        const swui::UiLoader::LoadResult result =
            swui::UiLoader::loadFromFile(toSwString(testCase.absoluteUiPath), loaded.owner.get());
        if (!result.ok || !result.root) {
            error = QStringLiteral("Sw load failed: %1").arg(toQString(result.error));
            return false;
        }
        loaded.effectiveRoot = result.root;
        return true;
    }

    const swui::UiLoader::LoadResult result =
        swui::UiLoader::loadFromFile(toSwString(testCase.absoluteUiPath), nullptr);
    if (!result.ok || !result.root) {
        error = QStringLiteral("Sw load failed: %1").arg(toQString(result.error));
        return false;
    }

    loaded.root.reset(result.root);
    loaded.effectiveRoot = loaded.root.get();
    return true;
}

class WidgetParitySwPreviewHost final : public QWidget {
public:
    explicit WidgetParitySwPreviewHost(QWidget* parent = nullptr)
        : QWidget(parent) {
        resizeSyncTimer_.setSingleShot(true);
        resizeSyncTimer_.setTimerType(Qt::PreciseTimer);
        QObject::connect(&resizeSyncTimer_, &QTimer::timeout, this, [this]() {
            flushPendingResizeSync_();
        });
        resizeIdleTimer_.setSingleShot(true);
        resizeIdleTimer_.setTimerType(Qt::PreciseTimer);
        QObject::connect(&resizeIdleTimer_, &QTimer::timeout, this, [this]() {
            setInteractiveResizeActive_(false);
        });
        setAttribute(Qt::WA_NativeWindow, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_OpaquePaintEvent, true);
        setAttribute(Qt::WA_PaintOnScreen, true);
        setAutoFillBackground(false);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
    }

    ~WidgetParitySwPreviewHost() override {
        shutdown();
    }

    bool loadUi(const QString& uiFilePath, const QString& qss, bool stripStyleSheets, QString& error) {
        shutdown();

        const swui::UiLoader::LoadResult loaded = swui::UiLoader::loadFromFile(toSwString(uiFilePath), nullptr);
        if (!loaded.ok || !loaded.root) {
            error = QStringLiteral("Sw load failed: %1").arg(toQString(loaded.error));
            return false;
        }

        root_.reset(loaded.root);
        if (stripStyleSheets) {
            clearSwStyleSheetsRecursive_(root_.get());
        } else if (!qss.isEmpty()) {
            root_->setStyleSheet(toSwString(qss));
        }
        if (root_->layout()) {
            root_->layout()->updateGeometry();
        }
        root_->show();

        hostBinding_.setRootWidget(root_.get());
        syncBridgeToNativeHost_();
        hostBinding_.attach();
        requestResizeSync_(width(), height(), true);
        root_->update();
        updateMinimumSizeCache_();
        updateGeometry();
        return true;
    }

    void shutdown() {
        resizeSyncTimer_.stop();
        resizeIdleTimer_.stop();
        setInteractiveResizeActive_(false);
        hostBinding_.shutdown();
        root_.reset();
        lastCommittedSyncWidth_ = -1;
        lastCommittedSyncHeight_ = -1;
        pendingSyncWidth_ = 1;
        pendingSyncHeight_ = 1;
#if defined(_WIN32)
        cachedHostHwnd_ = nullptr;
#endif
    }

    QSize minimumSizeHint() const override {
        if (root_) {
            const SwSize hint = root_->minimumSizeHint();
            return QSize(std::max(1, hint.width), std::max(1, hint.height));
        }
        return cachedMinimumSizeHint_;
    }

    QSize sizeHint() const override {
        if (root_) {
            const SwSize hint = root_->sizeHint();
            return QSize(std::max(1, hint.width), std::max(1, hint.height));
        }
        return cachedSizeHint_;
    }

    SwWidget* rootWidget() const {
        return root_.get();
    }

    QPaintEngine* paintEngine() const override {
        return nullptr;
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
    }

    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        syncBridgeToNativeHost_();
        hostBinding_.attach();
        requestResizeSync_(width(), height(), true);
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        requestResizeSync_(event ? event->size().width() : width(),
                           event ? event->size().height() : height(),
                           false);
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
        intptr_t nativeResult = 0;
        const bool handled = hostBinding_.handleMessage(msg, &nativeResult);
        if (msg->message == WM_SIZE) {
            noteNativeHostSize_(static_cast<UINT>(msg->wParam),
                                std::max(1, static_cast<int>(LOWORD(msg->lParam))),
                                std::max(1, static_cast<int>(HIWORD(msg->lParam))));
        }
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

private:
    static constexpr int kResizeSyncIntervalMs_ = 4;
    static constexpr int kResizeIdleReleaseMs_ = 90;

    void updateMinimumSizeCache_() {
        if (!root_) {
            cachedMinimumSizeHint_ = QSize(320, 240);
            cachedSizeHint_ = QSize(320, 240);
            return;
        }

        const SwSize minSize = root_->minimumSizeHint();
        const SwSize prefSize = root_->sizeHint();
        cachedMinimumSizeHint_ = QSize(std::max(1, minSize.width), std::max(1, minSize.height));
        cachedSizeHint_ = QSize(std::max(cachedMinimumSizeHint_.width(), prefSize.width),
                                std::max(cachedMinimumSizeHint_.height(), prefSize.height));
        setMinimumSize(cachedMinimumSizeHint_);
    }

#if defined(_WIN32)
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
        if (!cachedHostHwnd_) {
            cachedHostHwnd_ = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(winId()));
        }
    }

    HWND hostHwnd_() const {
        return cachedHostHwnd_;
    }

    void noteNativeHostSize_(UINT sizeType, int width, int height) {
        if (sizeType == SIZE_MINIMIZED) {
            return;
        }
        requestResizeSync_(width, height, true);
    }
#else
    void syncBridgeToNativeHost_() {
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

    std::unique_ptr<SwWidget> root_{};
    SwQtBindingWin32WidgetHost hostBinding_{};
    QTimer resizeSyncTimer_{};
    QTimer resizeIdleTimer_{};
    QElapsedTimer lastResizeSync_{};
    int pendingSyncWidth_{1};
    int pendingSyncHeight_{1};
    int lastCommittedSyncWidth_{-1};
    int lastCommittedSyncHeight_{-1};
    bool damageThrottleSuppressed_{false};
    QSize cachedMinimumSizeHint_{320, 240};
    QSize cachedSizeHint_{320, 240};
#if defined(_WIN32)
    HWND cachedHostHwnd_{nullptr};
#endif
};

int runWidgetParityPreview_(const QString& rawFileName, QApplication& qtApp, bool stripStyleSheets) {
    const QString uiPath = resolveUiPreviewPath_(rawFileName);
    if (uiPath.isEmpty()) {
        return 1;
    }

    const QString sharedQss = stripStyleSheets ? QString() : loadTextFile_(qssPathForUiFile_(uiPath));
    if (!stripStyleSheets && sharedQss.isEmpty()) {
        return 1;
    }

    QString qtError;
    std::unique_ptr<QWidget> qtRoot(loadQtRootWidgetFromUi_(uiPath, qtError));
    if (!qtRoot) {
        return 1;
    }

    auto swApp = std::make_unique<ScopedSwGuiApplication>();
    SwQtBindingEventPump swPump(swApp.get());

    QMainWindow window;
    const QString previewModeLabel = stripStyleSheets ? QStringLiteral(" [no stylesheet]") : QString();
    window.setWindowTitle(QStringLiteral("Qt/Sw Widget Preview - %1%2")
                              .arg(relativeUiPathForCase_(uiPath), previewModeLabel));

    QSplitter* splitter = new QSplitter(Qt::Horizontal, &window);
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);

    qtRoot->setParent(splitter);
    if (stripStyleSheets) {
        clearQtStyleSheetsRecursive_(qtRoot.get());
    } else {
        qtRoot->setStyleSheet(sharedQss);
    }
    qtRoot->ensurePolished();
    if (qtRoot->layout()) {
        qtRoot->layout()->activate();
    }

    WidgetParitySwPreviewHost* swHost = new WidgetParitySwPreviewHost(splitter);
    QString swError;
    if (!swHost->loadUi(uiPath, sharedQss, stripStyleSheets, swError)) {
        delete swHost;
        return 1;
    }

    splitter->addWidget(qtRoot.release());
    splitter->addWidget(swHost);
    window.setCentralWidget(splitter);

    const QSize qtMin = splitter->widget(0)->minimumSizeHint();
    const QSize swMin = swHost->minimumSizeHint();
    const QSize qtPref = splitter->widget(0)->sizeHint();
    const QSize swPref = swHost->sizeHint();
    const int paneWidth = std::max(std::max(qtMin.width(), swMin.width()), std::max(qtPref.width(), swPref.width()));
    const int paneHeight = std::max(std::max(qtMin.height(), swMin.height()), std::max(qtPref.height(), swPref.height()));

    splitter->setSizes(QList<int>() << paneWidth << paneWidth);
    const QSize targetCentralSize((paneWidth * 2) + splitter->handleWidth(), paneHeight);
    window.resize(targetCentralSize);
    window.show();
    QCoreApplication::processEvents();
    const QSize frameDelta = window.frameGeometry().size() - window.geometry().size();
    const QSize minimumOuterSize(qtMin.width() + swMin.width() + frameDelta.width(),
                                 std::max(qtMin.height(), swMin.height()) + frameDelta.height());
    const QSize preferredOuterSize(targetCentralSize.width() + frameDelta.width(),
                                   targetCentralSize.height() + frameDelta.height());
    window.setMinimumSize(minimumOuterSize);
    window.resize(preferredOuterSize.expandedTo(minimumOuterSize));
    QCoreApplication::processEvents();

    const QString previewDumpDir = normalizedSnapshotDir_(QStringLiteral("D:/GitHub/SwStack/visual_validation/widget_preview"));
    if (ensureSnapshotDir_(previewDumpDir)) {
        saveTextFile_(previewDumpDir + QStringLiteral("qt_tree.txt"), dumpQtWidgetTree_(splitter->widget(0)));
        if (swHost->rootWidget()) {
            saveTextFile_(previewDumpDir + QStringLiteral("sw_tree.txt"), dumpSwWidgetTree_(swHost->rootWidget()));
        }
    }

    AdaptiveSwPumpTimer_ swPumpTimer(&window, &swPump);
    swPumpTimer.start();

    QObject::connect(&qtApp, &QCoreApplication::aboutToQuit, &window, [&swPumpTimer, swHost]() {
        swPumpTimer.stop();
        if (swHost) {
            swHost->shutdown();
        }
    });

    const int exitCode = qtApp.exec();
    swPumpTimer.stop();
    if (swHost) {
        swHost->shutdown();
    }
    return exitCode;
}

int runWidgetParitySuite_(const QString& suiteOutputDir, SwQtBindingEventPump& swPump) {
    bool allOk = ensureSnapshotDir_(suiteOutputDir);
    bool allExact = allOk;
    QStringList summaryLines;
    summaryLines << QStringLiteral("UI parity suite")
                 << QStringLiteral("UI root: %1").arg(example42UiSuiteDirQt_())
                 << QString();

    const QList<UiSuiteCase_> cases = discoverUiSuiteCases_();
    if (cases.isEmpty()) {
        allOk = false;
        summaryLines << QStringLiteral("No .ui files found under %1").arg(example42UiSuiteDirQt_());
    }

    for (const UiSuiteCase_& testCase : cases) {
        const QString caseDir = suiteCaseOutputDir_(suiteOutputDir, testCase);
        bool caseOk = ensureSnapshotDir_(caseDir);
        SnapshotDiffSummary diff{false, 0, 0};
        QSize qtSize;
        QSize swSize;
        QString errorText;
        const QString sharedQss = loadTextFile_(testCase.absoluteQssPath);
        if (sharedQss.isEmpty()) {
            caseOk = false;
            errorText = QStringLiteral("Missing or empty QSS: %1").arg(testCase.absoluteQssPath);
        }

        std::unique_ptr<QWidget> qtRoot;
        QString qtError;
        if (caseOk) {
            qtRoot.reset(loadQtRootWidgetFromUi_(testCase.absoluteUiPath, qtError));
        }
        if (caseOk && !qtRoot) {
            caseOk = false;
            errorText = QStringLiteral("Qt load failed: %1").arg(qtError);
        }

        LoadedSwUi_ swLoaded;
        if (caseOk && !loadSwUiForParity_(testCase, swLoaded, errorText)) {
            caseOk = false;
        }

        if (caseOk) {
            qtRoot->setStyleSheet(sharedQss);
            const QSize targetSize = qtRoot->minimumSize().isValid() && !qtRoot->minimumSize().isEmpty()
                                         ? qtRoot->minimumSize()
                                         : qtRoot->sizeHint();
            qtRoot->setFixedSize(targetSize);
            qtRoot->ensurePolished();
            if (qtRoot->layout()) {
                qtRoot->layout()->activate();
            }
            QCoreApplication::processEvents();
            qtSize = qtRoot->size();
            caseOk = saveTextFile_(caseDir + QStringLiteral("qt_tree.txt"), dumpQtWidgetTree_(qtRoot.get())) && caseOk;

            swLoaded.effectiveRoot->setStyleSheet(toSwString(sharedQss));
            swLoaded.effectiveRoot->resize(qtSize.width(), qtSize.height());
            if (swLoaded.effectiveRoot->layout()) {
                swLoaded.effectiveRoot->layout()->updateGeometry();
            }
            swLoaded.effectiveRoot->show();
            swPump.drainPostedWork(256, true);
            QCoreApplication::processEvents();
            swPump.drainPostedWork(256, true);
            swSize = QSize(swLoaded.effectiveRoot->rect().width, swLoaded.effectiveRoot->rect().height);
            caseOk = saveTextFile_(caseDir + QStringLiteral("sw_tree.txt"), dumpSwWidgetTree_(swLoaded.effectiveRoot)) && caseOk;

            caseOk = saveWidgetSnapshot_(qtRoot.get(), caseDir + QStringLiteral("qt.png")) && caseOk;
            caseOk = SwWidgetSnapshot::savePng(swLoaded.effectiveRoot, toSwString(caseDir + QStringLiteral("sw.png"))) && caseOk;
            diff = saveDiffImage_(caseDir + QStringLiteral("qt.png"),
                                  caseDir + QStringLiteral("sw.png"),
                                  caseDir + QStringLiteral("diff.png"));
            if (!diff.comparable) {
                caseOk = false;
                if (errorText.isEmpty()) {
                    errorText = QStringLiteral("Diff images are not comparable");
                }
            }
        }

        const bool exact = caseOk && qtSize == swSize && diff.comparable && diff.differingPixels == 0;
        allOk = allOk && caseOk;
        allExact = allExact && exact;

        if (caseOk) {
            summaryLines << QStringLiteral("%1 | qss=%2 | qt=%3x%4 | sw=%5x%6 | diff=%7/%8 | %9")
                                .arg(testCase.displayName)
                                .arg(QFileInfo(testCase.absoluteQssPath).fileName())
                                .arg(qtSize.width())
                                .arg(qtSize.height())
                                .arg(swSize.width())
                                .arg(swSize.height())
                                .arg(diff.differingPixels)
                                .arg(diff.totalPixels)
                                .arg(exact ? QStringLiteral("exact") : QStringLiteral("different"));
        } else {
            summaryLines << QStringLiteral("%1 | error | %2").arg(testCase.displayName, errorText);
        }

    }

    summaryLines << QString()
                 << QStringLiteral("Result: %1")
                        .arg(!allOk ? QStringLiteral("error")
                                    : (allExact ? QStringLiteral("exact") : QStringLiteral("differences")));
    const QString summaryText = summaryLines.join(QLatin1Char('\n'));
    saveTextFile_(suiteOutputDir + QStringLiteral("ui_parity_summary.txt"), summaryText);
    saveTextFile_(suiteOutputDir + QStringLiteral("widget_parity_summary.txt"), summaryText);

    if (!allOk) {
        return 1;
    }
    return allExact ? 0 : 2;
}

} // namespace

int main(int argc, char* argv[]) {
    example42Trace("main: enter");
    QApplication qtApp(argc, argv);
    example42Trace("main: qapp created");
    const QString snapshotDir = normalizedSnapshotDir_(snapshotOutputDirFromArgs_(argc, argv));
    const bool snapshotMode = !snapshotDir.isEmpty();
    const QString widgetSuiteDir = normalizedSnapshotDir_(widgetSuiteOutputDirFromArgs_(argc, argv));
    const bool widgetSuiteMode = !widgetSuiteDir.isEmpty();
    const QString widgetPreviewFile = normalizedWidgetPreviewFileName_(widgetPreviewFileFromArgs_(argc, argv));
    const bool widgetPreviewMode = !widgetPreviewFile.isEmpty();
    const bool noStyleSheetMode = noStyleSheetFromArgs_(argc, argv);

    int exitCode = 0;
    std::unique_ptr<ScopedSwGuiApplication> swApp;

    if (widgetPreviewMode) {
        return runWidgetParityPreview_(widgetPreviewFile, qtApp, noStyleSheetMode);
    }

    if (widgetSuiteMode) {
        swApp = std::make_unique<ScopedSwGuiApplication>();
        SwQtBindingEventPump swPump(swApp.get());
        exitCode = runWidgetParitySuite_(widgetSuiteDir, swPump);
        swApp.reset();
        return exitCode;
    }

    {
        example42Trace("main: before window");
        QMainWindow window;
        window.setWindowTitle(QStringLiteral("Qt + Sw Embedded Interop"));
        example42Trace("main: window created");

        QSplitter* splitter = new QSplitter(Qt::Horizontal, &window);
        example42Trace("main: splitter created");
        QtStudioPane* qtPane = new QtStudioPane(splitter);
        example42Trace("main: qt pane created");
        QtSwHostWidget* swHost = new QtSwHostWidget(splitter);
        example42Trace("main: sw host widget created");
        const int initialPaneWidth = 587;

        splitter->addWidget(qtPane);
        splitter->addWidget(swHost);
        splitter->setChildrenCollapsible(false);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 1);
        splitter->setSizes(QList<int>() << initialPaneWidth << initialPaneWidth);
        window.setCentralWidget(splitter);
        window.resize((initialPaneWidth * 2) + splitter->handleWidth(), 700);
        window.setMinimumSize(qtPane->minimumSizeHint().width() + swHost->minimumSizeHint().width(),
                              std::max(qtPane->minimumSizeHint().height(), swHost->minimumSizeHint().height()));

        window.show();
        QCoreApplication::processEvents();
        example42Trace("main: window shown");

        swApp = std::make_unique<ScopedSwGuiApplication>();
        example42Trace("main: sw app created");

        QPointer<QtStudioPane> safeQtPane = qtPane;
        QPointer<QtSwHostWidget> safeSwHost = swHost;
        ExampleSwThreadFiberBridge runtimeWorker([safeQtPane, safeSwHost](const SwString& text) {
            const QString statusText = toQString(text);
            if (safeQtPane) {
                QMetaObject::invokeMethod(safeQtPane, [safeQtPane, statusText]() {
                    if (safeQtPane) {
                        safeQtPane->setRuntimeStatusText(statusText);
                    }
                }, Qt::QueuedConnection);
            }
            if (safeSwHost) {
                QMetaObject::invokeMethod(safeSwHost, [safeSwHost, statusText]() {
                    if (safeSwHost) {
                        safeSwHost->setRuntimeStatusText(statusText);
                    }
                }, Qt::QueuedConnection);
            }
        });

        swHost->initializeSw(
            [safeQtPane](const SwString& text) {
                if (!safeQtPane) {
                    return;
                }

                safeQtPane->setStatusText(QStringLiteral("Sw -> Qt: ") + toQString(text));
            },
            [&runtimeWorker](const SwString& text) {
                runtimeWorker.requestFiberRoundTrip("Sw", text);
            });
        example42Trace("main: sw initialized");

        if (runtimeWorker.start()) {
            qtPane->setRuntimeStatusText(QStringLiteral("SwThread ready | heartbeat active"));
            swHost->setRuntimeStatusText(QStringLiteral("SwThread ready | heartbeat active"));
        } else {
            qtPane->setRuntimeStatusText(QStringLiteral("SwThread failed to start"));
            swHost->setRuntimeStatusText(QStringLiteral("SwThread failed to start"));
        }

        SwQtBindingEventPump swPump(swApp.get());
        AdaptiveSwPumpTimer_ swPumpTimer(&window, &swPump);

        QObject::connect(qtPane->bridgeButton(), &QPushButton::clicked, &window, [qtPane, swHost]() {
            const QString typed = qtPane->messageText();
            const QString message = typed.isEmpty() ? QStringLiteral("hello from Qt") : typed;
            swHost->showIncomingMessage(QStringLiteral("Qt -> Sw: ") + message);
            qtPane->setStatusText(QStringLiteral("Qt -> Sw: ") + message);
        });

        QObject::connect(qtPane->fiberButton(), &QPushButton::clicked, &window, [&runtimeWorker, qtPane]() {
            const QString typed = qtPane->messageText();
            const SwString payload = toSwString(typed.isEmpty() ? QStringLiteral("hello from Qt") : typed);
            runtimeWorker.requestFiberRoundTrip("Qt", payload);
            qtPane->setStatusText(QStringLiteral("Qt -> SwThread: ") + toQString(payload));
        });

        bool shutdownRequested = false;
        const auto shutdownExample = [&shutdownRequested, &runtimeWorker, &swPumpTimer, swHost]() {
            if (shutdownRequested) {
                return;
            }

            shutdownRequested = true;
            runtimeWorker.shutdown();
            swPumpTimer.stop();
            swHost->shutdownSw();
        };

        QObject::connect(&qtApp, &QCoreApplication::aboutToQuit, &window, shutdownExample);

        swPumpTimer.start();
        if (snapshotMode) {
            QTimer::singleShot(180, &window, [&]() {
                bool ok = ensureSnapshotDir_(snapshotDir);

                swPumpTimer.drainNow(256, true);
                QCoreApplication::processEvents();
                swPumpTimer.drainNow(256, true);
                QCoreApplication::processEvents();

                const QRect windowRect(0, 0, window.width(), window.height());
                const QRect centralRect = window.centralWidget() ? window.centralWidget()->geometry() : windowRect;
                const QRect qtPaneRect(qtPane->mapTo(&window, QPoint(0, 0)), qtPane->size());
                const QRect swHostRect(swHost->mapTo(&window, QPoint(0, 0)), swHost->size());

                ok = saveWindowRegionSnapshot_(&window, windowRect, snapshotDir + QStringLiteral("qt_sw_interop_window.png")) && ok;
                ok = saveWindowRegionSnapshot_(&window, centralRect, snapshotDir + QStringLiteral("qt_sw_interop_central.png")) && ok;
                ok = saveWindowRegionSnapshot_(&window, qtPaneRect, snapshotDir + QStringLiteral("qt_sw_interop_qt_pane.png")) && ok;
                ok = saveWindowRegionSnapshot_(&window, swHostRect, snapshotDir + QStringLiteral("qt_sw_interop_sw_host.png")) && ok;
                ok = swHost->saveSwRootSnapshot(snapshotDir + QStringLiteral("qt_sw_interop_sw_root.png")) && ok;
                ok = saveTextFile_(snapshotDir + QStringLiteral("qt_sw_interop_qt_geometry.txt"), qtPane->debugGeometryReport()) && ok;
                ok = saveTextFile_(snapshotDir + QStringLiteral("qt_sw_interop_sw_geometry.txt"), swHost->debugGeometryReport()) && ok;

                const SnapshotDiffSummary diff = saveDiffImage_(snapshotDir + QStringLiteral("qt_sw_interop_qt_pane.png"),
                                                               snapshotDir + QStringLiteral("qt_sw_interop_sw_host.png"),
                                                               snapshotDir + QStringLiteral("qt_sw_interop_qt_vs_sw_diff.png"));
                const bool exactParity = diff.comparable && diff.differingPixels == 0;

                shutdownExample();
                exitCode = ok ? (exactParity ? 0 : 2) : 1;
                qtApp.exit(exitCode);
            });
        }
        exitCode = qtApp.exec();
        shutdownExample();
    }

    swApp.reset();
    return exitCode;
}
