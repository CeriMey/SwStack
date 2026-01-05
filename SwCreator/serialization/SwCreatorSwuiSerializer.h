#pragma once
/***************************************************************************************************
 * SwCreatorSwuiSerializer - minimal serializer for clipboard/undo operations.
 *
 * Format: <swui><widget class="..." name="..."> ... </widget></swui>
 * Designed to be compatible with swui::UiLoader.
 **************************************************************************************************/

#include "SwString.h"

class SwWidget;
class SwCreatorFormCanvas;

class SwCreatorSwuiSerializer {
public:
    static SwString serializeWidget(const SwWidget* widget);

    static SwWidget* deserializeWidget(const SwString& xml, SwWidget* parent, SwString* outError = nullptr);

    // SwCreator document (canvas) I/O helpers.
    // Notes:
    // - Uses a synthetic root widget; loading flattens it into the canvas.
    // - Stores a designer marker property (__SwCreator_DesignWidget) to re-register design widgets after load.
    static SwString serializeCanvas(const SwCreatorFormCanvas* canvas);

    static bool deserializeCanvas(const SwString& xml, SwCreatorFormCanvas* canvas, SwString* outError = nullptr);
};
