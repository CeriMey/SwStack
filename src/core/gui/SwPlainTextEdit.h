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

#pragma once

/**
 * @file src/core/gui/SwPlainTextEdit.h
 * @ingroup core_gui
 * @brief Multi-line plain text editor backed by a Piece Table.
 */


/***************************************************************************************************
 * SwPlainTextEdit - multi-line plain text editor.
 *
 * Goals:
 * - Plain text storage (no rich text rendering).
 * - Keyboard editing + cursor, suitable for snapshot-based visual validation.
 * - Styling via StyleSheet basics: background-color / border-color / border-width / border-radius /
 *   color / padding.
 *
 * Text storage uses SwPieceTable for O(log P) insert/delete regardless of document size.
 * Line index is maintained incrementally inside the piece table.
 **************************************************************************************************/

#include "SwFrame.h"
#include "SwGuiApplication.h"
#include "SwTimer.h"
#include "SwWidgetPlatformAdapter.h"
#include "SwPieceTable.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <sstream>
#include <string>

class SwPlainTextEdit : public SwFrame {
    SW_OBJECT(SwPlainTextEdit, SwFrame)

protected:
    struct EditState {
        SwString text;
        size_t cursorPos{0};
        size_t selectionStart{0};
        size_t selectionEnd{0};
        int firstVisibleLine{0};
        std::function<void()> applyExtra;
    };

    enum class UndoMergeKind {
        NoMerge,
        InsertText,
        Backspace,
        DeleteForward
    };

    virtual EditState captureEditState() const {
        EditState st;
        st.text = m_pieceTable.toPlainText();
        st.cursorPos = m_cursorPos;
        st.selectionStart = m_selectionStart;
        st.selectionEnd = m_selectionEnd;
        st.firstVisibleLine = m_firstVisibleLine;
        return st;
    }

public:
    explicit SwPlainTextEdit(SwWidget* parent = nullptr)
        : SwFrame(parent) {
        initDefaults();
        ensureBlinkTimer();
    }

    void setWordWrapEnabled(bool on) {
        if (m_wordWrapEnabled == on) {
            return;
        }
        m_wordWrapEnabled = on;
        rebuildVisualLines_();
        ensureCursorVisible();
        update();
    }

    bool wordWrapEnabled() const { return m_wordWrapEnabled; }

    virtual void setPlainText(const SwString& text) {
        if (m_pieceTable.equals(text)) {
            return;
        }
        m_pieceTable.setText(text);
        if (m_cursorPos > m_pieceTable.totalLength()) {
            m_cursorPos = m_pieceTable.totalLength();
        }
        m_selectionStart = m_selectionEnd = m_cursorPos;
        clearUndoRedo_();
        rebuildVisualLines_();
        ensureCursorVisible();
        textChanged();
        update();
    }

    SwString toPlainText() const { return m_pieceTable.toPlainText(); }

    virtual void appendPlainText(const SwString& text) {
        SwString current = m_pieceTable.toPlainText();
        if (!current.isEmpty() && !current.endsWith("\n")) {
            m_pieceTable.insert(m_pieceTable.totalLength(), SwString("\n"));
        }
        m_pieceTable.insert(m_pieceTable.totalLength(), text);
        m_cursorPos = m_pieceTable.totalLength();
        m_selectionStart = m_selectionEnd = m_cursorPos;
        clearUndoRedo_();
        rebuildVisualLines_();
        ensureCursorVisible();
        textChanged();
        update();
    }

    virtual void clear() {
        if (m_pieceTable.isEmpty()) {
            return;
        }
        m_pieceTable.setText(SwString());
        m_cursorPos = 0;
        m_selectionStart = m_selectionEnd = 0;
        m_firstVisibleLine = 0;
        clearUndoRedo_();
        rebuildVisualLines_();
        textChanged();
        update();
    }

    void setPlaceholderText(const SwString& text) {
        if (m_placeholder == text) {
            return;
        }
        m_placeholder = text;
        update();
    }

    SwString placeholderText() const { return m_placeholder; }

    void setReadOnly(bool on) { m_readOnly = on; }
    bool isReadOnly() const { return m_readOnly; }

    int cursorPosition() const { return static_cast<int>(m_cursorPos); }

    bool hasSelectedText() const { return m_selectionStart != m_selectionEnd; }

    SwString selectedText() const {
        if (!hasSelectedText()) {
            return {};
        }
        const size_t start = selectionMin_();
        const size_t end = selectionMax_();
        if (start >= m_pieceTable.totalLength() || end <= start) {
            return {};
        }
        return m_pieceTable.substr(start, end - start);
    }

    void selectAll() {
        m_selectionStart = 0;
        m_selectionEnd = m_pieceTable.totalLength();
        m_cursorPos = m_selectionEnd;
        update();
    }

    void setUndoRedoEnabled(bool on) {
        m_undoRedoEnabled = on;
        if (!m_undoRedoEnabled) {
            m_undoStack.clear();
            m_redoStack.clear();
        }
    }

    bool isUndoRedoEnabled() const { return m_undoRedoEnabled; }
    bool canUndo() const { return m_undoRedoEnabled && !m_undoStack.isEmpty(); }
    bool canRedo() const { return m_undoRedoEnabled && !m_redoStack.isEmpty(); }

    void undo() {
        if (m_readOnly || !canUndo()) {
            return;
        }
        EditState current = captureEditState();
        EditState previous = m_undoStack.back();
        m_undoStack.pop_back();
        m_redoStack.push_back(std::move(current));
        restoreEditState_(previous);
    }

    void redo() {
        if (m_readOnly || !canRedo()) {
            return;
        }
        EditState current = captureEditState();
        EditState next = m_redoStack.back();
        m_redoStack.pop_back();
        pushUndoState_(std::move(current));
        restoreEditState_(next);
    }

    void copy() {
        if (!hasSelectedText()) {
            return;
        }
        if (auto* platform = currentPlatformIntegration_()) {
            platform->setClipboardText(selectedText());
        }
    }

