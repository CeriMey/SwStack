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

#include "SwTextDocument.h"
#include "SwTextDecorationRenderer.h"
#include "SwPainter.h"
#include "graphics/SwFontMetrics.h"

#include <algorithm>
#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// SwTextDocumentLayout — layout engine and renderer for SwTextDocument
//
// Computes bounding rects for each block, handles word-wrapping, pagination,
// and rendering of the document via SwPainter.
// ---------------------------------------------------------------------------

class SwTextDocumentLayout {
public:
    SwTextDocumentLayout() = default;

    void setDocument(SwTextDocument* doc) { m_document = doc; m_dirty = true; }
    SwTextDocument* document() const { return m_document; }

    void setPageSize(int width, int height) { m_pageWidth = width; m_pageHeight = height; m_dirty = true; }
    int pageWidth() const  { return m_pageWidth; }
    int pageHeight() const { return m_pageHeight; }

    void setMargins(int top, int right, int bottom, int left) {
        m_marginTop = top; m_marginRight = right;
        m_marginBottom = bottom; m_marginLeft = left;
        m_dirty = true;
    }

    int contentWidth() const  { return m_pageWidth - m_marginLeft - m_marginRight; }
    int contentHeight() const { return m_pageHeight - m_marginTop - m_marginBottom; }

    void setDefaultFont(const SwFont& font) { m_defaultFont = font; m_dirty = true; }
    SwFont defaultFont() const { return m_defaultFont; }

    // --- Layout computation ---
    void layout() {
        if (!m_document) return;
        m_blockLayouts.clear();
        m_pageBreaks.clear();

        int y = m_marginTop;
        int pageNum = 0;
        m_pageBreaks.push_back(0); // page 0 starts at block 0

        for (int i = 0; i < m_document->blockCount(); ++i) {
            const SwTextBlock& block = m_document->blockAt(i);
            const SwTextBlockFormat& bf = block.blockFormat();

            BlockLayout bl;
            bl.blockIndex = i;

            // Top margin (auto-space headings and paragraphs for visual separation)
            int topM = bf.topMargin();
            if (topM == 0 && i > 0) {
                if (bf.headingLevel() > 0) {
                    topM = 8; // space before headings
                } else if (!block.list()) {
                    topM = 4; // small space between paragraphs
                }
            }
            y += topM;

            // Compute indentation
            int indent = bf.indent() * m_indentSize + bf.leftMargin() + bf.textIndent();
            int availWidth = contentWidth() - indent - bf.rightMargin();
            if (availWidth < 10) availWidth = 10;

            // Compute line height based on heading level, fragment font sizes, or default
            int maxPt = m_defaultFont.getPointSize();
            if (maxPt <= 0) maxPt = 12;
            if (bf.headingLevel() > 0) {
                maxPt = headingFontSize(bf.headingLevel());
            } else {
                // Scan fragments for the largest font size
                for (int fi = 0; fi < block.fragmentCount(); ++fi) {
                    const SwTextCharFormat& cf = block.fragmentAt(fi).charFormat();
                    if (cf.hasFontPointSize() && cf.fontPointSize() > maxPt) {
                        maxPt = cf.fontPointSize();
                    }
                }
            }
            int lineH = maxPt + 8;
            if (bf.headingLevel() > 0) lineH = maxPt + 12;
            if (bf.lineHeight() > 0) {
                lineH = bf.lineHeight();
            }
            if (lineH < 16) lineH = 16;

            // Check for table
            SwTextTable* table = m_document->tableForBlock(i);
            if (table) {
                bl.isTable = true;
                bl.table = table;
                int tableH = layoutTable(table, m_marginLeft + indent, y, availWidth);
                bl.rect = {m_marginLeft + indent, y, availWidth, tableH};
                bl.page = pageNum;
                m_blockLayouts.push_back(bl);
                y += tableH + bf.bottomMargin();

                if (y > m_pageHeight - m_marginBottom && i + 1 < m_document->blockCount()) {
                    ++pageNum;
                    m_pageBreaks.push_back(i + 1);
                    y = m_marginTop;
                }
                continue;
            }

            // Check for HR
            if (block.length() == 0 && block.fragmentCount() == 0) {
                // Could be an HR or empty block
                bl.rect = {m_marginLeft, y, contentWidth(), lineH / 2};
                bl.page = pageNum;
                m_blockLayouts.push_back(bl);
                y += lineH / 2 + bf.bottomMargin();
                continue;
            }

            // List bullet width
            int bulletWidth = 0;
            if (block.list()) {
                bulletWidth = m_indentSize;
            }

            // Word-wrap: compute visual lines
            bl.lines = wrapText(block, availWidth - bulletWidth);
            int totalHeight = static_cast<int>(bl.lines.size()) * lineH;

            // Page break check
            if (bf.pageBreakBefore() || (y + totalHeight > m_pageHeight - m_marginBottom && y > m_marginTop)) {
                ++pageNum;
                m_pageBreaks.push_back(i);
                y = m_marginTop + bf.topMargin();
            }

            bl.rect = {m_marginLeft + indent, y, availWidth, totalHeight};
            bl.lineHeight = lineH;
            bl.page = pageNum;
            m_blockLayouts.push_back(bl);

            y += totalHeight + bf.bottomMargin();

            if (bf.pageBreakAfter() && i + 1 < m_document->blockCount()) {
                ++pageNum;
                m_pageBreaks.push_back(i + 1);
                y = m_marginTop;
            }
        }

        m_pageCount = pageNum + 1;
        m_dirty = false;
    }

