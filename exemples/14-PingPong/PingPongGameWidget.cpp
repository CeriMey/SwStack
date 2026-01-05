#include "PingPongGameWidget.h"

#include <algorithm>
#include <cmath>

#include "ScoreBanner.h"
#include "SwPainter.h"
#include "SwWidgetPlatformAdapter.h"

#ifdef _WIN32
#include "platform/win/SwWindows.h"
#elif defined(__linux__)
#include <X11/keysym.h>
#endif

PingPongGameWidget::PingPongGameWidget(SwWidget* parent)
    : SwWidget(parent)
    , m_scoreBanner(nullptr)
    , m_gameTimer(new SwTimer(16, this))
    , m_leftScore(0)
    , m_rightScore(0)
    , m_leftPaddleY(0)
    , m_rightPaddleY(0)
    , m_ballX(0.0f)
    , m_ballY(0.0f)
    , m_ballVX(0.0f)
    , m_ballVY(0.0f)
    , m_ballSize(18)
    , m_paddleWidth(18)
    , m_paddleHeight(90)
    , m_sidePadding(28)
    , m_topMargin(18)
    , m_bottomMargin(24)
    , m_roundActive(false)
    , m_launchGoesUp(true)
    , m_pendingServeDirection(1)
    , m_leftBot(true) {
    resize(820, 460);
    setCursor(CursorType::Cross);
    setFocusPolicy(FocusPolicyEnum::Strong);

    resetPaddles();
    centerBall();

    SwObject::connect(m_gameTimer, &SwTimer::timeout, [this]() {
        stepGame();
    });
    m_gameTimer->start(16);
}

void PingPongGameWidget::setScoreBanner(ScoreBanner* banner) {
    m_scoreBanner = banner;
    if (m_scoreBanner) {
        m_scoreBanner->setScores(m_leftScore, m_rightScore);
        m_scoreBanner->showDefaultStatus();
    }
}

void PingPongGameWidget::replayRound() {
    resetPaddles();
    centerBall();
    m_roundActive = true;
    const float baseSpeed = computeCurrentSpeed();
    const int direction = (m_pendingServeDirection == 0) ? 1 : m_pendingServeDirection;
    m_ballVX = baseSpeed * static_cast<float>(direction);
    m_ballVY = baseSpeed * (m_launchGoesUp ? -0.35f : 0.35f);
    m_launchGoesUp = !m_launchGoesUp;

    if (m_scoreBanner) {
        m_scoreBanner->showDefaultStatus();
        m_scoreBanner->setScores(m_leftScore, m_rightScore);
    }
    update();
}

void PingPongGameWidget::paintEvent(PaintEvent* event) {
    SwPainter* painter = event->painter();
    if (!painter) {
        return;
    }

    SwRect canvas = getRect();
    painter->fillRoundedRect(canvas, 20, SwColor{16, 22, 42}, SwColor{40, 55, 90}, 2);

    auto translateRect = [&](SwRect rect) {
        rect.x += canvas.x;
        rect.y += canvas.y;
        return rect;
    };

    SwRect arena{canvas.x + 12,
                 canvas.y + m_topMargin,
                 canvas.width - 24,
                 canvas.height - m_topMargin - m_bottomMargin};
    painter->drawRect(arena, SwColor{58, 72, 108}, 1);

    SwRect centerLine{canvas.x + canvas.width / 2 - 2,
                      canvas.y + m_topMargin + 4,
                      4,
                      canvas.height - m_topMargin - m_bottomMargin - 8};
    painter->fillRect(centerLine, SwColor{64, 78, 126}, SwColor{64, 78, 126}, 0);

    painter->fillRoundedRect(translateRect(leftPaddleRect()), 6, SwColor{230, 234, 241}, SwColor{120, 150, 200}, 1);
    painter->fillRoundedRect(translateRect(rightPaddleRect()), 6, SwColor{230, 234, 241}, SwColor{120, 150, 200}, 1);

    SwRect ball = translateRect(ballRect());
    painter->fillRoundedRect(ball, ball.width / 2, SwColor{255, 205, 92}, SwColor{140, 110, 35}, 1);

    if (!m_roundActive) {
        SwRect infoRect{canvas.x + canvas.width / 2 - 160,
                        canvas.y + canvas.height / 2 - 25,
                        320,
                        50};
        painter->fillRoundedRect(infoRect, 10, SwColor{10, 12, 20}, SwColor{72, 72, 72}, 1);
        painter->drawText(infoRect,
                          SwString("Appuie sur Rejouer ou sur R pour servir."),
                          DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::WordBreak),
                          SwColor{245, 245, 245},
                          getFont());
    }
}

void PingPongGameWidget::keyPressEvent(KeyEvent* event) {
    const int keyCode = event->key();
    bool handled = false;

    if (SwWidgetPlatformAdapter::matchesShortcutKey(keyCode, 'O') ||
        SwWidgetPlatformAdapter::matchesShortcutKey(keyCode, 'I')) {
        moveRightPaddle(-1);
        handled = true;
    } else if (SwWidgetPlatformAdapter::matchesShortcutKey(keyCode, 'L') ||
               SwWidgetPlatformAdapter::matchesShortcutKey(keyCode, 'K')) {
        moveRightPaddle(1);
        handled = true;
    } else if (SwWidgetPlatformAdapter::matchesShortcutKey(keyCode, 'R')) {
        replayRound();
        handled = true;
    }

    if (handled) {
        event->accept();
        update();
        return;
    }

    SwWidget::keyPressEvent(event);
}

void PingPongGameWidget::mousePressEvent(MouseEvent* event) {
    setFocus(true);
    event->accept();
}

void PingPongGameWidget::resizeEvent(ResizeEvent* event) {
    SwWidget::resizeEvent(event);
    clampPaddles();
    centerBall();
}

