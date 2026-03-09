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

#include "../types/SwString.h"
#include "../types/SwList.h"
#include "../types/Sw.h"
#include "SwFont.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// SwPdfWriter — low-level PDF file generator
//
// Generates valid PDF 1.4 files with text rendering, lines, rectangles,
// and font embedding (using the 14 standard PDF fonts).
//
// Usage:
//   SwPdfWriter pdf("output.pdf");
//   pdf.setPageSize(595, 842);  // A4 in points (72 dpi)
//   pdf.beginPage();
//   pdf.setFont("Helvetica", 12);
//   pdf.drawText(72, 750, "Hello World!");
//   pdf.endPage();
//   pdf.save();
// ---------------------------------------------------------------------------

class SwPdfWriter {
public:
    explicit SwPdfWriter(const SwString& filename)
        : m_filename(filename.toStdString()) {}

    ~SwPdfWriter() = default;

    // --- Page setup ---
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

    int pageWidth() const  { return m_pageWidth; }
    int pageHeight() const { return m_pageHeight; }

    int contentWidth() const  { return m_pageWidth - m_marginLeft - m_marginRight; }
    int contentHeight() const { return m_pageHeight - m_marginTop - m_marginBottom; }

    // --- Page management ---
    void beginPage() {
        m_currentPageContent.clear();
        m_pageStarted = true;
    }

    void endPage() {
        if (m_pageStarted) {
            m_pages.push_back(m_currentPageContent);
            m_currentPageContent.clear();
            m_pageStarted = false;
        }
    }

    int pageCount() const { return static_cast<int>(m_pages.size()); }

    // --- Font ---
    void setFont(const std::string& name, int size) {
        m_currentFontName = name;
        m_currentFontSize = size;
        int fontIdx = registerFont(name);
        std::ostringstream oss;
        oss << "/F" << fontIdx << " " << size << " Tf\n";
        m_currentPageContent += oss.str();
    }

    void setFontFromSwFont(const SwFont& font) {
        std::string name = "Helvetica";
        // Map common font families
        std::wstring family = font.getFamily();
        std::string familyStr(family.begin(), family.end());
        if (familyStr.find("Courier") != std::string::npos || familyStr.find("Mono") != std::string::npos) {
            name = font.getWeight() == Bold ? "Courier-Bold" : "Courier";
            if (font.isItalic()) name += (font.getWeight() == Bold) ? "BoldOblique" : "-Oblique";
        } else if (familyStr.find("Times") != std::string::npos || familyStr.find("Serif") != std::string::npos) {
            name = font.getWeight() == Bold ? "Times-Bold" : "Times-Roman";
            if (font.isItalic()) name = font.getWeight() == Bold ? "Times-BoldItalic" : "Times-Italic";
        } else {
            name = font.getWeight() == Bold ? "Helvetica-Bold" : "Helvetica";
            if (font.isItalic()) name += (font.getWeight() == Bold) ? "BoldOblique" : "-Oblique";
        }
        setFont(name, font.getPointSize() > 0 ? font.getPointSize() : 12);
    }

    // --- Colors ---
    void setFillColor(const SwColor& c) {
        std::ostringstream oss;
        oss << (c.r / 255.0) << " " << (c.g / 255.0) << " " << (c.b / 255.0) << " rg\n";
        m_currentPageContent += oss.str();
    }

    void setStrokeColor(const SwColor& c) {
        std::ostringstream oss;
        oss << (c.r / 255.0) << " " << (c.g / 255.0) << " " << (c.b / 255.0) << " RG\n";
        m_currentPageContent += oss.str();
    }

    void setLineWidth(float w) {
        std::ostringstream oss;
        oss << w << " w\n";
        m_currentPageContent += oss.str();
    }

    // --- Drawing primitives ---
    // Note: PDF coordinates have (0,0) at bottom-left, Y goes up.
    // These methods accept top-left coordinates and convert internally.

    void drawText(int x, int y, const SwString& text) {
        // Convert from top-left to PDF bottom-left coordinates
        int pdfY = m_pageHeight - y;
        std::string escaped = escapePdfString(text.toStdString());
        std::ostringstream oss;
        oss << "BT\n"
            << x << " " << pdfY << " Td\n"
            << "(" << escaped << ") Tj\n"
            << "ET\n";
        m_currentPageContent += oss.str();
    }

    void drawTextAt(int x, int y, const SwString& text, const SwFont& font, const SwColor& color) {
        setFillColor(color);
        setFontFromSwFont(font);
        drawText(x, y, text);
    }

    void drawLine(int x1, int y1, int x2, int y2) {
        int py1 = m_pageHeight - y1;
        int py2 = m_pageHeight - y2;
        std::ostringstream oss;
        oss << x1 << " " << py1 << " m " << x2 << " " << py2 << " l S\n";
        m_currentPageContent += oss.str();
    }

    void drawLine(int x1, int y1, int x2, int y2, const SwColor& color, int width) {
        setStrokeColor(color);
        setLineWidth(static_cast<float>(width));
        drawLine(x1, y1, x2, y2);
    }

