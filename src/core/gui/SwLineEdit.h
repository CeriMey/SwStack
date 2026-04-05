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
#include "SwVector.h"
#include "graphics/SwFontMetrics.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cwctype>
#include <limits>
#include <sstream>
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
        setFrameShape(Shape::Box);
        SwFont font(L"Segoe UI", 10, Medium);
        font.setPixelSize(14);
        setFont(font);
        const SwString effectivePlaceholder = placeholderText.isEmpty() ? SwString("Enter text...") : placeholderText;
        setPlaceholder(effectivePlaceholder);
        connect(this, &SwLineEdit::TextChanged, [this](const SwString& text) {
            setDisplayText(text);
        });
        this->setFocusPolicy(FocusPolicyEnum::Strong);

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
                clearSelection_();
                resetPendingMultiClick_();
                isSelecting = false;
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
        resize(300, 30);
        setCursor(CursorType::IBeam);
        setFrameShape(Shape::Box);
        SwFont font(L"Segoe UI", 10, Medium);
        font.setPixelSize(14);
        setFont(font);
        this->setFocusPolicy(FocusPolicyEnum::Strong);
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
                clearSelection_();
                resetPendingMultiClick_();
                isSelecting = false;
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

    SwSize sizeHint() const override {
        StyleSheet* sheet = const_cast<SwLineEdit*>(this)->getToolSheet();
        const SwFont font = resolvedStyledFont_(sheet);
        const SwFontMetrics metrics(font);
        const Padding padding = const_cast<SwLineEdit*>(this)->resolvePadding();
        const int borderWidth = const_cast<SwLineEdit*>(this)->resolvedBorderWidth();
        const SwSize minSize = minimumSize();
        const SwSize maxSize = maximumSize();
        const SwSize styleMin = resolvedStyleMinimumSize_();
        const SwSize styleMax = resolvedStyleMaximumSize_();

        const SwString sample = getDisplayText().isEmpty() ? (getPlaceholder().isEmpty() ? SwString("M") : getPlaceholder()) : getDisplayText();
        SwSize hint{
            metrics.horizontalAdvance(sample) + padding.left + padding.right + (borderWidth * 2),
            metrics.height() + padding.top + padding.bottom + (borderWidth * 2)
        };
        hint.width = std::max(hint.width, std::max(minSize.width, styleMin.width));
        hint.height = std::max(hint.height, std::max(minSize.height, styleMin.height));
        hint.width = std::min(hint.width, std::min(maxSize.width, styleMax.width));
        hint.height = std::min(hint.height, std::min(maxSize.height, styleMax.height));
        return hint;
    }

    SwSize minimumSizeHint() const override {
        return sizeHint();
    }

    void selectAll() {
        selectAllText_();
        update();
    }

    void selectAllFromStart() {
        selectionStart = 0;
        selectionEnd = m_Text.length();
        cursorPos = 0;
        firstVisibleCharacter = 0;
        update();
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

        if (frameShape() == Shape::NoFrame) {
            paintBackground = false;
            bgAlpha = 0.0f;
            borderWidth = 0;
            radius = 0;
        }

        resolveBackground(sheet, bg, bgAlpha, paintBackground);
        resolveBorder(sheet, border, borderWidth, radius);

        const bool explicitHoverBackground =
            hasExplicitStyledStateProperty(StyleSheet::StateHovered, "background-color");
        const bool explicitHoverBorder =
            hasExplicitStyledStateProperty(StyleSheet::StateHovered, "border-color");
        const bool explicitFocusBorder =
            hasExplicitStyledStateProperty(StyleSheet::StateFocused, "border-color");

        if (!getEnable()) {
            // Derive disabled colors from the resolved bg (dark vs light aware)
            bool isDark = (bg.r + bg.g + bg.b) < 384;
            bg = isDark ? SwColor{(uint8_t)std::max(0, bg.r - 15),
                                  (uint8_t)std::max(0, bg.g - 15),
                                  (uint8_t)std::max(0, bg.b - 15)}
                        : SwColor{245, 245, 245};
            border = isDark ? SwColor{(uint8_t)std::max(0, border.r - 15),
                                      (uint8_t)std::max(0, border.g - 15),
                                      (uint8_t)std::max(0, border.b - 15)}
                            : SwColor{210, 210, 210};
        } else {
            const bool isDark = (bg.r + bg.g + bg.b) < 384;
            if (getHover()) {
                if (!explicitHoverBackground) {
                    bg = isDark
                             ? SwColor{(uint8_t)std::min(255, bg.r + 5),
                                       (uint8_t)std::min(255, bg.g + 5),
                                       (uint8_t)std::min(255, bg.b + 5)}
                             : SwColor{250, 251, 253};
                }
                if (!explicitHoverBorder) {
                    border = isDark ? SwColor{85, 85, 85} : SwColor{150, 150, 150};
                }
            }
            if (getFocus() && !explicitFocusBorder) {
                border = m_focusAccent;
            }
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

        SwFont font = resolvedStyledFont_(sheet);
        const SwFontMetrics metrics(font);
        const int fontPixelHeight = font.getPixelSize() > 0
                                        ? font.getPixelSize()
                                        : static_cast<int>(std::max(1, font.getPointSize()) * 96.0 / 72.0 + 0.5);
        const int textVisualHeight = std::max(1, std::min(textRect.height, fontPixelHeight + 4));
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

        const bool hasSelection = !showingPlaceholder && selectionStart != selectionEnd && textToDraw.length() > 0;
        SwRect selectionRect{};
        if (hasSelection) {
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
                const int selectionInsetY = std::max(0, (textRect.height - textVisualHeight) / 2);
                selectionRect.x = selectionX1;
                selectionRect.y = textRect.y + selectionInsetY;
                selectionRect.width = selectionX2 - selectionX1;
                selectionRect.height = textVisualHeight;
                const SwColor selFill = getFocus() ? m_focusAccent : SwColor{191, 219, 254};
                painter->fillRect(selectionRect, selFill, selFill, 0);
            }
        }

        painter->drawText(textRect,
                          visibleText,
                          DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          textColor,
                          font);

        if (selectionRect.width > 0 && selectionRect.height > 0) {
            painter->pushClipRect(selectionRect);
            painter->drawText(textRect,
                              visibleText,
                              DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              SwColor{255, 255, 255},
                              font);
            painter->popClipRect();
        }

        if (!showingPlaceholder && getFocus() && m_caretVisible) {
            int caretPx = SwWidgetPlatformAdapter::textWidthUntil(handle, textToDraw, font, cursorPos, textAreaWidth);
            int caretOffset = caretPx - viewStartPx;
            caretOffset = (std::max)(0, (std::min)(caretOffset, textRect.width));
            const int caretHeight = std::max(1, std::min(textRect.height, textVisualHeight));
            const int caretY = textRect.y + std::max(0, (textRect.height - caretHeight) / 2);
            SwRect caretRect{textRect.x + caretOffset, caretY, 1, caretHeight};
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
            if (isTripleClickPress_(event)) {
                selectAllText_();
                resetPendingMultiClick_();
                isSelecting = false;
                event->accept();
                update();
                SwWidget::mousePressEvent(event);
                return;
            }
            resetPendingMultiClick_();
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
        if (!event) {
            return;
        }
        if (!getReadOnly()) {
            this->setFocus(true);
        }
        cursorPos = getCharacterIndexAtPosition(event->x());
        selectWordAtCursor_();
        rememberDoubleClick_(event);
        isSelecting = false;
        event->accept();
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
    bool m_pendingMultiClick{false};
    SwPoint m_lastDoubleClickPos{};
    std::chrono::steady_clock::time_point m_lastDoubleClickTime{};

    static constexpr long long kTripleClickIntervalMs_ = 500;
    static constexpr int kTripleClickDistancePx_ = 6;

    static bool isUtf8ContinuationByte_(unsigned char byte) {
        return (byte & 0xC0U) == 0x80U;
    }

    struct Utf8CodePoint_ {
        size_t start{0};
        size_t end{0};
        char32_t value{0};
    };

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

    static char32_t decodeUtf8CodePoint_(const std::string& utf8, size_t start, size_t end) {
        if (start >= utf8.size() || end <= start) {
            return 0;
        }

        const unsigned char first = static_cast<unsigned char>(utf8[start]);
        const size_t length = end - start;
        if ((first & 0x80U) == 0 || length == 1) {
            return first;
        }
        if ((first & 0xE0U) == 0xC0U && length >= 2) {
            return (static_cast<char32_t>(first & 0x1FU) << 6) |
                   static_cast<char32_t>(static_cast<unsigned char>(utf8[start + 1]) & 0x3FU);
        }
        if ((first & 0xF0U) == 0xE0U && length >= 3) {
            return (static_cast<char32_t>(first & 0x0FU) << 12) |
                   (static_cast<char32_t>(static_cast<unsigned char>(utf8[start + 1]) & 0x3FU) << 6) |
                   static_cast<char32_t>(static_cast<unsigned char>(utf8[start + 2]) & 0x3FU);
        }
        if ((first & 0xF8U) == 0xF0U && length >= 4) {
            return (static_cast<char32_t>(first & 0x07U) << 18) |
                   (static_cast<char32_t>(static_cast<unsigned char>(utf8[start + 1]) & 0x3FU) << 12) |
                   (static_cast<char32_t>(static_cast<unsigned char>(utf8[start + 2]) & 0x3FU) << 6) |
                   static_cast<char32_t>(static_cast<unsigned char>(utf8[start + 3]) & 0x3FU);
        }
        return first;
    }

    static bool isWordCodePoint_(char32_t codePoint) {
        if (codePoint == U'_') {
            return true;
        }
        if (codePoint < 128U) {
            return std::isalnum(static_cast<unsigned char>(codePoint)) != 0;
        }
        if (codePoint <= static_cast<char32_t>((std::numeric_limits<wchar_t>::max)())) {
            const wchar_t wide = static_cast<wchar_t>(codePoint);
            if (std::iswspace(wide) != 0 || std::iswpunct(wide) != 0) {
                return false;
            }
            if (std::iswalnum(wide) != 0) {
                return true;
            }
        }
        return true;
    }

    static SwVector<Utf8CodePoint_> utf8CodePoints_(const SwString& text) {
        SwVector<Utf8CodePoint_> codePoints;
        const std::string utf8 = text.toStdString();
        size_t offset = 0;
        while (offset < utf8.size()) {
            const size_t next = nextUtf8Boundary_(text, offset);
            if (next <= offset) {
                break;
            }
            Utf8CodePoint_ codePoint;
            codePoint.start = offset;
            codePoint.end = next;
            codePoint.value = decodeUtf8CodePoint_(utf8, offset, next);
            codePoints.push_back(codePoint);
            offset = next;
        }
        return codePoints;
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
        SwFont font = resolvedStyledFont_(getToolSheet());
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
        clearSelection_();
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

    void clearSelection_() {
        selectionStart = selectionEnd = cursorPos;
    }

    void selectAllText_() {
        selectionStart = 0;
        selectionEnd = m_Text.length();
        cursorPos = selectionEnd;
    }

    void rememberDoubleClick_(const MouseEvent* event) {
        if (!event) {
            return;
        }
        m_pendingMultiClick = true;
        m_lastDoubleClickPos = event->pos();
        m_lastDoubleClickTime = std::chrono::steady_clock::now();
    }

    void resetPendingMultiClick_() {
        m_pendingMultiClick = false;
    }

    bool isTripleClickPress_(const MouseEvent* event) const {
        if (!event || !m_pendingMultiClick || event->button() != SwMouseButton::Left) {
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastDoubleClickTime).count();
        if (elapsedMs < 0 || elapsedMs > kTripleClickIntervalMs_) {
            return false;
        }

        const SwPoint pos = event->pos();
        return std::abs(pos.x - m_lastDoubleClickPos.x) <= kTripleClickDistancePx_ &&
               std::abs(pos.y - m_lastDoubleClickPos.y) <= kTripleClickDistancePx_;
    }

    void selectWordAtCursor_() {
        const size_t length = m_Text.length();
        if (length == 0) {
            cursorPos = 0;
            clearSelection_();
            return;
        }

        const SwVector<Utf8CodePoint_> codePoints = utf8CodePoints_(m_Text);
        if (codePoints.isEmpty()) {
            cursorPos = length;
            clearSelection_();
            return;
        }

        size_t pos = clampUtf8Boundary_(m_Text, (std::min)(cursorPos, length));
        if (pos == length) {
            pos = previousUtf8Boundary_(m_Text, pos);
        }
        if (pos >= length) {
            cursorPos = length;
            clearSelection_();
            return;
        }

        size_t anchorIndex = codePoints.size() - 1;
        for (size_t i = 0; i < codePoints.size(); ++i) {
            if (codePoints[i].start >= pos) {
                anchorIndex = i;
                break;
            }
        }

        if (codePoints[anchorIndex].start == pos &&
            !isWordCodePoint_(codePoints[anchorIndex].value) &&
            anchorIndex > 0 &&
            isWordCodePoint_(codePoints[anchorIndex - 1].value)) {
            --anchorIndex;
        }

        if (!isWordCodePoint_(codePoints[anchorIndex].value)) {
            selectionStart = codePoints[anchorIndex].start;
            selectionEnd = codePoints[anchorIndex].end;
            cursorPos = selectionEnd;
            return;
        }

        size_t startIndex = anchorIndex;
        while (startIndex > 0 && isWordCodePoint_(codePoints[startIndex - 1].value)) {
            --startIndex;
        }

        size_t endIndex = anchorIndex;
        while (endIndex + 1 < codePoints.size() && isWordCodePoint_(codePoints[endIndex + 1].value)) {
            ++endIndex;
        }

        selectionStart = codePoints[startIndex].start;
        selectionEnd = codePoints[endIndex].end;
        cursorPos = selectionEnd;
    }

    Padding resolvePadding() {
        Padding padding = intrinsicPadding_();
        StyleSheet* sheet = getToolSheet();
        if (!sheet) {
            return padding;
        }

        const unsigned int stateFlags = styleStateFlags_();
        const bool hasShorthandPadding = hasExplicitPaddingProperty_(sheet, stateFlags, "padding");
        const bool hasPaddingTop = hasExplicitPaddingProperty_(sheet, stateFlags, "padding-top");
        const bool hasPaddingRight = hasExplicitPaddingProperty_(sheet, stateFlags, "padding-right");
        const bool hasPaddingBottom = hasExplicitPaddingProperty_(sheet, stateFlags, "padding-bottom");
        const bool hasPaddingLeft = hasExplicitPaddingProperty_(sheet, stateFlags, "padding-left");

        if (!hasShorthandPadding && !hasPaddingTop && !hasPaddingRight && !hasPaddingBottom && !hasPaddingLeft) {
            return padding;
        }

        const StyleSheet::BoxEdges edges = resolvePaddingEdges_(sheet, stateFlags);
        if (hasShorthandPadding || hasPaddingTop) {
            padding.top = edges.top;
        }
        if (hasShorthandPadding || hasPaddingRight) {
            padding.right = edges.right;
        }
        if (hasShorthandPadding || hasPaddingBottom) {
            padding.bottom = edges.bottom;
        }
        if (hasShorthandPadding || hasPaddingLeft) {
            padding.left = edges.left;
        }
        return padding;
    }

    Padding parsePaddingShorthand(const SwString& value, Padding current) {
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

    Padding intrinsicPadding_() const {
        Padding padding;
        padding.top = 6;
        padding.right = 10;
        padding.bottom = 6;
        padding.left = 10;
        return padding;
    }

    bool hasExplicitPaddingProperty_(StyleSheet* sheet, unsigned int stateFlags, const SwString& propertyName) const {
        if (!sheet) {
            return false;
        }
        return !sheet->resolveStyleProperty(styleSelectors_(), getObjectName(), stateFlags, propertyName).isEmpty();
    }

    int resolvedBorderWidth() const {
        if (frameShape() == Shape::NoFrame) {
            return 0;
        }
        StyleSheet* sheet = const_cast<SwLineEdit*>(this)->getToolSheet();
        if (!sheet) {
            return std::max(1, frameWidth());
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
        SwFont font = resolvedStyledFont_(getToolSheet());
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
