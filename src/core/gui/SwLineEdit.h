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
 * @file src/core/gui/SwLineEdit.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwLineEdit in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the line edit interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwLineEdit.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */


#include "SwFrame.h"
#include "Sw.h"
#include "SwGuiApplication.h"
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <sstream>
#include <vector>
#include "SwTimer.h"

// echo mode â—â—â—â—â—â—â—â—â—â—
class SwLineEdit : public SwFrame {

    SW_OBJECT(SwLineEdit, SwFrame)

    CUSTOM_PROPERTY(SwString, Text, SwString()) {
        cursorPos = getText().size();
        selectionStart = selectionEnd = cursorPos;
        setDisplayText(getText());
    }


    CUSTOM_PROPERTY(SwString, DisplayText, SwString()) {
        if (getEchoMode() == EchoModeEnum::PasswordEcho) {
            SwString maskedText(m_DisplayText.size(), '*');
            m_DisplayText = maskedText;
        }
        update();
    }


    CUSTOM_PROPERTY(EchoModeEnum, EchoMode, EchoModeEnum::NormalEcho) {
        if (getEchoMode() == EchoModeEnum::PasswordEcho) {
            SwString maskedText(m_DisplayText.size(), '*');
            m_DisplayText = maskedText;
        }
        else if (getEchoMode() == EchoModeEnum::NoEcho) {
            m_DisplayText = m_Text;
        }
    }

        

    CUSTOM_PROPERTY(SwString, Placeholder, SwString()) {
        update();
    }

