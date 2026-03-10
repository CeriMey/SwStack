#pragma once

#include "SwCodeEditor.h"
#include "SwCppSyntaxHighlighter.h"
#include "SwStyledItemDelegate.h"

#include <algorithm>
#include <sstream>

struct SwCodeCompletionTheme {
    SwColor popupBackgroundColor{255, 255, 255};
    SwColor popupBorderColor{210, 214, 224};
    SwColor popupTextColor{24, 28, 36};
    SwColor popupSelectedTextColor{244, 246, 248};
    SwColor popupHoverRowColor{240, 244, 248};
    SwColor popupSelectedRowColor{59, 130, 246};
    SwColor popupSelectedIndicatorColor{37, 99, 235};
    int popupBorderRadius{8};
    int popupRowHeight{24};
    int popupViewportPadding{1};
};

inline SwCodeCompletionTheme swCodeCompletionThemeDefaultLight() {
    return SwCodeCompletionTheme();
}

inline SwCodeCompletionTheme swCodeCompletionThemeVsCodeLight() {
    SwCodeCompletionTheme theme;
    theme.popupBackgroundColor = SwColor{247, 247, 247};
    theme.popupBorderColor = SwColor{210, 210, 210};
    theme.popupTextColor = SwColor{51, 51, 51};
    theme.popupSelectedTextColor = SwColor{255, 255, 255};
    theme.popupHoverRowColor = SwColor{238, 242, 246};
    theme.popupSelectedRowColor = SwColor{0, 122, 204};
    theme.popupSelectedIndicatorColor = SwColor{0, 90, 158};
    theme.popupBorderRadius = 3;
    theme.popupRowHeight = 22;
    theme.popupViewportPadding = 1;
    return theme;
}

inline SwCodeCompletionTheme swCodeCompletionThemeVsCodeDark() {
    SwCodeCompletionTheme theme;
    theme.popupBackgroundColor = SwColor{28, 30, 34};
    theme.popupBorderColor = SwColor{50, 54, 60};
    theme.popupTextColor = SwColor{206, 211, 218};
    theme.popupSelectedTextColor = SwColor{244, 246, 248};
    theme.popupHoverRowColor = SwColor{34, 37, 42};
    theme.popupSelectedRowColor = SwColor{48, 52, 58};
    theme.popupSelectedIndicatorColor = SwColor{124, 130, 140};
    theme.popupBorderRadius = 3;
    theme.popupRowHeight = 22;
    theme.popupViewportPadding = 1;
    return theme;
}

inline SwCodeCompletionTheme swCodeCompletionThemeOneDark() {
    SwCodeCompletionTheme theme;
    theme.popupBackgroundColor = SwColor{40, 44, 52};
    theme.popupBorderColor = SwColor{33, 37, 43};
    theme.popupTextColor = SwColor{171, 178, 191};
    theme.popupSelectedTextColor = SwColor{248, 250, 252};
    theme.popupHoverRowColor = SwColor{46, 50, 58};
    theme.popupSelectedRowColor = SwColor{56, 60, 70};
    theme.popupSelectedIndicatorColor = SwColor{97, 175, 239};
    theme.popupBorderRadius = 4;
    theme.popupRowHeight = 22;
    theme.popupViewportPadding = 1;
    return theme;
}

inline SwCodeCompletionTheme swCodeCompletionThemeSolarizedLight() {
    SwCodeCompletionTheme theme;
    theme.popupBackgroundColor = SwColor{253, 246, 227};
    theme.popupBorderColor = SwColor{222, 213, 184};
    theme.popupTextColor = SwColor{88, 110, 117};
    theme.popupSelectedTextColor = SwColor{88, 110, 117};
    theme.popupHoverRowColor = SwColor{246, 238, 213};
    theme.popupSelectedRowColor = SwColor{238, 232, 213};
    theme.popupSelectedIndicatorColor = SwColor{38, 139, 210};
    theme.popupBorderRadius = 4;
    theme.popupRowHeight = 22;
    theme.popupViewportPadding = 1;
    return theme;
}

