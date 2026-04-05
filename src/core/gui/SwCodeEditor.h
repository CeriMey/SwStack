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
#include "SwScrollBar.h"
#include "SwSyntaxHighlighter.h"
#include "SwTextDecorationRenderer.h"
#include "SwTextDocument.h"
#include "SwTextDiagnostics.h"
#include "SwTextExtraSelection.h"

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <functional>
#include <sstream>

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
    SwColor scrollBarTrackColor{236, 236, 236};
    SwColor scrollBarThumbColor{200, 200, 200};
    SwColor scrollBarThumbHoverColor{170, 170, 170};
    int borderRadius{10};
    int scrollBarWidth{14};
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
    theme.scrollBarTrackColor = SwColor{30, 30, 30};
    theme.scrollBarThumbColor = SwColor{78, 78, 78};
    theme.scrollBarThumbHoverColor = SwColor{110, 110, 110};
    theme.borderRadius = 0;
    theme.scrollBarWidth = 14;
    return theme;
}

class SwCodeEditor : public SwPlainTextEdit {
    SW_OBJECT(SwCodeEditor, SwPlainTextEdit)

public:
    struct CompletionEntry {
        SwString displayText;
        SwString insertText;
        SwString toolTip;
    };

    explicit SwCodeEditor(SwWidget* parent = nullptr)
        : SwPlainTextEdit(parent) {
        setWordWrapEnabled(false);
        setFont(SwFont(L"Consolas", 10, Medium));
        setTheme(swCodeEditorDefaultTheme());
        m_document = new SwTextDocument(this);
        m_ownsDocument = true;
        bindDocument_();
        m_foldTimer = new SwTimer(this);
        m_foldTimer->setInterval(250);
        m_foldTimer->setSingleShot(true);
        SwObject::connect(m_foldTimer, &SwTimer::timeout, this, [this]() {
            refreshFolding_();
            update();
        });
        SwObject::connect(this, &SwPlainTextEdit::textChanged, this, [this]() {
            const int newLineCount = documentLineCount_();
            if (newLineCount != m_lastKnownLineCount) {
                m_lastKnownLineCount = newLineCount;
                syncVisibleLineCacheAfterTextChange_();
            }
            m_maxLineWidthDirty = true;
            if (!m_suppressFoldRefresh) {
                scheduleFoldRefresh_();
            }
            m_suppressFoldRefresh = false;
            syncScrollBar_();
        });
        refreshFolding_();

        m_vScrollBar = new SwScrollBar(SwScrollBar::Orientation::Vertical, this);
        m_vScrollBar->hide();
        m_vScrollBar->setSingleStep(1);
        applyThemeStyle_();
        SwObject::connect(m_vScrollBar, &SwScrollBar::valueChanged, this, [this](int val) {
            if (!m_syncingScrollBar) {
                m_firstVisibleLine = val;
                ensureVisibleDeferredHighlight_();
                scheduleDeferredHighlightWork_();
                update();
            }
        });

        m_hScrollBar = new SwScrollBar(SwScrollBar::Orientation::Horizontal, this);
        m_hScrollBar->hide();
        m_hScrollBar->setSingleStep(8);
        SwObject::connect(m_hScrollBar, &SwScrollBar::valueChanged, this, [this](int val) {
            if (!m_syncingScrollBar) {
                m_horizontalScrollPx = std::max(0, val);
                update();
            }
        });
        applyThemeStyle_();

        m_selectionScrollTimer = new SwTimer(this);
        m_selectionScrollTimer->setInterval(30);
        SwObject::connect(m_selectionScrollTimer, &SwTimer::timeout, this, [this]() {
            if (!m_isSelecting) {
                stopSelectionAutoScroll_();
                return;
            }
            const size_t oldPos = m_cursorPos;
            const size_t oldSelStart = m_selectionStart;
            const size_t oldSelEnd = m_selectionEnd;
            if (!applySelectionAutoScrollForPoint_(m_lastSelectionMousePos)) {
                stopSelectionAutoScroll_();
                return;
            }
            emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
        });

        m_deferredHighlightTimer = new SwTimer(this);
        m_deferredHighlightTimer->setInterval(15);
        m_deferredHighlightTimer->setSingleShot(true);
        SwObject::connect(m_deferredHighlightTimer, &SwTimer::timeout, this, [this]() {
            processDeferredHighlightChunk_();
        });

        SwObject::connect(this, &SwCodeEditor::VisibleChanged, this, [this](bool visible) {
            if (!visible) {
                if (m_foldTimer && m_foldTimer->isActive()) {
                    m_foldTimer->stop();
                }
                stopDeferredHighlightWork_();
                stopSelectionAutoScroll_();
            } else if (m_foldDirty) {
                scheduleFoldRefresh_();
                scheduleDeferredHighlightWork_();
            } else {
                scheduleDeferredHighlightWork_();
            }
        });

        syncScrollBar_();
    }

