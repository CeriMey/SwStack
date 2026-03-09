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
 * @brief Declares the public interface exposed by SwPlainTextEdit in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the plain text edit interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwPlainTextEdit.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */


/***************************************************************************************************
 * SwPlainTextEdit - multi-line plain text editor.
 *
 * Goals:
 * - Plain text storage (no rich text rendering).
 * - Keyboard editing + cursor, suitable for snapshot-based visual validation.
 * - Styling via StyleSheet basics: background-color / border-color / border-width / border-radius /
 *   color / padding.
 **************************************************************************************************/

#include "SwFrame.h"
#include "SwGuiApplication.h"
#include "SwTimer.h"
#include "SwWidgetPlatformAdapter.h"
#include "SwVector.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

class SwPlainTextEdit : public SwFrame {
    SW_OBJECT(SwPlainTextEdit, SwFrame)

protected:
    struct EditState {
        SwString text;
        size_t cursorPos{0};
        size_t selectionStart{0};
        size_t selectionEnd{0};
        int firstVisibleLine{0};
        /**
         * @brief Returns the current function<void.
         * @return The current function<void.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        std::function<void()> applyExtra;
    };

    /**
     * @brief Returns the current capture Edit State.
     * @return The current capture Edit State.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual EditState captureEditState() const {
        EditState st;
        st.text = m_text;
        st.cursorPos = m_cursorPos;
        st.selectionStart = m_selectionStart;
        st.selectionEnd = m_selectionEnd;
        st.firstVisibleLine = m_firstVisibleLine;
        return st;
    }

public:
    /**
     * @brief Constructs a `SwPlainTextEdit` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwPlainTextEdit(SwWidget* parent = nullptr)
        : SwFrame(parent) {
        initDefaults();
        rebuildLines();
        ensureBlinkTimer();
    }

    /**
     * @brief Sets the word Wrap Enabled.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setWordWrapEnabled(bool on) {
        if (m_wordWrapEnabled == on) {
            return;
        }
        m_wordWrapEnabled = on;
        rebuildLines();
        ensureCursorVisible();
        update();
    }

    /**
     * @brief Returns the current word Wrap Enabled.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool wordWrapEnabled() const { return m_wordWrapEnabled; }

    /**
     * @brief Sets the plain Text.
     * @param text Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    virtual void setPlainText(const SwString& text) {
        if (m_text == text) {
            return;
        }
        m_text = text;
        if (m_cursorPos > m_text.size()) {
            m_cursorPos = m_text.size();
        }
        m_selectionStart = m_selectionEnd = m_cursorPos;
        clearUndoRedo_();
        rebuildLines();
        ensureCursorVisible();
        textChanged();
        update();
    }

    /**
     * @brief Returns the current to Plain Text.
     * @return The current to Plain Text.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString toPlainText() const { return m_text; }

    /**
     * @brief Performs the `appendPlainText` operation.
     * @param text Value passed to the method.
     */
    virtual void appendPlainText(const SwString& text) {
        if (!m_text.isEmpty() && !m_text.endsWith("\n")) {
            m_text.append("\n");
        }
        m_text.append(text);
        m_cursorPos = m_text.size();
        m_selectionStart = m_selectionEnd = m_cursorPos;
        clearUndoRedo_();
        rebuildLines();
        ensureCursorVisible();
        textChanged();
        update();
    }

    /**
     * @brief Clears the current object state.
     */
    virtual void clear() {
        if (m_text.isEmpty()) {
            return;
        }
        m_text.clear();
        m_cursorPos = 0;
        m_selectionStart = m_selectionEnd = 0;
        m_firstVisibleLine = 0;
        clearUndoRedo_();
        rebuildLines();
        textChanged();
        update();
    }

