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

#include "SwWidget.h"
#include "SwPainter.h"

#include "SwGuiApplication.h"
#include "SwWidgetPlatformAdapter.h"

#include "graphics/SwFontMetrics.h"
#include "graphics/SwImage.h"

#include "chatbubble/SwChatBubbleTheme.h"
#include "chatbubble/SwChatBubbleTypes.h"

#include <cmath>
#include <vector>

class SwChatBubble final : public SwWidget {
    SW_OBJECT(SwChatBubble, SwWidget)

public:
    explicit SwChatBubble(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        setFocusPolicy(FocusPolicyEnum::NoFocus);
    }

    void setTheme(const SwChatBubbleTheme& theme) {
        m_theme = theme;
        update();
    }

    const SwChatBubbleTheme& theme() const { return m_theme; }

    void setMessage(const SwChatBubbleMessage& message) {
        m_message = message;
        clearSelection();
        update();
    }

    const SwChatBubbleMessage& message() const { return m_message; }

    void setTextSelectable(bool on) {
        if (m_textSelectable == on) {
            return;
        }
        m_textSelectable = on;
        clearSelection();

        if (m_textSelectable) {
            setFocusPolicy(FocusPolicyEnum::Strong);
            setCursor(CursorType::IBeam);
        } else {
            setFocusPolicy(FocusPolicyEnum::NoFocus);
            setCursor(CursorType::Arrow);
        }

        update();
    }

    bool textSelectable() const { return m_textSelectable; }

    bool hasSelectedText() const {
        if (!m_textSelectable) {
            return false;
        }
        if (m_message.kind != SwChatMessageKind::Text) {
            return false;
        }
        return m_selectionStart != m_selectionEnd;
    }

    SwString selectedText() const {
        if (!hasSelectedText()) {
            return {};
        }
        const size_t n = m_message.text.size();
        const size_t a = std::min(m_selectionStart, m_selectionEnd);
        const size_t b = std::max(m_selectionStart, m_selectionEnd);
        if (a >= n || b <= a) {
            return {};
        }
        return m_message.text.substr(a, b - a);
    }

    void clearSelection() {
        m_selecting = false;
        m_selectionStart = 0;
        m_selectionEnd = 0;
    }

    void selectAll() {
        if (!m_textSelectable) {
            return;
        }
        if (m_message.kind != SwChatMessageKind::Text) {
            return;
        }
        m_selectionStart = 0;
        m_selectionEnd = m_message.text.size();
        update();
    }

    void setSelectionRange(size_t start, size_t end) {
        if (!m_textSelectable) {
            return;
        }
        if (m_message.kind != SwChatMessageKind::Text) {
            return;
        }
        const size_t n = m_message.text.size();
        m_selectionStart = std::min(start, n);
        m_selectionEnd = std::min(end, n);
        update();
    }

    static SwChatBubbleTheme defaultTheme() { return swChatBubbleWhatsAppTheme(); }