void PingPongGameWidget::stepGame() {
    if (!m_roundActive) {
        update();
        return;
    }

    m_ballX += m_ballVX;
    m_ballY += m_ballVY;

    if (m_leftBot) {
        updateLeftBot();
    }

    const float minY = static_cast<float>(m_topMargin);
    const float maxY = static_cast<float>(std::max(0, height() - m_bottomMargin - m_ballSize));
    if (m_ballY <= minY) {
        m_ballY = minY;
        m_ballVY = -m_ballVY;
    }
    if (m_ballY >= maxY) {
        m_ballY = maxY;
        m_ballVY = -m_ballVY;
    }

    SwRect ball = ballRect();
    SwRect left = leftPaddleRect();
    SwRect right = rightPaddleRect();

    if (rectsIntersect(ball, left) && m_ballVX < 0) {
        m_ballX = static_cast<float>(left.x + left.width);
        bounceFromPaddle(true, left);
    } else if (rectsIntersect(ball, right) && m_ballVX > 0) {
        m_ballX = static_cast<float>(right.x - m_ballSize);
        bounceFromPaddle(false, right);
    }

    if (m_ballX + m_ballSize < 0) {
        registerGoal(false);
    } else if (m_ballX > width()) {
        registerGoal(true);
    }

    update();
}

void PingPongGameWidget::registerGoal(bool leftPlayerScored) {
    if (leftPlayerScored) {
        ++m_leftScore;
        m_pendingServeDirection = 1;
        updateScoreBanner(SwString("But pour le joueur gauche."));
    } else {
        ++m_rightScore;
        m_pendingServeDirection = -1;
        updateScoreBanner(SwString("But pour le joueur droit."));
    }
    stopRound();
}

void PingPongGameWidget::stopRound() {
    m_roundActive = false;
    m_ballVX = 0.0f;
    m_ballVY = 0.0f;
    centerBall();
}

void PingPongGameWidget::updateScoreBanner(const SwString& status) {
    if (m_scoreBanner) {
        m_scoreBanner->setScores(m_leftScore, m_rightScore);
        m_scoreBanner->setStatusText(status);
    }
}

void PingPongGameWidget::resetPaddles() {
    const int centerY = std::max(m_topMargin, (height() - m_paddleHeight) / 2);
    m_leftPaddleY = centerY;
    m_rightPaddleY = centerY;
}

void PingPongGameWidget::centerBall() {
    m_ballX = (width() - m_ballSize) / 2.0f;
    m_ballY = (height() - m_ballSize) / 2.0f;
}

SwRect PingPongGameWidget::leftPaddleRect() const {
    return SwRect{m_sidePadding, m_leftPaddleY, m_paddleWidth, m_paddleHeight};
}

SwRect PingPongGameWidget::rightPaddleRect() const {
    return SwRect{width() - m_sidePadding - m_paddleWidth, m_rightPaddleY, m_paddleWidth, m_paddleHeight};
}

SwRect PingPongGameWidget::ballRect() const {
    return SwRect{static_cast<int>(m_ballX), static_cast<int>(m_ballY), m_ballSize, m_ballSize};
}

void PingPongGameWidget::moveLeftPaddle(int direction) {
    const int delta = 18 * direction;
    m_leftPaddleY = clampValue(m_leftPaddleY + delta);
}

void PingPongGameWidget::moveRightPaddle(int direction) {
    const int delta = 18 * direction;
    m_rightPaddleY = clampValue(m_rightPaddleY + delta);
}

int PingPongGameWidget::clampValue(int value) const {
    const int minVal = m_topMargin;
    const int maxVal = std::max(m_topMargin, height() - m_bottomMargin - m_paddleHeight);
    if (value < minVal) {
        return minVal;
    }
    if (value > maxVal) {
        return maxVal;
    }
    return value;
}

void PingPongGameWidget::clampPaddles() {
    m_leftPaddleY = clampValue(m_leftPaddleY);
    m_rightPaddleY = clampValue(m_rightPaddleY);
}

void PingPongGameWidget::updateLeftBot() {
    float ballCenter = m_ballY + m_ballSize / 2.0f;
    int target = static_cast<int>(ballCenter - m_paddleHeight / 2.0f);
    int diff = target - m_leftPaddleY;
    const int maxStep = 14;
    if (std::abs(diff) <= maxStep) {
        m_leftPaddleY = target;
    } else {
        m_leftPaddleY += (diff > 0 ? maxStep : -maxStep);
    }
    m_leftPaddleY = clampValue(m_leftPaddleY);
}

void PingPongGameWidget::bounceFromPaddle(bool leftSide, const SwRect& paddleRect) {
    float paddleCenter = paddleRect.y + paddleRect.height / 2.0f;
    float ballCenter = m_ballY + m_ballSize / 2.0f;
    float relative = (ballCenter - paddleCenter) / (paddleRect.height / 2.0f);
    if (relative > 1.0f) {
        relative = 1.0f;
    } else if (relative < -1.0f) {
        relative = -1.0f;
    }

    float speed = computeCurrentSpeed() + 0.4f;
    m_ballVY = relative * (speed * 1.1f);
    m_ballVX = leftSide ? std::fabs(speed) : -std::fabs(speed);
}

float PingPongGameWidget::computeCurrentSpeed() const {
    return 4.2f + static_cast<float>(m_leftScore + m_rightScore) * 0.3f;
}

bool PingPongGameWidget::rectsIntersect(const SwRect& a, const SwRect& b) const {
    bool noOverlap = (a.x + a.width) < b.x || (b.x + b.width) < a.x ||
                     (a.y + a.height) < b.y || (b.y + b.height) < a.y;
    return !noOverlap;
}