    /**
     * @brief Sets the placeholder Text.
     * @param text Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPlaceholderText(const SwString& text) {
        if (m_placeholder == text) {
            return;
        }
        m_placeholder = text;
        update();
    }

    /**
     * @brief Returns the current placeholder Text.
     * @return The current placeholder Text.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString placeholderText() const { return m_placeholder; }

    /**
     * @brief Sets the read Only.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setReadOnly(bool on) { m_readOnly = on; }
    /**
     * @brief Returns whether the object reports read Only.
     * @return `true` when the object reports read Only; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isReadOnly() const { return m_readOnly; }

    /**
     * @brief Performs the `cursorPosition` operation.
     * @param m_cursorPos Value passed to the method.
     * @return The requested cursor Position.
     */
    int cursorPosition() const { return static_cast<int>(m_cursorPos); }

    /**
     * @brief Returns whether the object reports selected Text.
     * @return `true` when the object reports selected Text; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool hasSelectedText() const { return m_selectionStart != m_selectionEnd; }

    /**
     * @brief Returns the current selected Text.
     * @return The current selected Text.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString selectedText() const {
        if (!hasSelectedText()) {
            return {};
        }
        const size_t start = selectionMin_();
        const size_t end = selectionMax_();
        if (start >= m_text.size() || end <= start) {
            return {};
        }
        return m_text.substr(start, end - start);
    }

    /**
     * @brief Performs the `selectAll` operation.
     */
    void selectAll() {
        m_selectionStart = 0;
        m_selectionEnd = m_text.size();
        m_cursorPos = m_selectionEnd;
        update();
    }

    /**
     * @brief Sets the undo Redo Enabled.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setUndoRedoEnabled(bool on) {
        m_undoRedoEnabled = on;
        if (!m_undoRedoEnabled) {
            m_undoStack.clear();
            m_redoStack.clear();
        }
    }

    /**
     * @brief Returns whether the object reports undo Redo Enabled.
     * @return `true` when the object reports undo Redo Enabled; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isUndoRedoEnabled() const { return m_undoRedoEnabled; }
    /**
     * @brief Returns whether the object reports undo.
     * @return `true` when the object reports undo; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool canUndo() const { return m_undoRedoEnabled && !m_undoStack.empty(); }
    /**
     * @brief Returns whether the object reports redo.
     * @return `true` when the object reports redo; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool canRedo() const { return m_undoRedoEnabled && !m_redoStack.empty(); }

    /**
     * @brief Performs the `undo` operation.
     */
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

    /**
     * @brief Performs the `redo` operation.
     */
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

    /**
     * @brief Performs the `copy` operation.
     */
    void copy() {
        if (!hasSelectedText()) {
            return;
        }
        if (auto* platform = currentPlatformIntegration_()) {
            platform->setClipboardText(selectedText());
        }
    }

    /**
     * @brief Performs the `cut` operation.
     */
    void cut() {
        if (m_readOnly) {
            return;
        }
        copy();
        replaceSelectionWithText_(SwString());
        ensureCursorVisible();
        update();
    }

    /**
     * @brief Performs the `paste` operation.
     */
    void paste() {
        if (m_readOnly) {
            return;
        }
        if (auto* platform = currentPlatformIntegration_()) {
            SwString clip = platform->clipboardText();
            if (clip.isEmpty()) {
                return;
            }
            // Normalize to '\n' line breaks to keep internal storage consistent.
            clip.replace("\r\n", "\n");
            clip.replace("\r", "\n");
            replaceSelectionWithText_(clip);
            ensureCursorVisible();
            update();
        }
    }

