#include <memory>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QList>
#include <QMainWindow>
#include <QMetaObject>
#include <QPointer>
#include <QScreen>
#include <QPushButton>
#include <QPixmap>
#include <QRect>
#include <QSplitter>
#include <QTimer>
#include <QWindow>

#include "SwGuiApplication.h"
#include "gui/qtbinding/SwQtBindingEventPump.h"
#include "demo/Example42SketchSupport.h"
#include "demo/ExampleSwThreadFiberBridge.h"
#include "ui/QtSwHostWidget.h"
#include "ui/QtStudioPane.h"

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

QString snapshotOutputDirFromArgs_(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (argv[i] && QString::fromLocal8Bit(argv[i]) == QStringLiteral("--snapshot")) {
            return QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[i + 1]));
        }
    }
    return QString();
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

} // namespace

int main(int argc, char* argv[]) {
    QApplication qtApp(argc, argv);
    const QString snapshotDir = normalizedSnapshotDir_(snapshotOutputDirFromArgs_(argc, argv));
    const bool snapshotMode = !snapshotDir.isEmpty();

    int exitCode = 0;
    std::unique_ptr<ScopedSwGuiApplication> swApp;

    {
        QMainWindow window;
        window.setWindowTitle(QStringLiteral("Qt + Sw Embedded Interop"));

        QSplitter* splitter = new QSplitter(Qt::Horizontal, &window);
        QtStudioPane* qtPane = new QtStudioPane(splitter);
        QtSwHostWidget* swHost = new QtSwHostWidget(splitter);
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

        swApp = std::make_unique<ScopedSwGuiApplication>();

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

                safeQtPane->setStatusText(QStringLiteral("Sw -> Qt: %1").arg(toQString(text)));
            },
            [&runtimeWorker](const SwString& text) {
                runtimeWorker.requestFiberRoundTrip("Sw", text);
            });

        if (runtimeWorker.start()) {
            qtPane->setRuntimeStatusText(QStringLiteral("SwThread ready | heartbeat active"));
            swHost->setRuntimeStatusText(QStringLiteral("SwThread ready | heartbeat active"));
        } else {
            qtPane->setRuntimeStatusText(QStringLiteral("SwThread failed to start"));
            swHost->setRuntimeStatusText(QStringLiteral("SwThread failed to start"));
        }

        QTimer swPumpTimer;
        SwQtBindingEventPump swPump(swApp.get());
        swPumpTimer.setTimerType(Qt::PreciseTimer);
        swPumpTimer.setInterval(4);
        QObject::connect(&swPumpTimer, &QTimer::timeout, &window, [&swPump]() {
            swPump.drainPostedWork(64, true);
        });

        QObject::connect(qtPane->bridgeButton(), &QPushButton::clicked, &window, [qtPane, swHost]() {
            const QString typed = qtPane->messageText();
            const QString message = typed.isEmpty() ? QStringLiteral("hello from Qt") : typed;
            swHost->showIncomingMessage(QStringLiteral("Qt -> Sw: %1").arg(message));
            qtPane->setStatusText(QStringLiteral("Qt -> Sw: %1").arg(message));
        });

        QObject::connect(qtPane->fiberButton(), &QPushButton::clicked, &window, [&runtimeWorker, qtPane]() {
            const QString typed = qtPane->messageText();
            const SwString payload = toSwString(typed.isEmpty() ? QStringLiteral("hello from Qt") : typed);
            runtimeWorker.requestFiberRoundTrip("Qt", payload);
            qtPane->setStatusText(QStringLiteral("Qt -> SwThread: %1").arg(toQString(payload)));
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

                swPump.drainPostedWork(256, true);
                QCoreApplication::processEvents();
                swPump.drainPostedWork(256, true);
                QCoreApplication::processEvents();

                const QRect windowRect(0, 0, window.width(), window.height());
                const QRect centralRect = window.centralWidget() ? window.centralWidget()->geometry() : windowRect;
                const QRect qtPaneRect(qtPane->mapTo(&window, QPoint(0, 0)), qtPane->size());
                const QRect swHostRect(swHost->mapTo(&window, QPoint(0, 0)), swHost->size());

                ok = ok && saveWindowRegionSnapshot_(&window, windowRect, snapshotDir + QStringLiteral("qt_sw_interop_window.png"));
                ok = ok && saveWindowRegionSnapshot_(&window, centralRect, snapshotDir + QStringLiteral("qt_sw_interop_central.png"));
                ok = ok && saveWindowRegionSnapshot_(&window, qtPaneRect, snapshotDir + QStringLiteral("qt_sw_interop_qt_pane.png"));
                ok = ok && saveWindowRegionSnapshot_(&window, swHostRect, snapshotDir + QStringLiteral("qt_sw_interop_sw_host.png"));
                ok = ok && swHost->saveSwRootSnapshot(snapshotDir + QStringLiteral("qt_sw_interop_sw_root.png"));

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
