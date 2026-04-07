#include "SwCreatorShell.h"

#include "designer/SwCreatorMainPanel.h"
#include "editor/SwCreatorEditorPanel.h"
#include "theme/SwCreatorTheme.h"

#include "SwFrame.h"
#include "SwLabel.h"
#include "SwPainter.h"
#include "SwPushButton.h"
#include "SwStackedWidget.h"
#include "SwToolButton.h"

#include <algorithm>

namespace {
constexpr int kContentInset = 0;

// ─────────────────────────────────────────────────────────────────────────────
//  Sidebar button — draws icon + active indicator using theme tokens
// ─────────────────────────────────────────────────────────────────────────────

class SwCreatorSidebarButton final : public SwToolButton {
    SW_OBJECT(SwCreatorSidebarButton, SwToolButton)

public:
    enum class IconKind {
        Overview,
        Editor,
        Creator
    };

    explicit SwCreatorSidebarButton(IconKind iconKind, SwWidget* parent = nullptr)
        : SwToolButton(parent)
        , m_iconKind(iconKind) {
        const auto& th = SwCreatorTheme::current();
        resize(th.sidebarBtnSize, th.sidebarBtnSize);
        setCheckable(true);
        setStyleSheet("SwToolButton { background-color: rgba(0,0,0,0); border-width: 0px; }");
    }

protected:
    void paintEvent(PaintEvent* event) override {
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const auto& th = SwCreatorTheme::current();
        const SwRect bounds = rect();
        const bool active = isChecked();

        SwColor iconColor = th.textMuted;
        if (active) {
            iconColor = th.textPrimary;
        } else if (getHover()) {
            iconColor = th.textSecondary;
        }

        const SwRect hoverRect{bounds.x + 4, bounds.y + 4, bounds.width - 8, bounds.height - 8};
        if (active) {
            painter->fillRoundedRect(hoverRect, 8, th.pressedBg, th.pressedBg, 0);
        } else if (getPressed() || getHover()) {
            painter->fillRoundedRect(hoverRect, 8, th.hoverBg, th.hoverBg, 0);
        }

        if (active) {
            painter->fillRoundedRect(SwRect{bounds.x, bounds.y + 6, 3, bounds.height - 12},
                                     1,
                                     th.accentPrimary,
                                     th.accentPrimary,
                                     0);
        }

        drawIcon_(painter, bounds, iconColor);
    }

private:
    void drawIcon_(SwPainter* painter, const SwRect& bounds, const SwColor& color) {
        if (!painter) {
            return;
        }

        if (m_iconKind == IconKind::Overview) {
            const int size = 7;
            const int gap = 3;
            const int total = size * 2 + gap;
            const int startX = bounds.x + (bounds.width - total) / 2;
            const int startY = bounds.y + (bounds.height - total) / 2;
            for (int row = 0; row < 2; ++row) {
                for (int col = 0; col < 2; ++col) {
                    painter->drawRect(SwRect{startX + col * (size + gap),
                                             startY + row * (size + gap),
                                             size,
                                             size},
                                      color,
                                      1);
                }
            }
            return;
        }

        if (m_iconKind == IconKind::Editor) {
            const int cx = bounds.x + bounds.width / 2;
            const int cy = bounds.y + bounds.height / 2;
            painter->drawLine(cx - 8, cy, cx - 3, cy - 5, color, 1);
            painter->drawLine(cx - 8, cy, cx - 3, cy + 5, color, 1);
            painter->drawLine(cx + 8, cy, cx + 3, cy - 5, color, 1);
            painter->drawLine(cx + 8, cy, cx + 3, cy + 5, color, 1);
            painter->drawLine(cx + 1, cy - 7, cx - 1, cy + 7, color, 1);
            return;
        }

        const SwRect panelRect{bounds.x + 10, bounds.y + 9, bounds.width - 20, bounds.height - 18};
        painter->drawRect(panelRect, color, 1);
        painter->drawLine(panelRect.x + 6,
                          panelRect.y,
                          panelRect.x + 6,
                          panelRect.y + panelRect.height,
                          color,
                          1);
        painter->drawLine(panelRect.x + 11,
                          panelRect.y + 7,
                          panelRect.x + panelRect.width - 5,
                          panelRect.y + 7,
                          color,
                          1);
        painter->drawLine(panelRect.x + 11,
                          panelRect.y + 12,
                          panelRect.x + panelRect.width - 8,
                          panelRect.y + 12,
                          color,
                          1);
        painter->drawLine(panelRect.x + 11,
                          panelRect.y + 17,
                          panelRect.x + panelRect.width - 11,
                          panelRect.y + 17,
                          color,
                          1);
    }

