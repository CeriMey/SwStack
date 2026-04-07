#pragma once
/***************************************************************************************************
 * SwCreatorWidgetPalette - left palette with:
 * - Search bar (filter)
 * - Categorized toolbox (SwToolBox)
 **************************************************************************************************/

#include "SwWidget.h"

#include "SwCreatorPaletteEntry.h"

#include <map>
#include <vector>

class SwLineEdit;
class SwToolBox;
class SwScrollArea;
class SwCreatorPalettePage;

class SwCreatorWidgetPalette : public SwWidget {
    SW_OBJECT(SwCreatorWidgetPalette, SwWidget)

public:
    explicit SwCreatorWidgetPalette(SwWidget* parent = nullptr);
    SwSize minimumSizeHint() const override;

signals:
    DECLARE_SIGNAL(entryActivated, SwCreatorPaletteEntry);
    DECLARE_SIGNAL(entryDragStarted, SwCreatorPaletteEntry);
    DECLARE_SIGNAL(entryDragMoved, SwCreatorPaletteEntry, int, int);
    DECLARE_SIGNAL(entryDropped, SwCreatorPaletteEntry, int, int);

protected:
    void resizeEvent(ResizeEvent* event) override;

private:
    void buildUi_();
    void buildEntries_();
    void buildPages_();
    void updateLayout_();
    void applyFilter_(const SwString& text);

    SwLineEdit* m_search{nullptr};
    SwScrollArea* m_toolBoxScroll{nullptr};
    SwToolBox* m_toolBox{nullptr};

    std::vector<SwCreatorPaletteEntry> m_entries;
    std::map<SwString, SwCreatorPalettePage*> m_pages;
};