    void cut() {
        if (m_readOnly) {
            return;
        }
        copy();
        replaceSelectionWithText_(SwString());
        ensureCursorVisible();
        update();
    }

    void paste() {
        if (m_readOnly) {
            return;
        }
        if (auto* platform = currentPlatformIntegration_()) {
            SwString clip = platform->clipboardText();
            if (clip.isEmpty()) {
                return;
            }
            clip.replace("\r\n", "\n");
            clip.replace("\r", "\n");
            replaceSelectionWithText_(clip);
            ensureCursorVisible();
            update();
        }
    }

    DECLARE_SIGNAL_VOID(textChanged);

protected:
    void resizeEvent(ResizeEvent* event) override {
        SwFrame::resizeEvent(event);
        if (!m_wordWrapEnabled) {
            return;
        }
        rebuildVisualLines_();
        ensureCursorVisible();
        update();
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

        SwColor bg{255, 255, 255};
        float bgAlpha = 1.0f;
        bool paintBackground = true;

        SwColor border{220, 224, 232};
        int borderWidth = 1;
        int radius = 12;

        resolveBackground(sheet, bg, bgAlpha, paintBackground);
        resolveBorder(sheet, border, borderWidth, radius);

        if (getEnable() && getFocus()) {
            border = m_focusAccent;
        }

        if (paintBackground && bgAlpha > 0.0f) {
            painter->fillRoundedRect(bounds, radius, bg, border, borderWidth);
        } else {
            painter->drawRect(bounds, border, borderWidth);
        }

        const Padding pad = resolvePadding(sheet);
        SwRect inner = bounds;
        inner.x += borderWidth + pad.left;
        inner.y += borderWidth + pad.top;
        inner.width = std::max(0, inner.width - 2 * borderWidth - (pad.left + pad.right));
        inner.height = std::max(0, inner.height - 2 * borderWidth - (pad.top + pad.bottom));

        const int lh = lineHeightPx();
        const int visibleLines = (lh > 0) ? std::max(1, inner.height / lh) : 1;
        clampFirstVisibleLine(visibleLines);

        SwColor textColor = resolveTextColor(sheet, SwColor{24, 28, 36});
        if (!getEnable()) {
            textColor = SwColor{150, 150, 150};
        }

        painter->pushClipRect(inner);

        const bool hasSel = hasSelectedText();
        const size_t selMin = selectionMin_();
        const size_t selMax = selectionMax_();
        const SwColor selFill{219, 234, 254};
        const int fallbackWidth = std::max(1, inner.width);

        const int totalLines = effectiveLineCount_();
        const int first = std::max(0, m_firstVisibleLine);
        const int last = std::min(first + visibleLines + 1, totalLines);

        // Cache visible line content to avoid repeated piece table traversals
        for (int i = first; i < last; ++i) {
            const int row = i - first;
            SwRect lineRect{inner.x, inner.y + row * lh, inner.width, lh};

            const SwString lineText = effectiveLineContent_(i);
            const size_t lineStart = effectiveLineStart_(i);
            const size_t lineLen = lineText.size();

            if (hasSel) {
                const size_t lineEnd = lineStart + lineLen;
                const size_t segStart = (std::max)(selMin, lineStart);
                const size_t segEnd = (std::min)(selMax, lineEnd);
                if (segStart < segEnd) {
                    const size_t startCol = segStart - lineStart;
                    const size_t endCol = segEnd - lineStart;

                    const int x1 = inner.x + SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(),
                                                                                     lineText,
                                                                                     getFont(),
                                                                                     startCol,
                                                                                     fallbackWidth);
                    const int x2 = inner.x + SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(),
                                                                                     lineText,
                                                                                     getFont(),
                                                                                     endCol,
                                                                                     fallbackWidth);
                    const int left = (std::min)(x1, x2);
                    const int right = (std::max)(x1, x2);
                    if (right > left) {
                        painter->fillRect(SwRect{left, lineRect.y, right - left, lineRect.height}, selFill, selFill, 0);
                    }
                }
            }

