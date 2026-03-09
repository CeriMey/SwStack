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
#include "SwTextDocumentLayout.h"
#include "SwPdfWriter.h"

#include <fstream>

// ---------------------------------------------------------------------------
// SwTextDocumentWriter — high-level document export (PDF, HTML, plain text)
//
// Usage:
//   SwTextDocumentWriter writer;
//   writer.writePdf(document, "output.pdf");
//   writer.writeHtml(document, "output.html");
//   writer.writePlainText(document, "output.txt");
// ---------------------------------------------------------------------------

class SwTextDocumentWriter {
public:
    SwTextDocumentWriter() = default;

    // --- Page setup (for PDF) ---
    void setPageSize(int widthPt, int heightPt) {
        m_pageWidth = widthPt;
        m_pageHeight = heightPt;
    }

    void setPageSizeA4() { setPageSize(595, 842); }
    void setPageSizeLetter() { setPageSize(612, 792); }

    void setMargins(int top, int right, int bottom, int left) {
        m_marginTop = top; m_marginRight = right;
        m_marginBottom = bottom; m_marginLeft = left;
    }

    void setDefaultFont(const SwFont& font) { m_defaultFont = font; }

    // --- PDF export ---
    bool writePdf(SwTextDocument* doc, const SwString& filename) {
        if (!doc) return false;

        // Layout the document
        SwTextDocumentLayout docLayout;
        docLayout.setDocument(doc);
        docLayout.setPageSize(m_pageWidth, m_pageHeight);
        docLayout.setMargins(m_marginTop, m_marginRight, m_marginBottom, m_marginLeft);
        docLayout.setDefaultFont(m_defaultFont);
        docLayout.layout();

        // Create PDF writer
        SwPdfWriter pdf(filename);
        pdf.setPageSize(m_pageWidth, m_pageHeight);
        pdf.setMargins(m_marginTop, m_marginRight, m_marginBottom, m_marginLeft);

        int pages = docLayout.pageCount();

        for (int page = 0; page < pages; ++page) {
            pdf.beginPage();
            pdf.setFont("Helvetica", m_defaultFont.getPointSize() > 0 ? m_defaultFont.getPointSize() : 12);

            // Render each block on this page
            renderPageToPdf(doc, docLayout, pdf, page);

            pdf.endPage();
        }

        return pdf.writePdf();
    }

    // --- HTML export ---
    bool writeHtml(SwTextDocument* doc, const SwString& filename) {
        if (!doc) return false;

        SwString html = doc->toHtml();

        std::ofstream file(filename.toStdString());
        if (!file.is_open()) return false;

        std::string content = html.toStdString();
        file.write(content.c_str(), content.size());
        file.close();
        return true;
    }

    // --- Plain text export ---
    bool writePlainText(SwTextDocument* doc, const SwString& filename) {
        if (!doc) return false;

        SwString text = doc->toPlainText();

        std::ofstream file(filename.toStdString());
        if (!file.is_open()) return false;

        std::string content = text.toStdString();
        file.write(content.c_str(), content.size());
        file.close();
        return true;
    }

    // --- Static convenience ---
    static bool exportToPdf(SwTextDocument* doc, const SwString& filename,
                            const SwFont& font = SwFont()) {
        SwTextDocumentWriter writer;
        writer.setPageSizeA4();
        writer.setMargins(72, 72, 72, 72);
        writer.setDefaultFont(font);
        return writer.writePdf(doc, filename);
    }

    static bool exportToHtml(SwTextDocument* doc, const SwString& filename) {
        SwTextDocumentWriter writer;
        return writer.writeHtml(doc, filename);
    }

private:
    int m_pageWidth{595};
    int m_pageHeight{842};
    int m_marginTop{72};
    int m_marginRight{72};
    int m_marginBottom{72};
    int m_marginLeft{72};
    SwFont m_defaultFont;