    IconKind m_iconKind{IconKind::Overview};
};

// ─────────────────────────────────────────────────────────────────────────────
//  Overview page — dark-themed landing page
// ─────────────────────────────────────────────────────────────────────────────

class SwCreatorOverviewPage final : public SwWidget {
    SW_OBJECT(SwCreatorOverviewPage, SwWidget)

public:
    explicit SwCreatorOverviewPage(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        const auto& th = SwCreatorTheme::current();

        setStyleSheet("SwCreatorOverviewPage { background-color: " + SwCreatorTheme::rgb(th.surface1) + "; border-width: 0px; }");

        m_heroCard = makeCard_(this);
        m_title = makeLabel_("SwCreator", 26, true, m_heroCard);
        m_subtitle = makeLabel_("Le shell regroupe maintenant un editeur de code, son explorateur de fichiers et le designer visuel dans une seule navigation.",
                                12,
                                false,
                                m_heroCard);
        if (m_subtitle) {
            m_subtitle->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::Top | DrawTextFormat::WordBreak));
        }

        m_openEditor = new SwPushButton("Open editor", m_heroCard);
        m_openEditor->setStyleSheet(
            "SwPushButton { background-color: " + SwCreatorTheme::rgb(th.surface2)
            + "; border-color: " + SwCreatorTheme::rgb(th.accentPrimary)
            + "; color: " + SwCreatorTheme::rgb(th.accentPrimary)
            + "; border-radius: 12px; padding: 8px 14px; border-width: 1px; font-size: 14px; }"
            " SwPushButton:hover { background-color: " + SwCreatorTheme::rgb(th.surface3) + "; }"
            " SwPushButton:pressed { background-color: " + SwCreatorTheme::rgb(th.surface4) + "; }"
        );

        m_openCreator = new SwPushButton("Open designer", m_heroCard);
        m_openCreator->setStyleSheet(
            "SwPushButton { background-color: " + SwCreatorTheme::rgb(th.accentPrimary)
            + "; border-color: " + SwCreatorTheme::rgb(th.accentPrimary)
            + "; color: " + SwCreatorTheme::rgb(th.textInverse)
            + "; border-radius: 12px; padding: 8px 14px; border-width: 1px; font-size: 14px; }"
            " SwPushButton:hover { background-color: " + SwCreatorTheme::rgb(th.accentHover)
            + "; border-color: " + SwCreatorTheme::rgb(th.accentHover) + "; }"
            " SwPushButton:pressed { background-color: " + SwCreatorTheme::rgb(th.accentPressed)
            + "; border-color: " + SwCreatorTheme::rgb(th.accentPressed) + "; }"
        );

        m_flowCard = makeCard_(this);
        m_flowTitle = makeLabel_("Editor", 16, true, m_flowCard);
        m_flowText = makeLabel_("Explorateur systeme, onglets ouverts, etats dirty et sauvegarde sont maintenant regroupes dans un workspace dedie.",
                                11,
                                false,
                                m_flowCard);
        if (m_flowText) {
            m_flowText->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::Top | DrawTextFormat::WordBreak));
        }

        m_navCard = makeCard_(this);
        m_navTitle = makeLabel_("Designer", 16, true, m_navCard);
        m_navText = makeLabel_("Le designer reste intact et devient simplement la page suivante dans la navigation laterale du shell.",
                               11,
                               false,
                               m_navCard);
        if (m_navText) {
            m_navText->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::Top | DrawTextFormat::WordBreak));
        }

        SwObject::connect(m_openEditor, &SwPushButton::clicked, this, [this]() { openEditorRequested(); });
        SwObject::connect(m_openCreator, &SwPushButton::clicked, this, [this]() { openCreatorRequested(); });
        updateLayout_();
    }

    DECLARE_SIGNAL_VOID(openEditorRequested);
    DECLARE_SIGNAL_VOID(openCreatorRequested);

