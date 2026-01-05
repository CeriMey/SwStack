#pragma once

#include "SwWidget.h"
#include "SwString.h"

class ScoreBanner : public SwWidget {
    SW_OBJECT(ScoreBanner, SwWidget)

public:
    explicit ScoreBanner(SwWidget* parent = nullptr);

    void setScores(int leftScore, int rightScore);
    void setStatusText(const SwString& text);
    void showDefaultStatus();

protected:
    void paintEvent(PaintEvent* event) override;

private:
    int m_leftScore;
    int m_rightScore;
    SwString m_status;
    SwString m_defaultStatus;
    bool m_useDefault;
};

