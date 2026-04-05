#pragma once

/**
 * @file src/core/installer/SwInstallerShellDialog.h
 * @brief Dark, modern shell dialog for the Sw installer wizard.
 */

#include "SwDialog.h"
#include "SwFrame.h"
#include "SwLabel.h"
#include "SwList.h"
#include "SwPushButton.h"
#include "SwStackedWidget.h"
#include "SwWizard.h"

#include <functional>

namespace swinstaller {

class SwInstallerShellDialog : public SwDialog {
    SW_OBJECT(SwInstallerShellDialog, SwDialog)

public:
    explicit SwInstallerShellDialog(const SwString& productName,
                                    bool installed,
                                    SwWidget* parent = nullptr)
        : SwDialog(parent),
          productName_(productName),
          installed_(installed) {
        resize(880, 560);
        setFrameShape(SwFrame::Shape::StyledPanel);
        setStyleSheet("SwDialog { background-color: rgb(25, 25, 25); "
                      "border-color: rgb(55, 55, 55); border-width: 1px; border-radius: 10px; }");
        buildUi_();
    }

    int addPage(SwWizardPage* page) {
        if (!page || !stack_) return -1;

        pages_.append(page);
        stack_->addWidget(page);

        SidebarStepItem_* item = new SidebarStepItem_(stepsContainer_);
        item->setStep(static_cast<int>(pages_.size()), page->title());
        stepItems_.append(item);

        if (pages_.size() == 1) {
            currentIndex_ = 0;
            stack_->setCurrentIndex(0);
        }

        updateUi_();
        layoutShell_();
        return static_cast<int>(pages_.size()) - 1;
    }

    int currentId() const { return currentIndex_; }

    void setCurrentId(int id) { setCurrentIndex_(id, false); }

    SwWizardPage* currentPage() const {
        if (currentIndex_ < 0 || currentIndex_ >= pages_.size()) return nullptr;
        return pages_[currentIndex_];
    }

    void next() {
        if (currentIndex_ + 1 < pages_.size()) setCurrentIndex_(currentIndex_ + 1, true);
    }

    void back() {
        if (currentIndex_ > 0) setCurrentIndex_(currentIndex_ - 1, true);
    }

    void setCurrentIdChangedHandler(std::function<void(int)> handler) {
        currentIdChangedHandler_ = std::move(handler);
    }

    void setAbortVisible(bool visible) {
        if (abortBtn_) {
            abortBtn_->setVisible(visible);
            if (visible) {
                nextBtn_->setVisible(false);
            }
            layoutShell_();
        }
    }

    void setNextButtonText(const SwString& text) {
        if (nextBtn_) {
            nextBtn_->setText(text);
        }
    }

    void setNextButtonVisible(bool visible) {
        if (nextBtn_) {
            nextBtn_->setVisible(visible);
            layoutShell_();
        }
    }

protected:
    void resizeEvent(ResizeEvent* event) override {
        SwDialog::resizeEvent(event);
        layoutShell_();
    }

private:

    /* ─── Sidebar step indicator ──────────────────────────────── */

    class SidebarStepItem_ : public SwFrame {
        SW_OBJECT(SidebarStepItem_, SwFrame)

    public:
        explicit SidebarStepItem_(SwWidget* parent = nullptr)
            : SwFrame(parent) {
            setFrameShape(SwFrame::Shape::StyledPanel);
            setStyleSheet("SwFrame { background-color: rgba(0,0,0,0); "
                          "border-width: 0px; }");

            badge_ = new SwLabel("1", this);
            badge_->setAlignment(DrawTextFormats(DrawTextFormat::Center |
                                                  DrawTextFormat::VCenter |
                                                  DrawTextFormat::SingleLine));

            title_ = new SwLabel("", this);
            title_->setAlignment(DrawTextFormats(DrawTextFormat::Left |
                                                  DrawTextFormat::VCenter |
                                                  DrawTextFormat::SingleLine));

            applyState_(false, false);
        }

        void setStep(int stepNumber, const SwString& caption) {
            stepNumber_ = stepNumber;
            if (title_) title_->setText(caption);
            updateBadge_();
        }

        void setVisualState(bool current, bool completed) {
            applyState_(current, completed);
        }

    protected:
        void resizeEvent(ResizeEvent* event) override {
            SwFrame::resizeEvent(event);
            const int h = height();
            const int sz = 28;
            badge_->move(0, (h - sz) / 2);
            badge_->resize(sz, sz);
            title_->move(36, 0);
            title_->resize(width() - 40, h);
        }

    private:
        void updateBadge_() {
            if (!badge_) return;
            if (completed_) {
                badge_->setText("\xe2\x9c\x93");
                return;
            }
            badge_->setText(SwString::number(stepNumber_));
        }

