#pragma once

#include <memory>

#include <QString>
#include <QWidget>

class QByteArray;
class QPaintEngine;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;

class SwQtVideoPlayerWidget final : public QWidget {
public:
    explicit SwQtVideoPlayerWidget(QWidget* parent = nullptr);
    ~SwQtVideoPlayerWidget() override;

    bool openUrl(const QString& url);
    void play();
    void stop();
    void setAutoPlay(bool enabled);
    QString currentUrl() const;

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;
    QPaintEngine* paintEngine() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#else
    bool nativeEvent(const QByteArray& eventType, void* message, long* result) override;
#endif

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
