#pragma once

#include <algorithm>

#include <QColor>
#include <QString>

#include "Sw.h"
#include "SwString.h"

static inline SwString toSwString(const QString& value) {
    return SwString::fromUtf8(value.toUtf8().constData());
}

static inline QString toQString(const SwString& value) {
    return QString::fromUtf8(value.toStdString().c_str());
}

struct InkColorDef {
    const char* name;
    int red;
    int green;
    int blue;
};

static constexpr InkColorDef kInkPalette[] = {
    {"Azure", 44, 124, 255},
    {"Coral", 255, 121, 85},
    {"Mint", 48, 196, 141},
    {"Amber", 255, 202, 88},
    {"Violet", 161, 122, 255}
};

static constexpr int kInkPaletteCount = static_cast<int>(sizeof(kInkPalette) / sizeof(kInkPalette[0]));

static inline int clampInkIndex(int index) {
    return std::max(0, std::min(index, kInkPaletteCount - 1));
}

static inline QColor inkQColor(int index) {
    const InkColorDef& ink = kInkPalette[clampInkIndex(index)];
    return QColor(ink.red, ink.green, ink.blue);
}

static inline SwColor inkSwColor(int index) {
    const InkColorDef& ink = kInkPalette[clampInkIndex(index)];
    return SwColor{ink.red, ink.green, ink.blue};
}

static inline QString inkName(int index) {
    return QString::fromLatin1(kInkPalette[clampInkIndex(index)].name);
}

static inline SwString inkNameSw(int index) {
    return SwString(kInkPalette[clampInkIndex(index)].name);
}

static inline QString inkLabelText(int index) {
    return QStringLiteral("Ink: %1").arg(inkName(index));
}

static inline SwString inkLabelTextSw(int index) {
    return SwString("Ink: ") + inkNameSw(index);
}

struct Example42Rect {
    int x;
    int y;
    int width;
    int height;
};

struct Example42PaneLayout {
    Example42Rect title;
    Example42Rect subtitle;
    Example42Rect status;
    Example42Rect runtime;
    Example42Rect bridgeButton;
    Example42Rect fiberButton;
    Example42Rect lineEdit;
    Example42Rect paletteLabel;
    Example42Rect currentInkLabel;
    Example42Rect clearButton;
    Example42Rect canvas;
};

static inline int example42OuterMargin() {
    return 24;
}

static inline int example42Spacing() {
    return 12;
}

static inline int example42InlineGap() {
    return 12;
}

static inline int example42TitleHeight() {
    return 28;
}

static inline int example42SubtitleHeight() {
    return 20;
}

static inline int example42StatusHeight() {
    return 22;
}

static inline int example42RuntimeHeight() {
    return 20;
}

static inline int example42BridgeButtonHeight() {
    return 42;
}

static inline int example42FiberButtonHeight() {
    return 34;
}

static inline int example42LineEditHeight() {
    return 38;
}

static inline int example42PaletteRowHeight() {
    return 22;
}

static inline int example42PaletteGap() {
    return 10;
}

static inline int example42SwatchSize() {
    return 30;
}

static inline int example42SwatchStep() {
    return 38;
}

static inline int example42ClearButtonWidth() {
    return 124;
}

static inline int example42ClearButtonHeight() {
    return 34;
}

static inline int example42PaletteLabelWidth() {
    return 100;
}

static inline int example42CurrentInkLabelWidth() {
    return 160;
}

static inline int example42ContentMinimumWidth() {
    return 180;
}

static inline int example42CanvasMinimumWidth() {
    return 220;
}

static inline int example42CanvasMinimumHeight() {
    return 220;
}

static inline int example42PaneMinimumWidth() {
    const int swatchesWidth = kInkPaletteCount <= 0
                                  ? 0
                                  : (kInkPaletteCount * example42SwatchSize()) + ((kInkPaletteCount - 1) * (example42SwatchStep() - example42SwatchSize()));
    const int paletteHeaderWidth = example42PaletteLabelWidth() + example42InlineGap() + example42CurrentInkLabelWidth();
    const int paletteActionWidth = swatchesWidth + example42InlineGap() + example42ClearButtonWidth();
    const int contentWidth = std::max(example42ContentMinimumWidth(), std::max(paletteHeaderWidth, paletteActionWidth));
    return (example42OuterMargin() * 2) + contentWidth;
}

