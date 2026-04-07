#pragma once
/***************************************************************************************************
 * SwCreatorShell - high-level shell around the editor + embedded designer.
 *
 * - Left: compact icon sidebar
 * - Right: stacked content pages
 * - The editor and the existing creator each live as a page inside the stack
 **************************************************************************************************/

#include "SwWidget.h"

class SwFrame;
class SwStackedWidget;
class SwToolButton;
class SwWidget;

class SwCreatorMainPanel;
class SwCreatorEditorPanel;

class SwCreatorShell : public SwWidget {
    SW_OBJECT(SwCreatorShell, SwWidget)

public:
    explicit SwCreatorShell(SwWidget* parent = nullptr);

    SwCreatorMainPanel* creatorPanel() const;
    SwCreatorEditorPanel* editorPanel() const;
    void showCreatorPage();
    void showEditorPage();
    void openEditorPath(const SwString& path);
    bool isEditorPageActive() const;
    bool isCreatorPageActive() const;
    SwSize minimumSizeHint() const override;

signals:
    DECLARE_SIGNAL_VOID(currentPageChanged);

protected:
    void resizeEvent(ResizeEvent* event) override;

private:
    enum class PageId {
        Overview = 0,
        Editor = 1,
        Creator = 2
    };

    void updateLayout_();
    void setCurrentPage_(PageId page);
    int pageIndex_(PageId page) const;

    SwFrame* m_sidebarFrame{nullptr};
    SwFrame* m_brandBadge{nullptr};
    SwToolButton* m_overviewButton{nullptr};
    SwToolButton* m_editorButton{nullptr};
    SwToolButton* m_creatorButton{nullptr};

    SwFrame* m_contentFrame{nullptr};
    SwStackedWidget* m_stack{nullptr};
    SwWidget* m_overviewPage{nullptr};
    SwCreatorEditorPanel* m_editorPanel{nullptr};
    SwCreatorMainPanel* m_creatorPanel{nullptr};
};
