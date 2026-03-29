#include "RuntimeProfilerMonitoringBarWidget.h"

namespace {

class RuntimeProfilerStatusLedWidget : public SwWidget {
    SW_OBJECT(RuntimeProfilerStatusLedWidget, SwWidget)

public:
    explicit RuntimeProfilerStatusLedWidget(SwWidget* parent = nullptr)
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

        const SwColor outerFill = active_ ? SwColor{78, 51, 24} : SwColor{56, 56, 60};
        const SwColor outerBorder = active_ ? SwColor{154, 98, 38} : SwColor{88, 88, 94};
        const SwColor middleFill = active_ ? SwColor{255, 159, 67} : SwColor{98, 98, 104};
        const SwColor middleBorder = active_ ? SwColor{255, 191, 120} : SwColor{112, 112, 118};
        const SwColor innerFill = active_ ? SwColor{255, 221, 181} : SwColor{152, 152, 160};

        painter->fillEllipse(SwRect{cx - outer / 2, cy - outer / 2, outer, outer}, outerFill, outerBorder, 1);
        painter->fillEllipse(SwRect{cx - middle / 2, cy - middle / 2, middle, middle}, middleFill, middleBorder, 1);
        painter->fillEllipse(SwRect{cx - inner / 2 - 1, cy - inner / 2 - 1, inner, inner}, innerFill, innerFill, 0);
    }

private:
    bool active_{false};
};

} // namespace

RuntimeProfilerMonitoringBarWidget::RuntimeProfilerMonitoringBarWidget(SwWidget* parent)
    : SwFrame(parent) {
    setStyleSheet(R"(
        RuntimeProfilerMonitoringBarWidget {
            background-color: rgb(30, 30, 30);
            border-color: rgb(62, 62, 66);
            border-width: 1px;
            border-radius: 8px;
        }
    )");

    toggleButton_ = new SwPushButton("Demarrer le suivi", this);
    toggleButton_->setCheckable(true);
    toggleButton_->resize(160, 28);

    SwLabel* thresholdLabel = new SwLabel("Seuil:", this);
    thresholdLabel->resize(40, 16);
    thresholdLabel->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(156, 156, 156); font-size: 12px; }");

    thresholdSpinBox_ = new SwSpinBox(this);
    thresholdSpinBox_->resize(74, 28);
    thresholdSpinBox_->setRange(1, 5000);
    thresholdSpinBox_->setSingleStep(1);
    thresholdSpinBox_->setStyleSheet(R"(
        SwSpinBox {
            background-color: rgb(37, 37, 38);
            border-color: rgb(69, 69, 74);
            border-width: 1px;
            border-radius: 7px;
            divider-color: rgb(54, 54, 58);
            arrow-color: rgb(180, 180, 180);
            arrow-hover-color: rgb(255, 255, 255);
            arrow-pressed-color: rgb(255, 255, 255);
            arrow-disabled-color: rgb(96, 96, 100);
            arrow-hover-background-color: rgb(44, 44, 47);
            arrow-pressed-background-color: rgb(56, 56, 60);
        }
    )");
    if (thresholdSpinBox_->lineEdit()) {
        thresholdSpinBox_->lineEdit()->setStyleSheet(R"(
            SwLineEdit {
                background-color: rgba(0,0,0,0);
                border-width: 0px;
                border-radius: 0px;
                padding: 5px 9px;
                font-size: 12px;
                color: rgb(220, 220, 220);
            }
        )");
    }

    SwLabel* thresholdUnitLabel = new SwLabel("ms", this);
    thresholdUnitLabel->resize(20, 16);
    thresholdUnitLabel->setStyleSheet(R"(
        SwLabel {
            background-color: rgba(0,0,0,0);
            border-width: 0px;
            color: rgb(220, 220, 220);
            font-size: 12px;
        }
    )");

    stackCaptureLabel_ = new SwLabel("Piles: ON", this);
    stackCaptureLabel_->resize(72, 16);
    stackCaptureLabel_->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(156, 156, 156); font-size: 12px; }");

    statusLed_ = new RuntimeProfilerStatusLedWidget(this);
    statusLed_->resize(14, 14);

    stallLabel_ = new SwLabel("0 stalls", this);
    stallLabel_->resize(72, 16);
    stallLabel_->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine));
    stallLabel_->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(156, 156, 156); font-size: 12px; }");

    SwHorizontalLayout* layout = new SwHorizontalLayout();
    layout->setMargin(6);
    layout->setSpacing(8);
    layout->addWidget(toggleButton_, 0, 146);
    layout->addWidget(thresholdLabel, 0, 40);
    layout->addWidget(thresholdSpinBox_, 0, 74);
    layout->addWidget(thresholdUnitLabel, 0, 20);
    layout->addWidget(stackCaptureLabel_, 0, 70);
    layout->addStretch(1);
    layout->addWidget(statusLed_, 0, 14);
    layout->addWidget(stallLabel_, 0, 74);
    setLayout(layout);

    SwObject::connect(toggleButton_, &SwPushButton::toggled, this, [this](bool checked) {
        monitoringEnabled_ = checked;
        updateVisualState_();
        if (syncingState_) {
            return;
        }
        emit monitoringToggleRequested(checked);
    });
    SwObject::connect(thresholdSpinBox_, &SwSpinBox::valueChanged, this, [this](int valueMs) {
        thresholdUs_ = static_cast<long long>(valueMs) * 1000LL;
        updateVisualState_();
        if (syncingThreshold_) {
            return;
        }
        emit thresholdChangedUs(thresholdUs_);
    });

    syncingThreshold_ = true;
    thresholdSpinBox_->setValue(thresholdMsFromUs_(thresholdUs_));
    syncingThreshold_ = false;
    updateVisualState_();
}

