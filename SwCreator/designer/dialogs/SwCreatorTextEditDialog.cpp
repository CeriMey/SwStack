#include "SwCreatorTextEditDialog.h"

#include "SwCodeEditor.h"
#include "SwCompleter.h"
#include "SwCssSyntaxHighlighter.h"
#include "SwLayout.h"
#include "SwPushButton.h"
#include "SwStandardItemModel.h"
#include "editorTheme/SwCodeEditorVisualTheme.h"
#include "theme/SwCreatorTheme.h"

#include <algorithm>

namespace {

SwString normalizeStyleSheetEditorText_(const SwString& input) {
    if (input.isEmpty()) {
        return input;
    }

    SwString text = input;
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');

    SwVector<SwString> lines;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            lines.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }

    int firstNonEmpty = 0;
    while (firstNonEmpty < static_cast<int>(lines.size()) && lines[static_cast<size_t>(firstNonEmpty)].trimmed().isEmpty()) {
        ++firstNonEmpty;
    }

    int lastNonEmpty = static_cast<int>(lines.size()) - 1;
    while (lastNonEmpty >= firstNonEmpty && lines[static_cast<size_t>(lastNonEmpty)].trimmed().isEmpty()) {
        --lastNonEmpty;
    }

    if (firstNonEmpty > lastNonEmpty) {
        return SwString();
    }

    size_t commonIndent = static_cast<size_t>(-1);
    for (int i = firstNonEmpty; i <= lastNonEmpty; ++i) {
        const SwString& line = lines[static_cast<size_t>(i)];
        if (line.trimmed().isEmpty()) {
            continue;
        }

        size_t indent = 0;
        while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) {
            ++indent;
        }

        commonIndent = std::min(commonIndent, indent);
    }

    if (commonIndent == static_cast<size_t>(-1)) {
        commonIndent = 0;
    }

    SwString normalized;
    for (int i = firstNonEmpty; i <= lastNonEmpty; ++i) {
        SwString line = lines[static_cast<size_t>(i)];
        size_t removable = 0;
        while (removable < commonIndent &&
               removable < line.size() &&
               (line[removable] == ' ' || line[removable] == '\t')) {
            ++removable;
        }
        normalized.append(line.substr(removable));
        if (i < lastNonEmpty) {
            normalized.append('\n');
        }
    }

    return normalized;
}

} // namespace

SwCreatorTextEditDialog::SwCreatorTextEditDialog(SwWidget* parent)
    : SwCreatorDockDialog(parent) {
    setWindowTitle("Edit StyleSheet");
    setMinimumSize(620, 420);
    resize(820, 560);
    buildUi_();
}

void SwCreatorTextEditDialog::setText(const SwString& text) {
    if (m_editor) {
        m_editor->setPlainText(normalizeStyleSheetEditorText_(text));
    }
}

SwString SwCreatorTextEditDialog::text() const {
    return m_editor ? m_editor->toPlainText() : SwString();
}

void SwCreatorTextEditDialog::setPlaceholderText(const SwString&) {
    // SwCodeEditor does not support placeholder — ignored.
}

void SwCreatorTextEditDialog::setOnApply(const std::function<void(const SwString&)>& handler) {
    m_onApply = handler;
}

