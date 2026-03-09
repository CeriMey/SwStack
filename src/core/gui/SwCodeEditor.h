#pragma once

/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#include "SwCompleter.h"
#include "SwPlainTextEdit.h"
#include "SwSyntaxHighlighter.h"
#include "SwTextDecorationRenderer.h"
#include "SwTextDocument.h"
#include "SwTextDiagnostics.h"
#include "SwTextExtraSelection.h"

#include <algorithm>
#include <cwctype>
#include <sstream>
#include <vector>

struct SwCodeEditorTheme {
    SwColor backgroundColor{255, 255, 255};
    SwColor borderColor{220, 224, 232};
    SwColor focusBorderColor{59, 130, 246};
    SwColor textColor{24, 28, 36};
    SwColor disabledTextColor{150, 150, 150};
    SwColor gutterBackgroundColor{248, 250, 252};
    SwColor gutterTextColor{120, 128, 140};
    SwColor currentLineNumberColor{55, 65, 81};
    SwColor gutterSeparatorColor{226, 232, 240};
    SwColor currentLineBackgroundColor{243, 247, 255};
    SwColor selectionBackgroundColor{219, 234, 254};
    SwColor placeholderColor{160, 160, 160};
    SwColor diagnosticErrorColor{220, 38, 38};
    SwColor diagnosticWarningColor{217, 119, 6};
    SwColor diagnosticInformationColor{37, 99, 235};
    int borderRadius{10};
};

inline SwCodeEditorTheme swCodeEditorDefaultTheme() {
    return SwCodeEditorTheme();
}

inline SwCodeEditorTheme swCodeEditorVsCodeDarkTheme() {
    SwCodeEditorTheme theme;
    theme.backgroundColor = SwColor{30, 30, 30};
    theme.borderColor = SwColor{45, 45, 48};
    theme.focusBorderColor = SwColor{0, 122, 204};
    theme.textColor = SwColor{212, 212, 212};
    theme.disabledTextColor = SwColor{112, 112, 112};
    theme.gutterBackgroundColor = SwColor{30, 30, 30};
    theme.gutterTextColor = SwColor{133, 133, 133};
    theme.currentLineNumberColor = SwColor{198, 198, 198};
    theme.gutterSeparatorColor = SwColor{51, 51, 51};
    theme.currentLineBackgroundColor = SwColor{42, 45, 46};
    theme.selectionBackgroundColor = SwColor{38, 79, 120};
    theme.placeholderColor = SwColor{106, 115, 125};
    theme.diagnosticErrorColor = SwColor{244, 71, 71};
    theme.diagnosticWarningColor = SwColor{220, 180, 90};
    theme.diagnosticInformationColor = SwColor{79, 154, 255};
    theme.borderRadius = 0;
    return theme;
}

class SwCodeEditor : public SwPlainTextEdit {
    SW_OBJECT(SwCodeEditor, SwPlainTextEdit)

public:
    explicit SwCodeEditor(SwWidget* parent = nullptr)
        : SwPlainTextEdit(parent) {
        setWordWrapEnabled(false);
        setFont(SwFont(L"Consolas", 10, Medium));
        setTheme(swCodeEditorDefaultTheme());
        m_document = new SwTextDocument(this);
        m_ownsDocument = true;
        bindDocument_();
        SwObject::connect(this, &SwPlainTextEdit::textChanged, this, [this]() {
            refreshFolding_();
        });
        refreshFolding_();
    }

    void setPlainText(const SwString& text) override {
        ensureDocument_();
        if (m_syncingFromDocument) {
            SwPlainTextEdit::setPlainText(text);
            return;
        }
        m_syncingToDocument = true;
        m_document->setPlainText(text);
        m_syncingToDocument = false;
        SwPlainTextEdit::setPlainText(text);
        if (m_highlighter && m_highlighter->document() != m_document) {
            m_highlighter->setDocument(m_document);
        }
    }

    void appendPlainText(const SwString& text) override {
        SwString updated = toPlainText();
        if (!updated.isEmpty() && !updated.endsWith("\n")) {
            updated.append("\n");
        }
        updated.append(text);
        setPlainText(updated);
    }

    void clear() override {
        setPlainText(SwString());
    }

    SwTextDocument* document() const {
        return m_document;
    }

    void setDocument(SwTextDocument* doc) {
        if (doc == m_document && doc != nullptr) {
            return;
        }
        if (m_document) {
            SwObject::disconnect(m_document, this);
            if (m_ownsDocument) {
                delete m_document;
            }
        }
        m_document = doc;
        m_ownsDocument = false;
        ensureDocument_();
        bindDocument_();
        syncFromDocument_();
        if (m_highlighter) {
            m_highlighter->setDocument(m_document);
        }
        if (m_diagnosticsProvider) {
            m_diagnosticsProvider->setDocument(m_document);
            setDiagnostics(m_diagnosticsProvider->diagnostics());
        }
        refreshFolding_();
    }

    SwTextCursor textCursor() const {
        SwTextCursor cursor(m_document);
        cursor.setPosition(static_cast<int>(m_cursorPos));
        if (hasSelectedText()) {
            cursor.select(static_cast<int>(m_selectionStart), static_cast<int>(m_selectionEnd));
        }
        return cursor;
    }

    void setTextCursor(const SwTextCursor& cursor) {
        const size_t oldPos = m_cursorPos;
        const size_t oldSelStart = m_selectionStart;
        const size_t oldSelEnd = m_selectionEnd;

        m_cursorPos = std::min(static_cast<size_t>(std::max(0, cursor.position())), m_text.size());
        m_selectionStart = std::min(static_cast<size_t>(std::max(0, cursor.anchor())), m_text.size());
        m_selectionEnd = m_cursorPos;

        ensureCursorVisibleForCode_();
        update();
        emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
    }

    void setSyntaxHighlighter(SwSyntaxHighlighter* highlighter) {
        if (m_highlighter == highlighter) {
            return;
        }
        if (m_highlighter) {
            SwObject::disconnect(m_highlighter, this);
        }
        m_highlighter = highlighter;
        if (m_highlighter) {
            m_highlighter->setDocument(m_document);
            SwObject::connect(m_highlighter, &SwSyntaxHighlighter::formattingChanged, this, [this]() {
                update();
            });
        }
        update();
    }

    SwSyntaxHighlighter* syntaxHighlighter() const {
        return m_highlighter;
    }

    void setDiagnostics(const SwList<SwTextDiagnostic>& diagnostics) {
        m_diagnostics = diagnostics;
        if (m_diagnostics.isEmpty()) {
            setToolTips(SwString());
        }
        update();
    }

    SwList<SwTextDiagnostic> diagnostics() const {
        return m_diagnostics;
    }

    void clearDiagnostics() {
        if (m_diagnostics.isEmpty()) {
            return;
        }
        m_diagnostics.clear();
        setToolTips(SwString());
        update();
    }

    void setDiagnosticsProvider(SwTextDiagnosticsProvider* provider) {
        if (m_diagnosticsProvider == provider) {
            return;
        }
        if (m_diagnosticsProvider) {
            SwObject::disconnect(m_diagnosticsProvider, this);
        }
        m_diagnosticsProvider = provider;
        if (m_diagnosticsProvider) {
            m_diagnosticsProvider->setDocument(m_document);
            SwObject::connect(m_diagnosticsProvider, &SwTextDiagnosticsProvider::diagnosticsChanged, this, [this]() {
                if (!m_diagnosticsProvider) {
                    return;
                }
                setDiagnostics(m_diagnosticsProvider->diagnostics());
            });
            setDiagnostics(m_diagnosticsProvider->diagnostics());
        } else {
            clearDiagnostics();
        }
    }

    SwTextDiagnosticsProvider* diagnosticsProvider() const {
        return m_diagnosticsProvider;
    }

    void setCompleter(SwCompleter* completer) {
        if (m_completer == completer) {
            return;
        }
        if (m_completer) {
            SwObject::disconnect(m_completer, this);
        }
        m_completer = completer;
        if (m_completer) {
            m_completer->setWidget(this);
            SwObject::connect(m_completer, &SwCompleter::activated, this, [this](const SwString& completion) {
                insertCompletion_(completion);
            });
        }
    }

    SwCompleter* completer() const {
        return m_completer;
    }

    void triggerCompletion() {
        if (!m_completer) {
            return;
        }
        refreshCompletionPopup_(true);
    }

    void setExtraSelections(const SwList<SwTextExtraSelection>& selections) {
        m_extraSelections = selections;
        update();
    }

    SwList<SwTextExtraSelection> extraSelections() const {
        return m_extraSelections;
    }

    void setLineNumbersVisible(bool on) {
        if (m_lineNumbersVisible == on) {
            return;
        }
        m_lineNumbersVisible = on;
        updateRequest(rect(), 0);
        update();
    }

    bool lineNumbersVisible() const {
        return m_lineNumbersVisible;
    }

    void setCodeFoldingEnabled(bool on) {
        if (m_codeFoldingEnabled == on) {
            return;
        }
        m_codeFoldingEnabled = on;
        refreshFolding_();
        updateRequest(rect(), 0);
        update();
    }

    bool codeFoldingEnabled() const {
        return m_codeFoldingEnabled;
    }

    void setAutoCompletionEnabled(bool on) {
        if (m_autoCompletionEnabled == on) {
            return;
        }
        m_autoCompletionEnabled = on;
        if (!m_autoCompletionEnabled && m_completer) {
            m_completer->hidePopup();
        }
    }

    bool autoCompletionEnabled() const {
        return m_autoCompletionEnabled;
    }

    void setAutoCompletionMinPrefixLength(int length) {
        m_autoCompletionMinPrefixLength = std::max(1, length);
    }