    int pageCount() const { return m_pageCount; }

    // --- Rendering ---
    void draw(SwPainter* painter, int page = -1) const {
        if (!m_document || !painter || m_dirty) return;

        for (size_t i = 0; i < m_blockLayouts.size(); ++i) {
            const BlockLayout& bl = m_blockLayouts[i];
            if (page >= 0 && bl.page != page) continue;

            // Compute Y offset for the page
            int yOffset = 0;
            if (page >= 0) {
                // Page-relative rendering — blocks already have absolute Y from layout
                // For multi-page, we subtract the page's starting Y
                yOffset = 0; // blocks already positioned correctly per page due to page breaks
            }

            const SwTextBlock& block = m_document->blockAt(bl.blockIndex);
            const SwTextBlockFormat& bf = block.blockFormat();

            // Draw block background
            if (bf.hasBackground()) {
                painter->fillRect(bl.rect, bf.background(), bf.background(), 0);
            }

            // Table rendering
            if (bl.isTable && bl.table) {
                drawTable(painter, bl.table, bl.rect);
                continue;
            }

            // HR rendering (empty block with no fragments could be HR)
            if (block.length() == 0 && block.fragmentCount() == 0 && bl.rect.height < 20) {
                int hrY = bl.rect.y + bl.rect.height / 2;
                painter->drawLine(bl.rect.x + 4, hrY, bl.rect.x + bl.rect.width - 4, hrY,
                                  SwColor{180, 180, 180}, 1);
                continue;
            }

            // List bullet
            if (block.list()) {
                SwTextList* list = block.list();
                int itemNum = list->itemNumber(bl.blockIndex);
                SwString bullet = list->format().bulletText(itemNum);
                SwRect bulletRect = {bl.rect.x - m_indentSize, bl.rect.y, m_indentSize, bl.lineHeight};
                painter->drawText(bulletRect, bullet,
                                  DrawTextFormats(DrawTextFormat::Right | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                  SwColor{0, 0, 0}, m_defaultFont);
            }

            // Draw text lines
            int lineY = bl.rect.y;
            for (size_t li = 0; li < bl.lines.size(); ++li) {
                const LinePart& line = bl.lines[li];
                drawLine(painter, block, line, bl.rect.x, lineY, bl.rect.width, bl.lineHeight, bf.alignment());
                lineY += bl.lineHeight;
            }
        }
    }

    // --- Draw onto a specific rect (for widget use) ---
    void drawInRect(SwPainter* painter, const SwRect& rect, int scrollY = 0) const {
        if (!m_document || !painter || m_dirty) return;

        painter->pushClipRect(rect);

        for (size_t i = 0; i < m_blockLayouts.size(); ++i) {
            const BlockLayout& bl = m_blockLayouts[i];
            SwRect adjusted = bl.rect;
            adjusted.x = rect.x + (bl.rect.x - m_marginLeft);
            adjusted.y = rect.y + (bl.rect.y - m_marginTop) - scrollY;

            // Skip if out of visible area
            if (adjusted.y + adjusted.height < rect.y) continue;
            if (adjusted.y > rect.y + rect.height) break;

            const SwTextBlock& block = m_document->blockAt(bl.blockIndex);
            const SwTextBlockFormat& bf = block.blockFormat();

            if (bf.hasBackground()) {
                painter->fillRect(adjusted, bf.background(), bf.background(), 0);
            }

            if (bl.isTable && bl.table) {
                drawTable(painter, bl.table, adjusted);
                continue;
            }

            if (block.length() == 0 && block.fragmentCount() == 0 && adjusted.height < 20) {
                int hrY = adjusted.y + adjusted.height / 2;
                painter->drawLine(adjusted.x + 4, hrY, adjusted.x + adjusted.width - 4, hrY,
                                  SwColor{180, 180, 180}, 1);
                continue;
            }

            if (block.list()) {
                SwTextList* list = block.list();
                int itemNum = list->itemNumber(bl.blockIndex);
                SwString bullet = list->format().bulletText(itemNum);
                SwRect bulletRect = {adjusted.x - m_indentSize, adjusted.y, m_indentSize, bl.lineHeight};
                painter->drawText(bulletRect, bullet,
                                  DrawTextFormats(DrawTextFormat::Right | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                  SwColor{0, 0, 0}, m_defaultFont);
            }

            int lineY = adjusted.y;
            for (size_t li = 0; li < bl.lines.size(); ++li) {
                const LinePart& line = bl.lines[li];
                drawLine(painter, block, line, adjusted.x, lineY, adjusted.width, bl.lineHeight, bf.alignment());
                lineY += bl.lineHeight;
            }
        }

        painter->popClipRect();
    }

    // --- Hit testing ---
    int hitTest(int x, int y) const {
        if (!m_document || m_dirty) return 0;
        for (size_t i = 0; i < m_blockLayouts.size(); ++i) {
            const BlockLayout& bl = m_blockLayouts[i];
            if (y >= bl.rect.y && y < bl.rect.y + bl.rect.height) {
                // Found the block
                int relY = y - bl.rect.y;
                int lineIdx = (bl.lineHeight > 0) ? (relY / bl.lineHeight) : 0;
                lineIdx = std::min(lineIdx, static_cast<int>(bl.lines.size()) - 1);
                lineIdx = std::max(0, lineIdx);

                if (lineIdx < static_cast<int>(bl.lines.size())) {
                    int absPos = m_document->absolutePosition(bl.blockIndex, bl.lines[lineIdx].startOffset);
                    // Approximate character position from X
                    int relX = x - bl.rect.x;
                    int charOffset = estimateCharAtX(m_document->blockAt(bl.blockIndex), bl.lines[lineIdx], relX);
                    return absPos + charOffset;
                }
                return m_document->absolutePosition(bl.blockIndex, 0);
            }
        }
        return m_document->characterCount();
    }

    // --- Block geometry ---
    SwRect blockBoundingRect(int blockIndex) const {
        for (size_t i = 0; i < m_blockLayouts.size(); ++i) {
            if (m_blockLayouts[i].blockIndex == blockIndex) return m_blockLayouts[i].rect;
        }
        return {0, 0, 0, 0};
    }

    int blockPage(int blockIndex) const {
        for (size_t i = 0; i < m_blockLayouts.size(); ++i) {
            if (m_blockLayouts[i].blockIndex == blockIndex) return m_blockLayouts[i].page;
        }
        return -1;
    }

    int blockLineHeight(int blockIndex) const {
        for (size_t i = 0; i < m_blockLayouts.size(); ++i) {
            if (m_blockLayouts[i].blockIndex == blockIndex) return m_blockLayouts[i].lineHeight;
        }
        return 20;
    }

    struct LineRange { int startOffset; int length; };
    std::vector<LineRange> blockLines(int blockIndex) const {
        std::vector<LineRange> result;
        for (size_t i = 0; i < m_blockLayouts.size(); ++i) {
            if (m_blockLayouts[i].blockIndex == blockIndex) {
                for (size_t j = 0; j < m_blockLayouts[i].lines.size(); ++j) {
                    LineRange lr;
                    lr.startOffset = m_blockLayouts[i].lines[j].startOffset;
                    lr.length = m_blockLayouts[i].lines[j].length;
                    result.push_back(lr);
                }
                break;
            }
        }
        return result;
    }

    // --- Total document height ---
    int documentHeight() const {
        if (m_blockLayouts.empty()) return 0;
        const BlockLayout& last = m_blockLayouts.back();
        return last.rect.y + last.rect.height + m_marginBottom;
    }

private:
    SwTextDocument* m_document{nullptr};
    int m_pageWidth{595};
    int m_pageHeight{842};
    int m_marginTop{72};
    int m_marginRight{72};
    int m_marginBottom{72};
    int m_marginLeft{72};
    int m_indentSize{24};
    int m_pageCount{1};
    bool m_dirty{true};
    SwFont m_defaultFont;

    struct LinePart {
        int startOffset{0};   // offset within the block
        int length{0};        // character count
    };

    struct BlockLayout {
        int blockIndex{0};
        SwRect rect{0, 0, 0, 0};
        int lineHeight{20};
        int page{0};
        std::vector<LinePart> lines;
        bool isTable{false};
        SwTextTable* table{nullptr};
    };

    std::vector<BlockLayout> m_blockLayouts;
    std::vector<int> m_pageBreaks; // block index where each page starts

    int headingFontSize(int level) const {
        switch (level) {
        case 1: return 24; case 2: return 18; case 3: return 14;
        case 4: return 12; case 5: return 10; case 6: return 8;
        default: return m_defaultFont.getPointSize();
        }
    }

    SwFont fontForBlock(const SwTextBlock& block) const {
        SwFont f = m_defaultFont;
        int hl = block.blockFormat().headingLevel();
        if (hl > 0) {
            f.setPointSize(headingFontSize(hl));
            f.setWeight(Bold);
        }
        return f;
    }

    SwFont fontForFormat(const SwTextCharFormat& fmt) const {
        return fmt.toFont(m_defaultFont);
    }

    // --- Word wrapping ---
    std::vector<LinePart> wrapText(const SwTextBlock& block, int maxWidth) const {
        std::vector<LinePart> result;
        SwString text = block.text();
        if (text.isEmpty()) {
            result.push_back({0, 0});
            return result;
        }

        // Greedy word-wrap using per-glyph Helvetica widths
        int lineStart = 0;
        int lastBreak = 0;
        int x = 0;
        int ptSize = m_defaultFont.getPointSize();
        if (ptSize <= 0) ptSize = 12;

        for (int i = 0; i <= static_cast<int>(text.size()); ++i) {
            if (i == static_cast<int>(text.size()) || text[i] == '\n') {
                result.push_back({lineStart, i - lineStart});
                lineStart = i + 1;
                lastBreak = lineStart;
                x = 0;
                continue;
            }
            if (text[i] == ' ') {
                lastBreak = i + 1;
            }
            x += (helveticaGlyphWidth_(static_cast<unsigned char>(text[i])) * ptSize + 500) / 1000;
            if (x > maxWidth && i > lineStart) {
                if (lastBreak > lineStart) {
                    result.push_back({lineStart, lastBreak - lineStart});
                    lineStart = lastBreak;
                } else {
                    result.push_back({lineStart, i - lineStart});
                    lineStart = i;
                }
                lastBreak = lineStart;
                x = 0;
            }
        }
        if (lineStart <= static_cast<int>(text.size()) && result.empty()) {
            result.push_back({lineStart, static_cast<int>(text.size()) - lineStart});
        }
        if (result.empty()) {
            result.push_back({0, 0});
        }
        return result;
    }

    // --- Draw a single wrapped line ---
    void drawLine(SwPainter* painter, const SwTextBlock& block, const LinePart& line,
                  int x, int y, int width, int lineHeight,
                  SwTextBlockFormat::Alignment align) const {
        if (line.length <= 0) return;

        const SwString lineText = block.text().substr(line.startOffset, line.length);

        // Compute alignment offset
        int textWidth = estimateTextWidth(lineText);
        int alignX = x;
        if (align == SwTextBlockFormat::AlignCenter) {
            alignX = x + std::max(0, (width - textWidth) / 2);
        } else if (align == SwTextBlockFormat::AlignRight) {
            alignX = x + std::max(0, width - textWidth);
        }

        std::vector<SwTextCharFormat> perChar(static_cast<size_t>(line.length));
        std::vector<bool> hasFormat(static_cast<size_t>(line.length), false);

        int fragmentCursor = 0;
        for (int f = 0; f < block.fragmentCount(); ++f) {
            const SwTextFragment& fragment = block.fragmentAt(f);
            const int fragmentStart = fragmentCursor;
            const int fragmentEnd = fragmentCursor + fragment.length();
            fragmentCursor = fragmentEnd;

            const int overlapStart = std::max(line.startOffset, fragmentStart);
            const int overlapEnd = std::min(line.startOffset + line.length, fragmentEnd);
            if (overlapEnd <= overlapStart) {
                continue;
            }

            for (int pos = overlapStart; pos < overlapEnd; ++pos) {
                const size_t localIndex = static_cast<size_t>(pos - line.startOffset);
                perChar[localIndex] = fragment.charFormat();
                hasFormat[localIndex] = true;
            }
        }

        const SwList<SwTextLayoutFormatRange> additionalFormats = block.additionalFormats();
        for (int i = 0; i < additionalFormats.size(); ++i) {
            const SwTextLayoutFormatRange& range = additionalFormats[i];
            const int overlapStart = std::max(line.startOffset, range.start);
            const int overlapEnd = std::min(line.startOffset + line.length, range.start + range.length);
            if (overlapEnd <= overlapStart) {
                continue;
            }

            for (int pos = overlapStart; pos < overlapEnd; ++pos) {
                const size_t localIndex = static_cast<size_t>(pos - line.startOffset);
                if (hasFormat[localIndex]) {
                    perChar[localIndex].merge(range.format);
                } else {
                    perChar[localIndex] = range.format;
                    hasFormat[localIndex] = true;
                }
            }
        }

        const SwFont baseFont = fontForBlock(block);
        int drawX = alignX;
        int segmentStart = 0;
        while (segmentStart < line.length) {
            const bool formatted = hasFormat[static_cast<size_t>(segmentStart)];
            int segmentEnd = segmentStart + 1;
            while (segmentEnd < line.length &&
                   hasFormat[static_cast<size_t>(segmentEnd)] == formatted &&
                   (!formatted || perChar[static_cast<size_t>(segmentEnd)] == perChar[static_cast<size_t>(segmentStart)])) {
                ++segmentEnd;
            }

            const SwString segmentText = lineText.substr(static_cast<size_t>(segmentStart),
                                                         static_cast<size_t>(segmentEnd - segmentStart));
            if (segmentText.isEmpty()) {
                segmentStart = segmentEnd;
                continue;
            }

            SwTextCharFormat format;
            if (formatted) {
                format = perChar[static_cast<size_t>(segmentStart)];
            }

            const SwFont font = formatted ? format.toFont(baseFont) : baseFont;
            const SwColor color = (formatted && format.hasForeground()) ? format.foreground() : SwColor{0, 0, 0};
            const int segmentWidth = estimateTextWidth(segmentText, font);
            SwRect segmentRect{drawX, y, width - (drawX - x), lineHeight};

            if (formatted && format.hasBackground()) {
                painter->fillRect({drawX, y, segmentWidth, lineHeight}, format.background(), format.background(), 0);
            }

            painter->drawText(segmentRect, segmentText,
                              DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              color, font);

            if (formatted && format.fontStrikeOut()) {
                const int strikeY = y + lineHeight / 2;
                painter->drawLine(drawX, strikeY, drawX + segmentWidth, strikeY, color, 1);
            }
            if (formatted) {
                swDrawCustomUnderline(painter, SwRect{drawX, y, segmentWidth, lineHeight}, segmentWidth, format, color);
            }

            drawX += segmentWidth;
            segmentStart = segmentEnd;
        }
    }

    // --- Table layout ---
    int layoutTable(SwTextTable* table, int x, int y, int availWidth) const {
        if (!table) return 0;
        int rows = table->rows();
        int cols = table->columns();
        if (rows == 0 || cols == 0) return 0;

        const SwTextTableFormat& tf = table->tableFormat();
        int bw = std::max(1, tf.borderWidth());
        int cellPad = tf.cellPadding();
        int fontSize = m_defaultFont.getPointSize() > 0 ? m_defaultFont.getPointSize() : 12;

        int totalHeight = 0;
        for (int r = 0; r < rows; ++r) {
            int maxCellH = fontSize + 2 * cellPad + 4;
            for (int c = 0; c < cols; ++c) {
                const SwTextTable::Cell& cell = table->cellAt(r, c);
                int cellH = 0;
                for (int bi = 0; bi < cell.blocks.size(); ++bi) {
                    cellH += fontSize + 4;
                }
                maxCellH = std::max(maxCellH, cellH + 2 * cellPad);
            }
            totalHeight += maxCellH + bw;
        }
        totalHeight += bw; // bottom border
        return totalHeight;
    }

    void drawTable(SwPainter* painter, SwTextTable* table, const SwRect& rect) const {
        if (!table || !painter) return;
        int rows = table->rows();
        int cols = table->columns();
        if (rows == 0 || cols == 0) return;

        const SwTextTableFormat& tf = table->tableFormat();
        int bw = std::max(1, tf.borderWidth());
        int cellPad = tf.cellPadding();
        int cellWidth = (rect.width - (cols + 1) * bw) / cols;
        int fontSize = m_defaultFont.getPointSize() > 0 ? m_defaultFont.getPointSize() : 12;

        // Compute dynamic row heights
        std::vector<int> rowHeights(rows);
        for (int r = 0; r < rows; ++r) {
            int maxH = fontSize + 2 * cellPad + 4;
            for (int c = 0; c < cols; ++c) {
                const SwTextTable::Cell& cell = table->cellAt(r, c);
                int cellH = 0;
                for (int bi = 0; bi < cell.blocks.size(); ++bi) cellH += fontSize + 4;
                maxH = std::max(maxH, cellH + 2 * cellPad);
            }
            rowHeights[r] = maxH;
        }

        int curY = rect.y;
        for (int r = 0; r < rows; ++r) {
            int rh = rowHeights[r];
            for (int c = 0; c < cols; ++c) {
                int cx = rect.x + bw + c * (cellWidth + bw);
                int cy = curY;
                SwRect cellRect{cx, cy, cellWidth, rh};

                // Cell background
                SwTextTableCellFormat cf = table->cellFormat(r, c);
                if (cf.hasBackground()) {
                    painter->fillRect(cellRect, cf.background(), cf.background(), 0);
                } else if (tf.hasBackground()) {
                    painter->fillRect(cellRect, tf.background(), tf.background(), 0);
                }

                // Cell border
                painter->drawRect(cellRect, tf.borderColor(), bw);

                // Cell text — render each fragment with its own formatting
                const SwTextTable::Cell& cell = table->cellAt(r, c);
                int textY = cy + cellPad;
                for (int bi = 0; bi < cell.blocks.size(); ++bi) {
                    const SwTextBlock& block = cell.blocks[bi];
                    int textX = cx + cellPad;
                    int lineH = fontSize + 4;
                    for (int fi = 0; fi < block.fragmentCount(); ++fi) {
                        const SwTextFragment& frag = block.fragmentAt(fi);
                        SwFont font = fontForFormat(frag.charFormat());
                        SwColor color{0, 0, 0};
                        if (frag.charFormat().hasForeground()) color = frag.charFormat().foreground();
                        SwRect textRect{textX, textY, cellWidth - 2 * cellPad - (textX - cx - cellPad), lineH};
                        painter->drawText(textRect, frag.text(),
                                          DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                          color, font);
                        textX += estimateTextWidth(frag.text(), font);
                    }
                    textY += lineH;
                }
            }
            curY += rh + bw;
        }
    }

    // --- Estimate text width (without platform metrics for portability) ---
    // Helvetica glyph widths (per 1000 units, standard AFM data)
    static int helveticaGlyphWidth_(unsigned char ch) {
        static const int w[] = {
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            278,278,355,556,556,889,667,191,333,333,389,584,278,333,278,278,
            556,556,556,556,556,556,556,556,556,556,278,278,584,584,584,556,
            1015,667,667,722,722,667,611,778,722,278,500,667,556,833,722,778,
            667,778,722,667,611,722,667,944,667,667,611,278,278,278,469,556,
            333,556,556,500,556,556,278,556,556,222,222,500,222,833,556,556,
            556,556,333,500,278,556,500,722,500,500,500,334,260,334,584,0,
            556,0,222,556,333,1000,556,556,333,1000,667,333,1000,0,611,0,
            0,222,222,333,333,350,556,1000,333,1000,500,333,944,0,500,667,
            278,333,556,556,556,556,260,556,333,737,370,556,584,333,737,333,
            400,584,333,333,333,556,537,278,333,333,365,556,834,834,834,611,
            667,667,667,667,667,667,1000,722,667,667,667,667,278,278,278,278,
            722,722,778,778,778,778,778,584,778,722,722,722,722,667,667,611,
            556,556,556,556,556,556,889,500,556,556,556,556,278,278,278,278,
            556,556,556,556,556,556,556,584,611,556,556,556,556,500,556,500
        };
        return w[ch];
    }

    int estimateTextWidth(const SwString& text, const SwFont& font = SwFont()) const {
        int ptSize = font.getPointSize();
        if (ptSize <= 0) ptSize = m_defaultFont.getPointSize();
        if (ptSize <= 0) ptSize = 12;
        int totalUnits = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            totalUnits += helveticaGlyphWidth_(static_cast<unsigned char>(text[i]));
        }
        return (totalUnits * ptSize + 500) / 1000;
    }

    int estimateCharAtX(const SwTextBlock& block, const LinePart& line, int relX) const {
        if (line.length <= 0 || relX <= 0) return 0;
        SwString text = block.text().substr(line.startOffset, line.length);
        int avgCharWidth = std::max(1, m_defaultFont.getPointSize() / 2);
        int idx = relX / avgCharWidth;
        return std::min(idx, static_cast<int>(text.size()));
    }
};