inline SwCppSyntaxTheme swCppSyntaxThemeDefaultLight() {
    SwCppSyntaxTheme theme;
    theme.keywordFormat = swCppMakeFormat_(SwColor{0, 92, 197}, Medium);
    theme.typeFormat = swCppMakeFormat_(SwColor{43, 145, 175});
    theme.commentFormat = swCppMakeFormat_(SwColor{0, 128, 0}, Normal, true);
    theme.stringFormat = swCppMakeFormat_(SwColor{163, 21, 21});
    theme.numberFormat = swCppMakeFormat_(SwColor{9, 134, 88});
    theme.functionFormat = swCppMakeFormat_(SwColor{121, 94, 38});
    theme.preprocessorFormat = swCppMakeFormat_(SwColor{128, 0, 128});
    theme.macroFormat = swCppMakeFormat_(SwColor{111, 0, 138});
    theme.namespaceFormat = swCppMakeFormat_(SwColor{43, 145, 175});
    return theme;
}

inline SwCodeEditorTheme swCodeEditorVsCodeLightTheme() {
    SwCodeEditorTheme theme;
    theme.backgroundColor = SwColor{255, 255, 255};
    theme.borderColor = SwColor{225, 228, 232};
    theme.focusBorderColor = SwColor{0, 122, 204};
    theme.textColor = SwColor{30, 30, 30};
    theme.disabledTextColor = SwColor{160, 160, 160};
    theme.gutterBackgroundColor = SwColor{245, 245, 245};
    theme.gutterTextColor = SwColor{133, 133, 133};
    theme.currentLineNumberColor = SwColor{51, 51, 51};
    theme.gutterSeparatorColor = SwColor{230, 230, 230};
    theme.currentLineBackgroundColor = SwColor{245, 249, 252};
    theme.selectionBackgroundColor = SwColor{173, 214, 255};
    theme.placeholderColor = SwColor{150, 150, 150};
    theme.diagnosticErrorColor = SwColor{228, 86, 73};
    theme.diagnosticWarningColor = SwColor{191, 132, 0};
    theme.diagnosticInformationColor = SwColor{0, 122, 204};
    theme.borderRadius = 0;
    return theme;
}

inline SwCppSyntaxTheme swCppSyntaxThemeVsCodeLight() {
    SwCppSyntaxTheme theme;
    theme.keywordFormat = swCppMakeFormat_(SwColor{0, 0, 255}, Medium);
    theme.typeFormat = swCppMakeFormat_(SwColor{38, 127, 153});
    theme.commentFormat = swCppMakeFormat_(SwColor{0, 128, 0}, Normal, true);
    theme.stringFormat = swCppMakeFormat_(SwColor{163, 21, 21});
    theme.numberFormat = swCppMakeFormat_(SwColor{9, 134, 88});
    theme.functionFormat = swCppMakeFormat_(SwColor{121, 94, 38});
    theme.preprocessorFormat = swCppMakeFormat_(SwColor{175, 0, 219});
    theme.macroFormat = swCppMakeFormat_(SwColor{111, 0, 138});
    theme.namespaceFormat = swCppMakeFormat_(SwColor{38, 127, 153});
    return theme;
}

inline SwCodeEditorTheme swCodeEditorOneDarkTheme() {
    SwCodeEditorTheme theme;
    theme.backgroundColor = SwColor{40, 44, 52};
    theme.borderColor = SwColor{33, 37, 43};
    theme.focusBorderColor = SwColor{97, 175, 239};
    theme.textColor = SwColor{171, 178, 191};
    theme.disabledTextColor = SwColor{92, 99, 112};
    theme.gutterBackgroundColor = SwColor{40, 44, 52};
    theme.gutterTextColor = SwColor{92, 99, 112};
    theme.currentLineNumberColor = SwColor{224, 227, 233};
    theme.gutterSeparatorColor = SwColor{46, 50, 58};
    theme.currentLineBackgroundColor = SwColor{46, 50, 58};
    theme.selectionBackgroundColor = SwColor{62, 68, 81};
    theme.placeholderColor = SwColor{92, 99, 112};
    theme.diagnosticErrorColor = SwColor{224, 108, 117};
    theme.diagnosticWarningColor = SwColor{209, 154, 102};
    theme.diagnosticInformationColor = SwColor{97, 175, 239};
    theme.borderRadius = 0;
    return theme;
}

