#pragma once
/***************************************************************************************************
 * SwCreatorPaletteItem - one clickable entry with an icon + label.
 **************************************************************************************************/

#include "SwWidget.h"

#include "SwCreatorPaletteEntry.h"

class SwCreatorPaletteItem : public SwWidget {
    SW_OBJECT(SwCreatorPaletteItem, SwWidget)

public:
    explicit SwCreatorPaletteItem(const SwCreatorPaletteEntry& entry, SwWidget* parent = nullptr);

    SwCreatorPaletteEntry entry() const;

    void setSelected(bool on);
    bool isSelected() const;

signals:
    DECLARE_SIGNAL(clicked, SwCreatorPaletteEntry);
    DECLARE_SIGNAL(dragStarted, SwCreatorPaletteEntry);
    DECLARE_SIGNAL(dragMoved, SwCreatorPaletteEntry, int, int);
    DECLARE_SIGNAL(dragDropped, SwCreatorPaletteEntry, int, int);

protected:
    void paintEvent(PaintEvent* event) override;
    void mousePressEvent(MouseEvent* event) override;
    void mouseReleaseEvent(MouseEvent* event) override;
    void mouseMoveEvent(MouseEvent* event) override;

private:
    static void drawIcon_(SwPainter* painter, const SwRect& rect, const SwCreatorPaletteEntry& entry);
    SwWidget* rootWidget_() const;
    SwPoint rootWindowClientPos_(int localX, int localY) const;

    SwCreatorPaletteEntry m_entry;
    bool m_hover{false};
    bool m_pressed{false};
    bool m_dragging{false};
    bool m_selected{false};
    int m_pressX{0};
    int m_pressY{0};
};
