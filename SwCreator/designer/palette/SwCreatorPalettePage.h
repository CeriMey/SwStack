#pragma once
/***************************************************************************************************
 * SwCreatorPalettePage - category page used inside the palette toolbox.
 *
 * Displays a vertical list of entries (widgets or layouts) with a filter.
 **************************************************************************************************/

#include "SwWidget.h"

#include "SwCreatorPaletteEntry.h"

#include <vector>

class SwCreatorPaletteItem;

class SwCreatorPalettePage : public SwWidget {
    SW_OBJECT(SwCreatorPalettePage, SwWidget)

public:
    explicit SwCreatorPalettePage(SwWidget* parent = nullptr);

    void setEntries(const std::vector<SwCreatorPaletteEntry>& entries);
    void setFilterText(const SwString& text);
    void refreshLayout();

signals:
    DECLARE_SIGNAL(entryActivated, SwCreatorPaletteEntry);
    DECLARE_SIGNAL(entryDragStarted, SwCreatorPaletteEntry);
    DECLARE_SIGNAL(entryDragMoved, SwCreatorPaletteEntry, int, int);
    DECLARE_SIGNAL(entryDropped, SwCreatorPaletteEntry, int, int);

protected:
    void resizeEvent(ResizeEvent* event) override;
    void newParentEvent(SwObject* parent) override;
    SwSize sizeHint() const override;
    SwSize minimumSizeHint() const override;

private:
    void rebuildButtons_();
    void updateLayout_();
    bool entryVisible_(const SwCreatorPaletteEntry& e) const;
    void onParentResized_(int, int);

    std::vector<SwCreatorPaletteEntry> m_entries;
    std::vector<SwCreatorPaletteItem*> m_buttons;
    SwString m_filter;
    int m_itemHeight{24};
    int m_itemGap{0};
    int m_pad{0};
    bool m_inUpdateLayout{false};
    SwWidget* m_resizeParent{nullptr};
};
