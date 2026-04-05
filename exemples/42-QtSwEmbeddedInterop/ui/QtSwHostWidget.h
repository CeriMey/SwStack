#pragma once

#include <functional>
#include <memory>

#include <QtGlobal>
#include <QString>
#include <QWidget>

#include "SwString.h"

class QByteArray;
class QMouseEvent;
class QPaintEngine;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;

class QtSwHostWidget final : public QWidget {
public:
    using MessageSink = std::function<void(const SwString&)>;

    explicit QtSwHostWidget(QWidget* parent = nullptr);
    ~QtSwHostWidget() override;

    void initializeSw(MessageSink onSendToQt, MessageSink onWorkerFiberRequested);
    void shutdownSw();
    void showIncomingMessage(const QString& text);
    void setRuntimeStatusText(const QString& text);
    bool saveSwRootSnapshot(const QString& filePath) const;
    QString debugGeometryReport() const;
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

    QPaintEngine* paintEngine() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#else
    bool nativeEvent(const QByteArray& eventType, void* message, long* result) override;
#endif

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
