#pragma once

#include "SwWidget.h"
#include "SwTimer.h"
#include "SwString.h"

class ScoreBanner;

class PingPongGameWidget : public SwWidget {
    SW_OBJECT(PingPongGameWidget, SwWidget)

public:
    explicit PingPongGameWidget(SwWidget* parent = nullptr);

    void setScoreBanner(ScoreBanner* banner);
    void replayRound();

protected:
    void paintEvent(PaintEvent* event) override;
    void keyPressEvent(KeyEvent* event) override;
    void mousePressEvent(MouseEvent* event) override;
    void resizeEvent(ResizeEvent* event) override;

private:
    void stepGame();
    void registerGoal(bool leftPlayerScored);
    void stopRound();
    void updateScoreBanner(const SwString& status);
    void resetPaddles();
    void centerBall();
    SwRect leftPaddleRect() const;
    SwRect rightPaddleRect() const;
    SwRect ballRect() const;
    void moveLeftPaddle(int direction);
    void moveRightPaddle(int direction);
    int clampValue(int value) const;
    void clampPaddles();
    void updateLeftBot();
    void bounceFromPaddle(bool leftSide, const SwRect& paddleRect);
    float computeCurrentSpeed() const;
    bool rectsIntersect(const SwRect& a, const SwRect& b) const;

private:
    ScoreBanner* m_scoreBanner;
    SwTimer* m_gameTimer;
    int m_leftScore;
    int m_rightScore;
    int m_leftPaddleY;
    int m_rightPaddleY;
    float m_ballX;
    float m_ballY;
    float m_ballVX;
    float m_ballVY;
    int m_ballSize;
    int m_paddleWidth;
    int m_paddleHeight;
    int m_sidePadding;
    int m_topMargin;
    int m_bottomMargin;
    bool m_roundActive;
    bool m_launchGoesUp;
    int m_pendingServeDirection;
    bool m_leftBot;
};