protected:
    void resizeEvent(ResizeEvent* event) override {
        SwWidget::resizeEvent(event);
        updateLayout_();
    }

private:
    static SwFrame* makeCard_(SwWidget* parent) {
        const auto& th = SwCreatorTheme::current();
        auto* card = new SwFrame(parent);
        card->setFrameShape(SwFrame::Shape::StyledPanel);
        card->setStyleSheet(
            "SwFrame { background-color: " + SwCreatorTheme::rgb(th.cardBg)
            + "; border-color: " + SwCreatorTheme::rgb(th.cardBorder)
            + "; border-radius: 20px; border-width: 1px; }"
        );
        return card;
    }

    static SwLabel* makeLabel_(const SwString& text, int pointSize, bool strong, SwWidget* parent) {
        const auto& th = SwCreatorTheme::current();
        auto* label = new SwLabel(text, parent);
        label->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine));
        label->setFont(SwFont(L"Segoe UI", pointSize, strong ? Bold : Medium));
        label->setStyleSheet(
            "SwLabel { color: " + SwCreatorTheme::rgb(strong ? th.textPrimary : th.textSecondary)
            + "; border-width: 0px; }"
        );
        return label;
    }

    void updateLayout_() {
        const SwRect r = rect();
        const int heroH = std::max(220, r.height / 2);
        const int gap = 18;
        const int innerMargin = 28;

        if (m_heroCard) {
            m_heroCard->move(0, 0);
            m_heroCard->resize(r.width, heroH);
        }
        if (m_title && m_heroCard) {
            m_title->move(innerMargin, 30);
            m_title->resize(std::max(0, m_heroCard->width() - innerMargin * 2), 40);
        }
        if (m_subtitle && m_heroCard) {
            m_subtitle->move(innerMargin, 82);
            m_subtitle->resize(std::max(0, m_heroCard->width() - innerMargin * 2), 78);
        }
        if (m_openEditor && m_heroCard) {
            m_openEditor->move(innerMargin, heroH - 70);
            m_openEditor->resize(150, 42);
        }
        if (m_openCreator && m_heroCard) {
            m_openCreator->move(innerMargin + 162, heroH - 70);
            m_openCreator->resize(150, 42);
        }

        const int cardsY = heroH + gap;
        const int cardsH = std::max(0, r.height - cardsY);
        const int halfW = std::max(0, (r.width - gap) / 2);

        if (m_flowCard) {
            m_flowCard->move(0, cardsY);
            m_flowCard->resize(halfW, cardsH);
        }
        if (m_navCard) {
            m_navCard->move(halfW + gap, cardsY);
            m_navCard->resize(std::max(0, r.width - halfW - gap), cardsH);
        }

        layoutCard_(m_flowCard, m_flowTitle, m_flowText);
        layoutCard_(m_navCard, m_navTitle, m_navText);
    }

    static void layoutCard_(SwFrame* card, SwLabel* title, SwLabel* text) {
        if (!card) {
            return;
        }

        const int innerMargin = 22;
        if (title) {
            title->move(innerMargin, 20);
            title->resize(std::max(0, card->width() - innerMargin * 2), 28);
        }
        if (text) {
            text->move(innerMargin, 58);
            text->resize(std::max(0, card->width() - innerMargin * 2), std::max(0, card->height() - 78));
        }
    }

    SwFrame* m_heroCard{nullptr};
    SwLabel* m_title{nullptr};
    SwLabel* m_subtitle{nullptr};
    SwPushButton* m_openEditor{nullptr};
    SwPushButton* m_openCreator{nullptr};

    SwFrame* m_flowCard{nullptr};
    SwLabel* m_flowTitle{nullptr};
    SwLabel* m_flowText{nullptr};

    SwFrame* m_navCard{nullptr};
    SwLabel* m_navTitle{nullptr};
    SwLabel* m_navText{nullptr};
};
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  Shell
// ─────────────────────────────────────────────────────────────────────────────