inline SwCppSyntaxTheme swCppSyntaxThemeOneDark() {
    SwCppSyntaxTheme theme;
    theme.keywordFormat = swCppMakeFormat_(SwColor{198, 120, 221}, Medium);
    theme.typeFormat = swCppMakeFormat_(SwColor{86, 182, 194});
    theme.commentFormat = swCppMakeFormat_(SwColor{92, 99, 112}, Normal, true);
    theme.stringFormat = swCppMakeFormat_(SwColor{152, 195, 121});
    theme.numberFormat = swCppMakeFormat_(SwColor{209, 154, 102});
    theme.functionFormat = swCppMakeFormat_(SwColor{97, 175, 239});
    theme.preprocessorFormat = swCppMakeFormat_(SwColor{224, 108, 117});
    theme.macroFormat = swCppMakeFormat_(SwColor{86, 182, 194});
    theme.namespaceFormat = swCppMakeFormat_(SwColor{86, 182, 194});
    return theme;
}

inline SwCodeEditorTheme swCodeEditorSolarizedLightTheme() {
    SwCodeEditorTheme theme;
    theme.backgroundColor = SwColor{253, 246, 227};
    theme.borderColor = SwColor{222, 213, 184};
    theme.focusBorderColor = SwColor{38, 139, 210};
    theme.textColor = SwColor{88, 110, 117};
    theme.disabledTextColor = SwColor{147, 161, 161};
    theme.gutterBackgroundColor = SwColor{248, 240, 216};
    theme.gutterTextColor = SwColor{147, 161, 161};
    theme.currentLineNumberColor = SwColor{101, 123, 131};
    theme.gutterSeparatorColor = SwColor{238, 232, 213};
    theme.currentLineBackgroundColor = SwColor{246, 238, 213};
    theme.selectionBackgroundColor = SwColor{238, 232, 213};
    theme.placeholderColor = SwColor{147, 161, 161};
    theme.diagnosticErrorColor = SwColor{220, 50, 47};
    theme.diagnosticWarningColor = SwColor{181, 137, 0};
    theme.diagnosticInformationColor = SwColor{38, 139, 210};
    theme.borderRadius = 6;
    return theme;
}

inline SwCppSyntaxTheme swCppSyntaxThemeSolarizedLight() {
    SwCppSyntaxTheme theme;
    theme.keywordFormat = swCppMakeFormat_(SwColor{38, 139, 210}, Medium);
    theme.typeFormat = swCppMakeFormat_(SwColor{42, 161, 152});
    theme.commentFormat = swCppMakeFormat_(SwColor{147, 161, 161}, Normal, true);
    theme.stringFormat = swCppMakeFormat_(SwColor{42, 161, 152});
    theme.numberFormat = swCppMakeFormat_(SwColor{211, 54, 130});
    theme.functionFormat = swCppMakeFormat_(SwColor{181, 137, 0});
    theme.preprocessorFormat = swCppMakeFormat_(SwColor{108, 113, 196});
    theme.macroFormat = swCppMakeFormat_(SwColor{220, 50, 47});
    theme.namespaceFormat = swCppMakeFormat_(SwColor{42, 161, 152});
    return theme;
}

enum class SwCodeEditorVisualThemeId {
    DefaultLight,
    VsCodeLight,
    VsCodeDark,
    OneDark,
    SolarizedLight
};

struct SwCodeEditorVisualTheme {
    SwString name;
    SwCodeEditorTheme editorTheme;
    SwCppSyntaxTheme cppSyntaxTheme;
    SwCodeCompletionTheme completionTheme;
};

inline SwCodeEditorVisualTheme swCodeEditorVisualThemeDefaultLight() {
    SwCodeEditorVisualTheme theme;
    theme.name = "Default Light";
    theme.editorTheme = swCodeEditorDefaultTheme();
    theme.cppSyntaxTheme = swCppSyntaxThemeDefaultLight();
    theme.completionTheme = swCodeCompletionThemeDefaultLight();
    return theme;
}