    void renderPageToPdf(SwTextDocument* doc, SwTextDocumentLayout& layout,
                         SwPdfWriter& pdf, int page) {
        const int defaultFontSize = m_defaultFont.getPointSize() > 0 ? m_defaultFont.getPointSize() : 12;

        for (int i = 0; i < doc->blockCount(); ++i) {
            if (layout.blockPage(i) != page) continue;

            SwRect blockRect = layout.blockBoundingRect(i);
            if (blockRect.width == 0 && blockRect.height == 0) continue;

            const SwTextBlock& block = doc->blockAt(i);
            const SwTextBlockFormat& bf = block.blockFormat();

            // Background
            if (bf.hasBackground()) {
                pdf.fillRect(blockRect.x, blockRect.y, blockRect.width, blockRect.height, bf.background());
            }

            // HR
            if (block.length() == 0 && block.fragmentCount() == 0) {
                // Check if this is a table placeholder
                SwTextTable* tbl = doc->tableForBlock(i);
                if (tbl) {
                    renderTableToPdf(tbl, blockRect, pdf);
                    continue;
                }
                if (blockRect.height < 20) {
                    int hrY = blockRect.y + blockRect.height / 2;
                    pdf.drawLine(blockRect.x + 4, hrY, blockRect.x + blockRect.width - 4, hrY,
                                 SwColor{180, 180, 180}, 1);
                }
                continue;
            }

            // Determine base font size
            int baseFontSize = defaultFontSize;
            if (bf.headingLevel() > 0) {
                int sizes[] = {0, 24, 18, 14, 12, 10, 8};
                baseFontSize = sizes[bf.headingLevel()];
            } else {
                for (int f = 0; f < block.fragmentCount(); ++f) {
                    const SwTextCharFormat& cf = block.fragmentAt(f).charFormat();
                    if (cf.hasFontPointSize() && cf.fontPointSize() > baseFontSize) {
                        baseFontSize = cf.fontPointSize();
                    }
                }
            }

            int lineHeight = layout.blockLineHeight(i);

            // List bullet
            if (block.list()) {
                SwTextList* list = block.list();
                int itemNum = list->itemNumber(i);
                SwString bullet = list->format().bulletText(itemNum);
                pdf.setFont("Helvetica", defaultFontSize);
                pdf.setFillColor(SwColor{0, 0, 0});
                pdf.drawText(blockRect.x - 20, blockRect.y + baseFontSize, bullet);
            }

            // Get wrapped lines from layout
            std::vector<SwTextDocumentLayout::LineRange> lines = layout.blockLines(i);
            SwString blockText = block.text();

            // Build fragment position index
            struct FragPos { int start; int end; int fragIdx; };
            std::vector<FragPos> fragPositions;
            {
                int pos = 0;
                for (int f = 0; f < block.fragmentCount(); ++f) {
                    int flen = block.fragmentAt(f).length();
                    FragPos fp;
                    fp.start = pos;
                    fp.end = pos + flen;
                    fp.fragIdx = f;
                    fragPositions.push_back(fp);
                    pos += flen;
                }
            }

            // Render each wrapped line
            int lineY = blockRect.y;
            for (size_t li = 0; li < lines.size(); ++li) {
                int lineStart = lines[li].startOffset;
                int lineLen = lines[li].length;
                int lineEnd = lineStart + lineLen;

                // Alignment
                int lineX = blockRect.x;
                if (bf.alignment() == SwTextBlockFormat::AlignCenter || bf.alignment() == SwTextBlockFormat::AlignRight) {
                    int lineW = estimateTextWidth(blockText.substr(lineStart, lineLen), baseFontSize);
                    if (bf.alignment() == SwTextBlockFormat::AlignCenter) {
                        lineX += std::max(0, (blockRect.width - lineW) / 2);
                    } else {
                        lineX += std::max(0, blockRect.width - lineW);
                    }
                }

                int drawX = lineX;
                int drawY = lineY + baseFontSize; // baseline

                // Render fragments that overlap this line
                for (size_t fi = 0; fi < fragPositions.size(); ++fi) {
                    int segStart = std::max(lineStart, fragPositions[fi].start);
                    int segEnd = std::min(lineEnd, fragPositions[fi].end);
                    if (segEnd <= segStart) continue;

                    const SwTextFragment& frag = block.fragmentAt(fragPositions[fi].fragIdx);
                    const SwTextCharFormat& cf = frag.charFormat();

                    // Font
                    std::string fontName = "Helvetica";
                    int fontSize = baseFontSize;
                    if (cf.hasFontPointSize() && cf.fontPointSize() > 0) {
                        fontSize = cf.fontPointSize();
                    }
                    if (cf.hasFontWeight() && cf.fontWeight() == Bold) {
                        fontName = "Helvetica-Bold";
                    }
                    if (cf.fontItalic()) {
                        fontName += (fontName.find("Bold") != std::string::npos) ? "Oblique" : "-Oblique";
                    }
                    if (cf.hasFontFamily()) {
                        std::string fam = cf.fontFamily().toStdString();
                        if (fam.find("Courier") != std::string::npos || fam.find("Mono") != std::string::npos) {
                            fontName = (cf.hasFontWeight() && cf.fontWeight() == Bold) ? "Courier-Bold" : "Courier";
                        } else if (fam.find("Times") != std::string::npos) {
                            fontName = (cf.hasFontWeight() && cf.fontWeight() == Bold) ? "Times-Bold" : "Times-Roman";
                        }
                    }
                    pdf.setFont(fontName, fontSize);

                    // Color
                    SwColor color = cf.hasForeground() ? cf.foreground() : SwColor{0, 0, 0};
                    pdf.setFillColor(color);

                    // Extract segment text
                    int offInFrag = segStart - fragPositions[fi].start;
                    int segLen = segEnd - segStart;
                    SwString segText = frag.text().substr(offInFrag, segLen);
                    int segWidth = measureText(segText, fontSize, fontName);

                    // Background highlight
                    if (cf.hasBackground()) {
                        pdf.fillRect(drawX, lineY, segWidth, lineHeight, cf.background());
                        // Restore text fill color (fillRect changed it to bg color)
                        pdf.setFillColor(color);
                    }

                    // Draw text
                    pdf.drawText(drawX, drawY, segText);

                    // Strikethrough
                    if (cf.fontStrikeOut()) {
                        pdf.setStrokeColor(color);
                        pdf.setLineWidth(0.5f);
                        int strikeY = lineY + lineHeight / 2;
                        pdf.drawLine(drawX, strikeY, drawX + segWidth, strikeY);
                    }

                    // Underline
                    if (cf.fontUnderline()) {
                        pdf.setStrokeColor(color);
                        pdf.setLineWidth(0.5f);
                        pdf.drawLine(drawX, drawY + 2, drawX + segWidth, drawY + 2);
                    }

                    drawX += segWidth;
                }

                lineY += lineHeight;
            }
        }
    }

