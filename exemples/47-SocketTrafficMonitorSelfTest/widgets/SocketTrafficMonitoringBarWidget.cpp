#include "SocketTrafficMonitoringBarWidget.h"

namespace {

class SocketTrafficStatusLedWidget : public SwWidget {
    SW_OBJECT(SocketTrafficStatusLedWidget, SwWidget)

public:
    explicit SocketTrafficStatusLedWidget(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
    }

    void setActive(bool active) {
        if (active_ == active) {
            return;
        }
        active_ = active;
        update();
    }

protected:
    void paintEvent(PaintEvent* event) override {
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect bounds = rect();
        const int diameter = std::max(8, std::min(bounds.width, bounds.height) - 2);
        const int outer = diameter;
        const int middle = std::max(6, diameter - 4);
        const int inner = std::max(3, diameter - 8);
        const int cx = bounds.x + (bounds.width / 2);
        const int cy = bounds.y + (bounds.height / 2) - 1;

        const SwColor outerFill = active_ ? SwColor{25, 78, 59} : SwColor{56, 56, 60};
        const SwColor outerBorder = active_ ? SwColor{45, 137, 92} : SwColor{88, 88, 94};
        const SwColor middleFill = active_ ? SwColor{45, 212, 149} : SwColor{98, 98, 104};
        const SwColor middleBorder = active_ ? SwColor{110, 231, 183} : SwColor{112, 112, 118};
        const SwColor innerFill = active_ ? SwColor{209, 250, 229} : SwColor{152, 152, 160};

        painter->fillEllipse(SwRect{cx - outer / 2, cy - outer / 2, outer, outer}, outerFill, outerBorder, 1);
        painter->fillEllipse(SwRect{cx - middle / 2, cy - middle / 2, middle, middle}, middleFill, middleBorder, 1);
        painter->fillEllipse(SwRect{cx - inner / 2 - 1, cy - inner / 2 - 1, inner, inner}, innerFill, innerFill, 0);
    }

private:
    bool active_{false};
};

} // namespace

SocketTrafficMonitoringBarWidget::SocketTrafficMonitoringBarWidget(SwWidget* parent)
    : SwFrame(parent) {
    setStyleSheet(R"(
        SocketTrafficMonitoringBarWidget {
            background-color: rgba(0,0,0,0);
            border-width: 0px;
        }
    )");

    statusLed_ = new SocketTrafficStatusLedWidget(this);
    statusLed_->resize(14, 14);

    SwLabel* monitorLabel = new SwLabel("Monitor", this);
    monitorLabel->resize(52, 16);
    monitorLabel->setAlignment(
        DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine));
    monitorLabel->setStyleSheet(
        "SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(156, 156, 156); font-size: 12px; }");

    socketLabel_ = new SwLabel("0 sockets", this);
    socketLabel_->resize(88, 16);
    socketLabel_->setAlignment(
        DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine));
    socketLabel_->setStyleSheet(
        "SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(156, 156, 156); font-size: 12px; }");

    SwHorizontalLayout* layout = new SwHorizontalLayout();
    layout->setMargin(2);
    layout->setSpacing(6);
    layout->addStretch(1);
    layout->addWidget(statusLed_, 0, 14);
    layout->addWidget(monitorLabel, 0, 52);
    layout->addWidget(socketLabel_, 0, 88);
    setLayout(layout);

    updateVisualState_();
}

SwSize SocketTrafficMonitoringBarWidget::minimumSizeHint() const {
    return SwSize{180, 22};
}

void SocketTrafficMonitoringBarWidget::setMonitoringEnabled(bool enabled) {
    monitoringEnabled_ = enabled;
    updateVisualState_();
}

void SocketTrafficMonitoringBarWidget::setSampleIntervalUs(long long intervalUs) {
    sampleIntervalUs_ = intervalUs;
    updateVisualState_();
}

void SocketTrafficMonitoringBarWidget::setOpenSocketCount(unsigned long long socketCount) {
    openSocketCount_ = socketCount;
    updateVisualState_();
}

void SocketTrafficMonitoringBarWidget::updateVisualState_() {
    if (statusLed_) {
        SocketTrafficStatusLedWidget* led = dynamic_cast<SocketTrafficStatusLedWidget*>(statusLed_);
        if (led) {
            led->setActive(monitoringEnabled_);
        }
    }
    if (socketLabel_) {
        socketLabel_->setText(socketLabelText_(openSocketCount_));
        socketLabel_->setStyleSheet(monitoringEnabled_
                                        ? "SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(196, 196, 196); font-size: 12px; padding-top: 1px; }"
                                        : "SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(110, 110, 110); font-size: 12px; padding-top: 1px; }");
    }
}

SwString SocketTrafficMonitoringBarWidget::socketLabelText_(unsigned long long socketCount) {
    if (socketCount > 999ULL) {
        return "999+ sockets";
    }
    return SwString::number(socketCount) + " sockets";
}