    static SwSize sizeHintForRow(int rowWidth, const SwChatBubbleMessage& msg, const SwChatBubbleTheme& theme) {
        const SwChatBubbleStyle& style = (msg.role == SwChatBubbleRole::User) ? theme.user : theme.bot;
        const SwChatBubbleLayoutConfig& cfg = theme.layout;

        const int rowW = clampInt_(rowWidth, 0, 1 << 30);
        const int padY = clampInt_(cfg.rowPaddingY, 0, 64);

        const int marginX = clampInt_((cfg.marginXDivisor > 0) ? (rowW / cfg.marginXDivisor) : cfg.marginXMin,
                                      cfg.marginXMin,
                                      cfg.marginXMax);

        const int bubblePadX = clampInt_(cfg.bubblePaddingX, 0, 64);
        const int bubblePadTop = clampInt_(cfg.bubblePaddingTop, 0, 64);
        const int bubblePadBottom = clampInt_(cfg.bubblePaddingBottom, 0, 128);

        const int maxBubbleW = clampInt_(rowW - marginX * 2, cfg.maxBubbleMin, cfg.maxBubbleMax);

        const SwFontMetrics metaFm(style.metaFont);
        int metaW = metaFm.horizontalAdvance(msg.meta) + 10;
        if (style.showTicks) {
            metaW += 16;
        }
        metaW = clampInt_(metaW, style.showTicks ? 56 : 44, 260);

        int bubbleW = maxBubbleW;
        if (msg.kind == SwChatMessageKind::Image) {
            bubbleW = clampInt_(maxBubbleW, cfg.imageBubbleMinWidth, cfg.imageBubbleMaxWidth);
            if (bubbleW > maxBubbleW) {
                bubbleW = maxBubbleW;
            }
        } else {
            const SwFontMetrics fm(style.messageFont);
            const int textW = fm.horizontalAdvance(msg.text);
            bubbleW = clampInt_(textW + bubblePadX * 2 + metaW, cfg.bubbleMinWidth, maxBubbleW);
        }

        int bubbleH = 34;
        if (msg.kind == SwChatMessageKind::Image) {
            int imgW = bubbleW - bubblePadX * 2;
            imgW = clampInt_(imgW, cfg.imageInnerMinWidth, cfg.imageInnerMaxWidth);
            int imgH = (imgW * 9) / 16;
            imgH = clampInt_(imgH, cfg.imageMinHeight, cfg.imageMaxHeight);
            bubbleH = bubblePadTop + imgH + bubblePadBottom;
        } else {
            const SwFontMetrics fm(style.messageFont);
            int lineH = clampInt_(fm.height(), 14, 22);

            const int contentW = std::max(0, bubbleW - bubblePadX * 2);
            const int lines = estimateWrappedLines_(msg.text, contentW, fm);
            bubbleH = bubblePadTop + lines * lineH + bubblePadBottom;
        }

        const int reactionExtra = msg.reaction.isEmpty() ? 0 : clampInt_(cfg.reactionExtraHeight, 0, 64);
        const int rowH = padY * 2 + bubbleH + reactionExtra;
        return SwSize{rowW, rowH};
    }

    static void paintRow(SwPainter* painter, const SwRect& rowRect, const SwChatBubbleMessage& msg, const SwChatBubbleTheme& theme) {
        if (!painter) {
            return;
        }

        const SwChatBubbleStyle& style = (msg.role == SwChatBubbleRole::User) ? theme.user : theme.bot;
        const SwChatBubbleLayoutConfig& cfg = theme.layout;

        const int padY = clampInt_(cfg.rowPaddingY, 0, 64);
        const int marginX = clampInt_((cfg.marginXDivisor > 0) ? (rowRect.width / cfg.marginXDivisor) : cfg.marginXMin,
                                      cfg.marginXMin,
                                      cfg.marginXMax);

        const int bubblePadX = clampInt_(cfg.bubblePaddingX, 0, 64);
        const int bubblePadTop = clampInt_(cfg.bubblePaddingTop, 0, 64);
        const int bubblePadBottom = clampInt_(cfg.bubblePaddingBottom, 0, 128);

        const int maxBubbleW = clampInt_(rowRect.width - marginX * 2, cfg.maxBubbleMin, cfg.maxBubbleMax);

        const SwFontMetrics metaFm(style.metaFont);
        int metaW = metaFm.horizontalAdvance(msg.meta) + 10;
        if (style.showTicks) {
            metaW += 16;
        }
        metaW = clampInt_(metaW, style.showTicks ? 56 : 44, 260);

        int bubbleW = maxBubbleW;
        if (msg.kind == SwChatMessageKind::Image) {
            bubbleW = clampInt_(maxBubbleW, cfg.imageBubbleMinWidth, cfg.imageBubbleMaxWidth);
            if (bubbleW > maxBubbleW) {
                bubbleW = maxBubbleW;
            }
        } else {
            const SwFontMetrics fm(style.messageFont);
            const int textW = fm.horizontalAdvance(msg.text);
            bubbleW = clampInt_(textW + bubblePadX * 2 + metaW, cfg.bubbleMinWidth, maxBubbleW);
        }

        int bubbleH = rowRect.height - padY * 2;
        int desiredH = bubbleH;
        if (msg.kind == SwChatMessageKind::Image) {
            int imgW = bubbleW - bubblePadX * 2;
            imgW = clampInt_(imgW, cfg.imageInnerMinWidth, cfg.imageInnerMaxWidth);
            int imgH = (imgW * 9) / 16;
            imgH = clampInt_(imgH, cfg.imageMinHeight, cfg.imageMaxHeight);
            desiredH = bubblePadTop + imgH + bubblePadBottom;
        } else {
            const SwFontMetrics fm(style.messageFont);
            int lineH = clampInt_(fm.height(), 14, 22);

            const int contentW = std::max(0, bubbleW - bubblePadX * 2);
            const int lines = estimateWrappedLines_(msg.text, contentW, fm);
            desiredH = bubblePadTop + lines * lineH + bubblePadBottom;
        }
        bubbleH = clampInt_(desiredH, 34, rowRect.height - padY * 2);

        const bool outgoing = (msg.role == SwChatBubbleRole::User);

        SwRect bubbleRect{};
        bubbleRect.y = rowRect.y + padY;
        bubbleRect.height = bubbleH;
        bubbleRect.width = bubbleW;
        bubbleRect.x = outgoing ? (rowRect.x + rowRect.width - marginX - bubbleW) : (rowRect.x + marginX);

        painter->fillRoundedRect(bubbleRect,
                                 clampInt_(style.bubbleRadius, 0, 64),
                                 style.bubbleFill,
                                 style.bubbleBorder,
                                 clampInt_(style.bubbleBorderWidth, 0, 8));

        if (style.showTail) {
            paintTail_(painter, bubbleRect, style, outgoing);
        }

        SwRect content = bubbleRect;
        content.x += bubblePadX;
        content.y += bubblePadTop;
        content.width -= bubblePadX * 2;
        content.height -= (bubblePadTop + bubblePadBottom);
        if (content.width < 0) content.width = 0;
        if (content.height < 0) content.height = 0;

        if (msg.kind == SwChatMessageKind::Image && msg.image && !msg.image->isNull()) {
            int imgW = content.width;
            imgW = clampInt_(imgW, cfg.imageInnerMinWidth, cfg.imageInnerMaxWidth);
            int imgH = (imgW * 9) / 16;
            imgH = clampInt_(imgH, cfg.imageMinHeight, cfg.imageMaxHeight);

            SwRect imgRect{content.x, content.y, imgW, imgH};
            painter->drawImage(imgRect, *msg.image, nullptr);
        } else if (!msg.text.isEmpty()) {
            painter->drawText(content,
                              msg.text,
                              DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::Top | DrawTextFormat::WordBreak),
                              style.textColor,
                              style.messageFont);
        }

