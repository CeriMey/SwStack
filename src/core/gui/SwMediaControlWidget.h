/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#pragma once

/***************************************************************************************************
 * Compact media control widget composed of the SwAbstractSlider timeline and SwPushButton actions.
 *
 * Layout:
 *  - Top row: current time, timeline slider, total duration.
 *  - Bottom row: previous, play, pause, stop, next controls.
 **************************************************************************************************/

#include "SwAbstractSlider.h"
#include "SwLabel.h"
#include "SwLayout.h"
#include "SwPushButton.h"
#include "SwString.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

class SwMediaControlWidget : public SwWidget {
    SW_OBJECT(SwMediaControlWidget, SwWidget)

public:
    SwMediaControlWidget(SwWidget* parent = nullptr)
        : SwWidget(parent)
        , m_durationSeconds(300.0)
        , m_positionSeconds(0.0)
        , m_bufferedSeconds(0.0)
        , m_isPlaying(false) {
        setMinimumSize(520, 160);
        resize(640, 180);
        setStyleSheet(defaultStyleSheet());

        auto* rootLayout = new SwVerticalLayout(this);
        rootLayout->setMargin(12);
        rootLayout->setSpacing(10);
        setLayout(rootLayout);

        // Timeline row.
        auto* timelinePanel = new SwWidget(this);
        auto* timelineRow = new SwHorizontalLayout(timelinePanel);
        timelineRow->setSpacing(10);

        m_currentLabel = new SwLabel(timelinePanel);
        m_currentLabel->setText("00:00");
        m_currentLabel->resize(48, 24);

        m_timeline = new SwHorizontalSlider(timelinePanel);
        m_timeline->setRange(0.0, m_durationSeconds);
        m_timeline->setStep(1.0);
        m_timeline->setBufferedValue(m_bufferedSeconds);
        m_timeline->setValue(m_positionSeconds);
        m_timeline->setMinimumSize(260, 36);
        m_timeline->setAccentColor({248, 108, 96});
        m_timeline->setTrackColors({210, 218, 226}, {210, 218, 226});
        m_timeline->setBufferedColor({178, 196, 214});
        m_timeline->setHandleColors({255, 255, 255}, {178, 196, 214});

        m_totalLabel = new SwLabel(timelinePanel);
        m_totalLabel->setText("05:00");
        m_totalLabel->resize(48, 24);

        timelineRow->addWidget(m_currentLabel, 0, 48);
        timelineRow->addWidget(m_timeline, 1, 260);
        timelineRow->addWidget(m_totalLabel, 0, 48);
        timelinePanel->setLayout(timelineRow);

        // Control bar centered under the timeline.
        auto* controlsPanel = new SwWidget(this);
        auto* controlsRow = new SwHorizontalLayout(controlsPanel);
        controlsRow->setSpacing(16);

        m_prevButton = makeButton("<<", controlsPanel, 52, 52, 26);
        m_playButton = makeButton(">", controlsPanel, 64, 64, 32);
        m_stopButton = makeButton("[]", controlsPanel, 52, 52, 26);
        m_nextButton = makeButton(">>", controlsPanel, 52, 52, 26);

        // Stretchy spacers keep the cluster centered.
        SwWidget* leftSpacer = new SwWidget(controlsPanel);
        leftSpacer->setMinimumSize(0, 0);
        leftSpacer->resize(0, 0);
        SwWidget* rightSpacer = new SwWidget(controlsPanel);
        rightSpacer->setMinimumSize(0, 0);
        rightSpacer->resize(0, 0);

        controlsRow->addWidget(leftSpacer, 1, 0);
        controlsRow->addWidget(m_prevButton, 0, 52);
        controlsRow->addWidget(m_playButton, 0, 64);
        controlsRow->addWidget(m_stopButton, 0, 52);
        controlsRow->addWidget(m_nextButton, 0, 52);
        controlsRow->addWidget(rightSpacer, 1, 0);
        controlsPanel->setLayout(controlsRow);

        rootLayout->addWidget(timelinePanel, 1, 60);
        rootLayout->addWidget(controlsPanel, 0, 50);

        // Wire signals to external consumers.
        SwObject::connect(m_timeline, &SwHorizontalSlider::valueChanged, [this](double value) {
            m_positionSeconds = clampToDuration(value);
            updateTimeLabels();
            positionChanged(m_positionSeconds);
        });
        SwObject::connect(m_prevButton, &SwPushButton::clicked, [this]() { previousRequested(); });
        SwObject::connect(m_playButton, &SwPushButton::clicked, [this]() { togglePlayPause(); });
        SwObject::connect(m_stopButton, &SwPushButton::clicked, [this]() { setPlaying(false); stopRequested(); });
        SwObject::connect(m_nextButton, &SwPushButton::clicked, [this]() { nextRequested(); });

        updateTimeLabels();
    }