SwCreatorShell::SwCreatorShell(SwWidget* parent)
    : SwWidget(parent) {
    const auto& th = SwCreatorTheme::current();

    setStyleSheet("SwCreatorShell { background-color: " + SwCreatorTheme::rgb(th.surface1) + "; border-width: 0px; }");

    m_sidebarFrame = new SwFrame(this);
    m_sidebarFrame->setFrameShape(SwFrame::Shape::StyledPanel);
    m_sidebarFrame->setStyleSheet(
        "SwFrame { background-color: " + SwCreatorTheme::rgb(th.surface0)
        + "; border-color: " + SwCreatorTheme::rgb(th.surface0)
        + "; border-radius: 0px; border-width: 0px;"
        " border-top-right-radius: 10px; border-bottom-right-radius: 10px; }"
    );

    m_brandBadge = new SwFrame(m_sidebarFrame);
    m_brandBadge->setFrameShape(SwFrame::Shape::StyledPanel);
    m_brandBadge->setStyleSheet(
        "SwFrame { background-color: " + SwCreatorTheme::rgb(th.accentPrimary)
        + "; border-color: " + SwCreatorTheme::rgb(th.accentPrimary)
        + "; border-radius: 2px; border-width: 0px; }"
    );

    m_overviewButton = new SwCreatorSidebarButton(SwCreatorSidebarButton::IconKind::Overview, m_sidebarFrame);
    m_editorButton = new SwCreatorSidebarButton(SwCreatorSidebarButton::IconKind::Editor, m_sidebarFrame);
    m_creatorButton = new SwCreatorSidebarButton(SwCreatorSidebarButton::IconKind::Creator, m_sidebarFrame);

    m_contentFrame = new SwFrame(this);
    m_contentFrame->setFrameShape(SwFrame::Shape::NoFrame);
    m_contentFrame->setStyleSheet("SwFrame { background-color: rgba(0,0,0,0); border-width: 0px; }");

    m_stack = new SwStackedWidget(m_contentFrame);
    m_stack->setStyleSheet("SwStackedWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");

    auto* overviewPage = new SwCreatorOverviewPage(m_stack);
    m_overviewPage = overviewPage;
    m_editorPanel = new SwCreatorEditorPanel(m_stack);
    m_creatorPanel = new SwCreatorMainPanel(m_stack);

    m_stack->addWidget(m_overviewPage);
    m_stack->addWidget(m_editorPanel);
    m_stack->addWidget(m_creatorPanel);

    SwObject::connect(m_overviewButton, &SwToolButton::clicked, this, [this](bool) { setCurrentPage_(PageId::Overview); });
    SwObject::connect(m_editorButton, &SwToolButton::clicked, this, [this](bool) { setCurrentPage_(PageId::Editor); });
    SwObject::connect(m_creatorButton, &SwToolButton::clicked, this, [this](bool) { setCurrentPage_(PageId::Creator); });
    SwObject::connect(overviewPage, &SwCreatorOverviewPage::openEditorRequested, this, [this]() { showEditorPage(); });
    SwObject::connect(overviewPage, &SwCreatorOverviewPage::openCreatorRequested, this, [this]() { showCreatorPage(); });

    setCurrentPage_(PageId::Creator);
    updateLayout_();
}

SwCreatorMainPanel* SwCreatorShell::creatorPanel() const {
    return m_creatorPanel;
}

SwCreatorEditorPanel* SwCreatorShell::editorPanel() const {
    return m_editorPanel;
}