        if (!msg.meta.isEmpty()) {
            const int metaH = 14;
            const int metaY = bubbleRect.y + bubbleRect.height - metaH - 4;
            SwRect meta{bubbleRect.x + bubbleRect.width - metaW - 8, metaY, metaW, metaH};

            SwRect t = meta;
            t.width = style.showTicks ? (metaW - 16) : metaW;

            painter->drawText(t,
                              msg.meta,
                              DrawTextFormats(DrawTextFormat::Right | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              style.metaColor,
                              style.metaFont);

            if (style.showTicks) {
                const int cx = meta.x + meta.width - 12;
                const int cy = meta.y + 4;
                paintDoubleTick_(painter, cx, cy, style.tickColor);
            }
        }

        if (!msg.reaction.isEmpty()) {
            const SwFontMetrics rfm(style.reactionFont);
            int pillW = rfm.horizontalAdvance(msg.reaction) + 12;
            pillW = clampInt_(pillW, 22, 64);
            const int pillH = 18;
            const int pillY = bubbleRect.y + bubbleRect.height + 4;
            const int pillX = outgoing ? (bubbleRect.x + bubbleRect.width - pillW - 14) : (bubbleRect.x + 12);

            SwRect pill{pillX, pillY, pillW, pillH};
            painter->fillRoundedRect(pill, pillH / 2, style.reactionFill, style.reactionBorder, 1);
            painter->drawText(pill,
                              msg.reaction,
                              DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              style.reactionTextColor,
                              style.reactionFont);
        }
    }

    SwRect sizeHint() const override {
        return SwRect{0, 0, 10000, 60};
    }

protected:
    void paintEvent(PaintEvent* event) override {
        if (!isVisibleInHierarchy()) {
            return;
        }
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        if (!m_textSelectable || m_message.kind != SwChatMessageKind::Text) {
            paintRow(painter, getRect(), m_message, m_theme);
            return;
        }

        // Custom paint to support selection highlight in read-only mode.
        paintSelectableTextRow_(painter, getRect(), m_message, m_theme);
    }