            painter->drawText(lineRect,
                              lineText,
                              DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              textColor,
                              getFont());
        }

        if (m_pieceTable.isEmpty() && !m_placeholder.isEmpty() && !getFocus()) {
            SwRect phRect = inner;
            phRect.height = lh;
            SwColor ph{160, 160, 160};
            painter->drawText(phRect,
                              m_placeholder,
                              DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              ph,
                              getFont());
        }

        if (getFocus() && m_caretVisible) {
            const CursorInfo ci = cursorInfo();
            const int cursorLine = ci.line;
            const int cursorCol = ci.col;
            if (cursorLine >= first && cursorLine < last) {
                const SwString lineText = effectiveLineContent_(cursorLine);
                const size_t clampedCol = std::min(static_cast<size_t>(cursorCol), lineText.size());

                const int caretDx = SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(),
                                                                            lineText,
                                                                            getFont(),
                                                                            clampedCol,
                                                                            fallbackWidth);
                const int x = inner.x + caretDx;
                const int y = inner.y + (cursorLine - first) * lh;
                const int top = y + 4;
                const int bottom = y + lh - 4;
                SwColor caretColor = getEnable() ? SwColor{24, 28, 36} : SwColor{150, 150, 150};
                painter->drawLine(x, top, x, bottom, caretColor, 1);
            }
        }

        painter->popClipRect();
        painter->finalize();
    }

    void mousePressEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        if (!isPointInside(event->x(), event->y())) {
            SwFrame::mousePressEvent(event);
            return;
        }

        if (getEnable()) {
            setFocus(true);
        }

        const bool shift = SwWidgetPlatformAdapter::isShiftModifierActive();
        if (shift && !hasSelectedText()) {
            m_selectionStart = m_cursorPos;
        }

        updateCursorFromPosition(event->x(), event->y());

        if (shift) {
            m_selectionEnd = m_cursorPos;
        } else {
            m_selectionStart = m_selectionEnd = m_cursorPos;
        }
        m_isSelecting = true;
        ensureCursorVisible();
        event->accept();
        update();
    }

    void mouseMoveEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }
        if (m_isSelecting) {
            const size_t oldPos = m_cursorPos;
            updateCursorFromPosition(event->x(), event->y());
            m_selectionEnd = m_cursorPos;
            ensureCursorVisible();
            event->accept();
            if (oldPos != m_cursorPos) {
                update();
            }
        }
        SwFrame::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }
        if (m_isSelecting) {
            m_isSelecting = false;
            event->accept();
            update();
        }
        SwFrame::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }
        if (!isPointInside(event->x(), event->y())) {
            SwFrame::mouseDoubleClickEvent(event);
            return;
        }
        if (getEnable()) {
            setFocus(true);
        }

        updateCursorFromPosition(event->x(), event->y());
        selectWordAtCursor_();
        ensureCursorVisible();
        event->accept();
        update();
    }

    void wheelEvent(WheelEvent* event) override {
        if (!event) {
            return;
        }
        if (!isPointInside(event->x(), event->y())) {
            SwFrame::wheelEvent(event);
            return;
        }
        if (event->isShiftPressed()) {
            SwFrame::wheelEvent(event);
            return;
        }

        const int lh = lineHeightPx();
        if (lh <= 0) {
            SwFrame::wheelEvent(event);
            return;
        }

        const SwRect bounds = rect();
        const Padding pad = resolvePadding(getToolSheet());
        const int borderWidth = 1;
        SwRect inner = bounds;
        inner.x += borderWidth + pad.left;
        inner.y += borderWidth + pad.top;
        inner.width = std::max(0, inner.width - 2 * borderWidth - (pad.left + pad.right));
        inner.height = std::max(0, inner.height - 2 * borderWidth - (pad.top + pad.bottom));

        const int visibleLines = std::max(1, inner.height / lh);
        clampFirstVisibleLine(visibleLines);
        const int maxFirst = std::max(0, effectiveLineCount_() - visibleLines);

        int steps = event->delta() / 120;
        if (steps == 0) {
            steps = (event->delta() > 0) ? 1 : (event->delta() < 0 ? -1 : 0);
        }
        if (steps == 0) {
            SwFrame::wheelEvent(event);
            return;
        }

        m_firstVisibleLine = clampInt(m_firstVisibleLine - steps, 0, maxFirst);
        event->accept();
        update();
    }

    void keyPressEvent(KeyEvent* event) override {
        if (!event) {
            return;
        }

        if (!getEnable() || !getFocus()) {
            SwFrame::keyPressEvent(event);
            return;
        }

        const int key = event->key();
        const bool shift = event->isShiftPressed();
        const bool altGrTextInput = event->isCtrlPressed() &&
                                    event->isAltPressed() &&
                                    event->text() != L'\0';
        const bool shortcutCtrl = event->isCtrlPressed() && !altGrTextInput;

        if (shortcutCtrl) {
            if (SwWidgetPlatformAdapter::matchesShortcutKey(key, 'C')) {
                copy();
                event->accept();
            } else if (SwWidgetPlatformAdapter::matchesShortcutKey(key, 'X')) {
                cut();
                event->accept();
            } else if (SwWidgetPlatformAdapter::matchesShortcutKey(key, 'V')) {
                paste();
                event->accept();
            } else if (SwWidgetPlatformAdapter::matchesShortcutKey(key, 'A')) {
                selectAll();
                event->accept();
            } else if (SwWidgetPlatformAdapter::matchesShortcutKey(key, 'Z')) {
                if (shift) {
                    redo();
                } else {
                    undo();
                }
                event->accept();
            } else if (SwWidgetPlatformAdapter::matchesShortcutKey(key, 'Y')) {
                redo();
                event->accept();
            }

            if (event->isAccepted()) {
                ensureCursorVisible();
                update();
                return;
            }
        }

        if (SwWidgetPlatformAdapter::isLeftArrowKey(key)) {
            if (!shift && hasSelectedText()) {
                m_cursorPos = selectionMin_();
                m_selectionStart = m_selectionEnd = m_cursorPos;
            } else {
                if (shift && !hasSelectedText()) {
                    m_selectionStart = m_cursorPos;
                }
                moveCursorLeft();
                if (shift) {
                    m_selectionEnd = m_cursorPos;
                } else {
                    m_selectionStart = m_selectionEnd = m_cursorPos;
                }
            }
            event->accept();
        } else if (SwWidgetPlatformAdapter::isRightArrowKey(key)) {
            if (!shift && hasSelectedText()) {
                m_cursorPos = selectionMax_();
                m_selectionStart = m_selectionEnd = m_cursorPos;
            } else {
                if (shift && !hasSelectedText()) {
                    m_selectionStart = m_cursorPos;
                }
                moveCursorRight();
                if (shift) {
                    m_selectionEnd = m_cursorPos;
                } else {
                    m_selectionStart = m_selectionEnd = m_cursorPos;
                }
            }
            event->accept();
        } else if (SwWidgetPlatformAdapter::isHomeKey(key)) {
            if (!shift && hasSelectedText()) {
                m_cursorPos = selectionMin_();
                m_selectionStart = m_selectionEnd = m_cursorPos;
            } else {
                if (shift && !hasSelectedText()) {
                    m_selectionStart = m_cursorPos;
                }
                moveCursorHome();
                if (shift) {
                    m_selectionEnd = m_cursorPos;
                } else {
                    m_selectionStart = m_selectionEnd = m_cursorPos;
                }
            }
            event->accept();
        } else if (SwWidgetPlatformAdapter::isEndKey(key)) {
            if (!shift && hasSelectedText()) {
                m_cursorPos = selectionMax_();
                m_selectionStart = m_selectionEnd = m_cursorPos;
            } else {
                if (shift && !hasSelectedText()) {
                    m_selectionStart = m_cursorPos;
                }
                moveCursorEnd();
                if (shift) {
                    m_selectionEnd = m_cursorPos;
                } else {
                    m_selectionStart = m_selectionEnd = m_cursorPos;
                }
            }
            event->accept();
        } else if (SwWidgetPlatformAdapter::isUpArrowKey(key)) {
            if (!shift && hasSelectedText()) {
                m_cursorPos = selectionMin_();
                m_selectionStart = m_selectionEnd = m_cursorPos;
            } else {
                if (shift && !hasSelectedText()) {
                    m_selectionStart = m_cursorPos;
                }
                moveCursorUp();
                if (shift) {
                    m_selectionEnd = m_cursorPos;
                } else {
                    m_selectionStart = m_selectionEnd = m_cursorPos;
                }
            }
            event->accept();
        } else if (SwWidgetPlatformAdapter::isDownArrowKey(key)) {
            if (!shift && hasSelectedText()) {
                m_cursorPos = selectionMax_();
                m_selectionStart = m_selectionEnd = m_cursorPos;
            } else {
                if (shift && !hasSelectedText()) {
                    m_selectionStart = m_cursorPos;
                }
                moveCursorDown();
                if (shift) {
                    m_selectionEnd = m_cursorPos;
                } else {
                    m_selectionStart = m_selectionEnd = m_cursorPos;
                }
            }
            event->accept();
        } else if (!m_readOnly && SwWidgetPlatformAdapter::isBackspaceKey(key)) {
            backspace();
            event->accept();
        } else if (!m_readOnly && SwWidgetPlatformAdapter::isDeleteKey(key)) {
            deleteForward();
            event->accept();
        } else if (!m_readOnly && SwWidgetPlatformAdapter::isReturnKey(key)) {
            insertChar('\n');
            event->accept();
        } else if (SwWidgetPlatformAdapter::isEscapeKey(key)) {
            event->accept();
        } else if (!m_readOnly) {
            wchar_t wc = event->text();
            if (wc == L'\0' && !event->isTextProvided()) {
                char ascii = '\0';
                const bool caps = SwWidgetPlatformAdapter::isCapsLockKey(key);
                if (SwWidgetPlatformAdapter::translateCharacter(key, event->isShiftPressed(), caps, ascii)) {
                    wc = static_cast<wchar_t>(static_cast<unsigned char>(ascii));
                }
            }
            if (wc != L'\0' && wc != L'\r' && wc != L'\n') {
                insertText(utf8FromWideChar_(wc));
                event->accept();
            }
        }

        if (event->isAccepted()) {
            ensureCursorVisible();
            update();
            return;
        }

        SwFrame::keyPressEvent(event);
    }