    void drawRect(int x, int y, int w, int h) {
        int py = m_pageHeight - y - h;
        std::ostringstream oss;
        oss << x << " " << py << " " << w << " " << h << " re S\n";
        m_currentPageContent += oss.str();
    }

    void drawRect(int x, int y, int w, int h, const SwColor& borderColor, int borderWidth) {
        setStrokeColor(borderColor);
        setLineWidth(static_cast<float>(borderWidth));
        drawRect(x, y, w, h);
    }

    void fillRect(int x, int y, int w, int h) {
        int py = m_pageHeight - y - h;
        std::ostringstream oss;
        oss << x << " " << py << " " << w << " " << h << " re f\n";
        m_currentPageContent += oss.str();
    }

    void fillRect(int x, int y, int w, int h, const SwColor& color) {
        setFillColor(color);
        fillRect(x, y, w, h);
    }

    // --- Save state / Restore state ---
    void save() {
        m_currentPageContent += "q\n";
    }

    void restore() {
        m_currentPageContent += "Q\n";
    }

    // --- Write the PDF file ---
    bool writePdf() {
        if (m_pageStarted) endPage();
        if (m_pages.empty()) return false;

        std::ofstream file(m_filename, std::ios::binary);
        if (!file.is_open()) return false;

        // Object numbers:
        // 1 = Catalog
        // 2 = Pages
        // 3..N = Font objects (one per registered font)
        // N+1..M = Page objects
        // M+1..P = Content stream objects

        int nextObj = 1;
        std::vector<long> offsets; // byte offsets for xref

        auto writeObj = [&](const std::string& content) {
            offsets.push_back(file.tellp());
            file << nextObj << " 0 obj\n" << content << "\nendobj\n";
            ++nextObj;
        };

        // --- Header ---
        file << "%PDF-1.4\n%\xe2\xe3\xcf\xd3\n";

        // --- Object 1: Catalog ---
        writeObj("<< /Type /Catalog /Pages 2 0 R >>");

        // --- Object 2: Pages (placeholder, written later) ---
        long pagesOffset = file.tellp();
        offsets.push_back(pagesOffset);
        // Reserve space — we'll rewrite this object at the end
        std::string pagesPlaceholder(512, ' ');
        file << "2 0 obj\n" << pagesPlaceholder << "\nendobj\n";
        long pagesReservedEnd = file.tellp();
        ++nextObj;

        // --- Font objects ---
        int firstFontObj = nextObj;
        for (size_t i = 0; i < m_fonts.size(); ++i) {
            std::ostringstream oss;
            oss << "<< /Type /Font /Subtype /Type1 /BaseFont /" << m_fonts[i] << " /Encoding /WinAnsiEncoding >>";
            writeObj(oss.str());
        }

        // --- Font resource dictionary ---
        std::ostringstream fontDict;
        fontDict << "<< ";
        for (size_t i = 0; i < m_fonts.size(); ++i) {
            fontDict << "/F" << (i + 1) << " " << (firstFontObj + static_cast<int>(i)) << " 0 R ";
        }
        fontDict << ">>";
        std::string fontDictStr = fontDict.str();

        // --- Page and content stream objects ---
        int firstPageObj = nextObj;
        std::vector<int> pageObjNums;
        std::vector<int> contentObjNums;

        for (size_t p = 0; p < m_pages.size(); ++p) {
            int pageObjNum = nextObj;
            pageObjNums.push_back(pageObjNum);
            int contentObjNum = pageObjNum + 1;
            contentObjNums.push_back(contentObjNum);

            // Page object
            std::ostringstream pageOss;
            pageOss << "<< /Type /Page /Parent 2 0 R"
                    << " /MediaBox [0 0 " << m_pageWidth << " " << m_pageHeight << "]"
                    << " /Contents " << contentObjNum << " 0 R"
                    << " /Resources << /Font " << fontDictStr << " >>"
                    << " >>";
            writeObj(pageOss.str());

            // Content stream
            const std::string& content = m_pages[p];
            std::ostringstream contentOss;
            contentOss << "<< /Length " << content.size() << " >>\nstream\n" << content << "\nendstream";
            writeObj(contentOss.str());
        }

        // --- Rewrite Pages object ---
        long afterPages = file.tellp();
        file.seekp(pagesOffset);
        std::ostringstream pagesOss;
        pagesOss << "2 0 obj\n<< /Type /Pages /Kids [";
        for (size_t i = 0; i < pageObjNums.size(); ++i) {
            if (i > 0) pagesOss << " ";
            pagesOss << pageObjNums[i] << " 0 R";
        }
        pagesOss << "] /Count " << m_pages.size() << " >>\nendobj\n";
        std::string pagesStr = pagesOss.str();
        // Pad to fill reserved space exactly — must not overflow into next object
        size_t reservedSize = static_cast<size_t>(pagesReservedEnd - pagesOffset);
        while (pagesStr.size() < reservedSize) pagesStr += " ";
        pagesStr.resize(reservedSize); // ensure we never exceed the reserved space
        file << pagesStr;
        file.seekp(afterPages);

        // --- Cross-reference table ---
        long xrefOffset = file.tellp();
        file << "xref\n";
        file << "0 " << nextObj << "\n";
        file << "0000000000 65535 f \n";
        for (size_t i = 0; i < offsets.size(); ++i) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%010ld 00000 n \n", offsets[i]);
            file << buf;
        }