void SwCreatorTextEditDialog::buildUi_() {
    if (m_editor) {
        return;
    }

    const auto& th = SwCreatorTheme::current();
    setStyleSheet(
        "SwDialog {"
        " background-color: " + SwCreatorTheme::rgb(th.surface1) + ";"
        " border-color: " + SwCreatorTheme::rgb(th.borderStrong) + ";"
        " border-width: 1px;"
        " color: " + SwCreatorTheme::rgb(th.textPrimary) + ";"
        " }"
    );

    if (auto* content = contentWidget()) {
        content->setStyleSheet(
            "SwWidget {"
            " background-color: " + SwCreatorTheme::rgb(th.surface1) + ";"
            " border-width: 0px;"
            " }"
        );
        auto* layout = new SwVerticalLayout(content);
        layout->setMargin(0);
        layout->setSpacing(0);
        content->setLayout(layout);

        // --- Code editor with CSS highlighting ---
        m_editor = new SwCodeEditor(content);
        m_editor->setLineNumbersVisible(true);
        m_editor->setHighlightCurrentLine(true);
        m_editor->setCodeFoldingEnabled(false);
        m_editor->setIndentSize(4);

        // VS Code Dark theme
        auto visualTheme = swCodeEditorVisualThemeVsCodeDark();
        visualTheme.editorTheme.borderRadius = 2;
        m_editor->setTheme(visualTheme.editorTheme);

        // CSS syntax highlighter
        m_highlighter = new SwCssSyntaxHighlighter(m_editor->document());
        m_editor->setSyntaxHighlighter(m_highlighter);

        // Auto-completion
        setupCompletion_();

        layout->addWidget(m_editor, 1, 0);
    }

    if (auto* bar = buttonBarWidget()) {
        bar->setStyleSheet(
            "SwWidget {"
            " background-color: " + SwCreatorTheme::rgb(th.surface1) + ";"
            " border-width: 0px;"
            " }"
        );
        auto* barLayout = new SwHorizontalLayout(bar);
        barLayout->setMargin(0);
        barLayout->setSpacing(8);
        bar->setLayout(barLayout);

        auto* spacer = new SwWidget(bar);
        spacer->setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        barLayout->addWidget(spacer, 1, 0);

        m_apply = new SwPushButton("Apply", bar);
        m_apply->resize(100, 32);
        m_apply->setStyleSheet(
            "SwPushButton {"
            " background-color: " + SwCreatorTheme::rgb(th.accentPrimary) + ";"
            " border-color: " + SwCreatorTheme::rgb(th.accentPrimary) + ";"
            " color: " + SwCreatorTheme::rgb(th.textInverse) + ";"
            " border-radius: 2px;"
            " padding: 6px 14px;"
            " border-width: 1px;"
            " font-size: 13px;"
            " }"
            " SwPushButton:hover {"
            " background-color: " + SwCreatorTheme::rgb(th.accentHover) + ";"
            " border-color: " + SwCreatorTheme::rgb(th.accentHover) + ";"
            " }"
            " SwPushButton:pressed {"
            " background-color: " + SwCreatorTheme::rgb(th.accentPressed) + ";"
            " border-color: " + SwCreatorTheme::rgb(th.accentPressed) + ";"
            " }"
        );
        SwObject::connect(m_apply, &SwPushButton::clicked, this, [this]() {
            const SwString t = text();
            if (m_onApply) {
                m_onApply(t);
            }
            applied(t);
        });
        barLayout->addWidget(m_apply, 0, m_apply->width());

        m_close = new SwPushButton("Close", bar);
        m_close->resize(100, 32);
        m_close->setStyleSheet(
            "SwPushButton {"
            " background-color: " + SwCreatorTheme::rgb(th.surface3) + ";"
            " border-color: " + SwCreatorTheme::rgb(th.border) + ";"
            " color: " + SwCreatorTheme::rgb(th.textPrimary) + ";"
            " border-radius: 2px;"
            " padding: 6px 14px;"
            " border-width: 1px;"
            " font-size: 13px;"
            " }"
            " SwPushButton:hover {"
            " background-color: " + SwCreatorTheme::rgb(th.hoverBg) + ";"
            " }"
        );
        SwObject::connect(m_close, &SwPushButton::clicked, this, [this]() {
            if (isDockedOpen()) {
                closeDocked();
                return;
            }
            reject();
        });
        barLayout->addWidget(m_close, 0, m_close->width());
    }
}

void SwCreatorTextEditDialog::setupCompletion_() {
    if (!m_editor) {
        return;
    }

    m_editor->setAutoCompletionEnabled(true);
    m_editor->setAutoCompletionMinPrefixLength(2);

    // QSS/CSS property & value completions
    m_editor->setCompletionProvider(
        [](SwCodeEditor*, const SwString& prefix, size_t, bool)
            -> SwList<SwCodeEditor::CompletionEntry> {

        // Common CSS/QSS properties
        static const char* kProperties[] = {
            "background-color", "background-color-hover", "background-color-pressed",
            "background-color-checked", "background-color-disabled",
            "border-color", "border-width", "border-radius",
            "border-top-left-radius", "border-top-right-radius",
            "border-bottom-left-radius", "border-bottom-right-radius",
            "border-left-width", "border-right-width",
            "border-top-width", "border-bottom-width",
            "color", "color-disabled",
            "padding", "padding-left", "padding-right", "padding-top", "padding-bottom",
            "margin", "margin-left", "margin-right", "margin-top", "margin-bottom",
            "font-size", "font-weight", "font-family",
            "min-width", "min-height", "max-width", "max-height",
            "width", "height",
            "box-shadow",
            "divider-color", "indicator-color", "indicator-color-disabled",
            nullptr
        };

        // Common CSS/QSS values
        static const char* kValues[] = {
            "rgb(", "rgba(", "transparent",
            "bold", "normal", "italic",
            "none", "solid", "dashed",
            "left", "right", "top", "bottom", "center",
            "0px", "1px", "2px", "4px", "8px",
            nullptr
        };

        // Common SwStack widget selectors
        static const char* kSelectors[] = {
            "SwWidget", "SwFrame", "SwPushButton", "SwToolButton",
            "SwLabel", "SwLineEdit", "SwCheckBox", "SwRadioButton",
            "SwComboBox", "SwSpinBox", "SwProgressBar", "SwSlider",
            "SwTabWidget", "SwGroupBox", "SwScrollArea",
            "SwTreeWidget", "SwTableWidget", "SwListWidget",
            "SwPlainTextEdit", "SwTextEdit",
            ":hover", ":pressed", ":checked", ":disabled", ":focus",
            nullptr
        };

        const std::string pfx = prefix.toLower().toStdString();
        SwList<SwCodeEditor::CompletionEntry> entries;

        auto addMatches = [&](const char** list) {
            for (int i = 0; list[i]; ++i) {
                std::string item = list[i];
                std::string lower = item;
                for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (lower.find(pfx) != std::string::npos) {
                    SwCodeEditor::CompletionEntry e;
                    e.displayText = SwString(item);
                    e.insertText = SwString(item);
                    entries.append(e);
                }
            }
        };

        addMatches(kProperties);
        addMatches(kValues);
        addMatches(kSelectors);

        return entries;
    });

    // Setup completer visual
    m_completer = m_editor->completer();
    if (m_completer) {
        auto completionTheme = swCodeCompletionThemeVsCodeDark();
        completionTheme.popupBorderRadius = 2;
        swApplyCodeCompletionTheme(m_completer, completionTheme);
    }
}