void SwCreatorShell::showCreatorPage() {
    setCurrentPage_(PageId::Creator);
}

void SwCreatorShell::showEditorPage() {
    setCurrentPage_(PageId::Editor);
}

void SwCreatorShell::openEditorPath(const SwString& path) {
    if (m_editorPanel) {
        m_editorPanel->openPath(path);
    }
    showEditorPage();
}

bool SwCreatorShell::isEditorPageActive() const {
    return m_stack && m_stack->currentIndex() == pageIndex_(PageId::Editor);
}

bool SwCreatorShell::isCreatorPageActive() const {
    return m_stack && m_stack->currentIndex() == pageIndex_(PageId::Creator);
}

SwSize SwCreatorShell::minimumSizeHint() const {
    SwSize hint = SwWidget::minimumSizeHint();
    const auto& th = SwCreatorTheme::current();
    const SwSize contentMin = m_stack ? static_cast<const SwWidget*>(m_stack)->minimumSizeHint() : SwSize{0, 0};
    const int sidebarBottom = 126 + th.sidebarBtnSize + 12;

    hint.width = std::max(hint.width, th.sidebarWidth + contentMin.width + 2 * kContentInset);
    hint.height = std::max(hint.height, std::max(sidebarBottom, contentMin.height + 2 * kContentInset));
    return hint;
}

void SwCreatorShell::resizeEvent(ResizeEvent* event) {
    SwWidget::resizeEvent(event);
    updateLayout_();
}

void SwCreatorShell::updateLayout_() {
    const auto& th = SwCreatorTheme::current();
    const SwRect r = rect();
    const int shellH = std::max(0, r.height);
    const int contentX = th.sidebarWidth;
    const int contentW = std::max(0, r.width - contentX);
    const int contentH = std::max(0, r.height);

    if (m_sidebarFrame) {
        m_sidebarFrame->move(0, 0);
        m_sidebarFrame->resize(th.sidebarWidth, shellH);
    }

    if (m_brandBadge && m_sidebarFrame) {
        m_brandBadge->move(19, 14);
        m_brandBadge->resize(10, 10);
    }

    if (m_overviewButton) {
        m_overviewButton->move(6, 46);
        m_overviewButton->resize(th.sidebarBtnSize, th.sidebarBtnSize);
    }
    if (m_editorButton) {
        m_editorButton->move(6, 86);
        m_editorButton->resize(th.sidebarBtnSize, th.sidebarBtnSize);
    }
    if (m_creatorButton) {
        m_creatorButton->move(6, 126);
        m_creatorButton->resize(th.sidebarBtnSize, th.sidebarBtnSize);
    }

    if (m_contentFrame) {
        m_contentFrame->move(contentX, 0);
        m_contentFrame->resize(contentW, contentH);
    }
    if (m_stack && m_contentFrame) {
        m_stack->move(kContentInset, kContentInset);
        m_stack->resize(std::max(0, m_contentFrame->width() - 2 * kContentInset),
                        std::max(0, m_contentFrame->height() - 2 * kContentInset));
    }
}

void SwCreatorShell::setCurrentPage_(PageId page) {
    if (!m_stack) {
        return;
    }

    const int nextIndex = pageIndex_(page);
    const bool changed = m_stack->currentIndex() != nextIndex;
    m_stack->setCurrentIndex(nextIndex);

    if (m_overviewButton) {
        m_overviewButton->setChecked(page == PageId::Overview);
    }
    if (m_editorButton) {
        m_editorButton->setChecked(page == PageId::Editor);
    }
    if (m_creatorButton) {
        m_creatorButton->setChecked(page == PageId::Creator);
    }

    if (changed) {
        emit currentPageChanged();
    }
}

int SwCreatorShell::pageIndex_(PageId page) const {
    switch (page) {
    case PageId::Overview:
        return 0;
    case PageId::Editor:
        return 1;
    case PageId::Creator:
    default:
        return 2;
    }
}