protected:
    struct Padding {
        int top{10};
        int right{10};
        int bottom{10};
        int left{10};
    };

    struct CursorInfo {
        int line{0};
        int col{0};
        size_t lineStart{0};
    };

    static bool isUtf8ContinuationByte_(unsigned char byte) {
        return (byte & 0xC0U) == 0x80U;
    }

    static size_t clampUtf8BoundaryInText_(const SwString& text, size_t offset) {
        const std::string utf8 = text.toStdString();
        if (offset >= utf8.size()) {
            return utf8.size();
        }
        while (offset > 0 && isUtf8ContinuationByte_(static_cast<unsigned char>(utf8[offset]))) {
            --offset;
        }
        return offset;
    }

    static size_t nextUtf8BoundaryInText_(const SwString& text, size_t offset) {
        const std::string utf8 = text.toStdString();
        if (offset >= utf8.size()) {
            return utf8.size();
        }
        size_t next = clampUtf8BoundaryInText_(text, offset) + 1;
        while (next < utf8.size() && isUtf8ContinuationByte_(static_cast<unsigned char>(utf8[next]))) {
            ++next;
        }
        return next;
    }

    static SwString utf8FromWideChar_(wchar_t wc) {
        const std::wstring wide(1, wc);
        return SwString::fromWString(wide);
    }

    static Padding parsePaddingShorthand(const SwString& value, Padding current) {
        if (value.isEmpty()) {
            return current;
        }
        SwVector<SwString> tokens;
        std::istringstream ss(value.toStdString());
        std::string token;
        while (ss >> token) {
            tokens.push_back(SwString(token));
        }
        auto toPx = [&](const SwString& str, int fallback) -> int {
            return parsePixelValue(str, fallback);
        };
        if (tokens.size() == 1) {
            const int v = toPx(tokens[0], current.top);
            current.top = current.right = current.bottom = current.left = v;
        } else if (tokens.size() == 2) {
            const int v1 = toPx(tokens[0], current.top);
            const int v2 = toPx(tokens[1], current.right);
            current.top = current.bottom = v1;
            current.left = current.right = v2;
        } else if (tokens.size() == 3) {
            const int v1 = toPx(tokens[0], current.top);
            const int v2 = toPx(tokens[1], current.right);
            const int v3 = toPx(tokens[2], current.bottom);
            current.top = v1;
            current.left = current.right = v2;
            current.bottom = v3;
        } else if (tokens.size() >= 4) {
            current.top = toPx(tokens[0], current.top);
            current.right = toPx(tokens[1], current.right);
            current.bottom = toPx(tokens[2], current.bottom);
            current.left = toPx(tokens[3], current.left);
        }
        return current;
    }

    Padding resolvePadding(StyleSheet* sheet) const {
        Padding padding{};
        if (!sheet) {
            return padding;
        }

        SwString paddingValue;
        SwString paddingTopValue;
        SwString paddingRightValue;
        SwString paddingBottomValue;
        SwString paddingLeftValue;

        auto selectors = classHierarchy();
        if (!selectors.contains("SwWidget")) {
            selectors.append("SwWidget");
        }

        for (int i = static_cast<int>(selectors.size()) - 1; i >= 0; --i) {
            const SwString& selector = selectors[i];
            if (selector.isEmpty()) {
                continue;
            }
            SwString pad = sheet->getStyleProperty(selector, "padding");
            if (!pad.isEmpty()) {
                paddingValue = pad;
            }
            SwString padTop = sheet->getStyleProperty(selector, "padding-top");
            if (!padTop.isEmpty()) {
                paddingTopValue = padTop;
            }
            SwString padRight = sheet->getStyleProperty(selector, "padding-right");
            if (!padRight.isEmpty()) {
                paddingRightValue = padRight;
            }
            SwString padBottom = sheet->getStyleProperty(selector, "padding-bottom");
            if (!padBottom.isEmpty()) {
                paddingBottomValue = padBottom;
            }
            SwString padLeft = sheet->getStyleProperty(selector, "padding-left");
            if (!padLeft.isEmpty()) {
                paddingLeftValue = padLeft;
            }
        }

        if (!paddingValue.isEmpty()) {
            padding = parsePaddingShorthand(paddingValue, padding);
        }
        if (!paddingTopValue.isEmpty()) {
            padding.top = parsePixelValue(paddingTopValue, padding.top);
        }
        if (!paddingRightValue.isEmpty()) {
            padding.right = parsePixelValue(paddingRightValue, padding.right);
        }
        if (!paddingBottomValue.isEmpty()) {
            padding.bottom = parsePixelValue(paddingBottomValue, padding.bottom);
        }
        if (!paddingLeftValue.isEmpty()) {
            padding.left = parsePixelValue(paddingLeftValue, padding.left);
        }

        padding.top = std::max(0, padding.top);
        padding.right = std::max(0, padding.right);
        padding.bottom = std::max(0, padding.bottom);
        padding.left = std::max(0, padding.left);

        return padding;
    }

    int lineHeightPx() const {
        const int pt = getFont().getPointSize();
        return clampInt(pt + 10, 18, 34);
    }

    void ensureBlinkTimer() {
        if (m_blinkTimer) {
            return;
        }
        m_blinkTimer = new SwTimer(500, this);
        SwObject::connect(m_blinkTimer, &SwTimer::timeout, [this]() {
            if (!getFocus()) {
                return;
            }
            m_caretVisible = !m_caretVisible;
            update();
        });

        SwObject::connect(this, &SwPlainTextEdit::FocusChanged, [this](bool focus) {
            m_caretVisible = true;
            if (focus && isVisibleInHierarchy()) {
                if (m_blinkTimer) {
                    m_blinkTimer->start();
                }
            } else {
                if (m_blinkTimer) {
                    m_blinkTimer->stop();
                }
            }
            update();
        });

        SwObject::connect(this, &SwPlainTextEdit::VisibleChanged, [this](bool visible) {
            if (!visible && m_blinkTimer) {
                m_blinkTimer->stop();
            } else if (visible && getFocus() && m_blinkTimer) {
                m_blinkTimer->start();
            }
        });
    }

    // ─── Line access abstraction ────────────────────────────────────────
    // When word wrap is off, delegates directly to piece table (logical lines).
    // When word wrap is on, uses the visual line cache.

    int effectiveLineCount_() const {
        if (m_wordWrapEnabled && !m_visualLines.isEmpty()) {
            return m_visualLines.size();
        }
        return m_pieceTable.lineCount();
    }

    SwString effectiveLineContent_(int lineIdx) const {
        if (m_wordWrapEnabled && !m_visualLines.isEmpty()) {
            if (lineIdx < 0 || lineIdx >= m_visualLines.size()) {
                return SwString();
            }
            const VisualLine& vl = m_visualLines[lineIdx];
            return m_pieceTable.substr(vl.offset, vl.length);
        }
        return m_pieceTable.lineContent(lineIdx);
    }

    size_t effectiveLineStart_(int lineIdx) const {
        if (m_wordWrapEnabled && !m_visualLines.isEmpty()) {
            if (lineIdx < 0 || lineIdx >= m_visualLines.size()) {
                return 0;
            }
            return m_visualLines[lineIdx].offset;
        }
        return m_pieceTable.lineStart(lineIdx);
    }

    size_t effectiveLineLength_(int lineIdx) const {
        if (m_wordWrapEnabled && !m_visualLines.isEmpty()) {
            if (lineIdx < 0 || lineIdx >= m_visualLines.size()) {
                return 0;
            }
            return m_visualLines[lineIdx].length;
        }
        return m_pieceTable.lineLength(lineIdx);
    }

    int effectiveLineForOffset_(size_t pos) const {
        if (m_wordWrapEnabled && !m_visualLines.isEmpty()) {
            const size_t clamped = std::min(pos, m_pieceTable.totalLength());
            // Binary search on visual lines
            int lo = 0;
            int hi = m_visualLines.size() - 1;
            int result = 0;
            while (lo <= hi) {
                const int mid = lo + (hi - lo) / 2;
                if (m_visualLines[mid].offset <= clamped) {
                    result = mid;
                    lo = mid + 1;
                } else {
                    hi = mid - 1;
                }
            }
            return result;
        }
        return m_pieceTable.lineForOffset(pos);
    }

    // ─── Visual line cache (only used when word wrap is on) ─────────────
    struct VisualLine {
        size_t offset;
        size_t length;
    };

    void rebuildVisualLines_() {
        m_visualLines.clear();
        if (!m_wordWrapEnabled) {
            return; // Use piece table directly
        }

        const int wrapWidthPx = resolveInnerWrapWidthPx_();
        if (wrapWidthPx <= 1) {
            return;
        }

        // Iterate logical lines and wrap each one
        const int logicalCount = m_pieceTable.lineCount();
        for (int logLine = 0; logLine < logicalCount; ++logLine) {
            const size_t lineStart = m_pieceTable.lineStart(logLine);
            const size_t lineLen = m_pieceTable.lineLength(logLine);

            if (lineLen == 0) {
                m_visualLines.push_back(VisualLine{lineStart, 0});
                continue;
            }

            const SwString paragraph = m_pieceTable.substr(lineStart, lineLen);
            const int fallbackWidth = std::max(1, wrapWidthPx);
            size_t localStart = 0;

            while (localStart < lineLen) {
                const size_t clampedLocalStart = clampUtf8BoundaryInText_(paragraph, localStart);
                const size_t remaining = lineLen - clampedLocalStart;
                const SwString remainingText = paragraph.substr(clampedLocalStart, remaining);

                auto textWidth = [&](size_t count) -> int {
                    return SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(),
                                                                   remainingText,
                                                                   getFont(),
                                                                   count,
                                                                   fallbackWidth);
                };

                size_t best = nextUtf8BoundaryInText_(remainingText, 0);
                for (size_t probe = best; probe <= remaining;) {
                    if (textWidth(probe) <= wrapWidthPx) {
                        best = probe;
                    } else {
                        break;
                    }
                    if (probe == remaining) {
                        break;
                    }
                    const size_t nextProbe = nextUtf8BoundaryInText_(remainingText, probe);
                    if (nextProbe <= probe) {
                        break;
                    }
                    probe = nextProbe;
                }

                size_t take = std::max<size_t>(1, best);

                if (take < remaining) {
                    size_t breakPos = static_cast<size_t>(-1);
                    size_t idx = take;
                    while (idx > 0) {
                        --idx;
                        const char ch = remainingText[idx];
                        if (ch == ' ' || ch == '\t') {
                            breakPos = idx;
                            break;
                        }
                    }
                    if (breakPos != static_cast<size_t>(-1) && breakPos > 0) {
                        take = breakPos + 1;
                    }
                }

                m_visualLines.push_back(VisualLine{lineStart + clampedLocalStart, take});
                localStart = clampedLocalStart + take;
            }
        }

        if (m_visualLines.isEmpty()) {
            m_visualLines.push_back(VisualLine{0, 0});
        }
    }

    int resolveInnerWrapWidthPx_() {
        const SwRect bounds = rect();
        if (bounds.width <= 0) {
            return 1;
        }

        StyleSheet* sheet = getToolSheet();

        SwColor border{220, 224, 232};
        int borderWidth = 1;
        int radius = 12;
        resolveBorder(sheet, border, borderWidth, radius);

        const Padding pad = resolvePadding(sheet);
        const int innerW = std::max(0, bounds.width - 2 * borderWidth - (pad.left + pad.right));
        return std::max(1, innerW);
    }

    // ─── Cursor ─────────────────────────────────────────────────────────

    CursorInfo cursorInfo() const {
        CursorInfo ci;
        const size_t cursor = clampCursorBoundary_(std::min(m_cursorPos, m_pieceTable.totalLength()));
        const int line = effectiveLineForOffset_(cursor);
        const size_t ls = effectiveLineStart_(line);
        ci.line = std::max(0, std::min(line, effectiveLineCount_() - 1));
        ci.lineStart = ls;
        ci.col = static_cast<int>(cursor - ls);
        return ci;
    }

    void clampFirstVisibleLine(int visibleLines) {
        visibleLines = std::max(1, visibleLines);
        const int maxFirst = std::max(0, effectiveLineCount_() - visibleLines);
        m_firstVisibleLine = clampInt(m_firstVisibleLine, 0, maxFirst);
    }

    void ensureCursorVisible() {
        const int lh = lineHeightPx();
        if (lh <= 0) {
            return;
        }

        const SwRect bounds = rect();
        const Padding pad = resolvePadding(getToolSheet());
        const int borderWidth = 1;
        SwRect inner = bounds;
        inner.x += borderWidth + pad.left;
        inner.y += borderWidth + pad.top;
        inner.width = std::max(0, inner.width - 2 * borderWidth - (pad.left + pad.right));
        inner.height = std::max(0, inner.height - 2 * borderWidth - (pad.top + pad.bottom));

        const int visibleLines = std::max(1, inner.height / lh);
        clampFirstVisibleLine(visibleLines);

        const int line = cursorInfo().line;
        if (line < m_firstVisibleLine) {
            m_firstVisibleLine = line;
        } else if (line >= m_firstVisibleLine + visibleLines) {
            m_firstVisibleLine = line - visibleLines + 1;
        }
        clampFirstVisibleLine(visibleLines);
    }

    virtual void updateCursorFromPosition(int px, int py) {
        const SwRect bounds = rect();
        const Padding pad = resolvePadding(getToolSheet());
        const int borderWidth = 1;
        SwRect inner = bounds;
        inner.x += borderWidth + pad.left;
        inner.y += borderWidth + pad.top;
        inner.width = std::max(0, inner.width - 2 * borderWidth - (pad.left + pad.right));
        inner.height = std::max(0, inner.height - 2 * borderWidth - (pad.top + pad.bottom));

        const int lh = lineHeightPx();
        const int relativeY = py - inner.y;
        int row = (lh > 0) ? (relativeY / lh) : 0;
        row = std::max(0, row);

        const int lineIdx = clampInt(m_firstVisibleLine + row, 0, effectiveLineCount_() - 1);

        const SwString lineText = effectiveLineContent_(lineIdx);
        const int relativeX = px - inner.x;
        const size_t col = SwWidgetPlatformAdapter::characterIndexAtPosition(nativeWindowHandle(),
                                                                             lineText,
                                                                             getFont(),
                                                                             relativeX,
                                                                             std::max(1, inner.width));

        const size_t ls = effectiveLineStart_(lineIdx);
        const size_t clampedCol = clampUtf8BoundaryInText_(lineText, std::min(col, lineText.size()));
        m_cursorPos = std::min(ls + clampedCol, m_pieceTable.totalLength());
    }

    // ─── Text mutation ──────────────────────────────────────────────────

    virtual void insertTextAt(size_t pos, const SwString& text) {
        const size_t clamped = clampCursorBoundary_(std::min(pos, m_pieceTable.totalLength()));
        m_pieceTable.insert(clamped, text);
    }

    virtual void eraseTextAt(size_t pos, size_t len) {
        if (len == 0 || m_pieceTable.isEmpty()) {
            return;
        }
        const size_t clampedPos = std::min(pos, m_pieceTable.totalLength());
        if (clampedPos >= m_pieceTable.totalLength()) {
            return;
        }
        const size_t clampedLen = std::min(len, m_pieceTable.totalLength() - clampedPos);
        m_pieceTable.remove(clampedPos, clampedLen);
    }

    virtual void insertChar(char ch) {
        if (m_readOnly) {
            return;
        }
        replaceSelectionWithText_(SwString(1, ch), UndoMergeKind::InsertText);
    }

    virtual void insertText(const SwString& text) {
        if (m_readOnly || text.isEmpty()) {
            return;
        }
        replaceSelectionWithText_(text, UndoMergeKind::InsertText);
    }

    virtual void backspace() {
        if (m_readOnly) {
            return;
        }
        if (hasSelectedText()) {
            replaceSelectionWithText_(SwString());
            return;
        }
        if (m_cursorPos == 0 || m_pieceTable.isEmpty()) {
            return;
        }
        const size_t pos = clampCursorBoundary_(std::min(m_cursorPos, m_pieceTable.totalLength()));
        const size_t prev = previousCursorBoundary_(pos);
        if (prev == pos) {
            return;
        }
        recordUndoState_(UndoMergeKind::Backspace);
        eraseTextAt(prev, pos - prev);
        m_cursorPos = prev;
        m_selectionStart = m_selectionEnd = m_cursorPos;
        if (m_wordWrapEnabled) {
            rebuildVisualLines_();
        }
        rememberUndoMergeState_(UndoMergeKind::Backspace);
        textChanged();
    }

    virtual void deleteForward() {
        if (m_readOnly) {
            return;
        }
        if (hasSelectedText()) {
            replaceSelectionWithText_(SwString());
            return;
        }
        const size_t pos = clampCursorBoundary_(std::min(m_cursorPos, m_pieceTable.totalLength()));
        if (pos >= m_pieceTable.totalLength()) {
            return;
        }
        const size_t next = nextCursorBoundary_(pos);
        if (next == pos) {
            return;
        }
        recordUndoState_(UndoMergeKind::DeleteForward);
        eraseTextAt(pos, next - pos);
        m_cursorPos = std::min(m_cursorPos, m_pieceTable.totalLength());
        m_selectionStart = m_selectionEnd = m_cursorPos;
        if (m_wordWrapEnabled) {
            rebuildVisualLines_();
        }
        rememberUndoMergeState_(UndoMergeKind::DeleteForward);
        textChanged();
    }

    void moveCursorLeft() {
        if (m_cursorPos > 0) {
            m_cursorPos = previousCursorBoundary_(m_cursorPos);
        }
    }

    void moveCursorRight() {
        if (m_cursorPos < m_pieceTable.totalLength()) {
            m_cursorPos = nextCursorBoundary_(m_cursorPos);
        }
    }

    void moveCursorHome() {
        const CursorInfo ci = cursorInfo();
        m_cursorPos = ci.lineStart;
    }

    void moveCursorEnd() {
        const CursorInfo ci = cursorInfo();
        const size_t lineLen = effectiveLineLength_(ci.line);
        m_cursorPos = std::min(ci.lineStart + lineLen, m_pieceTable.totalLength());
    }

    void moveCursorUp() {
        const CursorInfo ci = cursorInfo();
        if (ci.line <= 0) {
            return;
        }
        const int targetLine = ci.line - 1;
        const size_t start = effectiveLineStart_(targetLine);
        const SwString lineText = effectiveLineContent_(targetLine);
        const size_t len = lineText.size();
        const size_t col = clampUtf8BoundaryInText_(lineText, std::min(static_cast<size_t>(ci.col), len));
        m_cursorPos = std::min(start + col, m_pieceTable.totalLength());
    }

    void moveCursorDown() {
        const CursorInfo ci = cursorInfo();
        if (ci.line >= effectiveLineCount_() - 1) {
            return;
        }
        const int targetLine = ci.line + 1;
        const size_t start = effectiveLineStart_(targetLine);
        const SwString lineText = effectiveLineContent_(targetLine);
        const size_t len = lineText.size();
        const size_t col = clampUtf8BoundaryInText_(lineText, std::min(static_cast<size_t>(ci.col), len));
        m_cursorPos = std::min(start + col, m_pieceTable.totalLength());
    }

    void initDefaults() {
        resize(420, 160);
        setCursor(CursorType::IBeam);
        setFocusPolicy(FocusPolicyEnum::Strong);
        setFrameShape(Shape::Box);
        setFont(SwFont(L"Segoe UI", 10, Medium));
        setStyleSheet(R"(
            SwPlainTextEdit {
                background-color: rgb(255, 255, 255);
                border-color: rgb(220, 224, 232);
                border-width: 1px;
                border-radius: 12px;
                padding: 10px 12px;
                color: rgb(24, 28, 36);
                font-size: 14px;
            }
        )");
    }

    size_t selectionMin_() const { return (std::min)(m_selectionStart, m_selectionEnd); }
    size_t selectionMax_() const { return (std::max)(m_selectionStart, m_selectionEnd); }

    size_t clampCursorBoundary_(size_t pos) const {
        const size_t total = m_pieceTable.totalLength();
        if (pos >= total) {
            return total;
        }
        while (pos > 0 && isUtf8ContinuationByte_(static_cast<unsigned char>(m_pieceTable.charAt(pos)))) {
            --pos;
        }
        return pos;
    }

    size_t previousCursorBoundary_(size_t pos) const {
        size_t current = std::min(pos, m_pieceTable.totalLength());
        if (current == 0) {
            return 0;
        }
        --current;
        while (current > 0 && isUtf8ContinuationByte_(static_cast<unsigned char>(m_pieceTable.charAt(current)))) {
            --current;
        }
        return current;
    }

    size_t nextCursorBoundary_(size_t pos) const {
        const size_t total = m_pieceTable.totalLength();
        size_t current = clampCursorBoundary_(std::min(pos, total));
        if (current >= total) {
            return total;
        }
        ++current;
        while (current < total && isUtf8ContinuationByte_(static_cast<unsigned char>(m_pieceTable.charAt(current)))) {
            ++current;
        }
        return current;
    }

    void clearUndoRedo_() {
        m_undoStack.clear();
        m_redoStack.clear();
        resetUndoMergeState_();
    }

    void pushUndoState_(EditState state) {
        if (!m_undoRedoEnabled) {
            return;
        }
        m_undoStack.push_back(std::move(state));
        if (m_undoStack.size() > m_undoLimit) {
            m_undoStack.erase(m_undoStack.begin());
        }
    }

    bool shouldMergeUndoState_(UndoMergeKind kind) const {
        if (kind == UndoMergeKind::NoMerge || m_undoStack.isEmpty()) {
            return false;
        }
        if (m_lastUndoMergeKind != kind || hasSelectedText()) {
            return false;
        }
        switch (kind) {
            case UndoMergeKind::InsertText:
                return m_cursorPos == m_lastUndoMergeCursorPos;
            case UndoMergeKind::Backspace:
                return m_cursorPos == m_lastUndoMergeCursorPos;
            case UndoMergeKind::DeleteForward:
                return m_cursorPos == m_lastUndoMergeCursorPos;
            case UndoMergeKind::NoMerge:
            default:
                return false;
        }
    }

    void rememberUndoMergeState_(UndoMergeKind kind) {
        m_lastUndoMergeKind = kind;
        m_lastUndoMergeCursorPos = m_cursorPos;
    }

    void resetUndoMergeState_() {
        m_lastUndoMergeKind = UndoMergeKind::NoMerge;
        m_lastUndoMergeCursorPos = static_cast<size_t>(-1);
    }

    void recordUndoState_(UndoMergeKind kind = UndoMergeKind::NoMerge) {
        if (!m_undoRedoEnabled) {
            return;
        }
        if (shouldMergeUndoState_(kind)) {
            m_redoStack.clear();
            return;
        }
        pushUndoState_(captureEditState());
        m_redoStack.clear();
        m_lastUndoMergeKind = kind;
    }

    void restoreEditState_(const EditState& state) {
        const bool textWasDifferent = !m_pieceTable.equals(state.text);

        m_pieceTable.setText(state.text);
        m_cursorPos = clampCursorBoundary_(std::min(state.cursorPos, m_pieceTable.totalLength()));
        m_selectionStart = clampCursorBoundary_(std::min(state.selectionStart, m_pieceTable.totalLength()));
        m_selectionEnd = clampCursorBoundary_(std::min(state.selectionEnd, m_pieceTable.totalLength()));
        m_firstVisibleLine = state.firstVisibleLine;

        if (state.applyExtra) {
            state.applyExtra();
        }

        rebuildVisualLines_();
        ensureCursorVisible();
        if (textWasDifferent) {
            textChanged();
        }
        resetUndoMergeState_();
        update();
    }

    void replaceSelectionWithText_(const SwString& text, UndoMergeKind mergeKind = UndoMergeKind::NoMerge) {
        size_t start = m_cursorPos;
        size_t end = m_cursorPos;
        if (hasSelectedText()) {
            start = selectionMin_();
            end = selectionMax_();
        }

        start = clampCursorBoundary_(std::min(start, m_pieceTable.totalLength()));
        end = clampCursorBoundary_(std::min(end, m_pieceTable.totalLength()));
        if (end <= start && text.isEmpty()) {
            return;
        }
        if (end > start) {
            mergeKind = UndoMergeKind::NoMerge;
        }
        recordUndoState_(mergeKind);

        if (end > start) {
            eraseTextAt(start, end - start);
        }
        if (!text.isEmpty()) {
            insertTextAt(start, text);
        }

        m_cursorPos = std::min(start + text.size(), m_pieceTable.totalLength());
        m_selectionStart = m_selectionEnd = m_cursorPos;

        if (m_wordWrapEnabled) {
            rebuildVisualLines_();
        }
        rememberUndoMergeState_(mergeKind);
        textChanged();
    }

    void selectWordAtCursor_() {
        if (m_pieceTable.isEmpty()) {
            return;
        }

        size_t pos = std::min(m_cursorPos, m_pieceTable.totalLength());
        if (pos == m_pieceTable.totalLength()) {
            pos = (pos > 0) ? (pos - 1) : 0;
        }

        auto isWordChar = [](unsigned char c) {
            return std::isalnum(c) != 0 || c == static_cast<unsigned char>('_');
        };

        if (!isWordChar(static_cast<unsigned char>(m_pieceTable.charAt(pos)))) {
            selectAll();
            return;
        }

        size_t start = pos;
        while (start > 0 && isWordChar(static_cast<unsigned char>(m_pieceTable.charAt(start - 1)))) {
            --start;
        }
        size_t end = pos;
        while (end < m_pieceTable.totalLength() && isWordChar(static_cast<unsigned char>(m_pieceTable.charAt(end)))) {
            ++end;
        }

        m_selectionStart = start;
        m_selectionEnd = end;
        m_cursorPos = end;
    }

    // ─── Data members ───────────────────────────────────────────────────
    mutable SwPieceTable m_pieceTable;
    SwString m_placeholder;
    SwVector<VisualLine> m_visualLines;   // only used when word wrap is on
    size_t m_cursorPos{0};
    size_t m_selectionStart{0};
    size_t m_selectionEnd{0};
    bool m_isSelecting{false};
    int m_firstVisibleLine{0};
    bool m_readOnly{false};
    bool m_undoRedoEnabled{true};
    size_t m_undoLimit{200};
    SwVector<EditState> m_undoStack;
    SwVector<EditState> m_redoStack;
    UndoMergeKind m_lastUndoMergeKind{UndoMergeKind::NoMerge};
    size_t m_lastUndoMergeCursorPos{static_cast<size_t>(-1)};

    bool m_caretVisible{true};
    SwTimer* m_blinkTimer{nullptr};

    SwColor m_focusAccent{59, 130, 246};

    bool m_wordWrapEnabled{false};

    SwPlatformIntegration* currentPlatformIntegration_() const {
        SwGuiApplication* app = SwGuiApplication::instance(false);
        return app ? app->platformIntegration() : nullptr;
    }
};