    CUSTOM_PROPERTY(bool, ReadOnly, false) {
        if (m_ReadOnly) {
            cursorPos = getText().size();
            selectionStart = selectionEnd = cursorPos;
            update();
        }
    }

public:
    /**
     * @brief Constructs a `SwLineEdit` instance.
     * @param placeholderText Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @param false Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwLineEdit(const SwString& placeholderText = SwString(), SwWidget* parent = nullptr)
        : SwFrame(parent), cursorPos(0),
          selectionStart(0), selectionEnd(0), isSelecting(false) {
        resize(300, 30);
        setCursor(CursorType::IBeam);
        const SwString effectivePlaceholder = placeholderText.isEmpty() ? SwString("Enter text...") : placeholderText;
        setPlaceholder(effectivePlaceholder);
        connect(this, &SwLineEdit::TextChanged, [this](const SwString& text) {
            setDisplayText(text);
        });
        this->setFocusPolicy(FocusPolicyEnum::Strong);
        SwString css = R"(
            SwLineEdit {
                background-color: rgb(255, 255, 255);
                border-color: rgb(220, 224, 232);
                border-width: 1px;
                border-radius: 12px;
                padding: 6px 10px;
                color: rgb(24, 28, 36);
            }
        )";
        this->setStyleSheet(css);

        monitorTimer = new SwTimer(500, this);
        connect(monitorTimer, &SwTimer::timeout, this, [this]() {
            if (this->getFocus() == true) {
                m_caretVisible = !m_caretVisible;
                update();
            }
        });

        connect(this, &SwWidget::FocusChanged, this, [this](bool focus) {
            m_caretVisible = true;
            if (focus == true) {
                monitorTimer->start();
            }
            else {
                monitorTimer->stop();
            }
            update();
        });
    }

    /**
     * @brief Constructs a `SwLineEdit` instance.
     * @param parent Optional parent object that owns this instance.
     * @param false Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwLineEdit(SwWidget* parent = nullptr)
        : SwFrame(parent), cursorPos(0),
        selectionStart(0), selectionEnd(0), isSelecting(false) {

        SwString css = R"(
            SwLineEdit {
                background-color: rgb(255, 255, 255);
                border-color: rgb(220, 224, 232);
                border-width: 1px;
                border-radius: 12px;
                padding: 6px 10px;
                color: rgb(24, 28, 36);
            }
        )";
        this->setStyleSheet(css);

        resize(300, 30);
        setCursor(CursorType::IBeam);
        setPlaceholder("Enter text...");
        connect(this, &SwLineEdit::TextChanged, [this](const SwString& text) {
            setDisplayText(text);
        });

        monitorTimer = new SwTimer(500, this);
        connect(monitorTimer, &SwTimer::timeout, this, [this]() {
            if (this->getFocus() == true) {
                m_caretVisible = !m_caretVisible;
                update();
            }
        });

        connect(this, &SwWidget::FocusChanged, this, [this](bool focus) {
            m_caretVisible = true;
            if (focus == true) {
                monitorTimer->start();
            } else {
                monitorTimer->stop();
            }
            update();
        });
    }

    /**
     * @brief Destroys the `SwLineEdit` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwLineEdit() override {
        if (monitorTimer) {
            monitorTimer->stop();
        }
    }


    // RedÃ©finir la mÃ©thode paintEvent pour dessiner le champ de texte
    /**
     * @brief Handles the paint Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested paint Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void paintEvent(PaintEvent* event) override {
        if (!isVisibleInHierarchy()) {
            return;
        }

        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect rect = this->rect();
        StyleSheet* sheet = getToolSheet();

        SwColor bg{255, 255, 255};
        float bgAlpha = 1.0f;
        bool paintBackground = true;

        SwColor border{220, 224, 232};
        int borderWidth = 1;
        int radius = 12;

        resolveBackground(sheet, bg, bgAlpha, paintBackground);
        resolveBorder(sheet, border, borderWidth, radius);

        if (!getEnable()) {
            bg = SwColor{245, 245, 245};
            border = SwColor{210, 210, 210};
        } else if (getFocus()) {
            border = m_focusAccent;
        }

        if (paintBackground && bgAlpha > 0.0f) {
            if (radius > 0) {
                painter->fillRoundedRect(rect, radius, bg, border, borderWidth);
            } else {
                painter->fillRect(rect, bg, border, borderWidth);
            }
        } else if (borderWidth > 0) {
            painter->drawRect(rect, border, borderWidth);
        }

        const Padding padding = resolvePadding();
        const SwRect textRect = contentRect(padding);
        const int textAreaWidth = std::max(1, textRect.width);

        bool showingPlaceholder = getDisplayText().isEmpty() && !getFocus() && !getPlaceholder().isEmpty();
        SwString textToDraw = showingPlaceholder ? getPlaceholder() : getDisplayText();
        if (showingPlaceholder) {
            firstVisibleCharacter = 0;
        }

        SwFont font = getFont();
        SwColor textColor = showingPlaceholder ? SwColor{160, 160, 160} : resolveTextColor(sheet, SwColor{24, 28, 36});
        if (!getEnable()) {
            textColor = SwColor{150, 150, 150};
        }
        auto handle = nativeWindowHandle();

        if (!textToDraw.isEmpty() && !showingPlaceholder) {
            ensureCursorVisible(textToDraw, textAreaWidth);
        } else if (textToDraw.isEmpty()) {
            firstVisibleCharacter = 0;
        }

        size_t clampedVisible = clampUtf8Boundary_(textToDraw, (std::min)(firstVisibleCharacter, textToDraw.length()));
        int viewStartPx = SwWidgetPlatformAdapter::textWidthUntil(handle,
                                                                  textToDraw,
                                                                  font,
                                                                  clampedVisible,
                                                                  textAreaWidth);
        SwString visibleText = textToDraw.substr(clampedVisible);

        painter->pushClipRect(textRect);

        if (!showingPlaceholder && selectionStart != selectionEnd && textToDraw.length() > 0) {
            size_t selStart = (std::min)(selectionStart, selectionEnd);
            size_t selEnd = (std::max)(selectionStart, selectionEnd);
            int selStartPx = SwWidgetPlatformAdapter::textWidthUntil(handle, textToDraw, font, selStart, textAreaWidth);
            int selEndPx = SwWidgetPlatformAdapter::textWidthUntil(handle, textToDraw, font, selEnd, textAreaWidth);
            int selectionX1 = textRect.x + (selStartPx - viewStartPx);
            int selectionX2 = textRect.x + (selEndPx - viewStartPx);
            int clipLeft = textRect.x;
            int clipRight = textRect.x + textRect.width;
            selectionX1 = (std::max)(selectionX1, clipLeft);
            selectionX2 = (std::min)(selectionX2, clipRight);
            if (selectionX2 > selectionX1) {
                SwRect selectionRect;
                selectionRect.x = selectionX1;
                selectionRect.y = textRect.y;
                selectionRect.width = selectionX2 - selectionX1;
                selectionRect.height = textRect.height;
                const SwColor selFill{219, 234, 254};
                painter->fillRect(selectionRect, selFill, selFill, 0);
            }
        }

        painter->drawText(textRect,
                          visibleText,
                          DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          textColor,
                          font);

        if (!showingPlaceholder && getFocus() && m_caretVisible && selectionStart == selectionEnd) {
            int caretPx = SwWidgetPlatformAdapter::textWidthUntil(handle, textToDraw, font, cursorPos, textAreaWidth);
            int caretOffset = caretPx - viewStartPx;
            caretOffset = (std::max)(0, (std::min)(caretOffset, textRect.width));
            SwRect caretRect{textRect.x + caretOffset, textRect.y, 1, textRect.height};
            const SwColor caret{24, 28, 36};
            painter->fillRect(caretRect, caret, caret, 0);
        }

        painter->popClipRect();
        painter->finalize();
    }
    // GÃ©rer les Ã©vÃ©nements de pression de touche
    /**
     * @brief Handles the key Press Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested key Press Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void keyPressEvent(KeyEvent* event) override {
        if (!getFocus()) {
            return;
        }

        const int keyCode = event->key();
        const bool altGrTextInput = event->isCtrlPressed() &&
                                    event->isAltPressed() &&
                                    event->text() != L'\0';
        const bool shortcutCtrl = event->isCtrlPressed() && !altGrTextInput;

        auto deleteBackward = [&]() {
            if (selectionStart != selectionEnd) {
                deleteSelection();
                emit TextChanged(m_Text);
            } else if (cursorPos > 0) {
                const size_t prev = previousUtf8Boundary_(m_Text, cursorPos);
                m_Text.erase(prev, cursorPos - prev);
                cursorPos = prev;
                emit TextChanged(m_Text);
            }
            if (firstVisibleCharacter > m_Text.length()) {
                firstVisibleCharacter = m_Text.length();
            }
        };

        auto deleteForward = [&]() {
            if (selectionStart != selectionEnd) {
                deleteSelection();
                emit TextChanged(m_Text);
            } else if (cursorPos < m_Text.length()) {
                const size_t next = nextUtf8Boundary_(m_Text, cursorPos);
                m_Text.erase(cursorPos, next - cursorPos);
                emit TextChanged(m_Text);
            }
            if (firstVisibleCharacter > m_Text.length()) {
                firstVisibleCharacter = m_Text.length();
            }
        };

        if (shortcutCtrl) {
            if (SwWidgetPlatformAdapter::matchesShortcutKey(keyCode, 'C')) {
                copySelectionToClipboard();
            } else if (SwWidgetPlatformAdapter::matchesShortcutKey(keyCode, 'V')) {
                pasteFromClipboard();
                emit TextChanged(m_Text);
            } else if (SwWidgetPlatformAdapter::matchesShortcutKey(keyCode, 'X')) {
                copySelectionToClipboard();
                deleteSelection();
                emit TextChanged(m_Text);
            } else if (SwWidgetPlatformAdapter::matchesShortcutKey(keyCode, 'A')) {
                selectionStart = 0;
                selectionEnd = m_Text.length();
                cursorPos = m_Text.length();
                update();
            }
        } else if (SwWidgetPlatformAdapter::isBackspaceKey(keyCode)) {
            deleteBackward();
        } else if (SwWidgetPlatformAdapter::isDeleteKey(keyCode)) {
            deleteForward();
        } else if (SwWidgetPlatformAdapter::isLeftArrowKey(keyCode)) {
            if (cursorPos > 0) {
                cursorPos = previousUtf8Boundary_(m_Text, cursorPos);
                if (event->isShiftPressed()) {
                    selectionEnd = cursorPos;
                } else {
                    selectionStart = selectionEnd = cursorPos;
                }
                update();
            }
        } else if (SwWidgetPlatformAdapter::isRightArrowKey(keyCode)) {
            if (cursorPos < m_Text.length()) {
                cursorPos = nextUtf8Boundary_(m_Text, cursorPos);
                if (event->isShiftPressed()) {
                    selectionEnd = cursorPos;
                } else {
                    selectionStart = selectionEnd = cursorPos;
                }
                update();
            }
        } else if (SwWidgetPlatformAdapter::isHomeKey(keyCode)) {
            cursorPos = 0;
            if (event->isShiftPressed()) {
                selectionEnd = cursorPos;
            } else {
                selectionStart = selectionEnd = cursorPos;
            }
            update();
        } else if (SwWidgetPlatformAdapter::isEndKey(keyCode)) {
            cursorPos = m_Text.length();
            if (event->isShiftPressed()) {
                selectionEnd = cursorPos;
            } else {
                selectionStart = selectionEnd = cursorPos;
            }
            update();
        } else if (SwWidgetPlatformAdapter::isCapsLockKey(keyCode)) {
            capsLockActive = !capsLockActive;
        } else {
            if (selectionStart != selectionEnd) {
                deleteSelection();
            }
            // Use the Unicode character provided by the platform (handles AZERTY, QWERTZ,
            // dead-key composition like ^+eâ†’Ãª, AltGr, etc.).
            // When textProvided is true the platform translated the key; skip the hardcoded
            // QWERTY fallback table â€” it would insert garbage for dead-key presses.
            wchar_t wc = event->text();
            if (wc == L'\0' && !event->isTextProvided()) {
                char ascii = '\0';
                if (SwWidgetPlatformAdapter::translateCharacter(keyCode,
                                                               event->isShiftPressed(),
                                                               capsLockActive,
                                                               ascii)) {
                    wc = static_cast<wchar_t>(static_cast<unsigned char>(ascii));
                }
            }
            if (wc != L'\0') {
                const SwString inserted = utf8FromWideChar_(wc);
                m_Text.insert(static_cast<size_t>(cursorPos), inserted);
                cursorPos += inserted.length();
                selectionStart = selectionEnd = cursorPos;
                emit TextChanged(m_Text);
                if (firstVisibleCharacter > m_Text.length()) {
                    firstVisibleCharacter = m_Text.length();
                }
            }
        }

        event->accept();
    }

    // GÃ©rer le clic de souris pour le focus et le positionnement du curseur
    /**
     * @brief Handles the mouse Press Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested mouse Press Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void mousePressEvent(MouseEvent* event) override {
        if (!getReadOnly()) {
            this->setFocus(true);
            cursorPos = getCharacterIndexAtPosition(event->x());
            if (SwWidgetPlatformAdapter::isShiftModifierActive()) {
                selectionEnd = cursorPos;
            } else {
                selectionStart = selectionEnd = cursorPos;
            }
            isSelecting = true;
            event->accept();
        } else {
            this->setFocus(false);
            selectionStart = selectionEnd = 0;
        }
        SwWidget::mousePressEvent(event);
    }

    /**
     * @brief Handles the mouse Double Click Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested mouse Double Click Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void mouseDoubleClickEvent(MouseEvent* event) override {
        selectionStart = 0;
        selectionEnd = getText().size();
        event->accept();
        isSelecting = true;
        update();
    }

    // GÃ©rer la sÃ©lection avec la souris
    /**
     * @brief Handles the mouse Move Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested mouse Move Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void mouseMoveEvent(MouseEvent* event) override {
        if (isSelecting) {
            size_t newPos = getCharacterIndexAtPosition(event->x());
            bool toBeUpdate = newPos != cursorPos;
            cursorPos = newPos;
            selectionEnd = cursorPos;
            event->accept();
            if(toBeUpdate) update();
        }
        if (getReadOnly()) {
            setCursor(CursorType::Arrow);
        }
        else {
            setCursor(CursorType::IBeam);
        }
        SwWidget::mouseMoveEvent(event);
    }

    // Terminer la sÃ©lection Ã  la libÃ©ration de la souris
    /**
     * @brief Handles the mouse Release Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested mouse Release Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void mouseReleaseEvent(MouseEvent* event) override {
        if (isSelecting) {
            event->accept();
        }
        isSelecting = false;
        update();
        SwWidget::mouseReleaseEvent(event);
    }




private:
    size_t cursorPos;          // Position du curseur
    size_t selectionStart;     // DÃ©but de la sÃ©lection
    size_t selectionEnd;       // Fin de la sÃ©lection
    bool isSelecting;          // Indique si une sÃ©lection est en cours
    bool capsLockActive{false};
    SwTimer* monitorTimer{nullptr};
    bool m_caretVisible{true};
    size_t firstVisibleCharacter{0};
    SwColor m_focusAccent{59, 130, 246};

    static bool isUtf8ContinuationByte_(unsigned char byte) {
        return (byte & 0xC0U) == 0x80U;
    }

    static size_t clampUtf8Boundary_(const SwString& text, size_t offset) {
        const std::string utf8 = text.toStdString();
        if (offset >= utf8.size()) {
            return utf8.size();
        }
        while (offset > 0 && isUtf8ContinuationByte_(static_cast<unsigned char>(utf8[offset]))) {
            --offset;
        }
        return offset;
    }

    static size_t nextUtf8Boundary_(const SwString& text, size_t offset) {
        const std::string utf8 = text.toStdString();
        if (offset >= utf8.size()) {
            return utf8.size();
        }
        size_t next = clampUtf8Boundary_(text, offset) + 1;
        while (next < utf8.size() && isUtf8ContinuationByte_(static_cast<unsigned char>(utf8[next]))) {
            ++next;
        }
        return next;
    }

    static size_t previousUtf8Boundary_(const SwString& text, size_t offset) {
        const std::string utf8 = text.toStdString();
        size_t current = std::min(offset, utf8.size());
        if (current == 0) {
            return 0;
        }
        --current;
        while (current > 0 && isUtf8ContinuationByte_(static_cast<unsigned char>(utf8[current]))) {
            --current;
        }
        return current;
    }

    static SwString utf8FromWideChar_(wchar_t wc) {
        const std::wstring wide(1, wc);
        return SwString::fromWString(wide);
    }

    SwPlatformIntegration* currentPlatformIntegration() const {
        SwGuiApplication* app = SwGuiApplication::instance(false);
        return app ? app->platformIntegration() : nullptr;
    }

    struct Padding {
        int top{0};
        int right{0};
        int bottom{0};
        int left{5};
    };

    // Obtenir l'index du caractÃ¨re Ã  une position x
    size_t getCharacterIndexAtPosition(int xPos) {
        Padding padding = resolvePadding();
        SwRect textRect = contentRect(padding);
        int relativeX = xPos - textRect.x;
        if (relativeX < 0) {
            relativeX = 0;
        }
        auto handle = nativeWindowHandle();
        SwFont font = getFont();
        const SwString& display = m_DisplayText;
        size_t clampedVisible = clampUtf8Boundary_(display, (std::min)(firstVisibleCharacter, display.length()));
        int viewStartPx = SwWidgetPlatformAdapter::textWidthUntil(handle,
                                                                  display,
                                                                  font,
                                                                  clampedVisible,
                                                                  (std::max)(1, textRect.width));
        int targetPx = viewStartPx + relativeX;
        return SwWidgetPlatformAdapter::characterIndexAtPosition(handle,
                                                                 m_Text,
                                                                 font,
                                                                 targetPx,
                                                                 (std::max)(1, textRect.width));
    }
    // Supprimer la sÃ©lection
    void deleteSelection() {
		size_t start = (std::min)(selectionStart, selectionEnd);
		size_t end = (std::max)(selectionStart, selectionEnd);
        m_Text.erase(start, end - start);
        cursorPos = start;
        selectionStart = selectionEnd = cursorPos;
        if (firstVisibleCharacter > m_Text.length()) {
            firstVisibleCharacter = m_Text.length();
        }
    }

    // Copier la sÃ©lection dans le presse-papiers
    void copySelectionToClipboard() {
        if (selectionStart == selectionEnd) {
            return;  // Rien Ã  copier
        }

		size_t start = (std::min)(selectionStart, selectionEnd);
		size_t end = (std::max)(selectionStart, selectionEnd);
        SwString selectedText = (getEchoMode() == EchoModeEnum::NormalEcho) ? m_Text.substr(start, end - start)
                                                                           : SwString("Better luck next time, mate!");

        if (auto* platform = currentPlatformIntegration()) {
            platform->setClipboardText(selectedText);
        }
    }

    // Coller depuis le presse-papiers
    void pasteFromClipboard() {
        if (auto* platform = currentPlatformIntegration()) {
            SwString clipboardText = platform->clipboardText();
            if (!clipboardText.isEmpty()) {
                if (selectionStart != selectionEnd) {
                    deleteSelection();
                }
                m_Text.insert(cursorPos, clipboardText);
                cursorPos += clipboardText.length();
                selectionStart = selectionEnd = cursorPos;
                if (firstVisibleCharacter > m_Text.length()) {
                    firstVisibleCharacter = m_Text.length();
                }
            }
        }
    }

    Padding resolvePadding() {
        Padding padding;
        StyleSheet* sheet = getToolSheet();
        if (!sheet) {
            return padding;
        }

        SwString paddingValue;
        SwString paddingTopValue;
        SwString paddingRightValue;
        SwString paddingBottomValue;
        SwString paddingLeftValue;

        auto hierarchy = classHierarchy();
        for (int i = static_cast<int>(hierarchy.size()) - 1; i >= 0; --i) {
            const SwString& selector = hierarchy[i];
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
        return padding;
    }

    Padding parsePaddingShorthand(const SwString& value, Padding current) {
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
            int v = toPx(tokens[0], current.top);
            current.top = current.right = current.bottom = current.left = v;
        } else if (tokens.size() == 2) {
            int v1 = toPx(tokens[0], current.top);
            int v2 = toPx(tokens[1], current.right);
            current.top = current.bottom = v1;
            current.left = current.right = v2;
        } else if (tokens.size() == 3) {
            int v1 = toPx(tokens[0], current.top);
            int v2 = toPx(tokens[1], current.right);
            int v3 = toPx(tokens[2], current.bottom);
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

    int resolvedBorderWidth() const {
        StyleSheet* sheet = const_cast<SwLineEdit*>(this)->getToolSheet();
        if (!sheet) {
            return 0;
        }
        SwColor border{0, 0, 0};
        int borderWidth = 1;
        int radius = 0;
        resolveBorder(sheet, border, borderWidth, radius);
        return std::max(0, borderWidth);
    }

    SwRect contentRect(const Padding& padding) const {
        const int bw = resolvedBorderWidth();
        SwRect rect = this->rect();
        rect.x += bw + padding.left;
        rect.y += bw + padding.top;
        rect.width = std::max(1, rect.width - 2 * bw - (padding.left + padding.right));
        rect.height = std::max(1, rect.height - 2 * bw - (padding.top + padding.bottom));
        return rect;
    }

    int textAreaWidth(const Padding& padding) const {
        const int bw = resolvedBorderWidth();
        return std::max(1, width() - 2 * bw - (padding.left + padding.right));
    }

    void ensureCursorVisible(const SwString& textToDraw, int areaWidth) {
        size_t length = textToDraw.length();
        if (firstVisibleCharacter > length) {
            firstVisibleCharacter = length;
        }
        if (length == 0) {
            firstVisibleCharacter = 0;
            return;
        }
        firstVisibleCharacter = clampUtf8Boundary_(textToDraw, firstVisibleCharacter);
        auto handle = nativeWindowHandle();
        SwFont font = getFont();
        areaWidth = std::max(1, areaWidth);
        auto widthUntil = [&](size_t index) {
            return SwWidgetPlatformAdapter::textWidthUntil(handle, textToDraw, font, index, areaWidth);
        };

        size_t clampedCursor = clampUtf8Boundary_(textToDraw, (std::min)(cursorPos, length));
        int caretPx = widthUntil(clampedCursor);
        int startPx = widthUntil(firstVisibleCharacter);

        if (caretPx < startPx) {
            firstVisibleCharacter = clampedCursor;
            return;
        }

        while (caretPx - startPx > areaWidth && firstVisibleCharacter < length) {
            firstVisibleCharacter = nextUtf8Boundary_(textToDraw, firstVisibleCharacter);
            startPx = widthUntil(firstVisibleCharacter);
        }

        int totalWidth = widthUntil(length);
        if (totalWidth <= areaWidth) {
            firstVisibleCharacter = 0;
            return;
        }

        int maxStartPx = totalWidth - areaWidth;
        while (startPx > maxStartPx && firstVisibleCharacter > 0) {
            firstVisibleCharacter = previousUtf8Boundary_(textToDraw, firstVisibleCharacter);
            startPx = widthUntil(firstVisibleCharacter);
        }

        if (firstVisibleCharacter > length) {
            firstVisibleCharacter = length;
        }
    }
};