        // --- Trailer ---
        file << "trailer\n<< /Size " << nextObj
             << " /Root 1 0 R >>\n"
             << "startxref\n" << xrefOffset << "\n%%EOF\n";

        file.close();
        return true;
    }

private:
    std::string m_filename;
    int m_pageWidth{595};
    int m_pageHeight{842};
    int m_marginTop{72};
    int m_marginRight{72};
    int m_marginBottom{72};
    int m_marginLeft{72};

    std::string m_currentPageContent;
    bool m_pageStarted{false};
    std::vector<std::string> m_pages;

    // Fonts
    std::vector<std::string> m_fonts;
    std::string m_currentFontName{"Helvetica"};
    int m_currentFontSize{12};

    int registerFont(const std::string& name) {
        for (size_t i = 0; i < m_fonts.size(); ++i) {
            if (m_fonts[i] == name) return static_cast<int>(i) + 1;
        }
        m_fonts.push_back(name);
        return static_cast<int>(m_fonts.size());
    }

    // Decode a UTF-8 string into WinAnsiEncoding (single-byte per character),
    // then PDF-escape the result. Type1 fonts with /WinAnsiEncoding expect
    // single-byte character codes, not raw UTF-8 multi-byte sequences.
    static std::string escapePdfString(const std::string& s) {
        // First pass: convert UTF-8 to WinAnsi code points
        std::string winAnsi;
        winAnsi.reserve(s.size());
        for (size_t i = 0; i < s.size(); ) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x80) {
                winAnsi += s[i];
                ++i;
            } else {
                // Decode UTF-8 to Unicode code point
                unsigned int cp = 0;
                int extra = 0;
                if ((c & 0xE0) == 0xC0)      { cp = c & 0x1F; extra = 1; }
                else if ((c & 0xF0) == 0xE0)  { cp = c & 0x0F; extra = 2; }
                else if ((c & 0xF8) == 0xF0)  { cp = c & 0x07; extra = 3; }
                else { ++i; continue; } // invalid, skip
                for (int j = 0; j < extra && (i + 1 + j) < s.size(); ++j)
                    cp = (cp << 6) | (static_cast<unsigned char>(s[i + 1 + j]) & 0x3F);
                i += 1 + extra;

                // Map Unicode code point to WinAnsiEncoding byte
                unsigned char wa = unicodeToWinAnsi(cp);
                if (wa > 0) winAnsi += static_cast<char>(wa);
                else winAnsi += '?'; // unmappable character
            }
        }

        // Second pass: PDF string escaping
        std::string out;
        out.reserve(winAnsi.size() + 16);
        for (size_t i = 0; i < winAnsi.size(); ++i) {
            char c = winAnsi[i];
            switch (c) {
            case '(': out += "\\("; break;
            case ')': out += "\\)"; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) >= 32) {
                    out += c;
                } else {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\%03o", static_cast<unsigned char>(c));
                    out += buf;
                }
                break;
            }
        }
        return out;
    }

    // Map a Unicode code point to the WinAnsiEncoding byte value.
    // Returns 0 if the character has no WinAnsi mapping.
    static unsigned char unicodeToWinAnsi(unsigned int cp) {
        // ASCII range maps directly
        if (cp < 0x80) return static_cast<unsigned char>(cp);
        // Latin-1 supplement (U+00A0..U+00FF) maps directly in WinAnsi
        if (cp >= 0x00A0 && cp <= 0x00FF) return static_cast<unsigned char>(cp);
        // Special mappings for the 0x80..0x9F range in WinAnsi
        switch (cp) {
        case 0x20AC: return 0x80; // €
        case 0x201A: return 0x82; // ‚
        case 0x0192: return 0x83; // ƒ
        case 0x201E: return 0x84; // „
        case 0x2026: return 0x85; // …
        case 0x2020: return 0x86; // †
        case 0x2021: return 0x87; // ‡
        case 0x02C6: return 0x88; // ˆ
        case 0x2030: return 0x89; // ‰
        case 0x0160: return 0x8A; // Š
        case 0x2039: return 0x8B; // ‹
        case 0x0152: return 0x8C; // Œ
        case 0x017D: return 0x8E; // Ž
        case 0x2018: return 0x91; // '
        case 0x2019: return 0x92; // '
        case 0x201C: return 0x93; // "
        case 0x201D: return 0x94; // "
        case 0x2022: return 0x95; // • (bullet)
        case 0x2013: return 0x96; // – (en dash)
        case 0x2014: return 0x97; // — (em dash)
        case 0x02DC: return 0x98; // ˜
        case 0x2122: return 0x99; // ™
        case 0x0161: return 0x9A; // š
        case 0x203A: return 0x9B; // ›
        case 0x0153: return 0x9C; // œ
        case 0x017E: return 0x9E; // ž
        case 0x0178: return 0x9F; // Ÿ
        default: return 0; // unmappable
        }
    }
};
