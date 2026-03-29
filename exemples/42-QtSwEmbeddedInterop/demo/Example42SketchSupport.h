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
        " font-weight: 600;"
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
