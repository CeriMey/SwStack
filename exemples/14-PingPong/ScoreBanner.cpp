#include "ScoreBanner.h"

#include "SwPainter.h"
#include "SwFont.h"

ScoreBanner::ScoreBanner(SwWidget* parent)
    : SwWidget(parent)
    , m_leftScore(0)
    , m_rightScore(0)
    , m_status("")
    , m_defaultStatus("Bot VS USER (O/L ou fleches).")
    , m_useDefault(true) {
    resize(520, 90);
    setFocusPolicy(FocusPolicyEnum::NoFocus);
}

void ScoreBanner::setScores(int leftScore, int rightScore) {
    m_leftScore = leftScore;
    m_rightScore = rightScore;
    update();
}

void ScoreBanner::setStatusText(const SwString& text) {
    m_status = text;
    m_useDefault = false;
    update();
}

void ScoreBanner::showDefaultStatus() {
    m_useDefault = true;
    update();
}

void ScoreBanner::paintEvent(PaintEvent* event) {
    SwPainter* painter = event->painter();
    if (!painter) {
        return;
    }

    SwRect rect = this->rect();
    painter->fillRoundedRect(rect, 14, SwColor{24, 34, 64}, SwColor{90, 120, 190}, 2);

    SwFont titleFont = getFont();
    titleFont.setPointSize(12);
    SwFont infoFont = titleFont;
    infoFont.setPointSize(10);

    SwString header = SwString("Bot gauche : %1    Joueur droit (O/L ou fleches) : %2");
    header = header.arg(SwString::number(m_leftScore));
    header = header.arg(SwString::number(m_rightScore));

    SwRect headerRect{rect.x + 12, rect.y + 10, rect.width - 24, rect.height / 2 - 14};
    painter->drawText(headerRect,
                      header,
                      DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                      SwColor{255, 255, 255},
                      titleFont);

    SwString infoText = m_useDefault ? m_defaultStatus : m_status;
    SwRect infoRect{rect.x + 16,
                    rect.y + rect.height / 2,
                    rect.width - 32,
                    rect.height / 2 - 16};
    painter->drawText(infoRect,
                      infoText,
                      DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::WordBreak),
                      SwColor{200, 205, 220},
                      infoFont);
}