    int autoCompletionMinPrefixLength() const {
        return m_autoCompletionMinPrefixLength;
    }

    void setIndentSize(int spaces) {
        m_indentSize = std::max(1, spaces);
    }

    int indentSize() const {
        return m_indentSize;
    }

    void setTheme(const SwCodeEditorTheme& theme) {
        m_theme = theme;
        m_focusAccent = theme.focusBorderColor;
        applyThemeStyle_();
        update();
    }

    SwCodeEditorTheme theme() const {
        return m_theme;
    }

    int lineNumberAreaWidth() const {
        const int lineCount = std::max(1, m_document ? m_document->blockCount() : static_cast<int>(m_lines.size()));
        const SwString digits = SwString::number(lineCount);
        const int textWidth = SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(),
                                                                     digits,
                                                                     getFont(),
                                                                     digits.size(),
                                                                     64);
        if (!m_lineNumbersVisible) {
            return 0;
        }
        return textWidth + 18 + foldMarkerAreaWidth_();
    }

    int firstVisibleBlock() const {
        if (foldingEnabled_()) {
            return documentLineForVisibleRow_(m_firstVisibleLine);
        }
        return std::max(0, std::min(m_firstVisibleLine, std::max(0, static_cast<int>(m_lines.size()) - 1)));
    }

    SwRect blockBoundingRect(int blockNumber) const {
        const SwRect inner = textRect_();
        const int lineHeight = lineHeightPx();
        const int visibleRow = foldingEnabled_()
            ? anchorVisibleRowForDocumentLine_(blockNumber)
            : blockNumber;
        const int y = inner.y + (visibleRow - m_firstVisibleLine) * lineHeight;
        return SwRect{inner.x, y, inner.width, lineHeight};
    }

    SwRect cursorRect() const {
        const SwRect inner = textRect_();
        const CursorInfo ci = cursorInfo();
        const int visualLine = foldingEnabled_() ? visibleLineForPainting_(ci.line) : ci.line;
        const int firstRow = std::max(0, m_firstVisibleLine);
        const int cursorRow = foldingEnabled_() ? visibleRowForDocumentLine_(visualLine) : visualLine;
        if (cursorRow < firstRow) {
            return SwRect{inner.x, inner.y, 1, lineHeightPx()};
        }
        const int row = cursorRow - firstRow;
        const SwString& lineText = m_lines[visualLine];
        const size_t column = (visualLine == ci.line)
            ? static_cast<size_t>(std::max(0, ci.col))
            : lineText.size();
        const size_t clampedCol = std::min(column, lineText.size());
        const int x = inner.x + SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(),
                                                                        lineText,
                                                                        getFont(),
                                                                        clampedCol,
                                                                        std::max(1, inner.width));
        const int y = inner.y + row * lineHeightPx();
        return SwRect{x, y, 1, lineHeightPx()};
    }

    DECLARE_SIGNAL_VOID(cursorPositionChanged)
    DECLARE_SIGNAL_VOID(selectionChanged)
    DECLARE_SIGNAL(updateRequest, const SwRect&, int)

