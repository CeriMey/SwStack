#include <cstdio>
#include <exception>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QEventLoop>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QShortcut>
#include <QSizePolicy>
#include <QStyle>
#include <QToolBar>
#include <QTimer>
#include <QUrl>

#include "SwQtVideoPlayerWidget.h"

class VideoPlayerWindow final : public QMainWindow {
public:
    explicit VideoPlayerWindow(QWidget* parent = nullptr)
        : QMainWindow(parent) {}

    void setVideoWidget(SwQtVideoPlayerWidget* video) {
        video_ = video;
        setCentralWidget(video_);
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        if (video_) {
            video_->stop();
        }
        event->accept();
        QCoreApplication::quit();
    }

private:
    SwQtVideoPlayerWidget* video_{nullptr};
};

int main(int argc, char** argv) {
    try {
        QApplication app(argc, argv);
        QApplication::setApplicationName(QStringLiteral("Qt Static Sw Video Player"));

        QString initialUrl = QStringLiteral("rtsp://172.16.40.80:5004/video");
        int autoCloseMs = 0;
        for (int i = 1; i < argc; ++i) {
            const QString arg = QString::fromLocal8Bit(argv[i]);
            if (arg.startsWith(QStringLiteral("--auto-close-ms="))) {
                bool ok = false;
                const int value = arg.mid(QStringLiteral("--auto-close-ms=").size()).toInt(&ok);
                if (ok && value > 0) {
                    autoCloseMs = value;
                }
                continue;
            }
            if (arg == QStringLiteral("--auto-close-ms") && i + 1 < argc) {
                bool ok = false;
                const int value = QString::fromLocal8Bit(argv[++i]).toInt(&ok);
                if (ok && value > 0) {
                    autoCloseMs = value;
                }
                continue;
            }
            initialUrl = arg;
        }

        VideoPlayerWindow window;
        window.setWindowTitle(QStringLiteral("Qt + SwStack Static Video Widget"));
        window.resize(1120, 720);

        SwQtVideoPlayerWidget* video = new SwQtVideoPlayerWidget(&window);
        window.setVideoWidget(video);

        QToolBar* toolbar = new QToolBar(&window);
        toolbar->setMovable(false);
        window.addToolBar(toolbar);

        QLabel* sourceLabel = new QLabel(QStringLiteral("URL:"), toolbar);
        toolbar->addWidget(sourceLabel);

        QLineEdit* sourceEdit = new QLineEdit(initialUrl, toolbar);
        sourceEdit->setClearButtonEnabled(true);
        sourceEdit->setMinimumWidth(420);
        sourceEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        toolbar->addWidget(sourceEdit);

        QAction* openAction = toolbar->addAction(
            window.style()->standardIcon(QStyle::SP_MediaPlay),
            QStringLiteral("Open"));
        QAction* stopAction = toolbar->addAction(
            window.style()->standardIcon(QStyle::SP_MediaStop),
            QStringLiteral("Stop"));
        QAction* reconnectAction = toolbar->addAction(
            window.style()->standardIcon(QStyle::SP_BrowserReload),
            QStringLiteral("Reconnect"));
        reconnectAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
        reconnectAction->setShortcutContext(Qt::ApplicationShortcut);

        const auto openCurrentUrl = [video, sourceEdit]() {
            const QString url = sourceEdit->text().trimmed();
            if (url.isEmpty()) {
                return;
            }
            sourceEdit->setText(url);
            video->openUrl(url);
        };

        QObject::connect(openAction, &QAction::triggered, video, openCurrentUrl);
        QObject::connect(sourceEdit, &QLineEdit::returnPressed, video, openCurrentUrl);
        QObject::connect(reconnectAction, &QAction::triggered, video, openCurrentUrl);
        QObject::connect(stopAction, &QAction::triggered, video, [video]() {
            video->stop();
        });

        window.show();
        openCurrentUrl();
        if (autoCloseMs > 0) {
            QTimer::singleShot(autoCloseMs, &window, [video]() {
                video->stop();
                QCoreApplication::quit();
            });
        }
        return app.exec();
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "QtStaticVideoPlayer error: %s\n", ex.what());
    } catch (...) {
        std::fprintf(stderr, "QtStaticVideoPlayer error: unknown exception\n");
    }
    return 1;
}