        void applyState_(bool current, bool completed) {
            current_ = current;
            completed_ = completed;
            updateBadge_();

            if (current_) {
                badge_->setStyleSheet("SwLabel { background-color: rgb(0, 120, 212); "
                                      "border-color: rgb(0, 120, 212); border-width: 1px; "
                                      "border-radius: 14px; color: rgb(255, 255, 255); "
                                      "font-size: 12px; font-weight: bold; }");
                title_->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; "
                                      "color: rgb(255, 255, 255); font-size: 13px; font-weight: bold; }");
                return;
            }

            if (completed_) {
                badge_->setStyleSheet("SwLabel { background-color: rgb(38, 79, 64); "
                                      "border-color: rgb(38, 79, 64); border-width: 1px; "
                                      "border-radius: 14px; color: rgb(75, 185, 145); "
                                      "font-size: 14px; font-weight: bold; }");
                title_->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; "
                                      "color: rgb(170, 170, 170); font-size: 13px; }");
                return;
            }

            /* pending */
            badge_->setStyleSheet("SwLabel { background-color: rgb(50, 50, 52); "
                                  "border-color: rgb(50, 50, 52); border-width: 1px; "
                                  "border-radius: 14px; color: rgb(120, 120, 124); "
                                  "font-size: 12px; }");
            title_->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; "
                                  "color: rgb(110, 110, 114); font-size: 13px; }");
        }

        int stepNumber_{1};
        bool current_{false};
        bool completed_{false};
        SwLabel* badge_{nullptr};
        SwLabel* title_{nullptr};
    };

    /* ─── Build ───────────────────────────────────────────────── */

    void buildUi_() {
        setWindowTitle(productName_ + (installed_ ? SwString(" Maintenance") : SwString(" Setup")));

        auto* content = contentWidget();
        auto* bar = buttonBarWidget();
        if (!content || !bar) return;

        content->setStyleSheet("SwWidget { background-color: rgb(25, 25, 25); border-width: 0px; }");
        bar->setStyleSheet("SwWidget { background-color: rgb(30, 30, 32); "
                           "border-color: rgb(48, 48, 50); border-width: 1px 0 0 0; }");

        /* ── Sidebar ──────────────────────────────────── */
        sidebar_ = new SwFrame(content);
        sidebar_->setFrameShape(SwFrame::Shape::StyledPanel);
        sidebar_->setStyleSheet("SwFrame { background-color: rgb(30, 30, 32); "
                                "border-color: rgb(48, 48, 50); border-width: 0px 1px 0px 0px; "
                                "border-radius: 0px; }");

        brandTitle_ = new SwLabel(productName_, sidebar_);
        brandTitle_->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; "
                                   "color: rgb(240, 240, 240); font-size: 15px; font-weight: bold; }");
        brandTitle_->setAlignment(DrawTextFormats(DrawTextFormat::Left |
                                                  DrawTextFormat::VCenter |
                                                  DrawTextFormat::SingleLine));

        brandTag_ = new SwLabel(installed_ ? "Maintenance" : "Setup", sidebar_);
        brandTag_->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; "
                                 "color: rgb(90, 90, 94); font-size: 11px; }");
        brandTag_->setAlignment(DrawTextFormats(DrawTextFormat::Left |
                                                DrawTextFormat::VCenter |
                                                DrawTextFormat::SingleLine));

        /* Separator under brand */
        brandSep_ = new SwFrame(sidebar_);
        brandSep_->setFrameShape(SwFrame::Shape::HLine);
        brandSep_->setStyleSheet("SwFrame { background-color: rgba(0,0,0,0); "
                                 "border-color: rgb(48, 48, 50); border-width: 1px; }");

        stepsContainer_ = new SwFrame(sidebar_);
        stepsContainer_->setFrameShape(SwFrame::Shape::StyledPanel);
        stepsContainer_->setStyleSheet("SwFrame { background-color: rgba(0,0,0,0); border-width: 0px; }");

        /* ── Content panel ────────────────────────────── */
        contentPanel_ = new SwFrame(content);
        contentPanel_->setFrameShape(SwFrame::Shape::StyledPanel);
        contentPanel_->setStyleSheet("SwFrame { background-color: rgb(25, 25, 25); "
                                     "border-width: 0px; }");

        pageTitle_ = new SwLabel("", contentPanel_);
        pageTitle_->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; "
                                  "color: rgb(240, 240, 240); font-size: 20px; font-weight: bold; }");

        pageSubtitle_ = new SwLabel("", contentPanel_);
        pageSubtitle_->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; "
                                     "color: rgb(140, 140, 144); font-size: 12px; }");
        pageSubtitle_->setAlignment(DrawTextFormats(DrawTextFormat::Left |
                                                    DrawTextFormat::Top |
                                                    DrawTextFormat::WordBreak));

        /* Separator line under title */
        titleSep_ = new SwFrame(contentPanel_);
        titleSep_->setFrameShape(SwFrame::Shape::HLine);
        titleSep_->setStyleSheet("SwFrame { background-color: rgba(0,0,0,0); "
                                 "border-color: rgb(48, 48, 50); border-width: 1px; }");

        /* Content card */
        pageCard_ = new SwFrame(contentPanel_);
        pageCard_->setFrameShape(SwFrame::Shape::StyledPanel);
        pageCard_->setStyleSheet("SwFrame { background-color: rgb(32, 32, 34); "
                                 "border-color: rgb(48, 48, 50); border-width: 1px; "
                                 "border-radius: 8px; }");

        stack_ = new SwStackedWidget(pageCard_);
        stack_->setStyleSheet("SwStackedWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");

        /* ── Footer buttons ──────────────────────────── */
        cancelBtn_ = new SwPushButton("Cancel", bar);
        backBtn_ = new SwPushButton("Back", bar);
        nextBtn_ = new SwPushButton("Next", bar);
        finishBtn_ = new SwPushButton("Finish", bar);
        abortBtn_ = new SwPushButton("Abort", bar);

        styleSecondary_(cancelBtn_);
        styleSecondary_(backBtn_);
        stylePrimary_(nextBtn_);
        stylePrimary_(finishBtn_);
        styleDanger_(abortBtn_);

        cancelBtn_->resize(90, 32);
        backBtn_->resize(90, 32);
        nextBtn_->resize(90, 32);
        finishBtn_->resize(90, 32);
        abortBtn_->resize(90, 32);
        abortBtn_->setVisible(false);

        SwObject::connect(cancelBtn_, &SwPushButton::clicked, this, [this]() { reject(); });
        SwObject::connect(backBtn_,   &SwPushButton::clicked, this, [this]() { back(); });
        SwObject::connect(nextBtn_,   &SwPushButton::clicked, this, [this]() { next(); });
        SwObject::connect(finishBtn_, &SwPushButton::clicked, this, [this]() { accept(); });
        SwObject::connect(abortBtn_,  &SwPushButton::clicked, this, [this]() { reject(); });

        updateUi_();
        layoutShell_();
    }

    /* ─── Button styles ───────────────────────────────────────── */

    static void stylePrimary_(SwPushButton* b) {
        if (!b) return;
        b->setStyleSheet("SwPushButton { background-color: rgb(0, 120, 212); "
                          "border-color: rgb(0, 120, 212); border-width: 1px; border-radius: 6px; "
                          "color: rgb(255, 255, 255); font-size: 13px; font-weight: bold; } "
                          "SwPushButton:hover { background-color: rgb(16, 132, 220); } "
                          "SwPushButton:pressed { background-color: rgb(14, 99, 156); }");
    }

    static void styleDanger_(SwPushButton* b) {
        if (!b) return;
        b->setStyleSheet("SwPushButton { background-color: rgb(50, 20, 20); "
                          "border-color: rgb(200, 50, 50); border-width: 1px; border-radius: 6px; "
                          "color: rgb(240, 80, 80); font-size: 13px; font-weight: bold; } "
                          "SwPushButton:hover { background-color: rgb(70, 25, 25); } "
                          "SwPushButton:pressed { background-color: rgb(90, 30, 30); }");
    }

    static void styleSecondary_(SwPushButton* b) {
        if (!b) return;
        b->setStyleSheet("SwPushButton { background-color: rgb(44, 44, 46); "
                          "border-color: rgb(60, 60, 64); border-width: 1px; border-radius: 6px; "
                          "color: rgb(200, 200, 200); font-size: 13px; } "
                          "SwPushButton:hover { background-color: rgb(55, 55, 58); } "
                          "SwPushButton:pressed { background-color: rgb(65, 65, 68); }");
    }

    /* ─── Navigation ──────────────────────────────────────────── */

    void setCurrentIndex_(int index, bool notify) {
        if (index < 0 || index >= pages_.size() || index == currentIndex_) return;
        currentIndex_ = index;
        if (stack_) stack_->setCurrentIndex(index);
        updateUi_();
        layoutShell_();
        if (notify && currentIdChangedHandler_) currentIdChangedHandler_(currentIndex_);
    }

    void updateUi_() {
        const int count = static_cast<int>(pages_.size());
        const bool hasPages = count > 0 && currentIndex_ >= 0 && currentIndex_ < count;
        const bool first = currentIndex_ <= 0;
        const bool last = hasPages && currentIndex_ == count - 1;

        for (size_t i = 0; i < stepItems_.size(); ++i) {
            if (stepItems_[i])
                stepItems_[i]->setVisualState(static_cast<int>(i) == currentIndex_,
                                              static_cast<int>(i) < currentIndex_);
        }

        if (hasPages) {
            SwWizardPage* page = pages_[currentIndex_];
            pageTitle_->setText(page ? page->title() : SwString());
            pageSubtitle_->setText(page ? page->subTitle() : SwString());
            pageSubtitle_->setVisible(page && !page->subTitle().isEmpty());
        } else {
            pageTitle_->setText(productName_);
            pageSubtitle_->setText(SwString());
            pageSubtitle_->setVisible(false);
        }

        backBtn_->setVisible(!first && hasPages);
        nextBtn_->setVisible(hasPages && !last);
        finishBtn_->setVisible(hasPages && last);
    }

    /* ─── Layout ──────────────────────────────────────────────── */

    void layoutShell_() {
        auto* content = contentWidget();
        auto* bar = buttonBarWidget();
        if (!content || !bar || !sidebar_ || !contentPanel_) return;

        const int cw = content->width();
        const int ch = content->height();
        const int sw = 220;

        /* Sidebar */
        sidebar_->move(0, 0);
        sidebar_->resize(sw, ch);

        brandTitle_->move(20, 18);
        brandTitle_->resize(sw - 40, 22);

        brandTag_->move(20, 42);
        brandTag_->resize(sw - 40, 16);

        brandSep_->move(20, 66);
        brandSep_->resize(sw - 40, 1);

        stepsContainer_->move(0, 76);
        stepsContainer_->resize(sw, ch - 76);

        const int stepH = 36;
        const int stepGap = 2;
        int sy = 8;
        for (size_t i = 0; i < stepItems_.size(); ++i) {
            if (!stepItems_[i]) continue;
            stepItems_[i]->move(16, sy);
            stepItems_[i]->resize(sw - 32, stepH);
            sy += stepH + stepGap;
        }

        /* Content panel */
        contentPanel_->move(sw, 0);
        contentPanel_->resize(cw - sw, ch);

        const int px = 32;
        const int pw = cw - sw - (2 * px);

        pageTitle_->move(px, 20);
        pageTitle_->resize(pw, 28);

        const bool hasSub = pageSubtitle_->getVisible();
        if (hasSub) {
            pageSubtitle_->move(px, 52);
            pageSubtitle_->resize(pw, 20);
        }

        const int sepY = hasSub ? 80 : 58;
        titleSep_->move(px, sepY);
        titleSep_->resize(pw, 1);

        const int cardY = sepY + 16;
        int cardH = ch - cardY - 12;
        if (cardH < 0) cardH = 0;
        pageCard_->move(px, cardY);
        pageCard_->resize(pw, cardH);

        const int cardPad = 24;
        stack_->move(cardPad, cardPad);
        stack_->resize(pw - 2 * cardPad, cardH - 2 * cardPad);

        /* Footer buttons */
        const int by = (bar->height() - 32) / 2;
        cancelBtn_->move(20, by);

        int bx = bar->width() - 20;
        if (abortBtn_->getVisible()) {
            bx -= abortBtn_->width();
            abortBtn_->move(bx, by);
            bx -= 8;
        }
        if (finishBtn_->getVisible()) {
            bx -= finishBtn_->width();
            finishBtn_->move(bx, by);
            bx -= 8;
        }
        if (nextBtn_->getVisible()) {
            bx -= nextBtn_->width();
            nextBtn_->move(bx, by);
            bx -= 8;
        }
        if (backBtn_->getVisible()) {
            bx -= backBtn_->width();
            backBtn_->move(bx, by);
        }
    }

    /* ─── Members ─────────────────────────────────────────────── */

    SwString productName_;
    bool installed_{false};

    SwFrame* sidebar_{nullptr};
    SwLabel* brandTitle_{nullptr};
    SwLabel* brandTag_{nullptr};
    SwFrame* brandSep_{nullptr};
    SwFrame* stepsContainer_{nullptr};
    SwList<SidebarStepItem_*> stepItems_;

    SwFrame* contentPanel_{nullptr};
    SwLabel* pageTitle_{nullptr};
    SwLabel* pageSubtitle_{nullptr};
    SwFrame* titleSep_{nullptr};
    SwFrame* pageCard_{nullptr};
    SwStackedWidget* stack_{nullptr};

    SwPushButton* cancelBtn_{nullptr};
    SwPushButton* backBtn_{nullptr};
    SwPushButton* nextBtn_{nullptr};
    SwPushButton* finishBtn_{nullptr};
    SwPushButton* abortBtn_{nullptr};

    SwList<SwWizardPage*> pages_;
    int currentIndex_{0};
    std::function<void(int)> currentIdChangedHandler_;
};

} // namespace swinstaller