protected:
    EditState captureEditState() const override {
        EditState state = SwPlainTextEdit::captureEditState();
        const SwString stateText = state.text;
        state.applyExtra = [this, stateText]() {
            SwCodeEditor* self = const_cast<SwCodeEditor*>(this);
            if (!self->m_document) {
                return;
            }
            self->m_syncingToDocument = true;
            self->m_document->setPlainText(stateText);
            self->m_syncingToDocument = false;
        };
        return state;
    }

    void resizeEvent(ResizeEvent* event) override {
        SwPlainTextEdit::resizeEvent(event);
        updateRequest(rect(), 0);
    }

    void paintEvent(PaintEvent* event) override {
        if (!isVisibleInHierarchy()) {
            return;
        }

        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect bounds = rect();
        StyleSheet* sheet = getToolSheet();

        SwColor bg = m_theme.backgroundColor;
        float bgAlpha = 1.0f;
        bool paintBackground = true;

        SwColor border = m_theme.borderColor;
        int borderWidth = 1;
        int radius = m_theme.borderRadius;
        int radiusTL = radius;
        int radiusTR = radius;
        int radiusBR = radius;
        int radiusBL = radius;

        resolveBackground(sheet, bg, bgAlpha, paintBackground);
        resolveBorder(sheet, border, borderWidth, radius);
        radiusTL = radius;
        radiusTR = radius;
        radiusBR = radius;
        radiusBL = radius;
        resolveBorderCornerRadii(sheet, radiusTL, radiusTR, radiusBR, radiusBL);

        if (getEnable() && getFocus()) {
            border = m_theme.focusBorderColor;
        }

        if (paintBackground && bgAlpha > 0.0f) {
            if (radiusTL == radiusTR && radiusTR == radiusBR && radiusBR == radiusBL) {
                painter->fillRoundedRect(bounds, radiusTL, bg, border, borderWidth);
            } else {
                painter->fillRoundedRect(bounds, radiusTL, radiusTR, radiusBR, radiusBL, bg, border, borderWidth);
            }
        } else {
            painter->drawRect(bounds, border, borderWidth);
        }

        const SwRect textRect = textRect_();
        const SwRect gutterRect = gutterRect_();
        const SwRect gutterVisualRect = gutterVisualRect_();
        const int lineHeight = lineHeightPx();
        const int visibleLines = (lineHeight > 0) ? std::max(1, textRect.height / lineHeight) : 1;
        clampFirstVisibleCodeLine_(visibleLines);

        const SwColor defaultTextColor = getEnable() ? m_theme.textColor : m_theme.disabledTextColor;
        const SwColor gutterBg = m_theme.gutterBackgroundColor;
        const SwColor gutterText = m_theme.gutterTextColor;
        const SwColor currentLineBg = m_theme.currentLineBackgroundColor;
        const SwColor selectionBg = m_theme.selectionBackgroundColor;

        if (m_lineNumbersVisible && gutterVisualRect.width > 0) {
            const int gutterRadiusTL = clampInt(radiusTL - borderWidth, 0, std::min(gutterVisualRect.width, gutterVisualRect.height) / 2);
            const int gutterRadiusBL = clampInt(radiusBL - borderWidth, 0, std::min(gutterVisualRect.width, gutterVisualRect.height) / 2);
            if (gutterRadiusTL == 0 && gutterRadiusBL == 0) {
                painter->fillRect(gutterVisualRect, gutterBg, gutterBg, 0);
            } else {
                painter->fillRoundedRect(gutterVisualRect,
                                         gutterRadiusTL,
                                         0,
                                         0,
                                         gutterRadiusBL,
                                         gutterBg,
                                         gutterBg,
                                         0);
            }
            painter->drawLine(gutterVisualRect.x + gutterVisualRect.width - 1,
                              gutterVisualRect.y,
                              gutterVisualRect.x + gutterVisualRect.width - 1,
                              gutterVisualRect.y + gutterVisualRect.height,
                              m_theme.gutterSeparatorColor,
                              1);
        }

        painter->pushClipRect(textRect);

        const int first = std::max(0, m_firstVisibleLine);
        const int last = std::min(first + visibleLines + 1, visibleLineCount_());
        const bool hasSel = hasSelectedText();
        const size_t selMin = selectionMin_();
        const size_t selMax = selectionMax_();
        const int highlightedLine = visibleLineForPainting_(cursorInfo().line);
        const int hoveredFoldRegionIndex = hoveredExpandedFoldRegionIndex_();
        const SwColor foldHoverBg = mixColor_(bg, selectionBg, 5, 2);

        for (int row = first; row < last; ++row) {
            const int i = documentLineForVisibleRow_(row);
            SwRect lineRect{textRect.x, textRect.y + (row - first) * lineHeight, textRect.width, lineHeight};

            if (hoveredFoldRegionIndex >= 0) {
                const FoldRegion& hoveredRegion = m_foldRegions[static_cast<size_t>(hoveredFoldRegionIndex)];
                if (i >= hoveredRegion.startLine && i <= hoveredRegion.endLine) {
                    painter->fillRect(lineRect, foldHoverBg, foldHoverBg, 0);
                }
            }

            if (m_highlightCurrentLine && i == highlightedLine) {
                painter->fillRect(lineRect, currentLineBg, currentLineBg, 0);
            }

            for (int es = 0; es < m_extraSelections.size(); ++es) {
                const SwTextExtraSelection& selection = m_extraSelections[es];
                if (!selection.format.hasBackground()) {
                    continue;
                }
                const size_t selectionStart = selection.cursor.hasSelection()
                    ? static_cast<size_t>(std::max(0, selection.cursor.selectionStart()))
                    : static_cast<size_t>(std::max(0, selection.cursor.position()));
                const size_t selectionEnd = selection.cursor.hasSelection()
                    ? static_cast<size_t>(std::max(0, selection.cursor.selectionEnd()))
                    : selectionStart;
                const int selectionLine = lineIndexForPosition_(selectionStart);
                if (selection.fullWidthSelection) {
                    if (selectionLine == i) {
                        painter->fillRect(lineRect,
                                          selection.format.background(),
                                          selection.format.background(),
                                          0);
                    }
                    continue;
                }
                if (selectionLine != i || selectionEnd <= selectionStart || i >= m_lineStarts.size()) {
                    continue;
                }
                const size_t lineStart = m_lineStarts[i];
                const size_t lineLen = m_lines[i].size();
                const size_t lineEnd = lineStart + lineLen;
                const size_t segStart = (std::max)(selectionStart, lineStart);
                const size_t segEnd = (std::min)(selectionEnd, lineEnd);
                if (segStart >= segEnd) {
                    continue;
                }
                const int x1 = textRect.x + SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(),
                                                                                     m_lines[i],
                                                                                     getFont(),
                                                                                     segStart - lineStart,
                                                                                     std::max(1, textRect.width));
                const int x2 = textRect.x + SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(),
                                                                                     m_lines[i],
                                                                                     getFont(),
                                                                                     segEnd - lineStart,
                                                                                     std::max(1, textRect.width));
                const int left = (std::min)(x1, x2);
                const int right = (std::max)(x1, x2);
                painter->fillRect(SwRect{left, lineRect.y, right - left, lineRect.height},
                                  selection.format.background(),
                                  selection.format.background(),
                                  0);
            }

            if (hasSel && i < m_lineStarts.size()) {
                const size_t lineStart = m_lineStarts[i];
                const size_t lineLen = m_lines[i].size();
                const size_t lineEnd = lineStart + lineLen;
                const size_t segStart = (std::max)(selMin, lineStart);
                const size_t segEnd = (std::min)(selMax, lineEnd);
                if (segStart < segEnd) {
                    const size_t startCol = segStart - lineStart;
                    const size_t endCol = segEnd - lineStart;
                    const int x1 = textRect.x + SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(),
                                                                                         m_lines[i],
                                                                                         getFont(),
                                                                                         startCol,
                                                                                         std::max(1, textRect.width));
                    const int x2 = textRect.x + SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(),
                                                                                         m_lines[i],
                                                                                         getFont(),
                                                                                         endCol,
                                                                                         std::max(1, textRect.width));
                    const int left = (std::min)(x1, x2);
                    const int right = (std::max)(x1, x2);
                    painter->fillRect(SwRect{left, lineRect.y, right - left, lineRect.height},
                                      selectionBg,
                                      selectionBg,
                                      0);
                }
            }

            const int contentEndX = drawLineText_(painter, lineRect, i, defaultTextColor);
            drawCollapsedLineIndicator_(painter, lineRect, i, contentEndX);
        }

        if (m_text.isEmpty() && !m_placeholder.isEmpty() && !getFocus()) {
            SwRect placeholderRect = textRect;
            placeholderRect.height = lineHeight;
            painter->drawText(placeholderRect,
                              m_placeholder,
                              DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              m_theme.placeholderColor,
                              getFont());
        }

        if (getFocus() && m_caretVisible) {
            const SwRect caret = cursorRect();
            const int top = caret.y + 4;
            const int bottom = caret.y + caret.height - 4;
            painter->drawLine(caret.x, top, caret.x, bottom, defaultTextColor, 1);
        }

        painter->popClipRect();

        if (m_lineNumbersVisible && gutterRect.width > 0) {
            painter->pushClipRect(gutterRect);
            drawFoldGuides_(painter, gutterRect, first, last, lineHeight);
            for (int row = first; row < last; ++row) {
                const int i = documentLineForVisibleRow_(row);
                const int y = gutterRect.y + (row - first) * lineHeight;
                const SwRect numberRect{gutterRect.x + 4, y, std::max(0, gutterRect.width - foldMarkerAreaWidth_() - 8), lineHeight};
                const SwColor numberColor = (i == highlightedLine) ? m_theme.currentLineNumberColor : gutterText;
                painter->drawText(numberRect,
                                  SwString::number(i + 1),
                                  DrawTextFormats(DrawTextFormat::Right | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                  numberColor,
                                  getFont());
                drawFoldMarker_(painter, gutterRect, row, first, lineHeight, i, numberColor);
            }
            painter->popClipRect();
        }

        painter->finalize();
    }

    void updateCursorFromPosition(int px, int py) override {
        const SwRect textRect = textRect_();
        const int lineHeight = lineHeightPx();
        if (lineHeight <= 0 || m_lines.isEmpty()) {
            return;
        }

        if (px < textRect.x) {
            px = textRect.x;
        }
        if (py < textRect.y) {
            py = textRect.y;
        }

        int row = (py - textRect.y) / lineHeight;
        row = std::max(0, row);
        const int visibleRow = clampInt(m_firstVisibleLine + row, 0, visibleLineCount_() - 1);
        const int lineIdx = documentLineForVisibleRow_(visibleRow);
        const int relativeX = std::max(0, px - textRect.x);
        const size_t col = SwWidgetPlatformAdapter::characterIndexAtPosition(nativeWindowHandle(),
                                                                             m_lines[lineIdx],
                                                                             getFont(),
                                                                             relativeX,
                                                                             std::max(1, textRect.width));
        m_cursorPos = std::min(m_lineStarts[lineIdx] + std::min(col, m_lines[lineIdx].size()), m_text.size());
    }

    void insertTextAt(size_t pos, const SwString& text) override {
        if (m_syncingFromDocument || !m_document) {
            SwPlainTextEdit::insertTextAt(pos, text);
            return;
        }
        SwString updated = m_text;
        updated.insert(std::min(pos, updated.size()), text);
        m_syncingToDocument = true;
        m_document->setPlainText(updated);
        m_document->setModified(true);
        m_syncingToDocument = false;
        m_text = updated;
    }

    void eraseTextAt(size_t pos, size_t len) override {
        if (m_syncingFromDocument || !m_document) {
            SwPlainTextEdit::eraseTextAt(pos, len);
            return;
        }
        if (len == 0 || pos >= m_text.size()) {
            return;
        }
        SwString updated = m_text;
        updated.erase(pos, std::min(len, updated.size() - pos));
        m_syncingToDocument = true;
        m_document->setPlainText(updated);
        m_document->setModified(true);
        m_syncingToDocument = false;
        m_text = updated;
    }

    void mousePressEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        const size_t oldPos = m_cursorPos;
        const size_t oldSelStart = m_selectionStart;
        const size_t oldSelEnd = m_selectionEnd;
        const int oldFirstVisibleLine = m_firstVisibleLine;
        if (m_completer && m_completer->popupVisible()) {
            m_completer->hidePopup();
        }
        int foldLine = -1;
        if (event->button() == SwMouseButton::Left && isPointInFoldMarker_(event->x(), event->y(), &foldLine)) {
            if (toggleFoldAtLine_(foldLine)) {
                event->accept();
                emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
                return;
            }
        }
        SwPlainTextEdit::mousePressEvent(event);
        restoreViewportAfterMouseInteraction_(oldFirstVisibleLine);
        emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
    }

    void mouseMoveEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        const size_t oldPos = m_cursorPos;
        const size_t oldSelStart = m_selectionStart;
        const size_t oldSelEnd = m_selectionEnd;
        const int oldFirstVisibleLine = m_firstVisibleLine;
        SwPlainTextEdit::mouseMoveEvent(event);
        restoreViewportAfterMouseInteraction_(oldFirstVisibleLine);
        updateHoverToolTip_(event);
        emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
    }

    void mouseReleaseEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        const size_t oldPos = m_cursorPos;
        const size_t oldSelStart = m_selectionStart;
        const size_t oldSelEnd = m_selectionEnd;
        const int oldFirstVisibleLine = m_firstVisibleLine;
        SwPlainTextEdit::mouseReleaseEvent(event);
        restoreViewportAfterMouseInteraction_(oldFirstVisibleLine);
        emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
    }

    void mouseDoubleClickEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        const size_t oldPos = m_cursorPos;
        const size_t oldSelStart = m_selectionStart;
        const size_t oldSelEnd = m_selectionEnd;
        const int oldFirstVisibleLine = m_firstVisibleLine;
        SwPlainTextEdit::mouseDoubleClickEvent(event);
        restoreViewportAfterMouseInteraction_(oldFirstVisibleLine);
        emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
    }

    void wheelEvent(WheelEvent* event) override {
        if (!event) {
            return;
        }
        if (!isPointInside(event->x(), event->y()) || event->isShiftPressed()) {
            SwPlainTextEdit::wheelEvent(event);
            return;
        }

        const int lineHeight = lineHeightPx();
        if (lineHeight <= 0) {
            SwPlainTextEdit::wheelEvent(event);
            return;
        }

        const int visibleLines = std::max(1, textRect_().height / lineHeight);
        clampFirstVisibleCodeLine_(visibleLines);

        int steps = event->delta() / 120;
        if (steps == 0) {
            steps = (event->delta() > 0) ? 1 : (event->delta() < 0 ? -1 : 0);
        }
        if (steps == 0) {
            SwPlainTextEdit::wheelEvent(event);
            return;
        }

        const int maxFirst = std::max(0, visibleLineCount_() - visibleLines);
        m_firstVisibleLine = clampInt(m_firstVisibleLine - steps, 0, maxFirst);
        event->accept();
        update();
    }

    void keyPressEvent(KeyEvent* event) override {
        if (!event) {
            return;
        }

        const size_t oldPos = m_cursorPos;
        const size_t oldSelStart = m_selectionStart;
        const size_t oldSelEnd = m_selectionEnd;
        const int key = event->key();
        const wchar_t typedChar = typedCharacter_(event);

        if (m_completer && m_completer->handleEditorKeyPress(event)) {
            emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
            update();
            return;
        }

        const bool ctrlSpace = event->isCtrlPressed() && (key == 32 || event->text() == L' ');
        if (ctrlSpace) {
            triggerCompletion();
            event->accept();
            return;
        }

        const bool plainTab = !event->isCtrlPressed() && !event->isAltPressed() && key == 9;
        if (plainTab) {
            if (event->isShiftPressed()) {
                unindentCurrentLine_();
            } else {
                insertSoftTab_();
            }
            if (m_completer) {
                m_completer->hidePopup();
            }
            ensureCursorVisibleForCode_();
            update();
            emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
            event->accept();
            return;
        }

        const bool plainReturn = !event->isCtrlPressed() &&
                                 !event->isAltPressed() &&
                                 SwWidgetPlatformAdapter::isReturnKey(key);
        if (plainReturn) {
            insertIndentedNewLine_();
            if (m_completer) {
                m_completer->hidePopup();
            }
            ensureCursorVisibleForCode_();
            update();
            emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
            event->accept();
            return;
        }

        const bool plainClosingBrace = typedChar == L'}';
        if (plainClosingBrace && shouldAutoIndentClosingBrace_()) {
            insertAutoIndentedClosingBrace_();
            if (m_completer) {
                m_completer->hidePopup();
            }
            ensureCursorVisibleForCode_();
            update();
            emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
            event->accept();
            return;
        }

        const bool isDeletionKey = !event->isCtrlPressed() &&
                                   !event->isAltPressed() &&
                                   (SwWidgetPlatformAdapter::isBackspaceKey(key) ||
                                    SwWidgetPlatformAdapter::isDeleteKey(key));
        const bool isTextInput = !event->isCtrlPressed() &&
                                 !event->isAltPressed() &&
                                 event->text() != L'\0' &&
                                 std::iswcntrl(static_cast<wint_t>(event->text())) == 0;
        const bool shouldRefreshCompletion = isTextInput || isDeletionKey || (m_completer && m_completer->popupVisible());

        SwPlainTextEdit::keyPressEvent(event);

        if (foldingEnabled_()) {
            normalizeCursorAfterKeyEvent_(event);
            ensureCursorVisibleForCode_();
        }

        if (m_completer && shouldRefreshCompletion) {
            refreshCompletionPopup_(false);
        }

        emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
    }

