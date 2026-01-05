#pragma once
/***************************************************************************************************
 * SwCreatorDockDialog
 *
 * Small reusable "dockable" dialog helper for SwCreator:
 * - In-client (non native window) dialog.
 * - Optional transparent overlay to keep it on top and to close on outside click.
 * - Opens near an anchor rect (ex: property row button) and clamps to root bounds.
 **************************************************************************************************/

#include "SwDialog.h"
#include "SwWidget.h"

class SwCreatorDockDialog : public SwDialog {
    SW_OBJECT(SwCreatorDockDialog, SwDialog)

public:
    enum class DockSide {
        Auto,
        Right,
        Left,
    };

    explicit SwCreatorDockDialog(SwWidget* parent = nullptr);

    void setCloseOnOutsideClick(bool on);
    bool closeOnOutsideClick() const;

    bool isDockedOpen() const;

    void openDocked(SwObject* startForRoot, const SwRect& anchorRect, DockSide side = DockSide::Auto, int gapPx = 12);
    void closeDocked();

protected:
    void resizeEvent(ResizeEvent* event) override;
    void keyPressEvent(KeyEvent* event) override;

private:
    class DockOverlay;

    static SwWidget* findRootWidget_(SwObject* start);
    void ensureOverlay_(SwWidget* root);
    void updateOverlayGeometry_();
    void reposition_();

    SwWidget* m_root{nullptr};
    DockOverlay* m_overlay{nullptr};
    SwWidget* m_overlayRootConnected{nullptr};

    SwRect m_anchorRect{};
    DockSide m_side{DockSide::Auto};
    int m_gapPx{12};
    bool m_closeOnOutsideClick{true};
    bool m_dockedOpen{false};
};