    void mousePressEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        if (!m_textSelectable || m_message.kind != SwChatMessageKind::Text) {
            SwWidget::mousePressEvent(event);
            return;
        }

        if (event->button() != SwMouseButton::Left) {
            SwWidget::mousePressEvent(event);
            return;
        }

        Layout layout = computeLayout_(getRect(), m_message, m_theme);
        if (!pointInRect_(layout.contentRect, event->x(), event->y())) {
            SwWidget::mousePressEvent(event);
            return;
        }

        const size_t idx = indexFromPosition_(layout, event->x(), event->y());
        m_selectionStart = idx;
        m_selectionEnd = idx;
        m_selecting = true;
        event->accept();
        update();
    }

    void mouseMoveEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        if (!m_textSelectable || m_message.kind != SwChatMessageKind::Text) {
            SwWidget::mouseMoveEvent(event);
            return;
        }

        if (!m_selecting) {
            SwWidget::mouseMoveEvent(event);
            return;
        }

        Layout layout = computeLayout_(getRect(), m_message, m_theme);
        const size_t idx = indexFromPosition_(layout, event->x(), event->y());
        m_selectionEnd = idx;
        event->accept();
        update();
    }

    void mouseReleaseEvent(MouseEvent* event) override {
        if (!event) {
            return;
        }

        if (!m_textSelectable || m_message.kind != SwChatMessageKind::Text) {
            SwWidget::mouseReleaseEvent(event);
            return;
        }

        if (!m_selecting) {
            SwWidget::mouseReleaseEvent(event);
            return;
        }

        Layout layout = computeLayout_(getRect(), m_message, m_theme);
        const size_t idx = indexFromPosition_(layout, event->x(), event->y());
        m_selectionEnd = idx;
        m_selecting = false;
        event->accept();
        update();
    }

    void keyPressEvent(KeyEvent* event) override {
        if (!event) {
            return;
        }

        if (!m_textSelectable || m_message.kind != SwChatMessageKind::Text || !getFocus()) {
            SwWidget::keyPressEvent(event);
            return;
        }

        const int key = event->key();
        if (event->isCtrlPressed()) {
            if (SwWidgetPlatformAdapter::matchesShortcutKey(key, 'C')) {
                copySelectionToClipboard_();
                event->accept();
            } else if (SwWidgetPlatformAdapter::matchesShortcutKey(key, 'A')) {
                selectAll();
                event->accept();
            }
        }

        if (event->isAccepted()) {
            update();
            return;
        }

        SwWidget::keyPressEvent(event);
    }