inline SwCodeEditorVisualTheme swCodeEditorVisualThemeVsCodeLight() {
    SwCodeEditorVisualTheme theme;
    theme.name = "VS Code Light";
    theme.editorTheme = swCodeEditorVsCodeLightTheme();
    theme.cppSyntaxTheme = swCppSyntaxThemeVsCodeLight();
    theme.completionTheme = swCodeCompletionThemeVsCodeLight();
    return theme;
}

inline SwCodeEditorVisualTheme swCodeEditorVisualThemeVsCodeDark() {
    SwCodeEditorVisualTheme theme;
    theme.name = "VS Code Dark";
    theme.editorTheme = swCodeEditorVsCodeDarkTheme();
    theme.cppSyntaxTheme = swCppSyntaxThemeVsCodeDark();
    theme.completionTheme = swCodeCompletionThemeVsCodeDark();
    return theme;
}

inline SwCodeEditorVisualTheme swCodeEditorVisualThemeOneDark() {
    SwCodeEditorVisualTheme theme;
    theme.name = "One Dark";
    theme.editorTheme = swCodeEditorOneDarkTheme();
    theme.cppSyntaxTheme = swCppSyntaxThemeOneDark();
    theme.completionTheme = swCodeCompletionThemeOneDark();
    return theme;
}

inline SwCodeEditorVisualTheme swCodeEditorVisualThemeSolarizedLight() {
    SwCodeEditorVisualTheme theme;
    theme.name = "Solarized Light";
    theme.editorTheme = swCodeEditorSolarizedLightTheme();
    theme.cppSyntaxTheme = swCppSyntaxThemeSolarizedLight();
    theme.completionTheme = swCodeCompletionThemeSolarizedLight();
    return theme;
}

inline SwString swCodeEditorVisualThemeName(SwCodeEditorVisualThemeId id) {
    switch (id) {
        case SwCodeEditorVisualThemeId::VsCodeLight:
            return "VS Code Light";
        case SwCodeEditorVisualThemeId::VsCodeDark:
            return "VS Code Dark";
        case SwCodeEditorVisualThemeId::OneDark:
            return "One Dark";
        case SwCodeEditorVisualThemeId::SolarizedLight:
            return "Solarized Light";
        case SwCodeEditorVisualThemeId::DefaultLight:
        default:
            return "Default Light";
    }
}

inline SwCodeEditorVisualTheme swCodeEditorVisualThemeById(SwCodeEditorVisualThemeId id) {
    switch (id) {
        case SwCodeEditorVisualThemeId::VsCodeLight:
            return swCodeEditorVisualThemeVsCodeLight();
        case SwCodeEditorVisualThemeId::VsCodeDark:
            return swCodeEditorVisualThemeVsCodeDark();
        case SwCodeEditorVisualThemeId::OneDark:
            return swCodeEditorVisualThemeOneDark();
        case SwCodeEditorVisualThemeId::SolarizedLight:
            return swCodeEditorVisualThemeSolarizedLight();
        case SwCodeEditorVisualThemeId::DefaultLight:
        default:
            return swCodeEditorVisualThemeDefaultLight();
    }
}

class SwCodeCompletionThemeDelegate : public SwStyledItemDelegate {
    SW_OBJECT(SwCodeCompletionThemeDelegate, SwStyledItemDelegate)

public:
    explicit SwCodeCompletionThemeDelegate(const SwCodeCompletionTheme& theme = swCodeCompletionThemeDefaultLight(),
                                           SwObject* parent = nullptr)
        : SwStyledItemDelegate(parent)
        , m_theme(theme) {}

    void setTheme(const SwCodeCompletionTheme& theme) {
        m_theme = theme;
        if (SwWidget* widget = dynamic_cast<SwWidget*>(parent())) {
            widget->update();
        }
    }

    const SwCodeCompletionTheme& theme() const {
        return m_theme;
    }