SwSize RuntimeProfilerMonitoringBarWidget::minimumSizeHint() const {
    return SwSize{320, 34};
}

void RuntimeProfilerMonitoringBarWidget::setMonitoringEnabled(bool enabled) {
    monitoringEnabled_ = enabled;
    if (toggleButton_) {
        syncingState_ = true;
        toggleButton_->setChecked(enabled);
        syncingState_ = false;
    } else {
        updateVisualState_();
    }
    updateVisualState_();
}

void RuntimeProfilerMonitoringBarWidget::setThresholdUs(long long thresholdUs) {
    thresholdUs_ = thresholdUs;
    if (thresholdSpinBox_) {
        syncingThreshold_ = true;
        thresholdSpinBox_->setValue(thresholdMsFromUs_(thresholdUs_));
        syncingThreshold_ = false;
    }
    updateVisualState_();
}

void RuntimeProfilerMonitoringBarWidget::setStallCount(unsigned long long stallCount) {
    stallCount_ = stallCount;
    updateVisualState_();
}

void RuntimeProfilerMonitoringBarWidget::updateVisualState_() {
    if (toggleButton_) {
        toggleButton_->setText(monitoringEnabled_ ? "Arreter le suivi" : "Demarrer le suivi");
        toggleButton_->setStyleSheet(monitoringEnabled_
                                         ? "SwPushButton { background-color: rgb(45, 45, 48); color: rgb(220, 220, 220); border-color: rgb(62, 62, 66); border-width: 1px; border-radius: 6px; padding: 6px 10px; font-size: 12px; }"
                                         : "SwPushButton { background-color: rgb(45, 45, 48); color: rgb(220, 220, 220); border-color: rgb(62, 62, 66); border-width: 1px; border-radius: 6px; padding: 6px 10px; font-size: 12px; }");
    }
    if (thresholdSpinBox_) {
        thresholdSpinBox_->setStyleSheet(monitoringEnabled_
                                             ? R"(
                                                   SwSpinBox {
                                                       background-color: rgb(37, 37, 38);
                                                       border-color: rgb(0, 122, 204);
                                                       border-width: 1px;
                                                       border-radius: 7px;
                                                       divider-color: rgb(54, 54, 58);
                                                       arrow-color: rgb(180, 180, 180);
                                                       arrow-hover-color: rgb(255, 255, 255);
                                                       arrow-pressed-color: rgb(255, 255, 255);
                                                       arrow-disabled-color: rgb(96, 96, 100);
                                                       arrow-hover-background-color: rgb(44, 44, 47);
                                                       arrow-pressed-background-color: rgb(56, 56, 60);
                                                   }
                                               )"
                                             : R"(
                                                   SwSpinBox {
                                                       background-color: rgb(37, 37, 38);
                                                       border-color: rgb(69, 69, 74);
                                                       border-width: 1px;
                                                       border-radius: 7px;
                                                       divider-color: rgb(54, 54, 58);
                                                       arrow-color: rgb(180, 180, 180);
                                                       arrow-hover-color: rgb(255, 255, 255);
                                                       arrow-pressed-color: rgb(255, 255, 255);
                                                       arrow-disabled-color: rgb(96, 96, 100);
                                                       arrow-hover-background-color: rgb(44, 44, 47);
                                                       arrow-pressed-background-color: rgb(56, 56, 60);
                                                   }
                                               )");
    }
    if (statusLed_) {
        RuntimeProfilerStatusLedWidget* led = dynamic_cast<RuntimeProfilerStatusLedWidget*>(statusLed_);
        if (led) {
            led->setActive(monitoringEnabled_);
        }
    }
    if (stallLabel_) {
        stallLabel_->setText(stallLabelText_(stallCount_));
        stallLabel_->setStyleSheet(monitoringEnabled_
                                       ? "SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(196, 196, 196); font-size: 12px; padding-top: 1px; }"
                                       : "SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(110, 110, 110); font-size: 12px; padding-top: 1px; }");
    }
    if (stackCaptureLabel_) {
        stackCaptureLabel_->setText("Piles: ON");
        stackCaptureLabel_->setStyleSheet(monitoringEnabled_
                                              ? "SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(156, 156, 156); font-size: 12px; }"
                                              : "SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(110, 110, 110); font-size: 12px; }");
    }
}

int RuntimeProfilerMonitoringBarWidget::thresholdMsFromUs_(long long durationUs) {
    return std::max(1, static_cast<int>((durationUs + 500LL) / 1000LL));
}

SwString RuntimeProfilerMonitoringBarWidget::stallLabelText_(unsigned long long stallCount) {
    if (stallCount > 999ULL) {
        return "999+ stalls";
    }
    return SwString::number(stallCount) + " stalls";
}