    DECLARE_SIGNAL_VOID(textChanged);

protected:
    /**
     * @brief Handles the resize Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwFrame::resizeEvent(event);
        if (!m_wordWrapEnabled) {
            return;
        }
        rebuildLines();
        ensureCursorVisible();
        update();
    }

    /**
     * @brief Handles the paint Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
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

        const int first = std::max(0, m_firstVisibleLine);
        const int last = std::min(first + visibleLines + 1, m_lines.size());
        for (int i = first; i < last; ++i) {
            const int row = i - first;
            SwRect lineRect{inner.x, inner.y + row * lh, inner.width, lh};

            if (hasSel && i < m_lineStarts.size()) {
                const size_t lineStart = m_lineStarts[i];
                const size_t lineLen = m_lines[i].size();
                const size_t lineEnd = lineStart + lineLen;

                const size_t segStart = (std::max)(selMin, lineStart);
                const size_t segEnd = (std::min)(selMax, lineEnd);
                if (segStart < segEnd) {
                    const size_t startCol = segStart - lineStart;
                    const size_t endCol = segEnd - lineStart;

                    const int x1 = inner.x + SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(),
                                                                                     m_lines[i],
                                                                                     getFont(),
                                                                                     startCol,
                                                                                     fallbackWidth);
                    const int x2 = inner.x + SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(),
                                                                                     m_lines[i],
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
                              m_lines[i],
                              DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              textColor,
                              getFont());
        }

        if (m_text.isEmpty() && !m_placeholder.isEmpty() && !getFocus()) {
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
                const SwString& lineText = m_lines[cursorLine];
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

    /**
     * @brief Handles the mouse Press Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
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

    /**
     * @brief Handles the mouse Move Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
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

    /**
     * @brief Handles the mouse Release Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
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

    /**
     * @brief Handles the mouse Double Click Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
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

    /**
     * @brief Handles the wheel Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void wheelEvent(WheelEvent* event) override {
        if (!event) {
            return;
        }
        if (!isPointInside(event->x(), event->y())) {
            SwFrame::wheelEvent(event);
            return;
        }
        if (event->isShiftPressed()) {
            // Plain text edit only implements vertical wheel scrolling. Let
            // horizontal wheel gestures bubble so parent scroll areas or
            // horizontal scroll bars can consume them.
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
        const int maxFirst = std::max(0, m_lines.size() - visibleLines);

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

    /**
     * @brief Handles the key Press Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
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

        if (event->isCtrlPressed()) {
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
                // Encode as UTF-8 before inserting (1 byte ASCII, 2 bytes Latin-1 like ÃƒÂª/ÃƒÂ /ÃƒÂ©)
                if (wc < 0x80) {
                    insertChar(static_cast<char>(wc));
                } else if (wc < 0x800) {
                    insertChar(static_cast<char>(0xC0 | (wc >> 6)));
                    insertChar(static_cast<char>(0x80 | (wc & 0x3F)));
                } else {
                    insertChar(static_cast<char>(0xE0 | (wc >> 12)));
                    insertChar(static_cast<char>(0x80 | ((wc >> 6) & 0x3F)));
                    insertChar(static_cast<char>(0x80 | (wc & 0x3F)));
                }
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

    /**
     * @brief Performs the `parsePaddingShorthand` operation.
     * @param value Value passed to the method.
     * @param current Value passed to the method.
     * @return The requested parse Padding Shorthand.
     */
    static Padding parsePaddingShorthand(const SwString& value, Padding current) {
        if (value.isEmpty()) {
            return current;
        }
        std::vector<std::string> tokens;
        std::istringstream ss(value.toStdString());
        std::string token;
        while (ss >> token) {
            tokens.push_back(token);
        }
        auto toPx = [&](const std::string& str, int fallback) -> int {
            return parsePixelValue(SwString(str), fallback);
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

    /**
     * @brief Performs the `resolvePadding` operation.
     * @param sheet Value passed to the method.
     * @return The requested resolve Padding.
     */
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

    /**
     * @brief Returns the current line Height Px.
     * @return The current line Height Px.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int lineHeightPx() const {
        const int pt = getFont().getPointSize();
        return clampInt(pt + 10, 18, 34);
    }

    /**
     * @brief Performs the `ensureBlinkTimer` operation.
     */
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
            if (focus) {
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
    }

    /**
     * @brief Performs the `rebuildLines` operation.
     */
    void rebuildLines() {
        m_lines.clear();
        m_lineStarts.clear();

        const size_t n = m_text.size();
        size_t lineStart = 0;

        const bool wrap = m_wordWrapEnabled;
        const int wrapWidthPx = wrap ? resolveInnerWrapWidthPx_() : 0;

        auto pushLine = [&](size_t start, size_t len) {
            m_lines.push_back(m_text.substr(start, len));
            m_lineStarts.push_back(start);
        };

        auto pushWrapped = [&](size_t start, size_t len) {
            if (len == 0) {
                pushLine(start, 0);
                return;
            }

            const int fallbackWidth = std::max(1, wrapWidthPx);
            const SwString paragraph = m_text.substr(start, len);
            size_t localStart = 0;

            while (localStart < len) {
                const size_t remaining = len - localStart;
                const SwString remainingText = paragraph.substr(localStart, remaining);

                auto textWidth = [&](size_t count) -> int {
                    return SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(),
                                                                   remainingText,
                                                                   getFont(),
                                                                   count,
                                                                   fallbackWidth);
                };

                size_t best = 1;
                size_t lo = 1;
                size_t hi = remaining;
                while (lo <= hi) {
                    const size_t mid = lo + ((hi - lo) / 2);
                    if (textWidth(mid) <= wrapWidthPx) {
                        best = mid;
                        lo = mid + 1;
                    } else {
                        if (mid == 0) {
                            break;
                        }
                        hi = mid - 1;
                    }
                }

                size_t take = std::max<size_t>(1, best);

                // Prefer breaking at whitespace when possible (keeps words together).
                if (take < remaining) {
                    size_t breakPos = static_cast<size_t>(-1);
                    size_t i = take;
                    while (i > 0) {
                        --i;
                        const char ch = remainingText[i];
                        if (ch == ' ' || ch == '\t') {
                            breakPos = i;
                            break;
                        }
                    }
                    if (breakPos != static_cast<size_t>(-1) && breakPos > 0) {
                        take = breakPos + 1; // include the whitespace (keeps 1:1 mapping with m_text)
                    }
                }

                pushLine(start + localStart, take);
                localStart += take;
            }
        };

        for (size_t i = 0; i <= n; ++i) {
            if (i == n || m_text[i] == '\n') {
                const size_t segLen = i - lineStart;
                if (!wrap || wrapWidthPx <= 1) {
                    pushLine(lineStart, segLen);
                } else {
                    pushWrapped(lineStart, segLen);
                }
                lineStart = i + 1;
            }
        }

        if (m_lines.size() == 0) {
            pushLine(0, 0);
        }
    }

    /**
     * @brief Returns the current resolve Inner Wrap Width Px.
     * @return The current resolve Inner Wrap Width Px.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
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

    /**
     * @brief Returns the current cursor Info.
     * @return The current cursor Info.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    CursorInfo cursorInfo() const {
        CursorInfo ci;
        const size_t cursor = std::min(m_cursorPos, m_text.size());

        int line = 0;
        size_t lineStart = 0;
        for (int i = 0; i < m_lineStarts.size(); ++i) {
            const size_t s = m_lineStarts[i];
            if (s <= cursor) {
                line = i;
                lineStart = s;
            } else {
                break;
            }
        }

        ci.line = std::max(0, std::min(line, m_lines.size() - 1));
        ci.lineStart = lineStart;
        ci.col = static_cast<int>(cursor - lineStart);
        return ci;
    }

    /**
     * @brief Performs the `clampFirstVisibleLine` operation.
     * @param visibleLines Value passed to the method.
     */
    void clampFirstVisibleLine(int visibleLines) {
        visibleLines = std::max(1, visibleLines);
        const int maxFirst = std::max(0, m_lines.size() - visibleLines);
        m_firstVisibleLine = clampInt(m_firstVisibleLine, 0, maxFirst);
    }

    /**
     * @brief Performs the `ensureCursorVisible` operation.
     */
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

    /**
     * @brief Updates the cursor From Position managed by the object.
     * @param px Value passed to the method.
     * @param py Value passed to the method.
     * @return The requested cursor From Position.
     */
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

        const int lineIdx = clampInt(m_firstVisibleLine + row, 0, m_lines.size() - 1);

        const int relativeX = px - inner.x;
        const size_t col = SwWidgetPlatformAdapter::characterIndexAtPosition(nativeWindowHandle(),
                                                                             m_lines[lineIdx],
                                                                             getFont(),
                                                                             relativeX,
                                                                             std::max(1, inner.width));

        const size_t lineStart = (lineIdx < m_lineStarts.size()) ? m_lineStarts[lineIdx] : 0;
        const size_t clampedCol = std::min(col, m_lines[lineIdx].size());
        m_cursorPos = std::min(lineStart + clampedCol, m_text.size());
    }

