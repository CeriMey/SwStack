#pragma once

#include <memory>

#include <QString>
#include <QWidget>

class QPaintEvent;
class QPushButton;
class QResizeEvent;

class QtStudioPane final : public QWidget {
public:
    explicit QtStudioPane(QWidget* parent = nullptr);
    ~QtStudioPane() override;

    QPushButton* bridgeButton() const;
    QPushButton* fiberButton() const;

    QString messageText() const;
    void setStatusText(const QString& text);
    void setRuntimeStatusText(const QString& text);
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