    void paint(SwPainter* painter,
               const SwStyleOptionViewItem& option,
               const SwModelIndex& index) const override {
        if (!painter || !index.isValid() || !index.model()) {
            return;
        }

        const SwColor rowFill = option.selected
            ? m_theme.popupSelectedRowColor
            : (option.hovered ? m_theme.popupHoverRowColor : m_theme.popupBackgroundColor);
        painter->fillRect(option.rect, rowFill, rowFill, 0);

        if (option.selected) {
            painter->fillRect(SwRect{option.rect.x, option.rect.y, 1, option.rect.height},
                              m_theme.popupSelectedIndicatorColor,
                              m_theme.popupSelectedIndicatorColor,
                              0);
        }

        SwRect textRect = option.rect;
        textRect.x += 8;
        textRect.width = std::max(0, textRect.width - 16);

        const SwColor textColor = option.selected ? m_theme.popupSelectedTextColor : m_theme.popupTextColor;
        painter->drawText(textRect,
                          index.model()->data(index, SwItemDataRole::DisplayRole).toString(),
                          DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          textColor,
                          option.font);
    }

    SwSize sizeHint(const SwStyleOptionViewItem& option,
                    const SwModelIndex& index) const override {
        SwSize hint = SwStyledItemDelegate::sizeHint(option, index);
        hint.height = std::max(m_theme.popupRowHeight, hint.height);
        return hint;
    }

private:
    SwCodeCompletionTheme m_theme;
};

inline SwString swCodeCompletionThemeStyleSheet(const SwCodeCompletionTheme& theme) {
    auto cssColor = [](const SwColor& color) {
        std::ostringstream oss;
        oss << "rgb(" << color.r << ", " << color.g << ", " << color.b << ")";
        return SwString(oss.str().c_str());
    };

    SwString css("SwListView {");
    css += " background-color: ";
    css += cssColor(theme.popupBackgroundColor);
    css += ";";
    css += " border-color: ";
    css += cssColor(theme.popupBorderColor);
    css += ";";
    css += " border-width: 1px;";
    css += " border-radius: ";
    css += SwString::number(theme.popupBorderRadius);
    css += "px;";
    css += " color: ";
    css += cssColor(theme.popupTextColor);
    css += ";";
    css += " }";
    return css;
}

inline void swApplyCodeCompletionTheme(SwCompleter* completer, const SwCodeCompletionTheme& theme) {
    if (!completer) {
        return;
    }

    SwListView* popup = completer->popup();
    if (!popup) {
        return;
    }

    popup->setStyleSheet(swCodeCompletionThemeStyleSheet(theme));
    popup->setSpacing(0);
    popup->setViewportPadding(theme.popupViewportPadding);
    popup->setRowHeight(theme.popupRowHeight);

    if (SwCodeCompletionThemeDelegate* delegate =
            dynamic_cast<SwCodeCompletionThemeDelegate*>(popup->itemDelegate())) {
        delegate->setTheme(theme);
    } else {
        popup->setItemDelegate(new SwCodeCompletionThemeDelegate(theme, popup));
    }

    popup->update();
}

inline void swApplyCodeEditorVisualTheme(SwCodeEditor* editor, const SwCodeEditorVisualTheme& theme) {
    if (!editor) {
        return;
    }

    editor->setTheme(theme.editorTheme);

    if (SwCppSyntaxHighlighter* cppHighlighter =
            dynamic_cast<SwCppSyntaxHighlighter*>(editor->syntaxHighlighter())) {
        cppHighlighter->setTheme(theme.cppSyntaxTheme);
    }

    swApplyCodeCompletionTheme(editor->completer(), theme.completionTheme);
}

inline SwString swCodeEditorVisualThemeContainerStyleSheet(const SwCodeEditorVisualTheme& theme) {
    std::ostringstream oss;
    oss << "SwWidget { background-color: rgb("
        << theme.editorTheme.backgroundColor.r << ", "
        << theme.editorTheme.backgroundColor.g << ", "
        << theme.editorTheme.backgroundColor.b
        << "); border-width: 0px; }";
    return SwString(oss.str().c_str());
}