    /**
     * @brief Performs the `insertTextAt` operation.
     * @param pos Position used by the operation.
     * @param text Value passed to the method.
     * @return The requested insert Text At.
     */
    virtual void insertTextAt(size_t pos, const SwString& text) {
        const size_t clamped = std::min(pos, m_text.size());
        m_text.insert(clamped, text);
    }

    /**
     * @brief Performs the `eraseTextAt` operation.
     * @param pos Position used by the operation.
     * @param len Value passed to the method.
     * @return The requested erase Text At.
     */
    virtual void eraseTextAt(size_t pos, size_t len) {
        if (len == 0 || m_text.isEmpty()) {
            return;
        }
        const size_t clampedPos = std::min(pos, m_text.size());
        if (clampedPos >= m_text.size()) {
            return;
        }
        const size_t clampedLen = std::min(len, m_text.size() - clampedPos);
        m_text.erase(clampedPos, clampedLen);
    }

    /**
     * @brief Performs the `insertChar` operation.
     * @param ch Value passed to the method.
     * @return The requested insert Char.
     */
    virtual void insertChar(char ch) {
        if (m_readOnly) {
            return;
        }
        replaceSelectionWithText_(SwString(1, ch));
    }

    /**
     * @brief Returns the current backspace.
     * @return The current backspace.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void backspace() {
        if (m_readOnly) {
            return;
        }
        if (hasSelectedText()) {
            replaceSelectionWithText_(SwString());
            return;
        }
        if (m_cursorPos == 0 || m_text.size() == 0) {
            return;
        }
        const size_t pos = std::min(m_cursorPos, m_text.size());
        recordUndoState_();
        eraseTextAt(pos - 1, 1);
        m_cursorPos = pos - 1;
        m_selectionStart = m_selectionEnd = m_cursorPos;
        rebuildLines();
        textChanged();
    }

    /**
     * @brief Returns the current delete Forward.
     * @return The current delete Forward.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void deleteForward() {
        if (m_readOnly) {
            return;
        }
        if (hasSelectedText()) {
            replaceSelectionWithText_(SwString());
            return;
        }
        const size_t pos = std::min(m_cursorPos, m_text.size());
        if (pos >= m_text.size()) {
            return;
        }
        recordUndoState_();
        eraseTextAt(pos, 1);
        m_cursorPos = std::min(m_cursorPos, m_text.size());
        m_selectionStart = m_selectionEnd = m_cursorPos;
        rebuildLines();
        textChanged();
    }

    /**
     * @brief Performs the `moveCursorLeft` operation.
     */
    void moveCursorLeft() {
        if (m_cursorPos > 0) {
            --m_cursorPos;
        }
    }