    void renderTableToPdf(SwTextTable* tbl, const SwRect& blockRect, SwPdfWriter& pdf) {
        if (!tbl) return;
        int rows = tbl->rows();
        int cols = tbl->columns();
        if (rows == 0 || cols == 0) return;

        const SwTextTableFormat& tf = tbl->tableFormat();
        int bw = std::max(1, tf.borderWidth());
        int cellPad = tf.cellPadding();
        int cellWidth = (blockRect.width - (cols + 1) * bw) / cols;
        const int defaultFontSize = m_defaultFont.getPointSize() > 0 ? m_defaultFont.getPointSize() : 12;

        // Compute row heights based on content
        std::vector<int> rowHeights(rows, defaultFontSize + 2 * cellPad + 4);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                const SwTextTable::Cell& cell = tbl->cellAt(r, c);
                int textH = 0;
                for (int bi = 0; bi < cell.blocks.size(); ++bi) {
                    textH += defaultFontSize + 4;
                }
                rowHeights[r] = std::max(rowHeights[r], textH + 2 * cellPad);
            }
        }

        int curY = blockRect.y;
        for (int r = 0; r < rows; ++r) {
            int rh = rowHeights[r];
            for (int c = 0; c < cols; ++c) {
                int cx = blockRect.x + bw + c * (cellWidth + bw);
                int cy = curY;

                // Cell background
                SwTextTableCellFormat cf = tbl->cellFormat(r, c);
                if (cf.hasBackground()) {
                    pdf.fillRect(cx, cy, cellWidth, rh, cf.background());
                } else if (tf.hasBackground()) {
                    pdf.fillRect(cx, cy, cellWidth, rh, tf.background());
                }

                // Cell border
                pdf.setStrokeColor(tf.borderColor());
                pdf.setLineWidth(static_cast<float>(bw));
                pdf.drawRect(cx, cy, cellWidth, rh);

                // Cell text
                const SwTextTable::Cell& cell = tbl->cellAt(r, c);
                int textY = cy + cellPad + defaultFontSize;
                for (int bi = 0; bi < cell.blocks.size(); ++bi) {
                    const SwTextBlock& blk = cell.blocks[bi];
                    int textX = cx + cellPad;
                    for (int f = 0; f < blk.fragmentCount(); ++f) {
                        const SwTextFragment& frag = blk.fragmentAt(f);
                        const SwTextCharFormat& chf = frag.charFormat();

                        std::string fontName = "Helvetica";
                        int fontSize = defaultFontSize;
                        if (chf.hasFontPointSize() && chf.fontPointSize() > 0) fontSize = chf.fontPointSize();
                        if (chf.hasFontWeight() && chf.fontWeight() == Bold) fontName = "Helvetica-Bold";
                        if (chf.fontItalic()) {
                            fontName += (fontName.find("Bold") != std::string::npos) ? "Oblique" : "-Oblique";
                        }
                        pdf.setFont(fontName, fontSize);
                        SwColor color = chf.hasForeground() ? chf.foreground() : SwColor{0, 0, 0};
                        pdf.setFillColor(color);
                        pdf.drawText(textX, textY, frag.text());
                        textX += measureText(frag.text(), fontSize, fontName);
                    }
                    textY += defaultFontSize + 4;
                }
            }
            curY += rh + bw;
        }
    }

    // Helvetica glyph widths (per 1000 units, standard AFM data)
    static int helveticaCharWidth(unsigned char ch) {
        // Widths for WinAnsiEncoding characters (selected common ones)
        // Full table from Helvetica AFM; defaulting to 556 for unmapped
        static const int w[] = {
        //  0-31: control chars (use 0)
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        //  32-127: ASCII printable
            278,278,355,556,556,889,667,191,333,333,389,584,278,333,278,278, // sp ! " # $ % & ' ( ) * + , - . /
            556,556,556,556,556,556,556,556,556,556,278,278,584,584,584,556, // 0-9 : ; < = > ?
            1015,667,667,722,722,667,611,778,722,278,500,667,556,833,722,778, // @ A-O
            667,778,722,667,611,722,667,944,667,667,611,278,278,278,469,556, // P-Z [ \ ] ^ _
            333,556,556,500,556,556,278,556,556,222,222,500,222,833,556,556, // ` a-o
            556,556,333,500,278,556,500,722,500,500,500,334,260,334,584,0,   // p-z { | } ~ DEL
        //  128-159: Windows-1252 extras
            556,0,222,556,333,1000,556,556,333,1000,667,333,1000,0,611,0,
            0,222,222,333,333,350,556,1000,333,1000,500,333,944,0,500,667,
        //  160-255: Latin-1 supplement
            278,333,556,556,556,556,260,556,333,737,370,556,584,333,737,333,
            400,584,333,333,333,556,537,278,333,333,365,556,834,834,834,611,
            667,667,667,667,667,667,1000,722,667,667,667,667,278,278,278,278,
            722,722,778,778,778,778,778,584,778,722,722,722,722,667,667,611,
            556,556,556,556,556,556,889,500,556,556,556,556,278,278,278,278,
            556,556,556,556,556,556,556,584,611,556,556,556,556,500,556,500
        };
        return w[ch];
    }

    static int courierCharWidth(unsigned char /*ch*/) {
        return 600; // Courier is monospaced
    }

    static int timesCharWidth(unsigned char ch) {
        // Times-Roman AFM widths (selected)
        static const int w[] = {
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            250,333,408,500,500,833,778,180,333,333,500,564,250,333,250,278,
            500,500,500,500,500,500,500,500,500,500,278,278,564,564,564,444,
            921,722,667,667,722,611,556,722,722,333,389,722,611,889,722,722,
            556,722,667,556,611,722,722,944,722,722,611,333,278,333,469,500,
            333,444,500,444,500,444,333,500,500,278,278,500,278,778,500,500,
            500,500,333,389,278,500,500,722,500,500,444,480,200,480,541,0,
            500,0,333,500,444,1000,500,500,333,1000,556,333,889,0,611,0,
            0,333,333,444,444,350,500,1000,333,980,389,333,722,0,444,722,
            250,333,500,500,500,500,200,500,333,760,276,500,564,333,760,333,
            400,564,300,300,333,500,453,250,333,300,310,500,750,750,750,444,
            722,722,722,722,722,722,889,667,611,611,611,611,333,333,333,333,
            722,722,722,722,722,722,722,564,722,722,722,722,722,722,556,500,
            444,444,444,444,444,444,667,444,444,444,444,444,278,278,278,278,
            500,500,500,500,500,500,500,564,500,500,500,500,500,500,500,500
        };
        return w[ch];
    }

    int measureText(const SwString& text, int fontSize, const std::string& fontName = "Helvetica") const {
        if (text.isEmpty() || fontSize <= 0) return 0;
        bool isCourier = fontName.find("Courier") != std::string::npos;
        bool isTimes = fontName.find("Times") != std::string::npos;
        int totalUnits = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            unsigned char ch = static_cast<unsigned char>(text[i]);
            if (isCourier)     totalUnits += courierCharWidth(ch);
            else if (isTimes)  totalUnits += timesCharWidth(ch);
            else               totalUnits += helveticaCharWidth(ch);
        }
        // AFM widths are per 1000 units at 1pt; scale to fontSize
        return (totalUnits * fontSize + 500) / 1000;
    }

    int estimateTextWidth(const SwString& text, int fontSize) const {
        return measureText(text, fontSize, "Helvetica");
    }

    int estimateFragWidth(const SwTextFragment& frag, int fontSize) const {
        return estimateTextWidth(frag.text(), fontSize);
    }

    int estimateBlockWidth(const SwTextBlock& block) const {
        int w = 0;
        int fontSize = m_defaultFont.getPointSize() > 0 ? m_defaultFont.getPointSize() : 12;
        for (int f = 0; f < block.fragmentCount(); ++f) {
            w += estimateFragWidth(block.fragmentAt(f), fontSize);
        }
        return w;
    }
};
