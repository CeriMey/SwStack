#pragma once
/***************************************************************************************************
 * SwCreatorFormCanvas - design surface canvas.
 *
 * Responsibilities:
 * - Host and manage "designer-owned" widgets.
 * - Provide click-to-place (MVP) and selection outline.
 **************************************************************************************************/

#include "SwFrame.h"

#include <map>
#include <string>
#include <vector>

class SwGridLayout;
class SwMenu;

class SwCreatorFormCanvas : public SwFrame {
    SW_OBJECT(SwCreatorFormCanvas, SwFrame)

public:
    explicit SwCreatorFormCanvas(SwWidget* parent = nullptr);

    static SwSize defaultFormSize();

    void setFormSize(int width, int height);
    SwSize formSize() const;
    SwSize minimumFormSize() const;

    void setCreateClass(const SwString& className);
    SwString createClass() const;

    SwWidget* createWidgetAt(const SwString& className, int globalX, int globalY);
    SwWidget* createLayoutContainerAt(const SwString& layoutClass, int globalX, int globalY);

    // External/system drag&drop helpers (e.g. palette OS drag).
    void updateDropPreview(int globalX, int globalY, const SwWidget* ignore = nullptr);
    void clearDropPreview();

    // Designer ownership helpers (used by undo/redo, paste, etc.)
    void registerDesignWidget(SwWidget* w);
    // Register without modifying parent layouts (useful for load/deserialize).
    void registerDesignWidgetNoLayout(SwWidget* w);
    bool removeDesignWidget(SwWidget* w);
    bool reparentDesignWidget(SwWidget* w, SwWidget* container);

    void setSelectedWidget(SwWidget* widget);
    SwWidget* selectedWidget() const;

    void setDebugOverlay(bool enabled) { m_debugOverlay = enabled; update(); }
    bool debugOverlay() const { return m_debugOverlay; }

    const std::vector<SwWidget*>& designWidgets() const;

    DECLARE_SIGNAL(widgetAdded, SwWidget*);
    DECLARE_SIGNAL(widgetRemoved, SwWidget*);
    DECLARE_SIGNAL(selectionChanged, SwWidget*);
    DECLARE_SIGNAL_VOID(designWidgetsChanged);

    // Edit requests (handled by the host panel / app).
    DECLARE_SIGNAL_VOID(requestUndo);
    DECLARE_SIGNAL_VOID(requestRedo);
    DECLARE_SIGNAL_VOID(requestCut);
    DECLARE_SIGNAL_VOID(requestCopy);
    DECLARE_SIGNAL_VOID(requestPaste);
    DECLARE_SIGNAL_VOID(requestDelete);

protected:
    void paintEvent(PaintEvent* event) override;
    void mousePressEvent(MouseEvent* event) override;

 private:
    class DesignOverlay;
    class RegistryOverlay;
    class RegistryPopup;

    std::string nextObjectNameForClass_(const SwString& className);
    static void defaultSizeFor_(SwWidget* w, const SwString& className);
    SwWidget* createWidgetAt_(const SwString& className, int globalX, int globalY);
    SwWidget* createLayoutContainerAt_(const SwString& layoutClass, int globalX, int globalY);
    SwWidget* layoutTarget_() const;
    SwWidget* designWidgetFromHit_(SwWidget* hit) const;
    static bool isContainerWidget_(const SwWidget* w);
    SwWidget* findContainerAt_(int globalX, int globalY, const SwWidget* ignore) const;
    SwWidget* dropContentParent_(SwWidget* container);
    void showContextMenu_(int globalX, int globalY);
    void ensureRegistryPopup_();
    void showRegistryPopup_(int globalX, int globalY);
    void hideRegistryPopup_();
    void updateRegistryPopupItems_();
    void applyLayout_(const SwString& layoutName);
    void breakLayout_();
    void registerDesignWidget_(SwWidget* w, bool attachToParentLayout);
    static void detachFromParentLayout_(SwWidget* parent, SwWidget* w);
    static void attachToParentLayout_(SwWidget* parent, SwWidget* w);
    SwSize computeMinimumFormSize_() const;

    SwString m_createClass;
    SwWidget* m_selected{nullptr};
    std::vector<SwWidget*> m_designWidgets;
    std::map<std::string, int> m_classCounters;

    SwWidget* m_designOverlay{nullptr};
    SwWidget* m_dropTarget{nullptr};
    bool m_debugOverlay{false};
    SwMenu* m_contextMenu{nullptr};

    // Layout drag-reorder indicator: a rect (line) showing where the widget will be inserted.
    SwRect m_dropIndicatorRect{0, 0, 0, 0};
    SwWidget* m_dropIndicatorParent{nullptr};

    RegistryOverlay* m_registryOverlay{nullptr};
    RegistryPopup* m_registryPopup{nullptr};
};
