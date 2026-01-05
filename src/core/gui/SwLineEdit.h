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

#include "SwFrame.h"
#include "Sw.h"
#include "SwGuiApplication.h"
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <sstream>
#include <vector>
#include "SwTimer.h"

// echo mode ●●●●●●●●●●
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
    SwLineEdit(const SwString& placeholderText = SwString(), SwWidget* parent = nullptr)
        : SwFrame(parent), cursorPos(0),
          selectionStart(0), selectionEnd(0), isSelecting(false) {
        resize(300, 30);
        setCursor(CursorType::IBeam);
        setPlaceholder(placeholderText);
        connect(this, SIGNAL(TextChanged), std::function<void(SwString)>([&](SwString text) {
            setDisplayText(text);
        }));
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
        connect(monitorTimer, SIGNAL(timeout), std::function<void()>([&]() {
            if (this->getFocus() == true) {
                m_caretVisible = !m_caretVisible;
                update();
            }
            }));

        connect(this, SIGNAL(FocusChanged), std::function<void(bool)>([&](bool focus) {
            m_caretVisible = true;
            if (focus == true) {
                monitorTimer->start();
            }
            else {
                monitorTimer->stop();
            }
            update();
            }));
    }

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
        connect(this, SIGNAL(TextChanged), std::function<void(SwString)>([&](SwString text) {
            setDisplayText(text);
        }));

        monitorTimer = new SwTimer(500, this);
        connect(monitorTimer, SIGNAL(timeout), std::function<void()>([&]() {
            if (this->getFocus() == true) {
                m_caretVisible = !m_caretVisible;
                update();
            }
        }));

        connect(this, SIGNAL(FocusChanged), std::function<void(bool)>([&](bool focus) {
            m_caretVisible = true;
            if (focus == true) {
                monitorTimer->start();
            } else {
                monitorTimer->stop();
            }
            update();
        }));
    }

    ~SwLineEdit() override {
        if (monitorTimer) {
            monitorTimer->stop();
        }
    }


    // Redéfinir la méthode paintEvent pour dessiner le champ de texte
    virtual void paintEvent(PaintEvent* event) override {
        if (!isVisibleInHierarchy()) {
            return;
        }

        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect rect = getRect();
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

        size_t clampedVisible = (std::min)(firstVisibleCharacter, textToDraw.length());
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
    // Gérer les événements de pression de touche
    virtual void keyPressEvent(KeyEvent* event) override {
        if (!getFocus()) {
            return;
        }

        const int keyCode = event->key();

        auto deleteBackward = [&]() {
            if (selectionStart != selectionEnd) {
                deleteSelection();
                emit TextChanged(m_Text);
            } else if (cursorPos > 0) {
                m_Text.erase(cursorPos - 1, 1);
                cursorPos--;
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
                m_Text.erase(cursorPos, 1);
                emit TextChanged(m_Text);
            }
            if (firstVisibleCharacter > m_Text.length()) {
                firstVisibleCharacter = m_Text.length();
            }
        };

        if (event->isCtrlPressed()) {
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
                cursorPos--;
                if (event->isShiftPressed()) {
                    selectionEnd = cursorPos;
                } else {
                    selectionStart = selectionEnd = cursorPos;
                }
                update();
            }
        } else if (SwWidgetPlatformAdapter::isRightArrowKey(keyCode)) {
            if (cursorPos < m_Text.length()) {
                cursorPos++;
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
            char inserted{};
            if (SwWidgetPlatformAdapter::translateCharacter(keyCode,
                                                            event->isShiftPressed(),
                                                            capsLockActive,
                                                            inserted)) {
                m_Text.insert(cursorPos, 1, inserted);
                cursorPos++;
                selectionStart = selectionEnd = cursorPos;
                emit TextChanged(m_Text);
                if (firstVisibleCharacter > m_Text.length()) {
                    firstVisibleCharacter = m_Text.length();
                }
            }
        }

        event->accept();
    }

    // Gérer le clic de souris pour le focus et le positionnement du curseur
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

    virtual void mouseDoubleClickEvent(MouseEvent* event) override {
        selectionStart = 0;
        selectionEnd = getText().size();
        event->accept();
        isSelecting = true;
        update();
    }

    // Gérer la sélection avec la souris
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

    // Terminer la sélection à la libération de la souris
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
    size_t selectionStart;     // Début de la sélection
    size_t selectionEnd;       // Fin de la sélection
    bool isSelecting;          // Indique si une sélection est en cours
    bool capsLockActive{false};
    SwTimer* monitorTimer{nullptr};
    bool m_caretVisible{true};
    size_t firstVisibleCharacter{0};
    SwColor m_focusAccent{59, 130, 246};

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

    // Obtenir l'index du caractère à une position x
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
        size_t clampedVisible = (std::min)(firstVisibleCharacter, display.length());
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
    // Supprimer la sélection
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

    // Copier la sélection dans le presse-papiers
    void copySelectionToClipboard() {
        if (selectionStart == selectionEnd) {
            return;  // Rien à copier
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
            std::string selector = hierarchy[i].toStdString();
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

    static int parsePixelValue(const SwString& value, int fallback) {
        if (value.isEmpty()) {
            return fallback;
        }
        std::string s = value.toStdString();
        size_t pos = s.find_first_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ%");
        if (pos != std::string::npos) {
            s = s.substr(0, pos);
        }
        try {
            return std::stoi(s);
        } catch (...) {
            return fallback;
        }
    }

    static int clampInt(int value, int minValue, int maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    static SwColor clampColor(const SwColor& c) {
        return SwColor{clampInt(c.r, 0, 255), clampInt(c.g, 0, 255), clampInt(c.b, 0, 255)};
    }

    void resolveBackground(StyleSheet* sheet,
                           SwColor& outColor,
                           float& outAlpha,
                           bool& outPaint) const {
        if (!sheet) {
            return;
        }

        auto selectors = classHierarchy();
        bool hasSwWidgetSelector = false;
        for (const SwString& selector : selectors) {
            if (selector == "SwWidget") {
                hasSwWidgetSelector = true;
                break;
            }
        }
        if (!hasSwWidgetSelector) {
            selectors.emplace_back("SwWidget");
        }

        for (const SwString& selector : selectors) {
            if (selector.isEmpty()) {
                continue;
            }
            SwString value = sheet->getStyleProperty(selector.toStdString(), "background-color");
            if (value.isEmpty()) {
                continue;
            }
            float alpha = 1.0f;
            try {
                SwColor resolved = sheet->parseColor(value.toStdString(), &alpha);
                if (alpha <= 0.0f) {
                    outPaint = false;
                } else {
                    outPaint = true;
                    outColor = clampColor(resolved);
                }
                outAlpha = alpha;
            } catch (...) {
                // ignore invalid colors
            }
            return;
        }
    }

    void resolveBorder(StyleSheet* sheet,
                       SwColor& outColor,
                       int& outWidth,
                       int& outRadius) const {
        if (!sheet) {
            return;
        }

        auto selectors = classHierarchy();
        bool hasSwWidgetSelector = false;
        for (const SwString& selector : selectors) {
            if (selector == "SwWidget") {
                hasSwWidgetSelector = true;
                break;
            }
        }
        if (!hasSwWidgetSelector) {
            selectors.emplace_back("SwWidget");
        }

        bool haveColor = false;
        bool haveWidth = false;
        bool haveRadius = false;

        for (const SwString& selector : selectors) {
            if (selector.isEmpty()) {
                continue;
            }
            const std::string key = selector.toStdString();

            if (!haveColor) {
                SwString borderColor = sheet->getStyleProperty(key, "border-color");
                if (!borderColor.isEmpty()) {
                    try {
                        SwColor resolved = sheet->parseColor(borderColor.toStdString(), nullptr);
                        outColor = clampColor(resolved);
                        haveColor = true;
                    } catch (...) {
                    }
                }
            }

            if (!haveWidth) {
                SwString borderWidth = sheet->getStyleProperty(key, "border-width");
                if (!borderWidth.isEmpty()) {
                    outWidth = clampInt(parsePixelValue(borderWidth, outWidth), 0, 20);
                    haveWidth = true;
                }
            }

            if (!haveRadius) {
                SwString borderRadius = sheet->getStyleProperty(key, "border-radius");
                if (!borderRadius.isEmpty()) {
                    outRadius = clampInt(parsePixelValue(borderRadius, outRadius), 0, 32);
                    haveRadius = true;
                }
            }

            if (haveColor && haveWidth && haveRadius) {
                break;
            }
        }
    }

    SwColor resolveTextColor(StyleSheet* sheet, const SwColor& fallback) const {
        if (!sheet) {
            return fallback;
        }

        auto selectors = classHierarchy();
        bool hasSwWidgetSelector = false;
        for (const SwString& selector : selectors) {
            if (selector == "SwWidget") {
                hasSwWidgetSelector = true;
                break;
            }
        }
        if (!hasSwWidgetSelector) {
            selectors.emplace_back("SwWidget");
        }

        for (const SwString& selector : selectors) {
            if (selector.isEmpty()) {
                continue;
            }
            SwString value = sheet->getStyleProperty(selector.toStdString(), "color");
            if (value.isEmpty()) {
                continue;
            }
            try {
                SwColor resolved = sheet->parseColor(value.toStdString(), nullptr);
                return clampColor(resolved);
            } catch (...) {
                return fallback;
            }
        }
        return fallback;
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
        SwRect rect = getRect();
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
        auto handle = nativeWindowHandle();
        SwFont font = getFont();
        areaWidth = std::max(1, areaWidth);
        auto widthUntil = [&](size_t index) {
            return SwWidgetPlatformAdapter::textWidthUntil(handle, textToDraw, font, index, areaWidth);
        };

        size_t clampedCursor = (std::min)(cursorPos, length);
        int caretPx = widthUntil(clampedCursor);
        int startPx = widthUntil(firstVisibleCharacter);

        if (caretPx < startPx) {
            firstVisibleCharacter = clampedCursor;
            return;
        }

        while (caretPx - startPx > areaWidth && firstVisibleCharacter < length) {
            ++firstVisibleCharacter;
            startPx = widthUntil(firstVisibleCharacter);
        }

        int totalWidth = widthUntil(length);
        if (totalWidth <= areaWidth) {
            firstVisibleCharacter = 0;
            return;
        }

        int maxStartPx = totalWidth - areaWidth;
        while (startPx > maxStartPx && firstVisibleCharacter > 0) {
            --firstVisibleCharacter;
            startPx = widthUntil(firstVisibleCharacter);
        }

        if (firstVisibleCharacter > length) {
            firstVisibleCharacter = length;
        }
    }
};