private:
    static int clampInt_(int value, int minValue, int maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    static int estimateWrappedLines_(const SwString& text, int contentW, const SwFontMetrics& fm) {
        if (contentW <= 0) {
            return 1;
        }
        if (text.isEmpty()) {
            return 1;
        }

        const int spaceW = fm.horizontalAdvance(" ");

        SwList<SwString> paragraphs = text.split('\n');
        int totalLines = 0;

        for (int p = 0; p < paragraphs.size(); ++p) {
            const SwString para = paragraphs.at(p);
            if (para.isEmpty()) {
                totalLines += 1;
                continue;
            }

            SwList<SwString> words = para.split(' ');
            int lineW = 0;
            int lines = 1;

            for (int i = 0; i < words.size(); ++i) {
                SwString w = words.at(i);
                if (w.isEmpty()) {
                    continue;
                }

                const int wordW = fm.horizontalAdvance(w);
                if (wordW > contentW) {
                    // A single word longer than the available width: approximate a character-break wrap
                    // so the bubble height does not under-estimate and clip the painted text.
                    if (lineW > 0) {
                        lines += 1;
                        lineW = 0;
                    }
                    const int chunks = std::max(1, (wordW + contentW - 1) / std::max(1, contentW));
                    lines += std::max(0, chunks - 1);
                    const int remainder = wordW - (chunks - 1) * contentW;
                    lineW = (remainder <= 0) ? contentW : std::min(remainder, contentW);
                    continue;
                }
                if (lineW <= 0) {
                    lineW = wordW;
                } else if (lineW + spaceW + wordW <= contentW) {
                    lineW += spaceW + wordW;
                } else {
                    lines += 1;
                    lineW = wordW;
                }
            }

            totalLines += lines;
        }

        if (totalLines < 1) totalLines = 1;
        // Keep a soft upper bound; SwListView clamps row heights anyway.
        if (totalLines > 200) totalLines = 200;
        return totalLines;
    }

    struct Layout {
        const SwChatBubbleStyle* style{nullptr};
        const SwChatBubbleLayoutConfig* cfg{nullptr};

        bool outgoing{false};

        int padY{0};
        int marginX{0};
        int bubblePadX{0};
        int bubblePadTop{0};
        int bubblePadBottom{0};
        int metaW{0};
        int bubbleW{0};
        int bubbleH{0};
        int lineH{0};

        SwRect bubbleRect{};
        SwRect contentRect{};
    };

    struct WrappedLine {
        size_t start{0};
        size_t len{0};
    };

    static bool pointInRect_(const SwRect& r, int px, int py) {
        return px >= r.x && px <= (r.x + r.width) && py >= r.y && py <= (r.y + r.height);
    }

    static Layout computeLayout_(const SwRect& rowRect, const SwChatBubbleMessage& msg, const SwChatBubbleTheme& theme) {
        Layout out;

        out.style = (msg.role == SwChatBubbleRole::User) ? &theme.user : &theme.bot;
        out.cfg = &theme.layout;
        const SwChatBubbleStyle& style = *out.style;
        const SwChatBubbleLayoutConfig& cfg = *out.cfg;

        out.padY = clampInt_(cfg.rowPaddingY, 0, 64);
        out.marginX = clampInt_((cfg.marginXDivisor > 0) ? (rowRect.width / cfg.marginXDivisor) : cfg.marginXMin,
                                cfg.marginXMin,
                                cfg.marginXMax);

        out.bubblePadX = clampInt_(cfg.bubblePaddingX, 0, 64);
        out.bubblePadTop = clampInt_(cfg.bubblePaddingTop, 0, 64);
        out.bubblePadBottom = clampInt_(cfg.bubblePaddingBottom, 0, 128);

        const int maxBubbleW = clampInt_(rowRect.width - out.marginX * 2, cfg.maxBubbleMin, cfg.maxBubbleMax);

        const SwFontMetrics metaFm(style.metaFont);
        int metaW = metaFm.horizontalAdvance(msg.meta) + 10;
        if (style.showTicks) {
            metaW += 16;
        }
        out.metaW = clampInt_(metaW, style.showTicks ? 56 : 44, 260);

        out.bubbleW = maxBubbleW;
        if (msg.kind == SwChatMessageKind::Image) {
            out.bubbleW = clampInt_(maxBubbleW, cfg.imageBubbleMinWidth, cfg.imageBubbleMaxWidth);
            if (out.bubbleW > maxBubbleW) {
                out.bubbleW = maxBubbleW;
            }
        } else {
            const SwFontMetrics fm(style.messageFont);
            const int textW = fm.horizontalAdvance(msg.text);
            out.bubbleW = clampInt_(textW + out.bubblePadX * 2 + out.metaW, cfg.bubbleMinWidth, maxBubbleW);
        }

        int desiredH = 34;
        if (msg.kind == SwChatMessageKind::Image) {
            int imgW = out.bubbleW - out.bubblePadX * 2;
            imgW = clampInt_(imgW, cfg.imageInnerMinWidth, cfg.imageInnerMaxWidth);
            int imgH = (imgW * 9) / 16;
            imgH = clampInt_(imgH, cfg.imageMinHeight, cfg.imageMaxHeight);
            desiredH = out.bubblePadTop + imgH + out.bubblePadBottom;
        } else {
            const SwFontMetrics fm(style.messageFont);
            out.lineH = clampInt_(fm.height(), 14, 22);
            const int contentW = std::max(0, out.bubbleW - out.bubblePadX * 2);
            const int lines = estimateWrappedLines_(msg.text, contentW, fm);
            desiredH = out.bubblePadTop + lines * out.lineH + out.bubblePadBottom;
        }

        out.bubbleH = clampInt_(desiredH, 34, rowRect.height - out.padY * 2);
        out.outgoing = (msg.role == SwChatBubbleRole::User);

        out.bubbleRect.y = rowRect.y + out.padY;
        out.bubbleRect.height = out.bubbleH;
        out.bubbleRect.width = out.bubbleW;
        out.bubbleRect.x =
            out.outgoing ? (rowRect.x + rowRect.width - out.marginX - out.bubbleW) : (rowRect.x + out.marginX);

        out.contentRect = out.bubbleRect;
        out.contentRect.x += out.bubblePadX;
        out.contentRect.y += out.bubblePadTop;
        out.contentRect.width -= out.bubblePadX * 2;
        out.contentRect.height -= (out.bubblePadTop + out.bubblePadBottom);
        if (out.contentRect.width < 0) out.contentRect.width = 0;
        if (out.contentRect.height < 0) out.contentRect.height = 0;

        return out;
    }

    static std::vector<WrappedLine> wrapTextLines_(const SwString& text,
                                                  int contentW,
                                                  const SwWidgetPlatformHandle& handle,
                                                  const SwFont& font) {
        std::vector<WrappedLine> out;
        if (contentW <= 1) {
            out.push_back(WrappedLine{0, text.size()});
            return out;
        }

        const size_t n = text.size();
        size_t paraStart = 0;
        auto pushLine = [&](size_t start, size_t len) { out.push_back(WrappedLine{start, len}); };

        auto pushWrappedParagraph = [&](size_t start, size_t len) {
            if (len == 0) {
                pushLine(start, 0);
                return;
            }

            const int fallbackWidth = std::max(1, contentW);
            size_t localStart = 0;
            while (localStart < len) {
                const size_t remaining = len - localStart;
                const SwString remainingText = text.substr(start + localStart, remaining);

                auto widthUntil = [&](size_t count) -> int {
                    return SwWidgetPlatformAdapter::textWidthUntil(handle, remainingText, font, count, fallbackWidth);
                };

                size_t best = 1;
                size_t lo = 1;
                size_t hi = remaining;
                while (lo <= hi) {
                    const size_t mid = lo + ((hi - lo) / 2);
                    if (widthUntil(mid) <= contentW) {
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

                // Prefer breaking at whitespace when possible (WordBreak-like).
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
                        take = breakPos + 1; // include whitespace to preserve 1:1 indices with `text`
                    }
                }

                pushLine(start + localStart, take);
                localStart += take;
            }
        };

        for (size_t i = 0; i <= n; ++i) {
            if (i == n || text[i] == '\n') {
                const size_t paraLen = i - paraStart;
                pushWrappedParagraph(paraStart, paraLen);
                paraStart = i + 1;
            }
        }

        if (out.empty()) {
            out.push_back(WrappedLine{0, 0});
        }

        return out;
    }

    size_t indexFromPosition_(const Layout& layout, int px, int py) const {
        if (m_message.text.isEmpty()) {
            return 0;
        }
        if (!layout.style || layout.lineH <= 0) {
            return 0;
        }

        const int lineH = layout.lineH;
        const int relY = py - layout.contentRect.y;
        int row = (lineH > 0) ? (relY / lineH) : 0;
        if (row < 0) {
            row = 0;
        }

        const std::vector<WrappedLine> lines =
            wrapTextLines_(m_message.text, layout.contentRect.width, nativeWindowHandle(), layout.style->messageFont);
        if (lines.empty()) {
            return 0;
        }

        if (row >= static_cast<int>(lines.size())) {
            row = static_cast<int>(lines.size()) - 1;
        }

        const WrappedLine& seg = lines[static_cast<size_t>(row)];
        const SwString lineText = m_message.text.substr(seg.start, seg.len);

        int relX = px - layout.contentRect.x;
        if (relX < 0) {
            relX = 0;
        }

        const size_t col = SwWidgetPlatformAdapter::characterIndexAtPosition(nativeWindowHandle(),
                                                                             lineText,
                                                                             layout.style->messageFont,
                                                                             relX,
                                                                             std::max(1, layout.contentRect.width));
        const size_t clampedCol = std::min(col, lineText.size());
        return std::min(seg.start + clampedCol, m_message.text.size());
    }

    void copySelectionToClipboard_() const {
        const SwString text = selectedText();
        if (text.isEmpty()) {
            return;
        }
        SwGuiApplication* app = SwGuiApplication::instance(false);
        SwPlatformIntegration* platform = app ? app->platformIntegration() : nullptr;
        if (!platform) {
            return;
        }
        platform->setClipboardText(text);
    }

    void paintSelectableTextRow_(SwPainter* painter,
                                const SwRect& rowRect,
                                const SwChatBubbleMessage& msg,
                                const SwChatBubbleTheme& theme) {
        if (!painter) {
            return;
        }

        const Layout layout = computeLayout_(rowRect, msg, theme);
        if (!layout.style || !layout.cfg) {
            return;
        }

        const SwChatBubbleStyle& style = *layout.style;
        const SwChatBubbleLayoutConfig& cfg = *layout.cfg;

        painter->fillRoundedRect(layout.bubbleRect,
                                 clampInt_(style.bubbleRadius, 0, 64),
                                 style.bubbleFill,
                                 style.bubbleBorder,
                                 clampInt_(style.bubbleBorderWidth, 0, 8));

        if (style.showTail) {
            paintTail_(painter, layout.bubbleRect, style, layout.outgoing);
        }

        // Text content with selection highlight.
        if (!msg.text.isEmpty() && layout.contentRect.width > 0 && layout.contentRect.height > 0 && layout.lineH > 0) {
            const std::vector<WrappedLine> lines =
                wrapTextLines_(msg.text, layout.contentRect.width, nativeWindowHandle(), style.messageFont);

            const bool hasSel = hasSelectedText();
            const size_t selMin = std::min(m_selectionStart, m_selectionEnd);
            const size_t selMax = std::max(m_selectionStart, m_selectionEnd);
            const SwColor selFill{219, 234, 254};
            const int fallbackWidth = std::max(1, layout.contentRect.width);

            for (size_t i = 0; i < lines.size(); ++i) {
                const int y = layout.contentRect.y + static_cast<int>(i) * layout.lineH;
                if (y > layout.contentRect.y + layout.contentRect.height) {
                    break;
                }

                const WrappedLine& seg = lines[i];
                const SwString lineText = msg.text.substr(seg.start, seg.len);
                SwRect lineRect{layout.contentRect.x, y, layout.contentRect.width, layout.lineH};

                if (hasSel) {
                    const size_t lineStart = seg.start;
                    const size_t lineEnd = seg.start + seg.len;

                    const size_t segStart = (std::max)(selMin, lineStart);
                    const size_t segEnd = (std::min)(selMax, lineEnd);
                    if (segStart < segEnd) {
                        const size_t startCol = segStart - lineStart;
                        const size_t endCol = segEnd - lineStart;

                        const int x1 = layout.contentRect.x +
                                       SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(),
                                                                              lineText,
                                                                              style.messageFont,
                                                                              startCol,
                                                                              fallbackWidth);
                        const int x2 = layout.contentRect.x +
                                       SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(),
                                                                              lineText,
                                                                              style.messageFont,
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
                                  style.textColor,
                                  style.messageFont);
            }
        }

        // Meta (time).
        if (!msg.meta.isEmpty()) {
            const int metaH = 14;
            const int metaY = layout.bubbleRect.y + layout.bubbleRect.height - metaH - 4;
            SwRect meta{layout.bubbleRect.x + layout.bubbleRect.width - layout.metaW - 8, metaY, layout.metaW, metaH};

            SwRect t = meta;
            t.width = style.showTicks ? (layout.metaW - 16) : layout.metaW;

            painter->drawText(t,
                              msg.meta,
                              DrawTextFormats(DrawTextFormat::Right | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              style.metaColor,
                              style.metaFont);

            if (style.showTicks) {
                const int cx = meta.x + meta.width - 12;
                const int cy = meta.y + 4;
                paintDoubleTick_(painter, cx, cy, style.tickColor);
            }
        }

        // Reaction pill.
        if (!msg.reaction.isEmpty()) {
            const SwFontMetrics rfm(style.reactionFont);
            int pillW = rfm.horizontalAdvance(msg.reaction) + 12;
            pillW = clampInt_(pillW, 22, 64);
            const int pillH = 18;
            const int pillY = layout.bubbleRect.y + layout.bubbleRect.height + 4;
            const int pillX =
                layout.outgoing ? (layout.bubbleRect.x + layout.bubbleRect.width - pillW - 14) : (layout.bubbleRect.x + 12);

            SwRect pill{pillX, pillY, pillW, pillH};
            painter->fillRoundedRect(pill, pillH / 2, style.reactionFill, style.reactionBorder, 1);
            painter->drawText(pill,
                              msg.reaction,
                              DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              style.reactionTextColor,
                              style.reactionFont);
        }
    }

    static void paintDoubleTick_(SwPainter* painter, int x, int y, const SwColor& color) {
        if (!painter) {
            return;
        }
        // Two small check marks (approx).
        painter->drawLine(x - 10, y + 6, x - 7, y + 9, color, 1);
        painter->drawLine(x - 7, y + 9, x - 3, y + 3, color, 1);
        painter->drawLine(x - 6, y + 6, x - 3, y + 9, color, 1);
        painter->drawLine(x - 3, y + 9, x + 1, y + 3, color, 1);
    }

    static void paintTail_(SwPainter* painter,
                           const SwRect& bubbleRect,
                           const SwChatBubbleStyle& style,
                           bool outgoing) {
        if (!painter) {
            return;
        }

        const int tailW = clampInt_(style.tailWidth, 0, 40);
        const int tailH = clampInt_(style.tailHeight, 0, 48);
        if (tailW <= 0 || tailH <= 0) {
            return;
        }

        const int radius = clampInt_(style.bubbleRadius, 0, 64);
        const int overlap = clampInt_(radius / 3, 2, 5);

        // WhatsApp-like tail: short vertical base, placed near the bottom corner.
        const int baseH = clampInt_((tailH * 70) / 100, 8, tailH);
        const int bottomInset = clampInt_(radius / 3 + 1, 3, 10);

        const int minBottomY = bubbleRect.y + baseH + 4;
        const int maxBottomY = bubbleRect.y + bubbleRect.height - 1;
        int baseBottomY = bubbleRect.y + bubbleRect.height - bottomInset;
        baseBottomY = clampInt_(baseBottomY, minBottomY, std::max(minBottomY, maxBottomY));
        const int baseTopY = baseBottomY - baseH;

        const int baseX = outgoing ? (bubbleRect.x + bubbleRect.width - overlap) : (bubbleRect.x + overlap);
        const int dir = outgoing ? 1 : -1;

        const int borderW = clampInt_(style.bubbleBorderWidth, 0, 8);

        SwPoint pts[64]{};
        int n = 0;
        auto addPt = [&](int x, int y) {
            if (n >= 64) {
                return;
            }
            pts[n++] = SwPoint{x, y};
        };

        auto iround = [](double v) -> int { return static_cast<int>(std::lround(v)); };
        auto quad = [](double t, double p0, double p1, double p2) -> double {
            const double u = 1.0 - t;
            return (u * u * p0) + (2.0 * u * t * p1) + (t * t * p2);
        };

        // Curved triangular tail (2 quadratic bezier edges).
        const int tipX = baseX + dir * tailW;
        const int tipY = baseTopY + (baseH * 70) / 100;

        // Keep the edges mostly straight (triangle) with a subtle outward curve.
        const double c1x = static_cast<double>(baseX + dir * (tailW * 56) / 100);
        const double c1y = static_cast<double>(baseTopY + (baseH * 28) / 100);
        const double c2x = static_cast<double>(baseX + dir * (tailW * 56) / 100);
        const double c2y = static_cast<double>(baseTopY + (baseH * 88) / 100);

        const int segA = clampInt_(baseH + 4, 8, 16);
        const int segB = segA;

        addPt(baseX, baseTopY);
        for (int i = 1; i <= segA; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(segA);
            const int x = iround(quad(t, static_cast<double>(baseX), c1x, static_cast<double>(tipX)));
            const int y = iround(quad(t, static_cast<double>(baseTopY), c1y, static_cast<double>(tipY)));
            addPt(x, y);
        }
        for (int i = 1; i <= segB; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(segB);
            const int x = iround(quad(t, static_cast<double>(tipX), c2x, static_cast<double>(baseX)));
            const int y = iround(quad(t, static_cast<double>(tipY), c2y, static_cast<double>(baseBottomY)));
            addPt(x, y);
        }

        painter->fillPolygon(pts, n, style.bubbleFill, style.bubbleBorder, borderW);
    }

    SwChatBubbleTheme m_theme{swChatBubbleWhatsAppTheme()};
    SwChatBubbleMessage m_message;

    bool m_textSelectable{false};
    bool m_selecting{false};
    size_t m_selectionStart{0};
    size_t m_selectionEnd{0};
};
