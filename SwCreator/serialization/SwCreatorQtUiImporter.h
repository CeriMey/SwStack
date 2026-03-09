#pragma once
/***************************************************************************************************
 * SwCreatorQtUiImporter - converts Qt Designer .ui XML to .swui XML for SwCreator.
 *
 * Parses the Qt Designer XML format (<ui version="4.0">) and produces a .swui document
 * compatible with SwCreatorSwuiSerializer::deserializeCanvas.
 *
 * Handles:
 *   - Qt → Sw class name mapping
 *   - Property filtering and renaming (geometry, text, styleSheet, etc.)
 *   - Layout flattening: widgets nested inside <layout>/<item> become direct children
 *   - Automatic geometry estimation for widgets that lack a <geometry> property
 *   - QMainWindow centralWidget promotion
 *   - Spacer elements are preserved as real spacer items
 **************************************************************************************************/

#include "SwString.h"

class SwCreatorFormCanvas;

class SwCreatorQtUiImporter {
public:
    // Convert Qt .ui XML string to .swui XML that can be loaded by
    // SwCreatorSwuiSerializer::deserializeCanvas.
    // Returns an empty string and sets *outError on failure.
    static SwString importFromQtUi(const SwString& qtUiXml, SwString* outError = nullptr);

    // Convenience: parse qtUiXml and load the result directly into canvas.
    static bool importToCanvas(const SwString& qtUiXml, SwCreatorFormCanvas* canvas,
                               SwString* outError = nullptr);
};