    void setDurationSeconds(double seconds) {
        m_durationSeconds = std::max(0.0, seconds);
        m_timeline->setRange(0.0, m_durationSeconds);
        setBufferedSeconds(m_bufferedSeconds);
        setPositionSeconds(m_positionSeconds);
        updateTimeLabels();
    }

    void setPositionSeconds(double seconds) {
        m_positionSeconds = clampToDuration(seconds);
        m_timeline->setValue(m_positionSeconds);
        updateTimeLabels();
    }

    void setBufferedSeconds(double seconds) {
        m_bufferedSeconds = clampToDuration(seconds);
        m_timeline->setBufferedValue(m_bufferedSeconds);
    }

    double durationSeconds() const { return m_durationSeconds; }
    double positionSeconds() const { return m_positionSeconds; }
    double bufferedSeconds() const { return m_bufferedSeconds; }
    bool isPlaying() const { return m_isPlaying; }

    DECLARE_SIGNAL(positionChanged, double);
    DECLARE_SIGNAL_VOID(previousRequested);
    DECLARE_SIGNAL_VOID(playRequested);
    DECLARE_SIGNAL_VOID(pauseRequested);
    DECLARE_SIGNAL_VOID(stopRequested);
    DECLARE_SIGNAL_VOID(nextRequested);

protected:
    SwPushButton* makeButton(const std::string& text, SwWidget* parent, int w, int h, int radius) {
        auto* btn = new SwPushButton(text, parent);
        btn->setCursor(CursorType::Hand);
        btn->resize(w, h);
        btn->setMinimumSize(w, h);
        // Button radius managed via stylesheet; store as property so styles can read if needed later.
        btn->setProperty("BorderRadius", radius);
        return btn;
    }

    std::string defaultStyleSheet() const {
        return R"(
            SwMediaControlWidget {
                background-color: rgba(0, 0, 0, 0);
                border-radius: 14px;
                padding: 12px;
            }
            SwPushButton {
                background-color: linear-gradient(135deg, rgb(0, 191, 165), rgb(0, 180, 190));
                border-color: rgb(0, 191, 165);
                color: rgb(12, 26, 38);
                border-radius: 999px;
                padding: 12px 18px;
                border-width: 0px;
                font-size: 16px;
                box-shadow: 0 8px 18px rgba(0, 0, 0, 0.18);
            }
            SwPushButton:hover {
                background-color: linear-gradient(135deg, rgb(0, 206, 174), rgb(0, 196, 206));
                color: rgb(8, 20, 32);
            }
            SwPushButton:pressed {
                background-color: linear-gradient(135deg, rgb(0, 171, 150), rgb(0, 161, 172));
                color: rgb(6, 14, 24);
                box-shadow: 0 4px 10px rgba(0, 0, 0, 0.2);
            }
            SwLabel {
                color: rgb(60, 75, 96);
            }
        )";
    }

    void updateTimeLabels() {
        m_currentLabel->setText(formatTime(m_positionSeconds));
        m_totalLabel->setText(formatTime(m_durationSeconds));
    }

    SwString formatTime(double seconds) const {
        int total = static_cast<int>(std::round(seconds));
        if (total < 0) {
            total = 0;
        }
        int mins = total / 60;
        int secs = total % 60;
        char buffer[16] = {};
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d", mins, secs);
        return SwString(buffer);
    }

    double clampToDuration(double seconds) const {
        if (seconds < 0.0) {
            return 0.0;
        }
        if (seconds > m_durationSeconds) {
            return m_durationSeconds;
        }
        return seconds;
    }

    void togglePlayPause() {
        setPlaying(!m_isPlaying);
        if (m_isPlaying) {
            playRequested();
        } else {
            pauseRequested();
        }
    }

    void setPlaying(bool playing) {
        m_isPlaying = playing;
        m_playButton->setText(playing ? "||" : ">");
    }

private:
    SwHorizontalSlider* m_timeline;
    SwLabel* m_currentLabel;
    SwLabel* m_totalLabel;
    SwPushButton* m_prevButton;
    SwPushButton* m_playButton;
    SwPushButton* m_stopButton;
    SwPushButton* m_nextButton;
    double m_durationSeconds;
    double m_positionSeconds;
    double m_bufferedSeconds;
    bool m_isPlaying;
};
