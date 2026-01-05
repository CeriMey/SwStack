#pragma once

#include "SwCreatorPaletteEntry.h"

#include <vector>

namespace swcreator {

inline SwCreatorPaletteEntry makeWidgetEntry(const char* category, const char* className) {
    SwCreatorPaletteEntry e;
    e.category = SwString(category);
    e.className = SwString(className);
    e.displayName = SwString(className);
    e.isLayout = false;
    return e;
}

inline SwCreatorPaletteEntry makeLayoutEntry(const char* category, const char* layoutName) {
    SwCreatorPaletteEntry e;
    e.category = SwString(category);
    e.className = SwString(layoutName);
    e.displayName = SwString(layoutName);
    e.isLayout = true;
    return e;
}

inline std::vector<SwCreatorPaletteEntry> widgetRegistryEntries() {
    std::vector<SwCreatorPaletteEntry> entries;
    entries.reserve(64);

    // Basic
    entries.push_back(makeWidgetEntry("Basic", "SwPushButton"));
    entries.push_back(makeWidgetEntry("Basic", "SwToolButton"));
    entries.push_back(makeWidgetEntry("Basic", "SwLabel"));
    entries.push_back(makeWidgetEntry("Basic", "SwLineEdit"));
    entries.push_back(makeWidgetEntry("Basic", "SwCheckBox"));
    entries.push_back(makeWidgetEntry("Basic", "SwRadioButton"));
    entries.push_back(makeWidgetEntry("Basic", "SwProgressBar"));

    // Input
    entries.push_back(makeWidgetEntry("Input", "SwComboBox"));
    entries.push_back(makeWidgetEntry("Input", "SwSpinBox"));
    entries.push_back(makeWidgetEntry("Input", "SwDoubleSpinBox"));
    entries.push_back(makeWidgetEntry("Input", "SwSlider"));
    entries.push_back(makeWidgetEntry("Input", "SwPlainTextEdit"));
    entries.push_back(makeWidgetEntry("Input", "SwTextEdit"));

    // Containers
    entries.push_back(makeWidgetEntry("Containers", "SwFrame"));
    entries.push_back(makeWidgetEntry("Containers", "SwGroupBox"));
    entries.push_back(makeWidgetEntry("Containers", "SwScrollArea"));
    entries.push_back(makeWidgetEntry("Containers", "SwTabWidget"));
    entries.push_back(makeWidgetEntry("Containers", "SwStackedWidget"));
    entries.push_back(makeWidgetEntry("Containers", "SwSplitter"));

    // Views
    entries.push_back(makeWidgetEntry("Views", "SwTableWidget"));
    entries.push_back(makeWidgetEntry("Views", "SwTreeWidget"));
    entries.push_back(makeWidgetEntry("Views", "SwTableView"));
    entries.push_back(makeWidgetEntry("Views", "SwTreeView"));

    // Layouts (not widgets): represented as palette entries.
    entries.push_back(makeLayoutEntry("Layouts", "SwVerticalLayout"));
    entries.push_back(makeLayoutEntry("Layouts", "SwHorizontalLayout"));
    entries.push_back(makeLayoutEntry("Layouts", "SwGridLayout"));
    entries.push_back(makeLayoutEntry("Layouts", "SwFormLayout"));

    return entries;
}

inline SwStringList widgetRegistryCategories() {
    return SwStringList{SwString("Basic"), SwString("Input"), SwString("Containers"), SwString("Views"), SwString("Layouts")};
}

} // namespace swcreator