    /**
     * @brief Performs the `moveCursorRight` operation.
     */
    void moveCursorRight() {
        if (m_cursorPos < m_text.size()) {
            ++m_cursorPos;
        }
    }

    /**
     * @brief Performs the `moveCursorHome` operation.
     */
    void moveCursorHome() {
        const CursorInfo ci = cursorInfo();
        m_cursorPos = ci.lineStart;
    }

    /**
     * @brief Performs the `moveCursorEnd` operation.
     */
    void moveCursorEnd() {
        const CursorInfo ci = cursorInfo();
        const size_t lineLen = (ci.line >= 0 && ci.line < m_lines.size()) ? m_lines[ci.line].size() : 0;
        m_cursorPos = std::min(ci.lineStart + lineLen, m_text.size());
    }

    /**
     * @brief Performs the `moveCursorUp` operation.
     */
    void moveCursorUp() {
        const CursorInfo ci = cursorInfo();
        if (ci.line <= 0) {
            return;
        }
        const int targetLine = ci.line - 1;
        const size_t start = m_lineStarts[targetLine];
        const size_t len = m_lines[targetLine].size();
        const size_t col = std::min(static_cast<size_t>(ci.col), len);
        m_cursorPos = std::min(start + col, m_text.size());
    }

    /**
     * @brief Performs the `moveCursorDown` operation.
     */
    void moveCursorDown() {
        const CursorInfo ci = cursorInfo();
        if (ci.line >= m_lines.size() - 1) {
            return;
        }
        const int targetLine = ci.line + 1;
        const size_t start = m_lineStarts[targetLine];
        const size_t len = m_lines[targetLine].size();
        const size_t col = std::min(static_cast<size_t>(ci.col), len);
        m_cursorPos = std::min(start + col, m_text.size());
    }