static inline int example42PaneMinimumHeight() {
    const int outer = example42OuterMargin() * 2;
    const int spacing = example42Spacing();
    return outer
           + example42TitleHeight()
           + example42SubtitleHeight() + 6
           + example42StatusHeight() + spacing
           + example42RuntimeHeight() + spacing
           + example42BridgeButtonHeight() + spacing
           + example42FiberButtonHeight() + spacing
           + example42LineEditHeight() + spacing
           + example42PaletteRowHeight() + example42PaletteGap()
           + example42SwatchSize() + spacing
           + example42CanvasMinimumHeight();
}

static inline int example42PanePreferredWidth() {
    return 520;
}

static inline int example42PanePreferredHeight() {
    return 640;
}

static inline Example42PaneLayout computeExample42PaneLayout(int width, int height) {
    const int safeWidth = std::max(width, example42PaneMinimumWidth());
    const int safeHeight = std::max(height, example42PaneMinimumHeight());
    const int outerMargin = example42OuterMargin();
    const int spacing = example42Spacing();
    const int availableWidth = std::max(example42ContentMinimumWidth(), safeWidth - (outerMargin * 2));
    int y = outerMargin;

    Example42PaneLayout layout;
    layout.title = Example42Rect{outerMargin, y, availableWidth, example42TitleHeight()};
    y += example42TitleHeight();

    layout.subtitle = Example42Rect{outerMargin, y, availableWidth, example42SubtitleHeight()};
    y += example42SubtitleHeight() + 6;

    layout.status = Example42Rect{outerMargin, y, availableWidth, example42StatusHeight()};
    y += example42StatusHeight() + spacing;

    layout.runtime = Example42Rect{outerMargin, y, availableWidth, example42RuntimeHeight()};
    y += example42RuntimeHeight() + spacing;

    layout.bridgeButton = Example42Rect{outerMargin, y, availableWidth, example42BridgeButtonHeight()};
    y += example42BridgeButtonHeight() + spacing;

    layout.fiberButton = Example42Rect{outerMargin, y, availableWidth, example42FiberButtonHeight()};
    y += example42FiberButtonHeight() + spacing;

    layout.lineEdit = Example42Rect{outerMargin, y, availableWidth, example42LineEditHeight()};
    y += example42LineEditHeight() + spacing;

    layout.paletteLabel = Example42Rect{outerMargin, y, example42PaletteLabelWidth(), example42PaletteRowHeight()};
    layout.currentInkLabel = Example42Rect{safeWidth - outerMargin - example42CurrentInkLabelWidth(),
                                           y,
                                           example42CurrentInkLabelWidth(),
                                           example42PaletteRowHeight()};
    y += example42PaletteRowHeight() + example42PaletteGap();

    layout.clearButton = Example42Rect{safeWidth - outerMargin - example42ClearButtonWidth(),
                                       y - 2,
                                       example42ClearButtonWidth(),
                                       example42ClearButtonHeight()};

    const int swatchesWidth = std::max(0, (kInkPaletteCount * example42SwatchStep()) - (example42SwatchStep() - example42SwatchSize()));
    const int canvasWidth = std::max(example42CanvasMinimumWidth(), availableWidth);
    const int swatchRowHeight = std::max(example42SwatchSize(), example42ClearButtonHeight());
    y += swatchRowHeight + spacing;

    SW_UNUSED(swatchesWidth);
    layout.canvas = Example42Rect{
        outerMargin,
        y,
        canvasWidth,
        std::max(example42CanvasMinimumHeight(), safeHeight - y - outerMargin)
    };
    return layout;
}

struct Example42PaneTextSet {
    const char* title;
    const char* subtitle;
    const char* readyStatus;
    const char* runtimeIdle;
    const char* bridgeButton;
    const char* fiberButton;
    const char* placeholder;
    const char* paletteLabel;
    const char* clearButton;
    const char* emptyCanvasHint;
};