    void setPlainText(const SwString& text) override {
        ensureDocument_();
        if (m_syncingFromDocument) {
            SwPlainTextEdit::setPlainText(text);
            return;
        }
        if (m_highlighter && m_highlighter->document() != m_document) {
            m_highlighter->setDocument(m_document);
        }
        const bool deferHighlight = shouldUseDeferredHighlight_(text) && m_highlighter != nullptr;
        if (deferHighlight) {
            m_highlighter->setAutoRehighlightSuspended(true);
        }
        m_syncingToDocument = true;
        m_document->setPlainText(text);
        m_syncingToDocument = false;
        if (deferHighlight) {
            m_highlighter->setAutoRehighlightSuspended(false);
        }
        SwPlainTextEdit::setPlainText(text);
        syncScrollBar_();
        if (deferHighlight) {
            ensureVisibleDeferredHighlight_();
            scheduleDeferredHighlightWork_();
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

    int firstVisibleLine() const {
        return m_firstVisibleLine;
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

        m_cursorPos = std::min(static_cast<size_t>(std::max(0, cursor.position())), documentLength_());
        m_selectionStart = std::min(static_cast<size_t>(std::max(0, cursor.anchor())), documentLength_());
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

    void setCompletionProvider(const std::function<SwList<CompletionEntry>(SwCodeEditor*,
                                                                            const SwString&,
                                                                            size_t,
                                                                            bool)>& provider) {
        m_completionProvider = provider;
    }

    void clearCompletionProvider() {
        m_completionProvider = std::function<SwList<CompletionEntry>(SwCodeEditor*,
                                                                     const SwString&,
                                                                     size_t,
                                                                     bool)>();
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

    void setHighlightCurrentLine(bool on) {
        if (m_highlightCurrentLine == on) {
            return;
        }
        m_highlightCurrentLine = on;
        update();
    }

    bool highlightCurrentLine() const {
        return m_highlightCurrentLine;
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
        layoutScrollBar_();
        syncScrollBar_();
        update();
    }

    SwCodeEditorTheme theme() const {
        return m_theme;
    }

    int lineNumberAreaWidth() const {
        if (!m_lineNumbersVisible) {
            return 0;
        }
        const int lineCount = documentLineCount_();
        const int digitCount = static_cast<int>(SwString::number(lineCount).size());
        const int charW = (m_cachedCharWidth > 0) ? m_cachedCharWidth : 8;
        return digitCount * charW + 18 + foldMarkerAreaWidth_();
    }

    int firstVisibleBlock() const {
        if (foldingEnabled_()) {
            return documentLineForVisibleRow_(m_firstVisibleLine);
        }
        return std::max(0, std::min(m_firstVisibleLine, std::max(0, documentLineCount_() - 1)));
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
        const SwString lineText = documentLineText_(visualLine);
        const size_t column = (visualLine == ci.line)
            ? static_cast<size_t>(std::max(0, ci.col))
            : lineText.size();
        const size_t clampedCol = std::min(column, lineText.size());
        const int x = inner.x - m_horizontalScrollPx + textWidthMono_(clampedCol);
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
        layoutScrollBar_();
        syncScrollBar_();
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

        refreshCharWidthCache_();

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
                if (selectionLine != i || selectionEnd <= selectionStart || i >= documentLineCount_()) {
                    continue;
                }
                const size_t lineStart = effectiveLineStart_(i);
                const size_t lineLen = documentLineLength_(i);
                const size_t lineEnd = lineStart + lineLen;
                const size_t segStart = (std::max)(selectionStart, lineStart);
                const size_t segEnd = (std::min)(selectionEnd, lineEnd);
                if (segStart >= segEnd) {
                    continue;
                }
                const int x1 = textRect.x - m_horizontalScrollPx + textWidthMono_(segStart - lineStart);
                const int x2 = textRect.x - m_horizontalScrollPx + textWidthMono_(segEnd - lineStart);
                const int left = (std::min)(x1, x2);
                const int right = (std::max)(x1, x2);
                painter->fillRect(SwRect{left, lineRect.y, right - left, lineRect.height},
                                  selection.format.background(),
                                  selection.format.background(),
                                  0);
            }

            if (hasSel && i < documentLineCount_()) {
                const size_t lineStart = effectiveLineStart_(i);
                const size_t lineLen = documentLineLength_(i);
                const size_t lineEnd = lineStart + lineLen;
                const size_t segStart = (std::max)(selMin, lineStart);
                const size_t segEnd = (std::min)(selMax, lineEnd);
                if (segStart < segEnd) {
                    const size_t startCol = segStart - lineStart;
                    const size_t endCol = segEnd - lineStart;
                    const int x1 = textRect.x - m_horizontalScrollPx + textWidthMono_(startCol);
                    const int x2 = textRect.x - m_horizontalScrollPx + textWidthMono_(endCol);
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

        if (m_pieceTable.isEmpty() && !m_placeholder.isEmpty() && !getFocus()) {
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

        paintChildren(event);
        painter->finalize();
    }

    void updateCursorFromPosition(int px, int py) override {
        const SwRect textRect = textRect_();
        const int lineHeight = lineHeightPx();
        if (lineHeight <= 0 || documentLineCount_() <= 0) {
            return;
        }

        if (px < textRect.x) {
            px = textRect.x;
        }
        if (py < textRect.y) {
            py = textRect.y;
        }
        if (textRect.height > 0 && py >= textRect.y + textRect.height) {
            py = textRect.y + textRect.height - 1;
        }

        int row = (py - textRect.y) / lineHeight;
        row = std::max(0, row);
        const int visibleRow = clampInt(m_firstVisibleLine + row, 0, visibleLineCount_() - 1);
        const int lineIdx = documentLineForVisibleRow_(visibleRow);
        const int relativeX = std::max(0, px - textRect.x + m_horizontalScrollPx);
        const SwString lineText = documentLineText_(lineIdx);
        const size_t col = SwWidgetPlatformAdapter::characterIndexAtPosition(nativeWindowHandle(),
                                                                             lineText,
                                                                             getFont(),
                                                                             relativeX,
                                                                             std::max(1, textRect.width));
        m_cursorPos = std::min(effectiveLineStart_(lineIdx) + std::min(col, lineText.size()), documentLength_());
    }

    void insertTextAt(size_t pos, const SwString& text) override {
        if (m_syncingFromDocument || !m_document) {
            SwPlainTextEdit::insertTextAt(pos, text);
            return;
        }
        const size_t clamped = std::min(pos, documentLength_());
        const bool multiline = containsNewline_(text);
        const bool heavyMultilineEdit = multiline && shouldUseDeferredHighlight_(text);
        const bool deferHighlight = heavyMultilineEdit && m_highlighter != nullptr;
        if (deferHighlight) {
            m_highlighter->setAutoRehighlightSuspended(true);
        }
        m_pieceTable.insert(clamped, text);
        m_syncingToDocument = true;
        if (heavyMultilineEdit) {
            syncDocumentFromPieceTable_();
        } else if (multiline) {
            const int lineIdx = lineIndexForPosition_(clamped);
            int currentBlock = lineIdx;
            int currentOffset = static_cast<int>(clamped - effectiveLineStart_(lineIdx));
            m_document->beginBatchEdit();
            size_t start = 0;
            for (size_t i = 0; i <= text.size(); ++i) {
                if (i == text.size() || text[i] == '\n') {
                    if (i > start) {
                        m_document->insertTextDirect(currentBlock,
                                                     currentOffset,
                                                     text.substr(start, i - start),
                                                     SwTextCharFormat());
                        currentOffset += static_cast<int>(i - start);
                    }
                    if (i < text.size()) {
                        m_document->insertBlockDirect(currentBlock,
                                                      currentOffset,
                                                      SwTextBlockFormat(),
                                                      SwTextCharFormat());
                        ++currentBlock;
                        currentOffset = 0;
                    }
                    start = i + 1;
                }
            }
            m_document->endBatchEdit();
        } else {
            // O(log N) line lookup via binary search on m_lineStarts,
            // then O(1) direct block access — bypasses blockPositionFromAbsolute.
            const int lineIdx = lineIndexForPosition_(clamped);
            const int offset = static_cast<int>(clamped - effectiveLineStart_(lineIdx));
            m_document->insertTextDirect(lineIdx, offset, text, SwTextCharFormat());
        }
        m_document->setModified(true);
        m_syncingToDocument = false;
        if (deferHighlight) {
            m_highlighter->setAutoRehighlightSuspended(false);
            ensureVisibleDeferredHighlight_();
            scheduleDeferredHighlightWork_();
        }
    }

    void eraseTextAt(size_t pos, size_t len) override {
        if (m_syncingFromDocument || !m_document) {
            SwPlainTextEdit::eraseTextAt(pos, len);
            return;
        }
        if (len == 0 || pos >= documentLength_()) {
            return;
        }
        const size_t clampedLen = std::min(len, documentLength_() - pos);
        // Check if deletion crosses a newline before modifying the piece table.
        bool crossesNewline = false;
        for (size_t i = pos; i < pos + clampedLen; ++i) {
            if (documentCharAt_(i) == '\n') { crossesNewline = true; break; }
        }
        const bool heavyMultilineErase = crossesNewline && clampedLen >= 16384;
        m_pieceTable.remove(pos, clampedLen);
        m_syncingToDocument = true;
        if (heavyMultilineErase) {
            syncDocumentFromPieceTable_();
        } else if (crossesNewline) {
            // Cross-block deletion: use absolute-position removeText
            // (handles block merging).
            m_document->removeText(static_cast<int>(pos), static_cast<int>(clampedLen));
        } else {
            // Single-block deletion: O(log N) line lookup + O(1) block access.
            const int lineIdx = lineIndexForPosition_(pos);
            const int offset = static_cast<int>(pos - effectiveLineStart_(lineIdx));
            m_document->removeTextDirect(lineIdx, offset, static_cast<int>(clampedLen));
        }
        m_document->setModified(true);
        m_syncingToDocument = false;
    }

    void mousePressEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        const size_t oldPos = m_cursorPos;
        const size_t oldSelStart = m_selectionStart;
        const size_t oldSelEnd = m_selectionEnd;
        const int oldFirstVisibleLine = m_firstVisibleLine;
        const int oldHorizontalScrollPx = m_horizontalScrollPx;
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
        if (dispatchMousePressToScrollBar_(event)) {
            stopSelectionAutoScroll_();
            emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
            return;
        }
        SwPlainTextEdit::mousePressEvent(event);
        restoreViewportAfterMouseInteraction_(oldFirstVisibleLine, oldHorizontalScrollPx);
        stopSelectionAutoScroll_();
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
        const int oldHorizontalScrollPx = m_horizontalScrollPx;
        if (dispatchMouseMoveToScrollBar_(event)) {
            stopSelectionAutoScroll_();
            updateHoverToolTip_(event);
            emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
            return;
        }
        SwPlainTextEdit::mouseMoveEvent(event);
        restoreViewportAfterMouseInteraction_(oldFirstVisibleLine, oldHorizontalScrollPx);
        m_lastSelectionMousePos = SwPoint{event->x(), event->y()};
        if (m_isSelecting) {
            const bool autoScrolled = applySelectionAutoScrollForPoint_(m_lastSelectionMousePos);
            updateSelectionAutoScrollState_();
            if (autoScrolled) {
                event->accept();
            }
        } else {
            stopSelectionAutoScroll_();
        }
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
        const int oldHorizontalScrollPx = m_horizontalScrollPx;
        const bool scrollBarWasDragging = (m_vScrollBar && m_vScrollBar->isSliderDown()) ||
                                          (m_hScrollBar && m_hScrollBar->isSliderDown());
        if (dispatchMouseReleaseToScrollBar_(event)) {
            stopSelectionAutoScroll_();
            if (scrollBarWasDragging || isPointInVerticalScrollBar_(event->x(), event->y()) ||
                isPointInHorizontalScrollBar_(event->x(), event->y())) {
                emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
                return;
            }
        }
        SwPlainTextEdit::mouseReleaseEvent(event);
        restoreViewportAfterMouseInteraction_(oldFirstVisibleLine, oldHorizontalScrollPx);
        stopSelectionAutoScroll_();
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
        const int oldHorizontalScrollPx = m_horizontalScrollPx;
        if (dispatchMouseDoubleClickToScrollBar_(event)) {
            stopSelectionAutoScroll_();
            emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
            return;
        }
        SwPlainTextEdit::mouseDoubleClickEvent(event);
        restoreViewportAfterMouseInteraction_(oldFirstVisibleLine, oldHorizontalScrollPx);
        stopSelectionAutoScroll_();
        emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
    }

    void wheelEvent(WheelEvent* event) override {
        if (!event) {
            return;
        }
        if (!isPointInside(event->x(), event->y())) {
            SwPlainTextEdit::wheelEvent(event);
            return;
        }

        if (event->isShiftPressed()) {
            const ScrollLayout_ layout = computeScrollLayout_();
            const int maxHorizontal = std::max(0, longestLineWidthPx_() - layout.textRect.width);
            if (maxHorizontal <= 0) {
                SwPlainTextEdit::wheelEvent(event);
                return;
            }

            int steps = event->delta() / 120;
            if (steps == 0) {
                steps = (event->delta() > 0) ? 1 : (event->delta() < 0 ? -1 : 0);
            }
            if (steps == 0) {
                SwPlainTextEdit::wheelEvent(event);
                return;
            }

            const int horizontalStep = std::max(16, m_cachedCharWidth * 3);
            m_horizontalScrollPx = clampInt(m_horizontalScrollPx - steps * horizontalStep, 0, maxHorizontal);
            syncScrollBar_();
            event->accept();
            update();
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
        syncScrollBar_();
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
        const bool altGrTextInput = event->isCtrlPressed() &&
                                    event->isAltPressed() &&
                                    typedChar != L'\0';
        const bool shortcutCtrl = event->isCtrlPressed() && !altGrTextInput;
        const bool plainAlt = event->isAltPressed() && !altGrTextInput;

        if (m_completer && m_completer->handleEditorKeyPress(event)) {
            emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
            update();
            return;
        }

        const bool ctrlSpace = shortcutCtrl && (key == 32 || event->text() == L' ');
        if (ctrlSpace) {
            triggerCompletion();
            event->accept();
            return;
        }

        if (shortcutCtrl && SwWidgetPlatformAdapter::matchesShortcutKey(key, 'V')) {
            pasteFromClipboardForCode_();
            if (m_completer) {
                m_completer->hidePopup();
            }
            update();
            emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
            event->accept();
            return;
        }

        const bool plainTab = !shortcutCtrl && !plainAlt && key == 9;
        if (plainTab) {
            m_suppressFoldRefresh = true;
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

        const bool plainReturn = !shortcutCtrl &&
                                 !plainAlt &&
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

        const bool isDeletionKey = !shortcutCtrl &&
                                   !plainAlt &&
                                   (SwWidgetPlatformAdapter::isBackspaceKey(key) ||
                                    SwWidgetPlatformAdapter::isDeleteKey(key));
        const bool isTextInput = !shortcutCtrl &&
                                 !plainAlt &&
                                 typedChar != L'\0' &&
                                 std::iswcntrl(static_cast<wint_t>(typedChar)) == 0;
        const bool shouldRefreshCompletion = isTextInput || isDeletionKey || (m_completer && m_completer->popupVisible());

        // Suppress fold refresh for edits that cannot change fold structure.
        // Only suppress for plain character input (not braces, not deletion,
        // not replacement of a selection which could contain braces).
        if (isTextInput && !isDeletionKey &&
            typedChar != L'{' && typedChar != L'}' && !hasSelectedText()) {
            m_suppressFoldRefresh = true;
        }

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
        return m_codeFoldingEnabled && !wordWrapEnabled() && documentLineCount_() > 0;
    }

    int foldMarkerRightInset_() const {
        return 4;
    }

    int foldMarkerAreaWidth_() const {
        return (foldingEnabled_() && m_lineNumbersVisible) ? 18 : 0;
    }

    int visibleLineCount_() const {
        if (m_identityVisibleLines) {
            return documentLineCount_();
        }
        if (m_visibleLineIndices.isEmpty()) {
            return documentLineCount_();
        }
        return static_cast<int>(m_visibleLineIndices.size());
    }

    int documentLineForVisibleRow_(int row) const {
        if (m_identityVisibleLines) {
            return clampInt(row, 0, std::max(0, documentLineCount_() - 1));
        }
        if (m_visibleLineIndices.isEmpty()) {
            return clampInt(row, 0, std::max(0, documentLineCount_() - 1));
        }
        const int clampedRow = clampInt(row, 0, static_cast<int>(m_visibleLineIndices.size()) - 1);
        return m_visibleLineIndices[static_cast<size_t>(clampedRow)];
    }

    int visibleRowForDocumentLine_(int line) const {
        if (line < 0 || line >= documentLineCount_()) {
            return -1;
        }
        if (m_identityVisibleLines) {
            return line;
        }
        if (m_visibleRowByLine.isEmpty() ||
            static_cast<int>(m_visibleRowByLine.size()) != documentLineCount_()) {
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
        return clampInt(line, 0, std::max(0, documentLineCount_() - 1));
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

    void clampHorizontalScroll_() {
        const int maxHorizontal = std::max(0, longestLineWidthPx_() - textRect_().width);
        m_horizontalScrollPx = clampInt(m_horizontalScrollPx, 0, maxHorizontal);
    }

    void ensureCursorVisibleForCode_() {
        if (documentLineCount_() <= 0) {
            m_firstVisibleLine = 0;
            m_horizontalScrollPx = 0;
            syncScrollBar_();
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

        const SwRect inner = textRect_();
        const SwString lineText = documentLineText_(line);
        const int cursorColumn = std::max(0, std::min(cursorInfo().col, static_cast<int>(lineText.size())));
        const int cursorX = textWidthMono_(static_cast<size_t>(cursorColumn));
        const int rightPadding = std::max(12, m_cachedCharWidth * 2);
        if (cursorX < m_horizontalScrollPx) {
            m_horizontalScrollPx = cursorX;
        } else if (cursorX >= m_horizontalScrollPx + std::max(1, inner.width - rightPadding)) {
            m_horizontalScrollPx = std::max(0, cursorX - std::max(1, inner.width - rightPadding));
        }

        clampFirstVisibleCodeLine_(visibleLines);
        clampHorizontalScroll_();
        syncScrollBar_();
        ensureVisibleDeferredHighlight_();
        scheduleDeferredHighlightWork_();
    }

    void restoreViewportAfterMouseInteraction_(int previousFirstVisibleLine, int previousHorizontalScrollPx) {
        m_firstVisibleLine = previousFirstVisibleLine;
        m_horizontalScrollPx = previousHorizontalScrollPx;
        clampFirstVisibleCodeLine_(visibleLinesForViewport_());
        clampHorizontalScroll_();
        syncScrollBar_();
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
        if (m_foldRegions.isEmpty() || line < 0) {
            return -1;
        }
        int lo = 0;
        int hi = static_cast<int>(m_foldRegions.size()) - 1;
        int result = -1;
        while (lo <= hi) {
            const int mid = lo + (hi - lo) / 2;
            const int midLine = m_foldRegions[static_cast<size_t>(mid)].startLine;
            if (midLine < line) {
                lo = mid + 1;
            } else if (midLine > line) {
                hi = mid - 1;
            } else {
                result = mid;
                hi = mid - 1;
            }
        }
        return result;
    }

    void coerceCursorIntoVisibleContent_(FoldMoveBias bias, bool preserveSelection) {
        if (!foldingEnabled_() || documentLineCount_() <= 0) {
            return;
        }

        const int currentLine = lineIndexForPosition_(m_cursorPos);
        const int collapsedRegion = collapsedRegionContainingLine_(currentLine);
        if (collapsedRegion < 0) {
            return;
        }

        const FoldRegion& region = m_foldRegions[static_cast<size_t>(collapsedRegion)];
        int targetLine = region.startLine;
        if (bias == FoldMoveBias::PreferEnd && region.endLine + 1 < documentLineCount_()) {
            targetLine = region.endLine + 1;
        }

        const size_t currentLineStart = effectiveLineStart_(currentLine);
        const size_t currentColumn = (m_cursorPos > currentLineStart) ? (m_cursorPos - currentLineStart) : 0;
        const size_t targetStart = effectiveLineStart_(targetLine);
        const size_t targetColumn = std::min(currentColumn, documentLineLength_(targetLine));
        m_cursorPos = std::min(targetStart + targetColumn, documentLength_());
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
        const SwString text = toPlainText();
        const size_t textLength = text.size();
        if (textLength == 0) {
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

        for (size_t i = 0; i < textLength; ++i) {
            const char ch = text[i];
            const char next = (i + 1 < textLength) ? text[i + 1] : '\0';

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

        const int lineCount = documentLineCount_();
        if (lineCount <= 0) {
            m_identityVisibleLines = true;
            return;
        }

        // Fast path: when no fold is collapsed, visible lines = all lines.
        // Use identity flag — accessors return line index directly, O(1).
        // No arrays allocated, no loop over 72K lines.
        bool hasCollapsed = false;
        for (int i = 0; i < static_cast<int>(m_foldRegions.size()); ++i) {
            if (m_foldRegions[static_cast<size_t>(i)].collapsed) {
                hasCollapsed = true;
                break;
            }
        }

        if (!hasCollapsed) {
            m_identityVisibleLines = true;
            return;
        }

        m_identityVisibleLines = false;
        m_visibleLineIndices.reserve(static_cast<size_t>(lineCount));
        m_visibleRowByLine.resize(static_cast<size_t>(lineCount));

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
                m_visibleRowByLine[static_cast<size_t>(line)] = -1;
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
        syncScrollBar_();
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
        const int labelWidth = textWidthMono_(label.size());
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

        if (region.startLine < 0 || region.endLine >= documentLineCount_() || region.startLine > region.endLine) {
            return tooltip;
        }

        SwString blockText;
        for (int line = region.startLine; line <= region.endLine; ++line) {
            SwString previewLine = documentLineText_(line);
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
                update();
            }
        });
        SwObject::connect(m_document, &SwTextDocument::blockCountChanged, this, [this]() {
            if (!m_syncingToDocument) {
                updateRequest(rect(), 0);
                update();
            }
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

    void syncDocumentFromPieceTable_() {
        if (!m_document) {
            return;
        }
        m_document->setPlainText(m_pieceTable.toPlainText());
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

    struct ScrollLayout_ {
        bool showVertical{false};
        bool showHorizontal{false};
        SwRect textRect{};
    };

    SwRect baseContentRect_() const {
        const SwRect bounds = rect();
        StyleSheet* sheet = const_cast<SwCodeEditor*>(this)->getToolSheet();
        const Padding pad = resolvePadding(sheet);
        const int borderWidth = resolvedBorderWidth_();
        const int gutterWidth = lineNumberAreaWidth();
        return SwRect{
            bounds.x + borderWidth + pad.left + gutterWidth,
            bounds.y + borderWidth + pad.top,
            std::max(0, bounds.width - 2 * borderWidth - (pad.left + pad.right) - gutterWidth),
            std::max(0, bounds.height - 2 * borderWidth - (pad.top + pad.bottom))
        };
    }

    int visibleLinesForHeight_(int height) const {
        const int lineHeight = lineHeightPx();
        if (lineHeight <= 0) {
            return 1;
        }
        return std::max(1, height / lineHeight);
    }

    int longestLineWidthPx_() const {
        refreshCharWidthCache_();
        if (!m_maxLineWidthDirty) {
            return m_cachedMaxLineWidthPx;
        }

        int longestChars = 0;
        const int lineCount = documentLineCount_();
        for (int i = 0; i < lineCount; ++i) {
            longestChars = std::max(longestChars, static_cast<int>(documentLineLength_(i)));
        }

        m_cachedMaxLineWidthPx = textWidthMono_(static_cast<size_t>(longestChars));
        m_maxLineWidthDirty = false;
        return m_cachedMaxLineWidthPx;
    }

    ScrollLayout_ computeScrollLayout_() const {
        ScrollLayout_ layout;
        const SwRect base = baseContentRect_();
        const int sbExtent = std::max(0, m_theme.scrollBarWidth);
        const int totalLines = visibleLineCount_();
        const int contentWidth = wordWrapEnabled() ? 0 : longestLineWidthPx_();

        for (int pass = 0; pass < 3; ++pass) {
            const int width = std::max(0, base.width - (layout.showVertical ? sbExtent : 0));
            const int height = std::max(0, base.height - (layout.showHorizontal ? sbExtent : 0));
            const int visibleLines = visibleLinesForHeight_(height);
            const bool showVertical = std::max(0, totalLines - visibleLines) > 0;
            const bool showHorizontal = !wordWrapEnabled() && contentWidth > width;
            if (showVertical == layout.showVertical && showHorizontal == layout.showHorizontal) {
                break;
            }
            layout.showVertical = showVertical;
            layout.showHorizontal = showHorizontal;
        }

        layout.textRect = SwRect{
            base.x,
            base.y,
            std::max(0, base.width - (layout.showVertical ? sbExtent : 0)),
            std::max(0, base.height - (layout.showHorizontal ? sbExtent : 0))
        };
        return layout;
    }

    SwRect textRect_() const {
        return computeScrollLayout_().textRect;
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
        if (!painter || lineIndex < 0 || lineIndex >= documentLineCount_()) {
            return lineRect.x;
        }

        const SwString lineText = documentLineText_(lineIndex);
        if (lineText.isEmpty()) {
            return lineRect.x - m_horizontalScrollPx;
        }

        if (!m_document || wordWrapEnabled() || lineIndex >= m_document->blockCount()) {
            SwRect shiftedLineRect = lineRect;
            shiftedLineRect.x -= m_horizontalScrollPx;
            painter->drawText(shiftedLineRect,
                              lineText,
                              DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              defaultTextColor,
                              getFont());
            return shiftedLineRect.x + textWidthMono_(lineText.size());
        }

        const SwList<SwTextLayoutFormatRange> formats = mergedFormatsForLine_(lineIndex);
        if (formats.isEmpty()) {
            SwRect shiftedLineRect = lineRect;
            shiftedLineRect.x -= m_horizontalScrollPx;
            painter->drawText(shiftedLineRect,
                              lineText,
                              DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              defaultTextColor,
                              getFont());
            return shiftedLineRect.x + textWidthMono_(lineText.size());
        }

        m_perCharFmtBuf.resize(lineText.size());
        m_hasFormatBuf.resize(lineText.size());
        std::memset(m_hasFormatBuf.data(), 0, lineText.size());
        auto& perChar = m_perCharFmtBuf;
        auto& hasFormat = m_hasFormatBuf;
        for (int i = 0; i < formats.size(); ++i) {
            const SwTextLayoutFormatRange& range = formats[i];
            const int start = std::max(0, range.start);
            const int end = std::min(static_cast<int>(lineText.size()), range.start + range.length);
            for (int c = start; c < end; ++c) {
                if (hasFormat[static_cast<size_t>(c)]) {
                    perChar[static_cast<size_t>(c)].merge(range.format);
                } else {
                    perChar[static_cast<size_t>(c)] = range.format;
                    hasFormat[static_cast<size_t>(c)] = 1;
                }
            }
        }

        int x = lineRect.x - m_horizontalScrollPx;
        int segmentStart = 0;
        while (segmentStart < static_cast<int>(lineText.size())) {
            const char formatted = hasFormat[static_cast<size_t>(segmentStart)];
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
            const int width = textWidthMono_(segmentText.size());
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
        if (!m_document || lineIndex < 0 || lineIndex >= m_document->blockCount()) {
            return SwList<SwTextLayoutFormatRange>();
        }

        const SwList<SwTextLayoutFormatRange>& blockFormats = m_document->blockAt(lineIndex).additionalFormats();
        if (m_diagnostics.isEmpty()) {
            return blockFormats;
        }
        SwList<SwTextLayoutFormatRange> formats = blockFormats;
        appendDiagnosticFormatsForLine_(lineIndex, formats);
        return formats;
    }

    void appendDiagnosticFormatsForLine_(int lineIndex, SwList<SwTextLayoutFormatRange>& formats) const {
        if (!m_document || lineIndex < 0 || lineIndex >= documentLineCount_() || lineIndex >= m_document->blockCount()) {
            return;
        }

        const int lineStart = m_document->absolutePosition(lineIndex, 0);
        const int lineEnd = lineStart + static_cast<int>(documentLineLength_(lineIndex));
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
        return format;
    }

    int positionAtPoint_(int px, int py) const {
        const SwRect textRect = textRect_();
        if (px < textRect.x || py < textRect.y || px > textRect.x + textRect.width || py > textRect.y + textRect.height) {
            return -1;
        }

        const int lineHeight = lineHeightPx();
        if (lineHeight <= 0 || documentLineCount_() <= 0) {
            return -1;
        }

        const int relativeY = py - textRect.y;
        int row = relativeY / lineHeight;
        row = std::max(0, row);

        const int visibleRow = clampInt(m_firstVisibleLine + row, 0, visibleLineCount_() - 1);
        const int lineIndex = documentLineForVisibleRow_(visibleRow);
        const int relativeX = px - textRect.x + m_horizontalScrollPx;
        const SwString lineText = documentLineText_(lineIndex);
        const size_t column = SwWidgetPlatformAdapter::characterIndexAtPosition(nativeWindowHandle(),
                                                                                lineText,
                                                                                getFont(),
                                                                                relativeX,
                                                                                std::max(1, textRect.width));
        const size_t clampedColumn = std::min(column, lineText.size());
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
        const size_t removedLen = (end > start) ? (end - start) : 0;
        if (end > start) {
            eraseTextAt(start, removedLen);
        }
        insertTextAt(start, completion);

        m_cursorPos = std::min(start + completion.size(), documentLength_());
        m_selectionStart = m_selectionEnd = m_cursorPos;
        textChanged();
        ensureCursorVisibleForCode_();
        update();
        emitEditorSignals_(oldPos, oldSelStart, oldSelEnd);
    }

    SwString wordUnderCursor_() const {
        if (m_pieceTable.isEmpty()) {
            return SwString();
        }
        const size_t start = wordStartAt_(m_cursorPos);
        const size_t end = wordEndAt_(m_cursorPos);
        if (end <= start || start >= documentLength_()) {
            return SwString();
        }
        return documentSubstring_(start, end - start);
    }

    SwString completionPrefixUnderCursor_() const {
        if (m_pieceTable.isEmpty()) {
            return SwString();
        }
        const size_t start = wordStartAt_(m_cursorPos);
        const size_t end = std::min(m_cursorPos, documentLength_());
        if (end <= start || start >= documentLength_()) {
            return SwString();
        }
        return documentSubstring_(start, end - start);
    }

    size_t wordStartAt_(size_t pos) const {
        size_t p = std::min(pos, documentLength_());
        while (p > 0 && isWordChar_(documentCharAt_(p - 1))) {
            --p;
        }
        return p;
    }

    size_t wordEndAt_(size_t pos) const {
        size_t p = std::min(pos, documentLength_());
        while (p < documentLength_() && isWordChar_(documentCharAt_(p))) {
            ++p;
        }
        return p;
    }

    static bool containsNewline_(const SwString& s) {
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\n') {
                return true;
            }
        }
        return false;
    }

    static bool shouldUseDeferredHighlight_(const SwString& text) {
        if (text.size() < 16384) {
            return false;
        }
        int newlineCount = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n') {
                ++newlineCount;
                if (newlineCount >= 128) {
                    return true;
                }
            }
        }
        return false;
    }

    void scheduleFoldRefresh_() {
        m_foldDirty = true;
        if (m_foldTimer) {
            m_foldTimer->start(250);
        }
    }

    void syncVisibleLineCacheAfterTextChange_() {
        rebuildVisibleLineCache_();
        clampFirstVisibleCodeLine_(visibleLinesForViewport_());
    }

    void ensureVisibleDeferredHighlight_() {
        if (!m_highlighter) {
            return;
        }
        const int totalVisibleLines = visibleLineCount_();
        if (totalVisibleLines <= 0) {
            return;
        }
        const int visibleLines = visibleLinesForViewport_();
        const int firstVisibleRow = clampInt(m_firstVisibleLine, 0, std::max(0, totalVisibleLines - 1));
        const int lastVisibleRow = clampInt(firstVisibleRow + visibleLines + 8,
                                            0,
                                            std::max(0, totalVisibleLines - 1));
        const int firstLine = documentLineForVisibleRow_(firstVisibleRow);
        const int targetLine = documentLineForVisibleRow_(lastVisibleRow);

        bool needsVisibleWindow = false;
        if (m_document) {
            const int blockCount = m_document->blockCount();
            const int visibleFirstBlock = clampInt(firstLine, 0, std::max(0, blockCount - 1));
            const int visibleLastBlock = clampInt(targetLine, 0, std::max(0, blockCount - 1));
            for (int block = visibleFirstBlock; block <= visibleLastBlock; ++block) {
                if (m_document->blockAt(block).userState() < 0) {
                    needsVisibleWindow = true;
                    break;
                }
            }
            if (needsVisibleWindow) {
                m_highlighter->rehighlightWindow(std::max(0, visibleFirstBlock - 8),
                                                 std::min(blockCount - 1, visibleLastBlock + 8),
                                                 512);
            }
        }

        if (m_highlighter->hasDeferredRehighlight()) {
            scheduleDeferredHighlightWork_();
        }
    }

    bool processDeferredHighlightChunk_() {
        if (!m_highlighter || !m_highlighter->hasDeferredRehighlight()) {
            stopDeferredHighlightWork_();
            return false;
        }
        const bool hasMore = m_highlighter->processDeferredRehighlight(256);
        if (hasMore && isVisibleInHierarchy() && m_deferredHighlightTimer) {
            m_deferredHighlightTimer->start(15);
        }
        return hasMore;
    }

    void scheduleDeferredHighlightWork_() {
        if (!m_deferredHighlightTimer || !m_highlighter || !m_highlighter->hasDeferredRehighlight()) {
            return;
        }
        if (!isVisibleInHierarchy()) {
            return;
        }
        if (!m_deferredHighlightTimer->isActive()) {
            m_deferredHighlightTimer->start(15);
        }
    }

    void stopDeferredHighlightWork_() {
        if (m_deferredHighlightTimer && m_deferredHighlightTimer->isActive()) {
            m_deferredHighlightTimer->stop();
        }
    }

    void pasteFromClipboardForCode_() {
        if (m_readOnly) {
            return;
        }
        SwGuiApplication* app = SwGuiApplication::instance(false);
        SwPlatformIntegration* platform = app ? app->platformIntegration() : nullptr;
        if (!platform) {
            return;
        }
        SwString clip = platform->clipboardText();
        if (clip.isEmpty()) {
            return;
        }
        clip.replace("\r\n", "\n");
        clip.replace("\r", "\n");
        replaceSelectionWithText_(clip);
        ensureCursorVisibleForCode_();
    }

    void layoutScrollBar_() {
        if (!m_vScrollBar && !m_hScrollBar) {
            return;
        }
        const ScrollLayout_ layout = computeScrollLayout_();
        const SwRect base = baseContentRect_();
        const int sbExtent = std::max(0, m_theme.scrollBarWidth);
        if (m_vScrollBar) {
            m_vScrollBar->setGeometry(base.x + base.width - sbExtent,
                                      base.y,
                                      sbExtent,
                                      std::max(0, layout.textRect.height));
        }
        if (m_hScrollBar) {
            m_hScrollBar->setGeometry(base.x,
                                      base.y + base.height - sbExtent,
                                      std::max(0, layout.textRect.width),
                                      sbExtent);
        }
    }

    bool isPointInVerticalScrollBar_(int px, int py) const {
        if (!m_vScrollBar || !m_vScrollBar->getVisible()) {
            return false;
        }
        const SwRect sb = m_vScrollBar->geometry();
        return px >= sb.x && px < (sb.x + sb.width) && py >= sb.y && py < (sb.y + sb.height);
    }

    bool isPointInHorizontalScrollBar_(int px, int py) const {
        if (!m_hScrollBar || !m_hScrollBar->getVisible()) {
            return false;
        }
        const SwRect sb = m_hScrollBar->geometry();
        return px >= sb.x && px < (sb.x + sb.width) && py >= sb.y && py < (sb.y + sb.height);
    }

    bool dispatchMousePressToScrollBar_(MouseEvent* event) {
        if (!event) {
            return false;
        }
        if (!isPointInVerticalScrollBar_(event->x(), event->y()) &&
            !isPointInHorizontalScrollBar_(event->x(), event->y())) {
            return false;
        }
        SwWidget::mousePressEvent(event);
        return true;
    }

    bool dispatchMouseMoveToScrollBar_(MouseEvent* event) {
        if (!event) {
            return false;
        }
        const bool verticalActive = m_vScrollBar && m_vScrollBar->getVisible() &&
                                    (m_vScrollBar->isSliderDown() || isPointInVerticalScrollBar_(event->x(), event->y()));
        const bool horizontalActive = m_hScrollBar && m_hScrollBar->getVisible() &&
                                      (m_hScrollBar->isSliderDown() || isPointInHorizontalScrollBar_(event->x(), event->y()));
        if (!verticalActive && !horizontalActive) {
            return false;
        }
        SwWidget::mouseMoveEvent(event);
        return true;
    }

    bool dispatchMouseReleaseToScrollBar_(MouseEvent* event) {
        if (!event) {
            return false;
        }
        const bool verticalActive = m_vScrollBar && m_vScrollBar->getVisible() &&
                                    (m_vScrollBar->isSliderDown() || isPointInVerticalScrollBar_(event->x(), event->y()));
        const bool horizontalActive = m_hScrollBar && m_hScrollBar->getVisible() &&
                                      (m_hScrollBar->isSliderDown() || isPointInHorizontalScrollBar_(event->x(), event->y()));
        if (!verticalActive && !horizontalActive) {
            return false;
        }
        SwWidget::mouseReleaseEvent(event);
        return true;
    }

    bool dispatchMouseDoubleClickToScrollBar_(MouseEvent* event) {
        if (!event) {
            return false;
        }
        if (!isPointInVerticalScrollBar_(event->x(), event->y()) &&
            !isPointInHorizontalScrollBar_(event->x(), event->y())) {
            return false;
        }
        SwWidget::mouseDoubleClickEvent(event);
        return true;
    }

    int selectionAutoScrollDirectionForPoint_(const SwPoint& point) const {
        const SwRect textRect = textRect_();
        if (textRect.height <= 0) {
            return 0;
        }
        if (point.y < textRect.y) {
            return -1;
        }
        if (point.y >= textRect.y + textRect.height) {
            return 1;
        }
        return 0;
    }

    void updateSelectionAutoScrollState_() {
        if (!m_selectionScrollTimer) {
            return;
        }
        if (m_isSelecting && selectionAutoScrollDirectionForPoint_(m_lastSelectionMousePos) != 0) {
            if (!m_selectionScrollTimer->isActive()) {
                m_selectionScrollTimer->start();
            }
        } else {
            stopSelectionAutoScroll_();
        }
    }

    void stopSelectionAutoScroll_() {
        if (m_selectionScrollTimer && m_selectionScrollTimer->isActive()) {
            m_selectionScrollTimer->stop();
        }
    }

    bool applySelectionAutoScrollForPoint_(const SwPoint& point) {
        if (!m_isSelecting) {
            return false;
        }

        const int direction = selectionAutoScrollDirectionForPoint_(point);
        if (direction == 0) {
            return false;
        }

        const int visibleLines = visibleLinesForViewport_();
        const int maxFirst = std::max(0, visibleLineCount_() - visibleLines);
        const SwRect textRect = textRect_();
        const int lineHeight = std::max(1, lineHeightPx());
        const int distance = (direction < 0)
            ? (textRect.y - point.y)
            : (point.y - (textRect.y + textRect.height) + 1);
        const int step = std::max(1, 1 + distance / lineHeight);

        const int oldFirstVisibleLine = m_firstVisibleLine;
        const size_t oldCursorPos = m_cursorPos;
        const size_t oldSelectionEnd = m_selectionEnd;

        m_firstVisibleLine = clampInt(m_firstVisibleLine + direction * step, 0, maxFirst);
        updateCursorFromPosition(point.x, point.y);
        m_selectionEnd = m_cursorPos;
        syncScrollBar_();

        if (m_firstVisibleLine == oldFirstVisibleLine &&
            m_cursorPos == oldCursorPos &&
            m_selectionEnd == oldSelectionEnd) {
            return false;
        }

        update();
        return true;
    }

    void syncScrollBar_() {
        if ((!m_vScrollBar && !m_hScrollBar) || m_syncingScrollBar) {
            return;
        }
        const ScrollLayout_ layout = computeScrollLayout_();
        const int visibleLines = visibleLinesForHeight_(layout.textRect.height);
        const int totalLines = visibleLineCount_();
        const int maxFirst = std::max(0, totalLines - visibleLines);
        const int maxHorizontal = std::max(0, longestLineWidthPx_() - layout.textRect.width);
        const bool verticalVisibilityChanged = m_vScrollBar && (m_vScrollBar->getVisible() != layout.showVertical);
        const bool horizontalVisibilityChanged = m_hScrollBar && (m_hScrollBar->getVisible() != layout.showHorizontal);
        m_firstVisibleLine = clampInt(m_firstVisibleLine, 0, maxFirst);
        m_horizontalScrollPx = clampInt(m_horizontalScrollPx, 0, maxHorizontal);
        m_syncingScrollBar = true;
        if (m_vScrollBar) {
            m_vScrollBar->setVisible(layout.showVertical);
            m_vScrollBar->setRange(0, maxFirst);
            m_vScrollBar->setPageStep(visibleLines);
            m_vScrollBar->setSingleStep(1);
            m_vScrollBar->setValue(m_firstVisibleLine);
        }
        if (m_hScrollBar) {
            m_hScrollBar->setVisible(layout.showHorizontal);
            m_hScrollBar->setRange(0, maxHorizontal);
            m_hScrollBar->setPageStep(std::max(1, layout.textRect.width));
            m_hScrollBar->setSingleStep(std::max(8, m_cachedCharWidth));
            m_hScrollBar->setValue(m_horizontalScrollPx);
        }
        m_syncingScrollBar = false;
        if (verticalVisibilityChanged || horizontalVisibilityChanged || layout.showVertical || layout.showHorizontal) {
            layoutScrollBar_();
        }
        ensureVisibleDeferredHighlight_();
        if (verticalVisibilityChanged || horizontalVisibilityChanged) {
            update();
        }
    }

    void refreshCharWidthCache_() const {
        const SwFont currentFont = getFont();
        if (m_cachedCharWidth > 0 && m_cachedCharWidthFont == currentFont) {
            return;
        }
        const int previousCharWidth = m_cachedCharWidth;
        m_cachedCharWidthFont = currentFont;
        m_cachedCharWidth = SwWidgetPlatformAdapter::textWidthUntil(
            nativeWindowHandle(), SwString("M"), currentFont, 1, 8);
        if (m_cachedCharWidth <= 0) {
            m_cachedCharWidth = 8;
        }
        if (m_cachedCharWidth != previousCharWidth) {
            m_maxLineWidthDirty = true;
        }
    }

    int textWidthMono_(size_t charCount) const {
        return static_cast<int>(charCount) * m_cachedCharWidth;
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

    int documentLineCount_() const {
        return std::max(1, m_document ? m_document->blockCount() : m_pieceTable.lineCount());
    }

    size_t documentLength_() const {
        return m_pieceTable.totalLength();
    }

    SwString documentLineText_(int lineIndex) const {
        if (lineIndex < 0 || lineIndex >= documentLineCount_()) {
            return SwString();
        }
        return m_pieceTable.lineContent(lineIndex);
    }

    size_t documentLineLength_(int lineIndex) const {
        if (lineIndex < 0 || lineIndex >= documentLineCount_()) {
            return 0;
        }
        return m_pieceTable.lineLength(lineIndex);
    }

    char documentCharAt_(size_t pos) const {
        return m_pieceTable.charAt(pos);
    }

    SwString documentSubstring_(size_t pos, size_t len) const {
        return m_pieceTable.substr(pos, len);
    }

    int lineIndexForPosition_(size_t pos) const {
        return m_pieceTable.lineForOffset(pos);
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
        if (lineIndex < 0 || lineIndex >= documentLineCount_()) {
            return SwString();
        }

        SwString indentation;
        int column = 0;
        const SwString line = documentLineText_(lineIndex);
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
        if (ci.line < 0 || ci.line >= documentLineCount_()) {
            return 0;
        }

        const SwString line = documentLineText_(ci.line);
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
        size_t cursor = std::min(pos, documentLength_());
        while (cursor > ci.lineStart) {
            const char ch = documentCharAt_(cursor - 1);
            if (ch != ' ' && ch != '\t') {
                return ch;
            }
            --cursor;
        }
        return '\0';
    }

    char nextNonSpaceCharOnLine_(size_t pos) const {
        const CursorInfo ci = cursorInfo();
        if (ci.line < 0 || ci.line >= documentLineCount_()) {
            return '\0';
        }

        const size_t lineEnd = ci.lineStart + documentLineLength_(ci.line);
        size_t cursor = std::min(pos, lineEnd);
        while (cursor < lineEnd) {
            const char ch = documentCharAt_(cursor);
            if (ch != ' ' && ch != '\t') {
                return ch;
            }
            ++cursor;
        }
        return '\0';
    }

    bool isWhitespaceOnlyBetween_(size_t start, size_t end) const {
        const size_t from = std::min(start, documentLength_());
        const size_t to = std::min(std::max(start, end), documentLength_());
        for (size_t i = from; i < to; ++i) {
            const char ch = documentCharAt_(i);
            if (ch != ' ' && ch != '\t') {
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

        SwVector<size_t> braceStack;
        ParseState state = ParseState::Normal;
        bool escaped = false;
        const size_t limit = std::min(m_cursorPos, documentLength_());

        for (size_t i = 0; i < limit; ++i) {
            const char ch = documentCharAt_(i);
            const char next = (i + 1 < limit) ? documentCharAt_(i + 1) : '\0';

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
                        if (!braceStack.isEmpty()) {
                            braceStack.removeAt(braceStack.size() - 1);
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

        if (!braceStack.isEmpty()) {
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
        if (ci.line < 0 || ci.line >= documentLineCount_()) {
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
        if (ci.line < 0 || ci.line >= documentLineCount_()) {
            return;
        }

        const SwString line = documentLineText_(ci.line);
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
        textChanged();
    }

    void insertAutoIndentedClosingBrace_() {
        const CursorInfo ci = cursorInfo();
        const SwString targetIndent = indentationForMatchingOpeningBrace_();
        recordUndoState_();

        const size_t removedLen = (m_cursorPos > ci.lineStart) ? (m_cursorPos - ci.lineStart) : 0;
        if (removedLen > 0) {
            eraseTextAt(ci.lineStart, removedLen);
        }

        const SwString braceText = targetIndent + SwString("}");
        insertTextAt(ci.lineStart, braceText);
        m_cursorPos = ci.lineStart + targetIndent.size() + 1;
        m_selectionStart = m_selectionEnd = m_cursorPos;
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

        if (m_completionProvider) {
            SwStandardItemModel* model = dynamic_cast<SwStandardItemModel*>(m_completer->model());
            if (model) {
                model->clear();
                const SwList<CompletionEntry> entries =
                    m_completionProvider(this, prefix, m_cursorPos, forceShow);
                for (int i = 0; i < entries.size(); ++i) {
                    SwStandardItem* row = new SwStandardItem(entries[i].displayText);
                    row->setEditText(entries[i].insertText.isEmpty() ? entries[i].displayText : entries[i].insertText);
                    if (!entries[i].toolTip.isEmpty()) {
                        row->setToolTip(entries[i].toolTip);
                    }
                    model->appendRow(row);
                }
            }
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

        if (m_vScrollBar || m_hScrollBar) {
            SwString scrollCss("SwScrollBar {");
            scrollCss += " background-color: ";
            scrollCss += cssColor_(m_theme.scrollBarTrackColor);
            scrollCss += ";";
            scrollCss += " border-color: ";
            scrollCss += cssColor_(m_theme.scrollBarTrackColor);
            scrollCss += ";";
            scrollCss += " background-color-disabled: ";
            scrollCss += cssColor_(m_theme.scrollBarTrackColor);
            scrollCss += ";";
            scrollCss += " border-color-disabled: ";
            scrollCss += cssColor_(m_theme.scrollBarTrackColor);
            scrollCss += ";";
            scrollCss += " border-width: 0px;";
            scrollCss += " border-radius: 6px;";
            scrollCss += " padding: 3px;";
            scrollCss += " thumb-color: ";
            scrollCss += cssColor_(m_theme.scrollBarThumbColor);
            scrollCss += ";";
            scrollCss += " thumb-border-color: ";
            scrollCss += cssColor_(m_theme.scrollBarThumbColor);
            scrollCss += ";";
            scrollCss += " thumb-color-hover: ";
            scrollCss += cssColor_(m_theme.scrollBarThumbHoverColor);
            scrollCss += ";";
            scrollCss += " thumb-border-color-hover: ";
            scrollCss += cssColor_(m_theme.scrollBarThumbHoverColor);
            scrollCss += ";";
            scrollCss += " thumb-color-pressed: ";
            scrollCss += cssColor_(m_theme.scrollBarThumbHoverColor);
            scrollCss += ";";
            scrollCss += " thumb-border-color-pressed: ";
            scrollCss += cssColor_(m_theme.scrollBarThumbHoverColor);
            scrollCss += ";";
            scrollCss += " thumb-color-disabled: ";
            scrollCss += cssColor_(m_theme.scrollBarTrackColor);
            scrollCss += ";";
            scrollCss += " thumb-border-color-disabled: ";
            scrollCss += cssColor_(m_theme.scrollBarTrackColor);
            scrollCss += ";";
            scrollCss += " thumb-radius: 5px;";
            scrollCss += " thumb-min-length: 30px;";
            scrollCss += "}";
            if (m_vScrollBar) {
                m_vScrollBar->setStyleSheet(scrollCss);
            }
            if (m_hScrollBar) {
                m_hScrollBar->setStyleSheet(scrollCss);
            }
        }
    }

    SwTextDocument* m_document{nullptr};
    SwSyntaxHighlighter* m_highlighter{nullptr};
    SwTextDiagnosticsProvider* m_diagnosticsProvider{nullptr};
    SwCompleter* m_completer{nullptr};
    std::function<SwList<CompletionEntry>(SwCodeEditor*, const SwString&, size_t, bool)> m_completionProvider;
    SwTimer* m_foldTimer{nullptr};
    SwTimer* m_deferredHighlightTimer{nullptr};
    SwTimer* m_selectionScrollTimer{nullptr};
    SwScrollBar* m_vScrollBar{nullptr};
    SwScrollBar* m_hScrollBar{nullptr};
    bool m_syncingScrollBar{false};
    int m_horizontalScrollPx{0};
    SwList<SwTextDiagnostic> m_diagnostics;
    SwList<SwTextExtraSelection> m_extraSelections;
    SwVector<FoldRegion> m_foldRegions;
    SwVector<int> m_visibleLineIndices;
    SwVector<int> m_visibleRowByLine;
    SwVector<SwTextCharFormat> m_perCharFmtBuf;
    SwVector<char> m_hasFormatBuf;
    bool m_identityVisibleLines{true};
    bool m_lineNumbersVisible{true};
    bool m_codeFoldingEnabled{true};
    bool m_highlightCurrentLine{true};
    bool m_syncingToDocument{false};
    bool m_syncingFromDocument{false};
    bool m_ownsDocument{false};
    bool m_autoCompletionEnabled{true};
    bool m_foldDirty{false};
    bool m_suppressFoldRefresh{false};
    mutable bool m_maxLineWidthDirty{true};
    mutable int m_cachedMaxLineWidthPx{0};
    int m_autoCompletionMinPrefixLength{2};
    int m_indentSize{4};
    int m_hoveredFoldStartLine{-1};
    int m_lastKnownLineCount{0};
    mutable int m_cachedCharWidth{0};
    mutable SwFont m_cachedCharWidthFont;
    SwPoint m_lastSelectionMousePos{0, 0};
    SwCodeEditorTheme m_theme{swCodeEditorDefaultTheme()};
};