    /**
     * @brief Performs the `initDefaults` operation.
     */
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

    void clearUndoRedo_() {
        m_undoStack.clear();
        m_redoStack.clear();
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

    void recordUndoState_() {
        if (!m_undoRedoEnabled) {
            return;
        }
        pushUndoState_(captureEditState());
        m_redoStack.clear();
    }

    void restoreEditState_(const EditState& state) {
        const bool textWasDifferent = (m_text != state.text);

        m_text = state.text;
        m_cursorPos = std::min(state.cursorPos, m_text.size());
        m_selectionStart = std::min(state.selectionStart, m_text.size());
        m_selectionEnd = std::min(state.selectionEnd, m_text.size());
        m_firstVisibleLine = state.firstVisibleLine;

        if (state.applyExtra) {
            state.applyExtra();
        }

        rebuildLines();
        ensureCursorVisible();
        if (textWasDifferent) {
            textChanged();
        }
        update();
    }

    void replaceSelectionWithText_(const SwString& text) {
        size_t start = m_cursorPos;
        size_t end = m_cursorPos;
        if (hasSelectedText()) {
            start = selectionMin_();
            end = selectionMax_();
        }

        start = std::min(start, m_text.size());
        end = std::min(end, m_text.size());
        if (end <= start && text.isEmpty()) {
            return;
        }
        recordUndoState_();
        if (end > start) {
            eraseTextAt(start, end - start);
        }
        if (!text.isEmpty()) {
            insertTextAt(start, text);
        }

        m_cursorPos = std::min(start + text.size(), m_text.size());
        m_selectionStart = m_selectionEnd = m_cursorPos;

        rebuildLines();
        textChanged();
    }

    void selectWordAtCursor_() {
        if (m_text.isEmpty()) {
            return;
        }

        size_t pos = std::min(m_cursorPos, m_text.size());
        if (pos == m_text.size()) {
            pos = (pos > 0) ? (pos - 1) : 0;
        }

        auto isWordChar = [](unsigned char c) {
            return std::isalnum(c) != 0 || c == static_cast<unsigned char>('_');
        };

        if (!isWordChar(static_cast<unsigned char>(m_text[pos]))) {
            selectAll();
            return;
        }

        size_t start = pos;
        while (start > 0 && isWordChar(static_cast<unsigned char>(m_text[start - 1]))) {
            --start;
        }
        size_t end = pos;
        while (end < m_text.size() && isWordChar(static_cast<unsigned char>(m_text[end]))) {
            ++end;
        }

        m_selectionStart = start;
        m_selectionEnd = end;
        m_cursorPos = end;
    }

    SwString m_text;
    SwString m_placeholder;
    SwVector<SwString> m_lines;
    SwVector<size_t> m_lineStarts;
    size_t m_cursorPos{0};
    size_t m_selectionStart{0};
    size_t m_selectionEnd{0};
    bool m_isSelecting{false};
    int m_firstVisibleLine{0};
    bool m_readOnly{false};
    bool m_undoRedoEnabled{true};
    size_t m_undoLimit{200};
    std::vector<EditState> m_undoStack;
    std::vector<EditState> m_redoStack;

    bool m_caretVisible{true};
    SwTimer* m_blinkTimer{nullptr};

    SwColor m_focusAccent{59, 130, 246};

    bool m_wordWrapEnabled{false};

    SwPlatformIntegration* currentPlatformIntegration_() const {
        SwGuiApplication* app = SwGuiApplication::instance(false);
        return app ? app->platformIntegration() : nullptr;
    }
};