static inline const Example42PaneTextSet& example42PaneTexts() {
    static const Example42PaneTextSet texts = {
        "Sketch Studio",
        "Widgets + Painter",
        "Bridge ready",
        "Worker idle",
        "Send bridge message",
        "Run worker fiber",
        "Write a bridge message",
        "Palette",
        "Clear canvas",
        "Hold the mouse and sketch here"
    };
    return texts;
}

static inline QString example42TextQt(const char* text) {
    return QString::fromLatin1(text ? text : "");
}

static inline SwString example42TextSw(const char* text) {
    return SwString(text ? text : "");
}

static inline QString qtLineEditStyleSheet() {
    return QStringLiteral(
        "QLineEdit {"
        " background-color: rgb(255, 255, 255);"
        " border: 1px solid rgb(220, 224, 232);"
        " border-radius: 12px;"
        " padding: 6px 10px;"
        " color: rgb(24, 28, 36);"
        " font-size: 14px;"
        "}");
}

static inline QString qtPrimaryButtonStyleSheet() {
    return QStringLiteral(
        "QPushButton {"
        " background-color: rgb(30, 102, 255);"
        " border: 1px solid rgb(22, 84, 220);"
        " color: rgb(255, 255, 255);"
        " border-radius: 12px;"
        " padding: 10px 14px;"
        " font-size: 14px;"
        "}"
        "QPushButton:hover {"
        " background-color: rgb(44, 114, 255);"
        " border-color: rgb(30, 102, 255);"
        "}"
        "QPushButton:pressed {"
        " background-color: rgb(20, 76, 206);"
        " border-color: rgb(15, 62, 176);"
        "}");
}

static inline QString qtSecondaryButtonStyleSheet() {
    return QStringLiteral(
        "QPushButton {"
        " background-color: rgb(255, 255, 255);"
        " border: 1px solid rgb(220, 224, 232);"
        " color: rgb(24, 28, 36);"
        " border-radius: 12px;"
        " padding: 9px 14px;"
        " font-size: 13px;"
        "}"
        "QPushButton:hover {"
        " background-color: rgb(248, 250, 252);"
        " border-color: rgb(203, 213, 225);"
        "}"
        "QPushButton:pressed {"
        " background-color: rgb(241, 245, 249);"
        "}");
}

static inline SwString swPrimaryButtonStyleSheet() {
    return SwString(R"(
        SwPushButton {
            background-color: rgb(30, 102, 255);
            border-color: rgb(22, 84, 220);
            color: rgb(255, 255, 255);
            border-radius: 12px;
            padding: 10px 14px;
            border-width: 1px;
            font-size: 14px;
        }
        SwPushButton:hover {
            background-color: rgb(44, 114, 255);
            border-color: rgb(30, 102, 255);
        }
        SwPushButton:pressed {
            background-color: rgb(20, 76, 206);
            border-color: rgb(15, 62, 176);
        }
    )");
}

static inline SwString swSecondaryButtonStyleSheet() {
    return SwString(R"(
        SwPushButton {
            background-color: rgb(255, 255, 255);
            border-color: rgb(220, 224, 232);
            color: rgb(24, 28, 36);
            border-radius: 12px;
            padding: 9px 14px;
            border-width: 1px;
            font-size: 13px;
        }
        SwPushButton:hover {
            background-color: rgb(248, 250, 252);
            border-color: rgb(203, 213, 225);
        }
        SwPushButton:pressed {
            background-color: rgb(241, 245, 249);
            border-color: rgb(203, 213, 225);
        }
    )");
}

static inline SwString swLineEditStyleSheet() {
    return SwString(R"(
        SwLineEdit {
            background-color: rgb(255, 255, 255);
            border-color: rgb(220, 224, 232);
            border-radius: 12px;
            padding: 6px 10px;
            border-width: 1px;
            color: rgb(24, 28, 36);
            font-size: 14px;
        }
    )");
}