private:
    enum class FoldMoveBias {
        Preserve,
        PreferStart,
        PreferEnd
    };

    struct FoldRegion {
        int startLine{0};
        int endLine{0};
        bool collapsed{false};
    };

    bool foldingEnabled_() const {
        return m_codeFoldingEnabled && !wordWrapEnabled() && !m_lines.isEmpty();
    }

    int foldMarkerRightInset_() const {
        return 4;
    }

    int foldMarkerAreaWidth_() const {
        return (foldingEnabled_() && m_lineNumbersVisible) ? 18 : 0;
    }

    int visibleLineCount_() const {
        if (m_visibleLineIndices.isEmpty()) {
            return std::max(1, static_cast<int>(m_lines.size()));
        }
        return static_cast<int>(m_visibleLineIndices.size());
    }

    int documentLineForVisibleRow_(int row) const {
        if (m_visibleLineIndices.isEmpty()) {
            return clampInt(row, 0, std::max(0, static_cast<int>(m_lines.size()) - 1));
        }
        const int clampedRow = clampInt(row, 0, static_cast<int>(m_visibleLineIndices.size()) - 1);
        return m_visibleLineIndices[static_cast<size_t>(clampedRow)];
    }

    int visibleRowForDocumentLine_(int line) const {
        if (line < 0 || line >= static_cast<int>(m_lines.size())) {
            return -1;
        }
        if (m_visibleRowByLine.isEmpty()) {
            return line;
        }
        return m_visibleRowByLine[static_cast<size_t>(line)];
    }

    int anchorVisibleRowForDocumentLine_(int line) const {
        const int visibleRow = visibleRowForDocumentLine_(line);
        if (visibleRow >= 0) {
            return visibleRow;
        }

        const int collapsedRegion = collapsedRegionContainingLine_(line);
        if (collapsedRegion >= 0) {
            return visibleRowForDocumentLine_(m_foldRegions[static_cast<size_t>(collapsedRegion)].startLine);
        }
        return clampInt(line, 0, std::max(0, visibleLineCount_() - 1));
    }

    int visibleLineForPainting_(int line) const {
        const int collapsedRegion = collapsedRegionContainingLine_(line);
        if (collapsedRegion >= 0) {
            return m_foldRegions[static_cast<size_t>(collapsedRegion)].startLine;
        }
        return clampInt(line, 0, std::max(0, static_cast<int>(m_lines.size()) - 1));
    }

    int visibleLinesForViewport_() const {
        const int lineHeight = lineHeightPx();
        if (lineHeight <= 0) {
            return 1;
        }
        return std::max(1, textRect_().height / lineHeight);
    }

    static SwColor mixColor_(const SwColor& lhs, const SwColor& rhs, int lhsWeight, int rhsWeight) {
        const int totalWeight = std::max(1, lhsWeight + rhsWeight);
        return SwColor{
            (lhs.r * lhsWeight + rhs.r * rhsWeight) / totalWeight,
            (lhs.g * lhsWeight + rhs.g * rhsWeight) / totalWeight,
            (lhs.b * lhsWeight + rhs.b * rhsWeight) / totalWeight
        };
    }

    void clampFirstVisibleCodeLine_(int visibleLines) {
        visibleLines = std::max(1, visibleLines);
        const int maxFirst = std::max(0, visibleLineCount_() - visibleLines);
        m_firstVisibleLine = clampInt(m_firstVisibleLine, 0, maxFirst);
    }

    void ensureCursorVisibleForCode_() {
        if (m_lines.isEmpty()) {
            m_firstVisibleLine = 0;
            return;
        }

        const int visibleLines = visibleLinesForViewport_();
        clampFirstVisibleCodeLine_(visibleLines);

        const int line = cursorInfo().line;
        const int visibleRow = anchorVisibleRowForDocumentLine_(line);
        if (visibleRow < m_firstVisibleLine) {
            m_firstVisibleLine = visibleRow;
        } else if (visibleRow >= m_firstVisibleLine + visibleLines) {
            m_firstVisibleLine = visibleRow - visibleLines + 1;
        }
        clampFirstVisibleCodeLine_(visibleLines);
    }

    void restoreViewportAfterMouseInteraction_(int previousFirstVisibleLine) {
        m_firstVisibleLine = previousFirstVisibleLine;
        clampFirstVisibleCodeLine_(visibleLinesForViewport_());
        update();
    }

    int hoveredExpandedFoldRegionIndex_() const {
        const int regionIndex = foldRegionIndexForStartLine_(m_hoveredFoldStartLine);
        if (regionIndex < 0) {
            return -1;
        }
        return m_foldRegions[static_cast<size_t>(regionIndex)].collapsed ? -1 : regionIndex;
    }

    int collapsedRegionContainingLine_(int line) const {
        int bestIndex = -1;
        for (int i = 0; i < static_cast<int>(m_foldRegions.size()); ++i) {
            const FoldRegion& region = m_foldRegions[static_cast<size_t>(i)];
            if (!region.collapsed || line <= region.startLine || line > region.endLine) {
                continue;
            }
            if (bestIndex < 0 || region.startLine < m_foldRegions[static_cast<size_t>(bestIndex)].startLine) {
                bestIndex = i;
            }
        }
        return bestIndex;
    }

    int foldRegionIndexForStartLine_(int line) const {
        int bestIndex = -1;
        for (int i = 0; i < static_cast<int>(m_foldRegions.size()); ++i) {
            const FoldRegion& region = m_foldRegions[static_cast<size_t>(i)];
            if (region.startLine != line) {
                continue;
            }
            if (bestIndex < 0 || region.endLine > m_foldRegions[static_cast<size_t>(bestIndex)].endLine) {
                bestIndex = i;
            }
        }
        return bestIndex;
    }

    void coerceCursorIntoVisibleContent_(FoldMoveBias bias, bool preserveSelection) {
        if (!foldingEnabled_() || m_lines.isEmpty()) {
            return;
        }

        const int currentLine = lineIndexForPosition_(m_cursorPos);
        const int collapsedRegion = collapsedRegionContainingLine_(currentLine);
        if (collapsedRegion < 0) {
            return;
        }

        const FoldRegion& region = m_foldRegions[static_cast<size_t>(collapsedRegion)];
        int targetLine = region.startLine;
        if (bias == FoldMoveBias::PreferEnd && region.endLine + 1 < static_cast<int>(m_lines.size())) {
            targetLine = region.endLine + 1;
        }

        const size_t currentLineStart = m_lineStarts[static_cast<size_t>(currentLine)];
        const size_t currentColumn = (m_cursorPos > currentLineStart) ? (m_cursorPos - currentLineStart) : 0;
        const size_t targetStart = m_lineStarts[static_cast<size_t>(targetLine)];
        const size_t targetColumn = std::min(currentColumn, m_lines[static_cast<size_t>(targetLine)].size());
        m_cursorPos = std::min(targetStart + targetColumn, m_text.size());
        if (preserveSelection) {
            m_selectionEnd = m_cursorPos;
        } else {
            m_selectionStart = m_selectionEnd = m_cursorPos;
        }
    }

    void normalizeCursorAfterKeyEvent_(const KeyEvent* event) {
        if (!event || !foldingEnabled_()) {
            return;
        }

        FoldMoveBias bias = FoldMoveBias::Preserve;
        if (SwWidgetPlatformAdapter::isUpArrowKey(event->key())) {
            bias = FoldMoveBias::PreferStart;
        } else if (SwWidgetPlatformAdapter::isDownArrowKey(event->key())) {
            bias = FoldMoveBias::PreferEnd;
        }
        coerceCursorIntoVisibleContent_(bias, event->isShiftPressed());
    }

    SwVector<FoldRegion> parseFoldRegions_() const {
        SwVector<FoldRegion> parsed;
        if (!foldingEnabled_()) {
            return parsed;
        }

        enum class ParseState {
            Normal,
            LineComment,
            BlockComment,
            StringLiteral,
            CharLiteral
        };

        struct BraceEntry {
            int line{0};
        };

        ParseState state = ParseState::Normal;
        SwVector<BraceEntry> braceStack;
        bool escaped = false;
        int currentLine = 0;

        for (size_t i = 0; i < m_text.size(); ++i) {
            const char ch = m_text[i];
            const char next = (i + 1 < m_text.size()) ? m_text[i + 1] : '\0';

            switch (state) {
                case ParseState::Normal:
                    if (ch == '/' && next == '/') {
                        state = ParseState::LineComment;
                        ++i;
                    } else if (ch == '/' && next == '*') {
                        state = ParseState::BlockComment;
                        ++i;
                    } else if (ch == '"') {
                        state = ParseState::StringLiteral;
                        escaped = false;
                    } else if (ch == '\'') {
                        state = ParseState::CharLiteral;
                        escaped = false;
                    } else if (ch == '{') {
                        BraceEntry entry;
                        entry.line = currentLine;
                        braceStack.push_back(entry);
                    } else if (ch == '}') {
                        if (!braceStack.isEmpty()) {
                            const BraceEntry entry = braceStack.back();
                            braceStack.removeAt(static_cast<int>(braceStack.size()) - 1);
                            if (currentLine > entry.line) {
                                FoldRegion region;
                                region.startLine = entry.line;
                                region.endLine = currentLine;
                                parsed.push_back(region);
                            }
                        }
                    }
                    break;

                case ParseState::LineComment:
                    if (ch == '\n') {
                        state = ParseState::Normal;
                    }
                    break;

                case ParseState::BlockComment:
                    if (ch == '*' && next == '/') {
                        state = ParseState::Normal;
                        ++i;
                    }
                    break;

                case ParseState::StringLiteral:
                    if (!escaped && ch == '"') {
                        state = ParseState::Normal;
                    }
                    escaped = (!escaped && ch == '\\');
                    break;

                case ParseState::CharLiteral:
                    if (!escaped && ch == '\'') {
                        state = ParseState::Normal;
                    }
                    escaped = (!escaped && ch == '\\');
                    break;
            }

            if (ch == '\n') {
                ++currentLine;
                if (state == ParseState::LineComment) {
                    state = ParseState::Normal;
                }
            }
        }

        std::sort(parsed.begin(), parsed.end(), [](const FoldRegion& lhs, const FoldRegion& rhs) {
            if (lhs.startLine != rhs.startLine) {
                return lhs.startLine < rhs.startLine;
            }
            return lhs.endLine > rhs.endLine;
        });
        return parsed;
    }

    void rebuildVisibleLineCache_() {
        m_visibleLineIndices.clear();
        m_visibleRowByLine.clear();

        const int lineCount = static_cast<int>(m_lines.size());
        if (lineCount <= 0) {
            return;
        }

        m_visibleRowByLine = SwVector<int>(static_cast<size_t>(lineCount), -1);
        SwVector<char> hidden(static_cast<size_t>(lineCount), 0);

        for (int i = 0; i < static_cast<int>(m_foldRegions.size()); ++i) {
            const FoldRegion& region = m_foldRegions[static_cast<size_t>(i)];
            if (!region.collapsed) {
                continue;
            }
            for (int line = region.startLine + 1; line <= region.endLine && line < lineCount; ++line) {
                hidden[static_cast<size_t>(line)] = 1;
            }
        }

        for (int line = 0; line < lineCount; ++line) {
            if (hidden[static_cast<size_t>(line)] != 0) {
                continue;
            }
            m_visibleRowByLine[static_cast<size_t>(line)] = static_cast<int>(m_visibleLineIndices.size());
            m_visibleLineIndices.push_back(line);
        }
    }

    void refreshFolding_() {
        SwMap<int, bool> collapsedByStartLine;
        for (int i = 0; i < static_cast<int>(m_foldRegions.size()); ++i) {
            const FoldRegion& region = m_foldRegions[static_cast<size_t>(i)];
            if (region.collapsed) {
                collapsedByStartLine[region.startLine] = true;
            }
        }

        m_foldRegions = parseFoldRegions_();
        for (int i = 0; i < static_cast<int>(m_foldRegions.size()); ++i) {
            FoldRegion& region = m_foldRegions[static_cast<size_t>(i)];
            if (collapsedByStartLine.contains(region.startLine)) {
                region.collapsed = collapsedByStartLine.value(region.startLine, false);
            }
        }

        rebuildVisibleLineCache_();
        coerceCursorIntoVisibleContent_(FoldMoveBias::PreferStart, false);
        clampFirstVisibleCodeLine_(visibleLinesForViewport_());
        ensureCursorVisibleForCode_();
        if (foldRegionIndexForStartLine_(m_hoveredFoldStartLine) < 0) {
            m_hoveredFoldStartLine = -1;
        }
    }

    bool toggleFoldAtLine_(int line) {
        const int regionIndex = foldRegionIndexForStartLine_(line);
        if (regionIndex < 0) {
            return false;
        }

        const int previousTopDocumentLine = documentLineForVisibleRow_(m_firstVisibleLine);
        m_foldRegions[static_cast<size_t>(regionIndex)].collapsed =
            !m_foldRegions[static_cast<size_t>(regionIndex)].collapsed;
        rebuildVisibleLineCache_();
        coerceCursorIntoVisibleContent_(FoldMoveBias::PreferStart, false);
        m_firstVisibleLine = anchorVisibleRowForDocumentLine_(previousTopDocumentLine);
        clampFirstVisibleCodeLine_(visibleLinesForViewport_());
        updateRequest(rect(), 0);
        update();
        return true;
    }

    SwRect foldMarkerRectForLine_(const SwRect& gutterRect, int visibleRow, int firstVisibleRow, int lineHeight) const {
        const int areaWidth = foldMarkerAreaWidth_();
        if (areaWidth <= 0) {
            return SwRect{};
        }

        const int markerSize = clampInt(lineHeight - 10, 8, 12);
        const int areaX = gutterRect.x + gutterRect.width - areaWidth - foldMarkerRightInset_();
        const int x = areaX + std::max(0, (areaWidth - markerSize) / 2);
        const int y = gutterRect.y + (visibleRow - firstVisibleRow) * lineHeight + std::max(0, (lineHeight - markerSize) / 2);
        return SwRect{x, y, markerSize, markerSize};
    }

    bool isPointInFoldMarker_(int px, int py, int* outLine = nullptr) const {
        if (!foldingEnabled_() || !m_lineNumbersVisible) {
            return false;
        }

        const SwRect gutterRect = gutterRect_();
        if (px < gutterRect.x || py < gutterRect.y || px > gutterRect.x + gutterRect.width || py > gutterRect.y + gutterRect.height) {
            return false;
        }

        const int lineHeight = lineHeightPx();
        if (lineHeight <= 0) {
            return false;
        }

        const int relativeY = py - gutterRect.y;
        const int row = std::max(0, relativeY / lineHeight);
        const int visibleRow = clampInt(m_firstVisibleLine + row, 0, visibleLineCount_() - 1);
        const int line = documentLineForVisibleRow_(visibleRow);
        if (foldRegionIndexForStartLine_(line) < 0) {
            return false;
        }

        const SwRect markerRect = foldMarkerRectForLine_(gutterRect, visibleRow, m_firstVisibleLine, lineHeight);
        const bool contains = px >= markerRect.x && px <= markerRect.x + markerRect.width &&
                              py >= markerRect.y && py <= markerRect.y + markerRect.height;
        if (contains && outLine) {
            *outLine = line;
        }
        return contains;
    }

    void drawFoldMarker_(SwPainter* painter,
                         const SwRect& gutterRect,
                         int visibleRow,
                         int firstVisibleRow,
                         int lineHeight,
                         int lineIndex,
                         const SwColor& baseColor) const {
        if (!painter) {
            return;
        }

        const int regionIndex = foldRegionIndexForStartLine_(lineIndex);
        if (regionIndex < 0) {
            return;
        }

        const FoldRegion& region = m_foldRegions[static_cast<size_t>(regionIndex)];
        const SwRect markerRect = foldMarkerRectForLine_(gutterRect, visibleRow, firstVisibleRow, lineHeight);
        const int cx = markerRect.x + markerRect.width / 2;
        const int cy = markerRect.y + markerRect.height / 2;
        const SwColor markerColor = (m_hoveredFoldStartLine == lineIndex)
            ? m_theme.currentLineNumberColor
            : baseColor;

        if (region.collapsed) {
            painter->drawLine(cx - 2, cy - 4, cx + 2, cy, markerColor, 1);
            painter->drawLine(cx - 2, cy + 4, cx + 2, cy, markerColor, 1);
        } else {
            painter->drawLine(cx - 4, cy - 2, cx, cy + 2, markerColor, 1);
            painter->drawLine(cx, cy + 2, cx + 4, cy - 2, markerColor, 1);
        }
    }

    void drawFoldGuides_(SwPainter* painter,
                         const SwRect& gutterRect,
                         int firstVisibleRow,
                         int lastVisibleRow,
                         int lineHeight) const {
        if (!painter || !foldingEnabled_() || foldMarkerAreaWidth_() <= 0) {
            return;
        }

        const int guideX = gutterRect.x + gutterRect.width - foldMarkerAreaWidth_() / 2 - foldMarkerRightInset_();
        for (int i = 0; i < static_cast<int>(m_foldRegions.size()); ++i) {
            const FoldRegion& region = m_foldRegions[static_cast<size_t>(i)];
            if (region.collapsed) {
                continue;
            }

            const int startRow = visibleRowForDocumentLine_(region.startLine);
            const int endRow = visibleRowForDocumentLine_(region.endLine);
            if (startRow < 0 || endRow < 0 || endRow <= startRow) {
                continue;
            }
            if (endRow < firstVisibleRow || startRow >= lastVisibleRow) {
                continue;
            }

            const int drawStart = std::max(startRow, firstVisibleRow);
            const int drawEnd = std::min(endRow, lastVisibleRow - 1);
            const int y1 = gutterRect.y + (drawStart - firstVisibleRow) * lineHeight + lineHeight / 2;
            const int y2 = gutterRect.y + (drawEnd - firstVisibleRow) * lineHeight + lineHeight / 2;
            painter->drawLine(guideX, y1, guideX, y2, m_theme.gutterSeparatorColor, 1);
        }
    }

    void drawCollapsedLineIndicator_(SwPainter* painter, const SwRect& lineRect, int lineIndex, int contentEndX) {
        if (!painter) {
            return;
        }

        const int regionIndex = foldRegionIndexForStartLine_(lineIndex);
        if (regionIndex < 0 || !m_foldRegions[static_cast<size_t>(regionIndex)].collapsed) {
            return;
        }

        const SwString label("...");
        const int labelWidth = SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(),
                                                                       label,
                                                                       getFont(),
                                                                       label.size(),
                                                                       std::max(1, lineRect.width));
        const int x = std::min(lineRect.x + lineRect.width - labelWidth - 12,
                               std::max(lineRect.x + 12, contentEndX + 8));
        painter->drawText(SwRect{x, lineRect.y, labelWidth + 4, lineRect.height},
                          label,
                          DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          m_theme.gutterTextColor,
                          getFont());

        const int lineStartX = x + labelWidth + 8;
        const int lineY = lineRect.y + lineRect.height / 2;
        if (lineStartX < lineRect.x + lineRect.width - 8) {
            painter->drawLine(lineStartX,
                              lineY,
                              lineRect.x + lineRect.width - 8,
                              lineY,
                              m_theme.gutterSeparatorColor,
                              1);
        }
    }

    SwString foldRegionPreviewText_(const FoldRegion& region) const {
        const SwString action = region.collapsed ? SwString("Click to expand lines ")
                                                 : SwString("Click to collapse lines ");
        SwString tooltip = action + SwString::number(region.startLine + 1) + "-" + SwString::number(region.endLine + 1);

        if (region.startLine < 0 || region.endLine >= static_cast<int>(m_lines.size()) || region.startLine > region.endLine) {
            return tooltip;
        }

        SwString blockText;
        for (int line = region.startLine; line <= region.endLine; ++line) {
            SwString previewLine = m_lines[static_cast<size_t>(line)];
            previewLine.replace(SwString("\t"), SwString("    "));
            blockText += previewLine;
            if (line < region.endLine) {
                blockText += "\n";
            }
        }

        if (blockText.isEmpty()) {
            return tooltip;
        }
        return tooltip + SwString("\n\n") + blockText;
    }

    SwString foldMarkerToolTipAt_(int px, int py) const {
        int line = -1;
        if (!isPointInFoldMarker_(px, py, &line)) {
            return SwString();
        }

        const int regionIndex = foldRegionIndexForStartLine_(line);
        if (regionIndex < 0) {
            return SwString();
        }
        return foldRegionPreviewText_(m_foldRegions[static_cast<size_t>(regionIndex)]);
    }

    void ensureDocument_() {
        if (m_document) {
            return;
        }
        m_document = new SwTextDocument(this);
        m_ownsDocument = true;
    }

    void bindDocument_() {
        if (!m_document) {
            return;
        }
        m_document->setDefaultFont(getFont());
        SwObject::connect(m_document, &SwTextDocument::contentsChanged, this, [this]() {
            if (!m_syncingToDocument) {
                syncFromDocument_();
            }
            update();
        });
        SwObject::connect(m_document, &SwTextDocument::blockCountChanged, this, [this]() {
            updateRequest(rect(), 0);
            update();
        });
    }

    void syncFromDocument_() {
        if (!m_document) {
            return;
        }
        m_syncingFromDocument = true;
        SwPlainTextEdit::setPlainText(m_document->toPlainText());
        m_syncingFromDocument = false;
    }

    SwRect gutterRect_() const {
        const SwRect bounds = rect();
        StyleSheet* sheet = const_cast<SwCodeEditor*>(this)->getToolSheet();
        const Padding pad = resolvePadding(sheet);
        const int borderWidth = resolvedBorderWidth_();
        const int width = lineNumberAreaWidth();
        return SwRect{
            bounds.x + borderWidth + pad.left,
            bounds.y + borderWidth + pad.top,
            std::max(0, width),
            std::max(0, bounds.height - 2 * borderWidth - (pad.top + pad.bottom))
        };
    }

    SwRect gutterVisualRect_() const {
        const int gutterWidth = lineNumberAreaWidth();
        if (gutterWidth <= 0) {
            return SwRect{};
        }

        const SwRect bounds = rect();
        StyleSheet* sheet = const_cast<SwCodeEditor*>(this)->getToolSheet();
        const Padding pad = resolvePadding(sheet);
        const int borderWidth = resolvedBorderWidth_();
        const int visualWidth = std::min(std::max(0, gutterWidth + pad.left), std::max(0, bounds.width - 2 * borderWidth));
        return SwRect{
            bounds.x + borderWidth,
            bounds.y + borderWidth,
            visualWidth,
            std::max(0, bounds.height - 2 * borderWidth)
        };
    }

    SwRect textRect_() const {
        const SwRect bounds = rect();
        StyleSheet* sheet = const_cast<SwCodeEditor*>(this)->getToolSheet();
        const Padding pad = resolvePadding(sheet);
        const int borderWidth = resolvedBorderWidth_();
        const int gutterWidth = lineNumberAreaWidth();
        SwRect inner{
            bounds.x + borderWidth + pad.left + gutterWidth,
            bounds.y + borderWidth + pad.top,
            std::max(0, bounds.width - 2 * borderWidth - (pad.left + pad.right) - gutterWidth),
            std::max(0, bounds.height - 2 * borderWidth - (pad.top + pad.bottom))
        };
        return inner;
    }

    int resolvedBorderWidth_() const {
        StyleSheet* sheet = const_cast<SwCodeEditor*>(this)->getToolSheet();
        SwColor border = m_theme.borderColor;
        int borderWidth = 1;
        int radius = m_theme.borderRadius;
        resolveBorder(sheet, border, borderWidth, radius);
        return borderWidth;
    }

    int drawLineText_(SwPainter* painter, const SwRect& lineRect, int lineIndex, const SwColor& defaultTextColor) {
        if (!painter || lineIndex < 0 || lineIndex >= static_cast<int>(m_lines.size())) {
            return lineRect.x;
        }

        const SwString& lineText = m_lines[lineIndex];
        if (lineText.isEmpty()) {
            return lineRect.x;
        }

        if (!m_document || wordWrapEnabled() || lineIndex >= m_document->blockCount()) {
            painter->drawText(lineRect,
                              lineText,
                              DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              defaultTextColor,
                              getFont());
            return lineRect.x + SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(),
                                                                        lineText,
                                                                        getFont(),
                                                                        lineText.size(),
                                                                        std::max(1, lineRect.width));
        }

        const SwList<SwTextLayoutFormatRange> formats = mergedFormatsForLine_(lineIndex);
        if (formats.isEmpty()) {
            painter->drawText(lineRect,
                              lineText,
                              DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              defaultTextColor,
                              getFont());
            return lineRect.x + SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(),
                                                                        lineText,
                                                                        getFont(),
                                                                        lineText.size(),
                                                                        std::max(1, lineRect.width));
        }

        std::vector<SwTextCharFormat> perChar(lineText.size());
        std::vector<bool> hasFormat(lineText.size(), false);
        for (int i = 0; i < formats.size(); ++i) {
            const SwTextLayoutFormatRange& range = formats[i];
            const int start = std::max(0, range.start);
            const int end = std::min(static_cast<int>(lineText.size()), range.start + range.length);
            for (int c = start; c < end; ++c) {
                if (hasFormat[static_cast<size_t>(c)]) {
                    perChar[static_cast<size_t>(c)].merge(range.format);
                } else {
                    perChar[static_cast<size_t>(c)] = range.format;
                    hasFormat[static_cast<size_t>(c)] = true;
                }
            }
        }

        int x = lineRect.x;
        int segmentStart = 0;
        while (segmentStart < static_cast<int>(lineText.size())) {
            const bool formatted = hasFormat[static_cast<size_t>(segmentStart)];
            int segmentEnd = segmentStart + 1;
            while (segmentEnd < static_cast<int>(lineText.size()) &&
                   hasFormat[static_cast<size_t>(segmentEnd)] == formatted &&
                   (!formatted || perChar[static_cast<size_t>(segmentEnd)] == perChar[static_cast<size_t>(segmentStart)])) {
                ++segmentEnd;
            }

            const SwString segmentText = lineText.substr(segmentStart, segmentEnd - segmentStart);
            SwTextCharFormat fmt;
            if (formatted) {
                fmt = perChar[static_cast<size_t>(segmentStart)];
            }
            const SwFont font = formatted ? fmt.toFont(getFont()) : getFont();
            const SwColor fg = (formatted && fmt.hasForeground()) ? fmt.foreground() : defaultTextColor;
            const int width = SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(),
                                                                      segmentText,
                                                                      font,
                                                                      segmentText.size(),
                                                                      std::max(1, lineRect.width));
            const SwRect segmentRect{x, lineRect.y, std::max(1, width), lineRect.height};
            if (formatted && fmt.hasBackground()) {
                painter->fillRect(segmentRect, fmt.background(), fmt.background(), 0);
            }
            painter->drawText(segmentRect,
                              segmentText,
                              DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              fg,
                              font);
            if (formatted && fmt.fontStrikeOut()) {
                const int strikeY = lineRect.y + lineRect.height / 2;
                painter->drawLine(segmentRect.x,
                                  strikeY,
                                  segmentRect.x + segmentRect.width,
                                  strikeY,
                                  fg,
                                  1);
            }
            if (formatted) {
                swDrawCustomUnderline(painter, segmentRect, segmentRect.width, fmt, fg);
            }
            x += width;
            segmentStart = segmentEnd;
        }
        return x;
    }

    SwList<SwTextLayoutFormatRange> mergedFormatsForLine_(int lineIndex) const {
        SwList<SwTextLayoutFormatRange> formats;
        if (!m_document || lineIndex < 0 || lineIndex >= m_document->blockCount()) {
            return formats;
        }

        formats = m_document->blockAt(lineIndex).additionalFormats();
        appendDiagnosticFormatsForLine_(lineIndex, formats);
        return formats;
    }

    void appendDiagnosticFormatsForLine_(int lineIndex, SwList<SwTextLayoutFormatRange>& formats) const {
        if (!m_document || lineIndex < 0 || lineIndex >= static_cast<int>(m_lines.size()) || lineIndex >= m_document->blockCount()) {
            return;
        }

        const int lineStart = m_document->absolutePosition(lineIndex, 0);
        const int lineEnd = lineStart + static_cast<int>(m_lines[lineIndex].size());
        if (lineEnd <= lineStart) {
            return;
        }

        for (int i = 0; i < m_diagnostics.size(); ++i) {
            const SwTextDiagnostic& diagnostic = m_diagnostics[i];
            const int diagnosticStart = diagnostic.range.start;
            const int diagnosticEnd = diagnostic.range.end();
            const int overlapStart = std::max(lineStart, diagnosticStart);
            const int overlapEnd = std::min(lineEnd, diagnosticEnd);
            if (overlapStart >= overlapEnd) {
                continue;
            }

            SwTextLayoutFormatRange range;
            range.start = overlapStart - lineStart;
            range.length = overlapEnd - overlapStart;
            range.format = resolvedDiagnosticFormat_(diagnostic);
            formats.append(range);
        }
    }

    SwTextCharFormat resolvedDiagnosticFormat_(const SwTextDiagnostic& diagnostic) const {
        SwTextCharFormat format = diagnostic.format;
        SwColor diagnosticColor = m_theme.diagnosticErrorColor;
        switch (diagnostic.severity) {
            case SwTextDiagnosticSeverity::Hint:
            case SwTextDiagnosticSeverity::Information:
                diagnosticColor = m_theme.diagnosticInformationColor;
                break;
            case SwTextDiagnosticSeverity::Warning:
                diagnosticColor = m_theme.diagnosticWarningColor;
                break;
            case SwTextDiagnosticSeverity::Error:
            default:
                diagnosticColor = m_theme.diagnosticErrorColor;
                break;
        }

        if (!format.hasUnderlineStyle()) {
            format.setUnderlineStyle(SwTextCharFormat::WaveUnderline);
        }
        if (!format.hasUnderlineColor()) {
            format.setUnderlineColor(diagnosticColor);
        }
        if (!format.hasForeground()) {
            format.setForeground(diagnosticColor);
        }
        return format;
    }

    int positionAtPoint_(int px, int py) const {
        const SwRect textRect = textRect_();
        if (px < textRect.x || py < textRect.y || px > textRect.x + textRect.width || py > textRect.y + textRect.height) {
            return -1;
        }

        const int lineHeight = lineHeightPx();
        if (lineHeight <= 0 || m_lines.isEmpty()) {
            return -1;
        }

        const int relativeY = py - textRect.y;
        int row = relativeY / lineHeight;
        row = std::max(0, row);

        const int visibleRow = clampInt(m_firstVisibleLine + row, 0, visibleLineCount_() - 1);
        const int lineIndex = documentLineForVisibleRow_(visibleRow);
        const int relativeX = px - textRect.x;
        const size_t column = SwWidgetPlatformAdapter::characterIndexAtPosition(nativeWindowHandle(),
                                                                                m_lines[lineIndex],
                                                                                getFont(),
                                                                                relativeX,
                                                                                std::max(1, textRect.width));
        const size_t clampedColumn = std::min(column, m_lines[lineIndex].size());
        return m_document ? m_document->absolutePosition(lineIndex, static_cast<int>(clampedColumn)) : -1;
    }

    SwString diagnosticToolTipAtPosition_(int position) const {
        if (position < 0) {
            return SwString();
        }
        for (int i = 0; i < m_diagnostics.size(); ++i) {
            if (m_diagnostics[i].range.contains(position)) {
                return m_diagnostics[i].message;
            }
        }
        return SwString();
    }

    void updateHoverToolTip_(MouseEvent* event) {
        if (!event) {
            if (m_hoveredFoldStartLine != -1) {
                m_hoveredFoldStartLine = -1;
                update();
            }
            setToolTips(SwString());
            return;
        }

        int hoveredFoldLine = -1;
        if (isPointInFoldMarker_(event->x(), event->y(), &hoveredFoldLine)) {
            if (m_hoveredFoldStartLine != hoveredFoldLine) {
                m_hoveredFoldStartLine = hoveredFoldLine;
                update();
            }
            setToolTips(foldMarkerToolTipAt_(event->x(), event->y()));
            return;
        }

        if (m_hoveredFoldStartLine != -1) {
            m_hoveredFoldStartLine = -1;
            update();
        }

        const int position = positionAtPoint_(event->x(), event->y());
        setToolTips(diagnosticToolTipAtPosition_(position));
    }

    void insertCompletion_(const SwString& completion) {
        if (completion.isEmpty()) {
            return;
        }

        const size_t oldPos = m_cursorPos;
        const size_t oldSelStart = m_selectionStart;
        const size_t oldSelEnd = m_selectionEnd;

        const size_t start = wordStartAt_(m_cursorPos);
        const size_t end = wordEndAt_(m_cursorPos);

        recordUndoState_();
        if (end > start) {
            eraseTextAt(start, end - start);
        }
        insertTextAt(start, completion);

        m_cursorPos = std::min(start + completion.size(), m_text.size());
        m_selectionStart = m_selectionEnd = m_cursorPos;
        rebuildLines();
        textChanged();
        ensureCursorVisibleForCode_();
        update();
        emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
    }

    SwString wordUnderCursor_() const {
        if (m_text.isEmpty()) {
            return SwString();
        }
        const size_t start = wordStartAt_(m_cursorPos);
        const size_t end = wordEndAt_(m_cursorPos);
        if (end <= start || start >= m_text.size()) {
            return SwString();
        }
        return m_text.substr(start, end - start);
    }

    SwString completionPrefixUnderCursor_() const {
        if (m_text.isEmpty()) {
            return SwString();
        }
        const size_t start = wordStartAt_(m_cursorPos);
        const size_t end = std::min(m_cursorPos, m_text.size());
        if (end <= start || start >= m_text.size()) {
            return SwString();
        }
        return m_text.substr(start, end - start);
    }

    size_t wordStartAt_(size_t pos) const {
        size_t p = std::min(pos, m_text.size());
        while (p > 0 && isWordChar_(m_text[p - 1])) {
            --p;
        }
        return p;
    }

    size_t wordEndAt_(size_t pos) const {
        size_t p = std::min(pos, m_text.size());
        while (p < m_text.size() && isWordChar_(m_text[p])) {
            ++p;
        }
        return p;
    }

    static bool isWordChar_(char ch) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        return std::isalnum(uch) != 0 || uch == static_cast<unsigned char>('_');
    }

    static wchar_t typedCharacter_(const KeyEvent* event) {
        if (!event) {
            return L'\0';
        }

        wchar_t wc = event->text();
        if (wc != L'\0' || event->isTextProvided()) {
            return wc;
        }

        char ascii = '\0';
        const bool caps = SwWidgetPlatformAdapter::isCapsLockKey(event->key());
        if (SwWidgetPlatformAdapter::translateCharacter(event->key(),
                                                        event->isShiftPressed(),
                                                        caps,
                                                        ascii)) {
            return static_cast<wchar_t>(static_cast<unsigned char>(ascii));
        }
        return L'\0';
    }

    int lineIndexForPosition_(size_t pos) const {
        const size_t clamped = std::min(pos, m_text.size());
        int line = 0;
        for (int i = 0; i < m_lineStarts.size(); ++i) {
            if (m_lineStarts[i] <= clamped) {
                line = i;
            } else {
                break;
            }
        }
        return line;
    }

    void emitEditorSignals_(size_t oldPos, size_t oldSelStart, size_t oldSelEnd) {
        if (oldPos != m_cursorPos) {
            cursorPositionChanged();
        }
        if (oldSelStart != m_selectionStart || oldSelEnd != m_selectionEnd) {
            selectionChanged();
        }
    }

    SwString indentationForLine_(int lineIndex) const {
        if (lineIndex < 0 || lineIndex >= static_cast<int>(m_lines.size())) {
            return SwString();
        }

        SwString indentation;
        int column = 0;
        const SwString& line = m_lines[lineIndex];
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == ' ') {
                indentation.append(' ');
                ++column;
            } else if (line[i] == '\t') {
                const int spaces = m_indentSize - (column % m_indentSize);
                indentation.append(SwString(static_cast<size_t>(spaces), ' '));
                column += spaces;
            } else {
                break;
            }
        }
        return indentation;
    }

    int visualColumnAtCursor_() const {
        const CursorInfo ci = cursorInfo();
        if (ci.line < 0 || ci.line >= static_cast<int>(m_lines.size())) {
            return 0;
        }

        const SwString& line = m_lines[ci.line];
        const size_t columnEnd = std::min(static_cast<size_t>(std::max(0, ci.col)), line.size());
        int visualColumn = 0;
        for (size_t i = 0; i < columnEnd; ++i) {
            if (line[i] == '\t') {
                visualColumn += m_indentSize - (visualColumn % m_indentSize);
            } else {
                ++visualColumn;
            }
        }
        return visualColumn;
    }

    char previousNonSpaceCharOnLine_(size_t pos) const {
        const CursorInfo ci = cursorInfo();
        size_t cursor = std::min(pos, m_text.size());
        while (cursor > ci.lineStart) {
            const char ch = m_text[cursor - 1];
            if (ch != ' ' && ch != '\t') {
                return ch;
            }
            --cursor;
        }
        return '\0';
    }

    char nextNonSpaceCharOnLine_(size_t pos) const {
        const CursorInfo ci = cursorInfo();
        if (ci.line < 0 || ci.line >= static_cast<int>(m_lines.size())) {
            return '\0';
        }

        const size_t lineEnd = ci.lineStart + m_lines[ci.line].size();
        size_t cursor = std::min(pos, lineEnd);
        while (cursor < lineEnd) {
            const char ch = m_text[cursor];
            if (ch != ' ' && ch != '\t') {
                return ch;
            }
            ++cursor;
        }
        return '\0';
    }

    bool isWhitespaceOnlyBetween_(size_t start, size_t end) const {
        const size_t from = std::min(start, m_text.size());
        const size_t to = std::min(std::max(start, end), m_text.size());
        for (size_t i = from; i < to; ++i) {
            if (m_text[i] != ' ' && m_text[i] != '\t') {
                return false;
            }
        }
        return true;
    }

    SwString indentationForPosition_(size_t pos) const {
        const int line = lineIndexForPosition_(pos);
        return indentationForLine_(line);
    }

    SwString indentationForMatchingOpeningBrace_() const {
        enum class ParseState {
            Normal,
            LineComment,
            BlockComment,
            StringLiteral,
            CharLiteral
        };

        std::vector<size_t> braceStack;
        ParseState state = ParseState::Normal;
        bool escaped = false;
        const size_t limit = std::min(m_cursorPos, m_text.size());

        for (size_t i = 0; i < limit; ++i) {
            const char ch = m_text[i];
            const char next = (i + 1 < limit) ? m_text[i + 1] : '\0';

            switch (state) {
                case ParseState::Normal:
                    if (ch == '/' && next == '/') {
                        ++i;
                        state = ParseState::LineComment;
                    } else if (ch == '/' && next == '*') {
                        ++i;
                        state = ParseState::BlockComment;
                    } else if (ch == '"') {
                        state = ParseState::StringLiteral;
                        escaped = false;
                    } else if (ch == '\'') {
                        state = ParseState::CharLiteral;
                        escaped = false;
                    } else if (ch == '{') {
                        braceStack.push_back(i);
                    } else if (ch == '}') {
                        if (!braceStack.empty()) {
                            braceStack.pop_back();
                        }
                    }
                    break;

                case ParseState::LineComment:
                    if (ch == '\n') {
                        state = ParseState::Normal;
                    }
                    break;

                case ParseState::BlockComment:
                    if (ch == '*' && next == '/') {
                        ++i;
                        state = ParseState::Normal;
                    }
                    break;

                case ParseState::StringLiteral:
                    if (!escaped && ch == '"') {
                        state = ParseState::Normal;
                    }
                    escaped = (!escaped && ch == '\\');
                    break;

                case ParseState::CharLiteral:
                    if (!escaped && ch == '\'') {
                        state = ParseState::Normal;
                    }
                    escaped = (!escaped && ch == '\\');
                    break;
            }
        }

        if (!braceStack.empty()) {
            return indentationForPosition_(braceStack.back());
        }

        const CursorInfo ci = cursorInfo();
        SwString currentIndent = indentationForLine_(ci.line);
        if (currentIndent.size() <= static_cast<size_t>(m_indentSize)) {
            return SwString();
        }
        return currentIndent.substr(0, currentIndent.size() - static_cast<size_t>(m_indentSize));
    }

    bool shouldAutoIndentClosingBrace_() const {
        if (hasSelectedText()) {
            return false;
        }

        const CursorInfo ci = cursorInfo();
        if (ci.line < 0 || ci.line >= static_cast<int>(m_lines.size())) {
            return false;
        }

        return isWhitespaceOnlyBetween_(ci.lineStart, m_cursorPos);
    }

    void insertSoftTab_() {
        const int visualColumn = visualColumnAtCursor_();
        int spaces = m_indentSize - (visualColumn % m_indentSize);
        if (spaces <= 0) {
            spaces = m_indentSize;
        }
        replaceSelectionWithText_(SwString(static_cast<size_t>(spaces), ' '));
    }

    void unindentCurrentLine_() {
        const CursorInfo ci = cursorInfo();
        if (ci.line < 0 || ci.line >= static_cast<int>(m_lines.size())) {
            return;
        }

        const SwString& line = m_lines[ci.line];
        if (line.isEmpty()) {
            return;
        }

        size_t removeCount = 0;
        if (line[0] == '\t') {
            removeCount = 1;
        } else {
            while (removeCount < line.size() &&
                   removeCount < static_cast<size_t>(m_indentSize) &&
                   line[removeCount] == ' ') {
                ++removeCount;
            }
        }
        if (removeCount == 0) {
            return;
        }

        recordUndoState_();
        eraseTextAt(ci.lineStart, removeCount);
        m_cursorPos = (m_cursorPos >= ci.lineStart + removeCount) ? (m_cursorPos - removeCount) : ci.lineStart;
        m_selectionStart = m_selectionEnd = m_cursorPos;
        rebuildLines();
        textChanged();
    }

    void insertAutoIndentedClosingBrace_() {
        const CursorInfo ci = cursorInfo();
        const SwString targetIndent = indentationForMatchingOpeningBrace_();
        recordUndoState_();

        if (m_cursorPos > ci.lineStart) {
            eraseTextAt(ci.lineStart, m_cursorPos - ci.lineStart);
        }

        insertTextAt(ci.lineStart, targetIndent + SwString("}"));
        m_cursorPos = ci.lineStart + targetIndent.size() + 1;
        m_selectionStart = m_selectionEnd = m_cursorPos;
        rebuildLines();
        textChanged();
    }

    void insertIndentedNewLine_() {
        const CursorInfo ci = cursorInfo();
        const SwString baseIndent = indentationForLine_(ci.line);
        const char previousChar = previousNonSpaceCharOnLine_(m_cursorPos);
        const char nextChar = nextNonSpaceCharOnLine_(m_cursorPos);

        SwString insertText("\n");
        insertText += baseIndent;

        if (previousChar == '{') {
            insertText += SwString(static_cast<size_t>(m_indentSize), ' ');
        }

        const bool splitBracePair = previousChar == '{' && nextChar == '}';
        if (splitBracePair) {
            insertText += "\n";
            insertText += baseIndent;
        }

        replaceSelectionWithText_(insertText);

        if (splitBracePair) {
            const size_t rewind = 1 + baseIndent.size();
            m_cursorPos = (m_cursorPos >= rewind) ? (m_cursorPos - rewind) : 0;
            m_selectionStart = m_selectionEnd = m_cursorPos;
        }
    }

    void refreshCompletionPopup_(bool forceShow) {
        if (!m_completer) {
            return;
        }

        const SwString prefix = completionPrefixUnderCursor_();
        const bool shouldShow = forceShow ||
                                (m_autoCompletionEnabled &&
                                 !prefix.isEmpty() &&
                                 static_cast<int>(prefix.size()) >= m_autoCompletionMinPrefixLength);

        if (!shouldShow) {
            m_completer->hidePopup();
            return;
        }

        m_completer->setWidget(this);
        m_completer->setCompletionPrefix(prefix);
        m_completer->complete(cursorRect());
    }

    static SwString cssColor_(const SwColor& color) {
        std::ostringstream oss;
        oss << "rgb(" << color.r << ", " << color.g << ", " << color.b << ")";
        return SwString(oss.str().c_str());
    }

    void applyThemeStyle_() {
        SwString css("SwCodeEditor {");
        css += " background-color: ";
        css += cssColor_(m_theme.backgroundColor);
        css += ";";
        css += " border-color: ";
        css += cssColor_(m_theme.borderColor);
        css += ";";
        css += " border-width: 1px;";
        css += " border-radius: ";
        css += SwString::number(m_theme.borderRadius);
        css += "px;";
        css += " padding: 10px 12px 10px 12px;";
        css += " color: ";
        css += cssColor_(m_theme.textColor);
        css += ";";
        css += "}";
        setStyleSheet(css);
    }

    SwTextDocument* m_document{nullptr};
    SwSyntaxHighlighter* m_highlighter{nullptr};
    SwTextDiagnosticsProvider* m_diagnosticsProvider{nullptr};
    SwCompleter* m_completer{nullptr};
    SwList<SwTextDiagnostic> m_diagnostics;
    SwList<SwTextExtraSelection> m_extraSelections;
    SwVector<FoldRegion> m_foldRegions;
    SwVector<int> m_visibleLineIndices;
    SwVector<int> m_visibleRowByLine;
    bool m_lineNumbersVisible{true};
    bool m_codeFoldingEnabled{true};
    bool m_highlightCurrentLine{true};
    bool m_syncingToDocument{false};
    bool m_syncingFromDocument{false};
    bool m_ownsDocument{false};
    bool m_autoCompletionEnabled{true};
    int m_autoCompletionMinPrefixLength{2};
    int m_indentSize{4};
    int m_hoveredFoldStartLine{-1};
    SwCodeEditorTheme m_theme{swCodeEditorDefaultTheme()};
};
