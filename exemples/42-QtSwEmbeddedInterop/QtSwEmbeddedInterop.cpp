#include <memory>

#include <QApplication>
#include <QCoreApplication>
#include <QList>
#include <QMainWindow>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>

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
        QtSwHostWidget* swHost = new QtSwHostWidget(splitter);

        splitter->addWidget(qtPane);
        splitter->addWidget(swHost);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 1);
        splitter->setSizes(QList<int>() << 590 << 590);
        window.setCentralWidget(splitter);

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
        exitCode = qtApp.exec();
        shutdownExample();
    }

    swApp.reset();
    return exitCode;
}
