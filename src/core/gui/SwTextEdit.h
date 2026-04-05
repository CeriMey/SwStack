#pragma once

/**
 * @file src/core/gui/SwTextEdit.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwTextEdit in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the text edit interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwTextEdit.
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

#include "SwPlainTextEdit.h"
#include "SwTextDocument.h"
#include "SwTextCursor.h"
#include "SwTextDocumentWriter.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

class SwTextEdit : public SwPlainTextEdit {
    SW_OBJECT(SwTextEdit, SwPlainTextEdit)

public:
    /**
     * @brief Constructs a `SwTextEdit` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwTextEdit(SwWidget* parent = nullptr);

    /**
     * @brief Sets the text.
     * @param text Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setText(const SwString& text) { setPlainText(text); }

    /**
     * @brief Sets the plain Text.
     * @param text Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPlainText(const SwString& text);
    /**
     * @brief Performs the `appendPlainText` operation.
     * @param text Value passed to the method.
     */
    void appendPlainText(const SwString& text);
    /**
     * @brief Clears the current object state.
     */
    void clear();

    /**
     * @brief Sets the html.
     * @param html Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setHtml(const SwString& html);
    /**
     * @brief Returns the current to Html.
     * @return The current to Html.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString toHtml() const;

    // --- Document model integration (Qt-compatible API) ---
    SwTextDocument* document() const;
    void setDocument(SwTextDocument* doc);
    SwTextCursor textCursor() const;

    // --- PDF export ---
    bool print(const SwString& pdfFilename, const SwFont& font = SwFont()) const;

protected:
    /**
     * @brief Returns the current capture Edit State.
     * @return The current capture Edit State.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    EditState captureEditState() const override;

    /**
     * @brief Handles the paint Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void paintEvent(PaintEvent* event) override;
    void wheelEvent(WheelEvent* event) override;
    /**
     * @brief Updates the cursor From Position managed by the object.
     * @param px Value passed to the method.
     * @param py Value passed to the method.
     */
    void updateCursorFromPosition(int px, int py) override;

    /**
     * @brief Performs the `insertTextAt` operation.
     * @param pos Position used by the operation.
     * @param text Value passed to the method.
     */
    void insertTextAt(size_t pos, const SwString& text) override;
    /**
     * @brief Performs the `eraseTextAt` operation.
     * @param pos Position used by the operation.
     * @param len Value passed to the method.
     */
    void eraseTextAt(size_t pos, size_t len) override;

private:
    struct TextFormat {
        bool bold{false};
        bool italic{false};
        bool underline{false};
        bool strikethrough{false};
        bool hasColor{false};
        SwColor color{0, 0, 0};
        bool hasBgColor{false};
        SwColor bgColor{255, 255, 255};
        int fontSize{0};            // 0 = inherit from widget font
        std::string fontFamily;     // empty = inherit
        std::string href;           // non-empty = hyperlink

        bool operator==(const TextFormat& other) const;
        bool operator!=(const TextFormat& other) const { return !(*this == other); }
    };

    struct Run {
        SwString text;
        TextFormat format;
    };

    struct LineFormat {
        int headingLevel{0};        // 0=normal, 1-6 = h1-h6
        enum ListType { NoList, Unordered, Ordered };
        ListType listType{NoList};
        int listIndex{0};           // 1-based for ordered lists
        int indentLevel{0};
        enum Align { AlignLeft, AlignCenter, AlignRight };
        Align alignment{AlignLeft};
        bool isHr{false};           // horizontal rule
    };

    mutable std::vector<Run> m_runs;
    std::vector<LineFormat> m_lineFormats;

    static std::string toLower(std::string s);
    static std::string trim(std::string s);

    static SwString escapeHtmlWithLineBreaks(const SwString& text);
    static SwString colorToCss(const SwColor& c);
    static bool parseInt(const std::string& s, int& out);
    static bool parseCssColor(const std::string& color, SwColor& out);
    static SwString decodeHtmlEntities(const std::string& in);
    static TextFormat parseInlineStyle(const std::string& style, TextFormat base);
    static std::string extractAttribute(const std::string& attrs, const std::string& name);
    static std::vector<Run> parseHtml(const SwString& html);

    void normalizeRuns();
    void rebuildRunsFromPlainText();
    bool runsMatchPlainText() const;
    void ensureRunsInSync() const;

    TextFormat formatForInsertionAt(size_t pos) const;
    TextFormat formatAtChar(size_t pos) const;
    SwFont fontForFormat(const TextFormat& fmt) const;

    void insertIntoRuns(size_t pos, char ch, const TextFormat& fmt);
    void eraseFromRuns(size_t pos);

    void drawRichLine(SwPainter* painter,
                      const SwRect& inner,
                      int y,
                      int lh,
                      size_t lineStart,
                      size_t lineLen,
                      const SwColor& defaultTextColor) const;
    int richTextWidth(size_t start, size_t length, int defaultWidth) const;
    size_t richCharacterIndexAtPosition(size_t lineStart, size_t lineLen, int relativeX, int defaultWidth) const;

    int lineHeightForLine(int lineIndex) const;
    int yOffsetForLine(int lineIndex, int firstVisible) const;
    int lineIndexAtY(int relativeY, int firstVisible) const;
    int visibleLineCount(int firstVisible, int availableHeight) const;
    LineFormat lineFormatAt(int lineIndex) const;
    int headingFontSize(int headingLevel) const;
    int listIndentPx() const { return 24; }

    struct TableCell {
        SwString text;
        TextFormat format;
        bool isHeader{false};
        bool hasBg{false};
        SwColor bgColor{255,255,255};
    };
    struct EmbeddedTable {
        int lineIndex{-1};          // which logical line this table sits on
        int borderWidth{1};
        int cellPadding{4};
        SwColor borderColor{100,100,100};
        bool hasBg{false};
        SwColor bgColor{255,255,255};
        std::vector<std::vector<TableCell>> rows;
        int columns() const {
            int mx = 0;
            for (auto& r : rows) mx = std::max(mx, static_cast<int>(r.size()));
            return mx;
        }
    };
    std::vector<EmbeddedTable> m_tables;

    struct ParseResult {
        std::vector<Run> runs;
        std::vector<LineFormat> lineFormats;
        std::vector<EmbeddedTable> tables;
    };
    static ParseResult parseHtmlFull(const SwString& html);

    const EmbeddedTable* tableForLogicalLine(int logicalLine) const {
        for (size_t i = 0; i < m_tables.size(); ++i) {
            if (m_tables[i].lineIndex == logicalLine) return &m_tables[i];
        }
        return nullptr;
    }

    size_t documentLength_() const {
        return m_pieceTable.totalLength();
    }

    bool documentIsEmpty_() const {
        return m_pieceTable.isEmpty();
    }

    SwString documentText_() const {
        return m_pieceTable.toPlainText();
    }

    int logicalLineCount_() const {
        return m_pieceTable.lineCount();
    }

    size_t logicalLineStart_(int lineIndex) const {
        if (lineIndex < 0) {
            return 0;
        }
        return m_pieceTable.lineStart(lineIndex);
    }

    size_t logicalLineLength_(int lineIndex) const {
        if (lineIndex < 0 || lineIndex >= logicalLineCount_()) {
            return 0;
        }
        return m_pieceTable.lineLength(lineIndex);
    }

    SwString logicalLineText_(int lineIndex) const {
        if (lineIndex < 0 || lineIndex >= logicalLineCount_()) {
            return SwString();
        }
        return m_pieceTable.lineContent(lineIndex);
    }

    const EmbeddedTable* tableForLine(int visualLineIndex) const {
        if (visualLineIndex < 0 || visualLineIndex >= logicalLineCount_())
            return nullptr;
        const size_t charPos = logicalLineStart_(visualLineIndex);
        const int logicalLine = m_pieceTable.lineForOffset(charPos);
        return tableForLogicalLine(logicalLine);
    }

    int tableHeight(const EmbeddedTable& tbl) const {
        int fontSize = getFont().getPointSize() > 0 ? getFont().getPointSize() : 12;
        int rowH = fontSize + 2 * tbl.cellPadding + 4;
        return static_cast<int>(tbl.rows.size()) * rowH + (static_cast<int>(tbl.rows.size()) + 1) * tbl.borderWidth;
    }
};

inline bool SwTextEdit::TextFormat::operator==(const TextFormat& other) const {
    if (bold != other.bold || italic != other.italic || underline != other.underline ||
        strikethrough != other.strikethrough || hasColor != other.hasColor ||
        hasBgColor != other.hasBgColor || fontSize != other.fontSize ||
        fontFamily != other.fontFamily || href != other.href) {
        return false;
    }
    if (hasColor && (color.r != other.color.r || color.g != other.color.g || color.b != other.color.b)) {
        return false;
    }
    if (hasBgColor && (bgColor.r != other.bgColor.r || bgColor.g != other.bgColor.g || bgColor.b != other.bgColor.b)) {
        return false;
    }
    return true;
}

inline std::string SwTextEdit::toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

inline std::string SwTextEdit::trim(std::string s) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && isSpace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && isSpace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

inline SwString SwTextEdit::escapeHtmlWithLineBreaks(const SwString& text) {
    SwString out;
    out.reserve(text.size() + 16);
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        switch (ch) {
        case '&': out.append("&amp;"); break;
        case '<': out.append("&lt;"); break;
        case '>': out.append("&gt;"); break;
        case '\"': out.append("&quot;"); break;
        case '\'': out.append("&#39;"); break;
        case '\n': out.append("<br/>"); break;
        default: out.append(ch); break;
        }
    }
    return out;
}

inline SwString SwTextEdit::colorToCss(const SwColor& c) {
    std::ostringstream oss;
    oss << "#";
    auto hex2 = [&](int v) {
        const char* digits = "0123456789ABCDEF";
        v = std::max(0, std::min(255, v));
        oss << digits[(v >> 4) & 0xF] << digits[v & 0xF];
    };
    hex2(c.r);
    hex2(c.g);
    hex2(c.b);
    return SwString(oss.str());
}

inline bool SwTextEdit::parseInt(const std::string& s, int& out) {
    try {
        size_t idx = 0;
        int v = std::stoi(s, &idx);
        if (idx == 0) {
            return false;
        }
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

inline bool SwTextEdit::parseCssColor(const std::string& color, SwColor& out) {
    std::string c = trim(toLower(color));
    if (c.empty()) {
        return false;
    }
    auto hex = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
        if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
        return -1;
    };
    if (c[0] == '#' && c.size() == 7) {
        const int r1 = hex(c[1]), r2 = hex(c[2]);
        const int g1 = hex(c[3]), g2 = hex(c[4]);
        const int b1 = hex(c[5]), b2 = hex(c[6]);
        if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) {
            return false;
        }
        out = SwColor{r1 * 16 + r2, g1 * 16 + g2, b1 * 16 + b2};
        return true;
    }
    if (c[0] == '#' && c.size() == 4) {
        const int r = hex(c[1]), g = hex(c[2]), b = hex(c[3]);
        if (r < 0 || g < 0 || b < 0) {
            return false;
        }
        out = SwColor{r * 17, g * 17, b * 17};
        return true;
    }
    if (c.rfind("rgb(", 0) == 0 && c.back() == ')') {
        std::string inner = c.substr(4, c.size() - 5);
        std::stringstream ss(inner);
        std::string item;
        int rgb[3] = {0, 0, 0};
        int i = 0;
        while (std::getline(ss, item, ',') && i < 3) {
            item = trim(item);
            int v = 0;
            if (!parseInt(item, v)) {
                return false;
            }
            rgb[i++] = std::max(0, std::min(255, v));
        }
        if (i != 3) {
            return false;
        }
        out = SwColor{rgb[0], rgb[1], rgb[2]};
        return true;
    }

    if (c == "black") {
        out = SwColor{0, 0, 0};
        return true;
    }
    if (c == "white") {
        out = SwColor{255, 255, 255};
        return true;
    }
    if (c == "red") {
        out = SwColor{255, 0, 0};
        return true;
    }
    if (c == "green") {
        out = SwColor{0, 128, 0};
        return true;
    }
    if (c == "blue") {
        out = SwColor{0, 0, 255};
        return true;
    }
    if (c == "gray" || c == "grey") {
        out = SwColor{128, 128, 128};
        return true;
    }
    if (c == "yellow") {
        out = SwColor{255, 255, 0};
        return true;
    }
    if (c == "orange") {
        out = SwColor{255, 165, 0};
        return true;
    }
    if (c == "purple") {
        out = SwColor{128, 0, 128};
        return true;
    }
    if (c == "cyan" || c == "aqua") {
        out = SwColor{0, 255, 255};
        return true;
    }
    if (c == "magenta" || c == "fuchsia") {
        out = SwColor{255, 0, 255};
        return true;
    }
    if (c == "navy") {
        out = SwColor{0, 0, 128};
        return true;
    }
    if (c == "teal") {
        out = SwColor{0, 128, 128};
        return true;
    }
    if (c == "maroon") {
        out = SwColor{128, 0, 0};
        return true;
    }
    if (c == "olive") {
        out = SwColor{128, 128, 0};
        return true;
    }
    if (c == "silver") {
        out = SwColor{192, 192, 192};
        return true;
    }
    if (c == "lime") {
        out = SwColor{0, 255, 0};
        return true;
    }

    return false;
}

inline SwString SwTextEdit::decodeHtmlEntities(const std::string& in) {
    std::string out;
    out.reserve(in.size());

    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '&') {
            out.push_back(in[i]);
            continue;
        }

        const size_t semi = in.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 16) {
            out.push_back(in[i]);
            continue;
        }

        const std::string ent = in.substr(i + 1, semi - i - 1);
        if (ent == "lt") {
            out.push_back('<');
        } else if (ent == "gt") {
            out.push_back('>');
        } else if (ent == "amp") {
            out.push_back('&');
        } else if (ent == "quot") {
            out.push_back('\"');
        } else if (ent == "apos") {
            out.push_back('\'');
        } else if (ent == "nbsp") {
            out.push_back(' ');
        } else if (!ent.empty() && ent[0] == '#') {
            int code = 0;
            bool ok = false;
            if (ent.size() > 2 && (ent[1] == 'x' || ent[1] == 'X')) {
                try {
                    size_t idx = 0;
                    code = std::stoi(ent.substr(2), &idx, 16);
                    ok = idx > 0;
                } catch (...) {
                    ok = false;
                }
            } else {
                ok = parseInt(ent.substr(1), code);
            }

            if (ok && code >= 0 && code <= 0x7F) {
                out.push_back(static_cast<char>(code));
            } else {
                out.push_back('?');
            }
        } else {
            out.append("&");
            out.append(ent);
            out.append(";");
        }

        i = semi;
    }

    return SwString(out);
}

inline SwTextEdit::TextFormat SwTextEdit::parseInlineStyle(const std::string& style, TextFormat base) {
    std::stringstream ss(style);
    std::string decl;
    while (std::getline(ss, decl, ';')) {
        decl = trim(decl);
        if (decl.empty()) {
            continue;
        }
        const size_t colon = decl.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string key = toLower(trim(decl.substr(0, colon)));
        std::string value = trim(decl.substr(colon + 1));
        std::string valueLower = toLower(value);

        if (key == "color") {
            SwColor c{0, 0, 0};
            if (parseCssColor(value, c)) {
                base.hasColor = true;
                base.color = c;
            }
        } else if (key == "background-color" || key == "background") {
            SwColor c{255, 255, 255};
            if (parseCssColor(value, c)) {
                base.hasBgColor = true;
                base.bgColor = c;
            }
        } else if (key == "font-weight") {
            if (valueLower.find("bold") != std::string::npos) {
                base.bold = true;
            } else {
                int w = 0;
                if (parseInt(valueLower, w)) {
                    base.bold = (w >= 600);
                }
            }
        } else if (key == "font-style") {
            base.italic = (valueLower.find("italic") != std::string::npos);
        } else if (key == "text-decoration" || key == "text-decoration-line") {
            base.underline = (valueLower.find("underline") != std::string::npos);
            if (valueLower.find("line-through") != std::string::npos) {
                base.strikethrough = true;
            }
        } else if (key == "font-size") {
            int sz = 0;
            if (parseInt(valueLower, sz) && sz > 0) {
                base.fontSize = sz;
            } else if (valueLower == "xx-small") {
                base.fontSize = 7;
            } else if (valueLower == "x-small") {
                base.fontSize = 8;
            } else if (valueLower == "small") {
                base.fontSize = 10;
            } else if (valueLower == "medium") {
                base.fontSize = 12;
            } else if (valueLower == "large") {
                base.fontSize = 14;
            } else if (valueLower == "x-large") {
                base.fontSize = 18;
            } else if (valueLower == "xx-large") {
                base.fontSize = 24;
            }
        } else if (key == "font-family") {
            std::string fam = value;
            // Remove quotes
            if (fam.size() >= 2 && (fam.front() == '\'' || fam.front() == '\"')) {
                fam = fam.substr(1, fam.size() - 2);
            }
            // Take first family if comma-separated
            const size_t comma = fam.find(',');
            if (comma != std::string::npos) {
                fam = trim(fam.substr(0, comma));
            }
            if (!fam.empty()) {
                base.fontFamily = fam;
            }
        }
    }
    return base;
}

inline std::string SwTextEdit::extractAttribute(const std::string& attrs, const std::string& name) {
    const std::string needle = toLower(name) + "=";
    const std::string lower = toLower(attrs);
    size_t pos = lower.find(needle);
    if (pos == std::string::npos) {
        return "";
    }
    pos += needle.size();
    if (pos >= attrs.size()) {
        return "";
    }

    const char quote = attrs[pos];
    if (quote == '\'' || quote == '\"') {
        size_t end = attrs.find(quote, pos + 1);
        if (end == std::string::npos) {
            return "";
        }
        return attrs.substr(pos + 1, end - pos - 1);
    }

    size_t end = attrs.find_first_of(" \t\r\n>", pos);
    if (end == std::string::npos) {
        end = attrs.size();
    }
    return attrs.substr(pos, end - pos);
}

inline std::vector<SwTextEdit::Run> SwTextEdit::parseHtml(const SwString& html) {
    ParseResult result = parseHtmlFull(html);
    return result.runs;
}

inline SwTextEdit::ParseResult SwTextEdit::parseHtmlFull(const SwString& html) {
    ParseResult result;
    std::vector<Run>& runs = result.runs;
    std::vector<LineFormat>& lineFormats = result.lineFormats;

    TextFormat fmt{};
    std::vector<TextFormat> fmtStack;
    std::string s = html.toStdString();

    LineFormat currentLineFmt{};

    // List context stack
    struct ListContext {
        LineFormat::ListType type;
        int counter;
    };
    std::vector<ListContext> listStack;

    std::string buffer;
    buffer.reserve(s.size());

    auto flush = [&]() {
        if (buffer.empty()) {
            return;
        }
        Run run;
        run.text = decodeHtmlEntities(buffer);
        run.format = fmt;
        runs.push_back(run);
        buffer.clear();
    };

    auto emitNewline = [&]() {
        flush();
        runs.push_back(Run{SwString("\n"), fmt});
        lineFormats.push_back(currentLineFmt);
        // Reset line format but keep list context
        currentLineFmt = LineFormat{};
        if (!listStack.empty()) {
            currentLineFmt.indentLevel = static_cast<int>(listStack.size());
        }
    };

    auto isWhitespaceOnlyStr = [](const SwString& str) -> bool {
        for (size_t j = 0; j < str.size(); ++j) {
            char c = str[j];
            if (c == '\n') return false; // newline runs are meaningful
            if (!std::isspace(static_cast<unsigned char>(c))) return false;
        }
        return true;
    };

    auto ensureNewline = [&]() {
        // Discard whitespace-only buffer before block boundaries
        {
            bool allWs = true;
            for (size_t j = 0; j < buffer.size(); ++j) {
                if (!std::isspace(static_cast<unsigned char>(buffer[j]))) { allWs = false; break; }
            }
            if (allWs) buffer.clear();
        }
        // Pop trailing whitespace-only runs (inter-tag whitespace already flushed by flush())
        while (!runs.empty() && !runs.back().text.isEmpty() && isWhitespaceOnlyStr(runs.back().text)) {
            runs.pop_back();
        }
        if (!runs.empty()) {
            const Run& last = runs.back();
            if (!last.text.isEmpty() && !last.text.endsWith(SwString("\n"))) {
                emitNewline();
            }
        }
    };

    auto pushFmt = [&]() { fmtStack.push_back(fmt); };
    auto popFmt = [&]() {
        if (!fmtStack.empty()) {
            fmt = fmtStack.back();
            fmtStack.pop_back();
        }
    };

    // Heading font sizes (approximate pt values like Qt)
    auto headingFontPt = [](int level) -> int {
        switch (level) {
        case 1: return 24;
        case 2: return 18;
        case 3: return 14;
        case 4: return 12;
        case 5: return 10;
        case 6: return 8;
        default: return 0;
        }
    };

    // Helper: check if text is only whitespace
    auto isWhitespaceOnly = [](const std::string& str) -> bool {
        for (size_t j = 0; j < str.size(); ++j) {
            if (!std::isspace(static_cast<unsigned char>(str[j]))) return false;
        }
        return true;
    };

    for (size_t i = 0; i < s.size();) {
        if (s[i] != '<') {
            const size_t next = s.find('<', i);
            std::string text = s.substr(i, (next == std::string::npos) ? std::string::npos : (next - i));
            // Collapse inter-tag whitespace: if text between tags is all whitespace,
            // collapse to a single space (HTML whitespace normalization)
            if (isWhitespaceOnly(text)) {
                if (!text.empty()) buffer.append(" ");
            } else {
                // Collapse internal whitespace runs (newlines + spaces → single space)
                std::string collapsed;
                collapsed.reserve(text.size());
                bool lastWasSpace = false;
                for (size_t j = 0; j < text.size(); ++j) {
                    if (std::isspace(static_cast<unsigned char>(text[j]))) {
                        if (!lastWasSpace) { collapsed += ' '; lastWasSpace = true; }
                    } else {
                        collapsed += text[j];
                        lastWasSpace = false;
                    }
                }
                buffer.append(collapsed);
            }
            i = (next == std::string::npos) ? s.size() : next;
            continue;
        }

        const size_t close = s.find('>', i + 1);
        if (close == std::string::npos) {
            buffer.push_back(s[i]);
            ++i;
            continue;
        }

        std::string tag = trim(s.substr(i + 1, close - i - 1));
        i = close + 1;

        if (tag.empty()) {
            continue;
        }

        bool isClosing = false;
        if (!tag.empty() && tag[0] == '/') {
            isClosing = true;
            tag = trim(tag.substr(1));
        }

        bool selfClosing = false;
        if (!tag.empty() && tag.back() == '/') {
            selfClosing = true;
            tag = trim(tag.substr(0, tag.size() - 1));
        }

        std::string tagName;
        std::string attrs;
        const size_t ws = tag.find_first_of(" \t\r\n");
        if (ws == std::string::npos) {
            tagName = toLower(tag);
        } else {
            tagName = toLower(tag.substr(0, ws));
            attrs = tag.substr(ws + 1);
        }

        // --- Self-closing / void elements ---
        if (tagName == "br") {
            emitNewline();
            continue;
        }

        if (tagName == "hr") {
            ensureNewline();
            currentLineFmt.isHr = true;
            emitNewline();
            continue;
        }

        if (tagName == "img") {
            flush();
            // Emit alt text or placeholder
            std::string alt = extractAttribute(attrs, "alt");
            if (alt.empty()) {
                alt = "[image]";
            }
            runs.push_back(Run{SwString(alt), fmt});
            continue;
        }

        // --- Block elements ---
        if (tagName == "p" || tagName == "div") {
            if (isClosing) {
                ensureNewline();
            } else {
                ensureNewline();
                // Parse text-align from style
                const std::string styleAttr = extractAttribute(attrs, "style");
                if (!styleAttr.empty()) {
                    std::string lower = toLower(styleAttr);
                    if (lower.find("text-align") != std::string::npos) {
                        if (lower.find("center") != std::string::npos) {
                            currentLineFmt.alignment = LineFormat::AlignCenter;
                        } else if (lower.find("right") != std::string::npos) {
                            currentLineFmt.alignment = LineFormat::AlignRight;
                        }
                    }
                }
                const std::string alignAttr = toLower(extractAttribute(attrs, "align"));
                if (alignAttr == "center") {
                    currentLineFmt.alignment = LineFormat::AlignCenter;
                } else if (alignAttr == "right") {
                    currentLineFmt.alignment = LineFormat::AlignRight;
                }
            }
            continue;
        }

        // --- Headings h1-h6 ---
        if (tagName.size() == 2 && tagName[0] == 'h' && tagName[1] >= '1' && tagName[1] <= '6') {
            const int level = tagName[1] - '0';
            flush();
            if (isClosing) {
                popFmt();
                ensureNewline();
            } else {
                ensureNewline();
                currentLineFmt.headingLevel = level;
                pushFmt();
                fmt.bold = true;
                fmt.fontSize = headingFontPt(level);
            }
            continue;
        }

        // --- Lists ---
        if (tagName == "ul") {
            flush();
            if (isClosing) {
                if (!listStack.empty()) {
                    listStack.pop_back();
                }
                ensureNewline();
            } else {
                ensureNewline();
                listStack.push_back({LineFormat::Unordered, 0});
            }
            continue;
        }

        if (tagName == "ol") {
            flush();
            if (isClosing) {
                if (!listStack.empty()) {
                    listStack.pop_back();
                }
                ensureNewline();
            } else {
                ensureNewline();
                int start = 1;
                const std::string startAttr = extractAttribute(attrs, "start");
                if (!startAttr.empty()) {
                    parseInt(startAttr, start);
                }
                listStack.push_back({LineFormat::Ordered, start - 1});
            }
            continue;
        }

        if (tagName == "li") {
            flush();
            if (isClosing) {
                ensureNewline();
            } else {
                ensureNewline();
                if (!listStack.empty()) {
                    ListContext& ctx = listStack.back();
                    currentLineFmt.listType = ctx.type;
                    currentLineFmt.indentLevel = static_cast<int>(listStack.size());
                    if (ctx.type == LineFormat::Ordered) {
                        ctx.counter++;
                        currentLineFmt.listIndex = ctx.counter;
                    }
                }
            }
            continue;
        }

        // --- Blockquote ---
        if (tagName == "blockquote") {
            flush();
            if (isClosing) {
                ensureNewline();
            } else {
                ensureNewline();
                currentLineFmt.indentLevel = std::max(currentLineFmt.indentLevel, 1);
                if (!listStack.empty()) {
                    currentLineFmt.indentLevel += static_cast<int>(listStack.size());
                }
            }
            continue;
        }

        // --- Pre / Code ---
        if (tagName == "pre" || tagName == "code") {
            flush();
            if (isClosing) {
                popFmt();
            } else {
                pushFmt();
                fmt.fontFamily = "Courier New";
                if (tagName == "pre") {
                    ensureNewline();
                }
            }
            if (selfClosing) {
                popFmt();
            }
            continue;
        }

        // --- Table parsing ---
        if (tagName == "table" && !isClosing) {
            ensureNewline();

            // Parse table attributes
            int tblBorder = 1;
            int tblPad = 4;
            SwColor tblBorderColor{100,100,100};
            bool tblHasBg = false;
            SwColor tblBg{255,255,255};

            std::string borderAttr = extractAttribute(attrs, "border");
            if (!borderAttr.empty()) parseInt(borderAttr, tblBorder);
            std::string cellpadAttr = extractAttribute(attrs, "cellpadding");
            if (!cellpadAttr.empty()) parseInt(cellpadAttr, tblPad);
            std::string bgAttr = extractAttribute(attrs, "bgcolor");
            if (!bgAttr.empty()) { parseCssColor(bgAttr, tblBg); tblHasBg = true; }
            std::string styleAttr = extractAttribute(attrs, "style");
            if (!styleAttr.empty()) {
                std::string lower = toLower(styleAttr);
                if (lower.find("background-color") != std::string::npos) {
                    size_t pos = lower.find("background-color");
                    size_t col = lower.find(':', pos);
                    size_t semi = lower.find(';', col);
                    if (col != std::string::npos) {
                        std::string val = trim(styleAttr.substr(col+1, (semi==std::string::npos ? styleAttr.size() : semi) - col - 1));
                        if (parseCssColor(val, tblBg)) tblHasBg = true;
                    }
                }
            }

            // Pre-scan to collect table content
            EmbeddedTable etable;
            etable.borderWidth = tblBorder;
            etable.cellPadding = tblPad;
            etable.borderColor = tblBorderColor;
            etable.hasBg = tblHasBg;
            etable.bgColor = tblBg;

            std::vector<TableCell> curRow;
            TableCell curCell;
            bool inCell = false;
            bool inRow = false;
            int tableDepth = 1;

            while (i < s.size() && tableDepth > 0) {
                if (s[i] != '<') {
                    if (inCell) curCell.text.append(s[i]);
                    ++i;
                    continue;
                }
                size_t tc = s.find('>', i + 1);
                if (tc == std::string::npos) { ++i; continue; }
                std::string itag = trim(s.substr(i + 1, tc - i - 1));
                i = tc + 1;
                if (itag.empty()) continue;
                bool iClose = itag[0] == '/';
                if (iClose) itag = trim(itag.substr(1));
                bool iSelf = !itag.empty() && itag.back() == '/';
                if (iSelf) itag = trim(itag.substr(0, itag.size() - 1));
                std::string iName, iAttrs;
                size_t iws = itag.find_first_of(" \t\r\n");
                if (iws == std::string::npos) { iName = toLower(itag); }
                else { iName = toLower(itag.substr(0, iws)); iAttrs = itag.substr(iws + 1); }

                if (iName == "table" && !iClose) { tableDepth++; continue; }
                if (iName == "table" && iClose) {
                    tableDepth--;
                    if (tableDepth > 0) continue;
                    if (inCell) { curRow.push_back(curCell); inCell = false; }
                    if (inRow && !curRow.empty()) etable.rows.push_back(curRow);
                    break;
                }
                if (tableDepth > 1) continue; // skip nested tables

                if (iName == "tr") {
                    if (!iClose) { curRow.clear(); inRow = true; }
                    else {
                        if (inCell) { curRow.push_back(curCell); inCell = false; }
                        if (!curRow.empty()) etable.rows.push_back(curRow);
                        inRow = false;
                    }
                    continue;
                }
                if (iName == "td" || iName == "th") {
                    if (!iClose) {
                        if (inCell) curRow.push_back(curCell);
                        curCell = TableCell();
                        curCell.isHeader = (iName == "th");
                        curCell.format = fmt;
                        if (curCell.isHeader) curCell.format.bold = true;
                        // Parse bg
                        std::string cbg = extractAttribute(iAttrs, "bgcolor");
                        if (!cbg.empty() && parseCssColor(cbg, curCell.bgColor)) curCell.hasBg = true;
                        std::string cstyle = extractAttribute(iAttrs, "style");
                        if (!cstyle.empty()) {
                            std::string cl = toLower(cstyle);
                            if (cl.find("background-color") != std::string::npos) {
                                size_t cp = cl.find("background-color");
                                size_t cc = cl.find(':', cp);
                                size_t cs = cl.find(';', cc);
                                if (cc != std::string::npos) {
                                    std::string cv = trim(cstyle.substr(cc+1, (cs==std::string::npos ? cstyle.size() : cs) - cc - 1));
                                    if (parseCssColor(cv, curCell.bgColor)) curCell.hasBg = true;
                                }
                            }
                        }
                        inCell = true;
                    } else {
                        if (inCell) curRow.push_back(curCell);
                        inCell = false;
                    }
                    continue;
                }
                // Handle inline formatting inside cells — just pass through as text for now
                if (inCell) {
                    if (iName == "b" || iName == "strong") {
                        // Already handled via isHeader — just skip tags
                    } else if (iName == "br") {
                        curCell.text.append('\n');
                    }
                    // Other tags are ignored, text content is captured above
                }
            }

            // Store the line index where this table will appear
            // Count current newlines in runs to determine the line index
            int lineIdx = 0;
            for (const auto& r : runs) {
                for (size_t ci = 0; ci < r.text.size(); ++ci) {
                    if (r.text[ci] == '\n') lineIdx++;
                }
            }
            etable.lineIndex = lineIdx;
            result.tables.push_back(etable);

            // Emit a placeholder line (empty) for the table
            currentLineFmt = LineFormat{};
            emitNewline();
            continue;
        }

        if (tagName == "table" && isClosing) {
            // Already consumed by the inner loop
            continue;
        }

        // --- Inline formatting tags ---
        const bool isFmtTag =
            tagName == "b" || tagName == "strong" || tagName == "i" || tagName == "em" || tagName == "u" ||
            tagName == "s" || tagName == "del" || tagName == "strike" ||
            tagName == "a" || tagName == "span" || tagName == "font" ||
            tagName == "sub" || tagName == "sup" || tagName == "mark" ||
            tagName == "small" || tagName == "big";

        if (!isFmtTag) {
            continue;
        }

        flush();

        if (isClosing) {
            popFmt();
            continue;
        }

        pushFmt();

        if (tagName == "b" || tagName == "strong") {
            fmt.bold = true;
        } else if (tagName == "i" || tagName == "em") {
            fmt.italic = true;
        } else if (tagName == "u") {
            fmt.underline = true;
        } else if (tagName == "s" || tagName == "del" || tagName == "strike") {
            fmt.strikethrough = true;
        } else if (tagName == "a") {
            const std::string href = extractAttribute(attrs, "href");
            if (!href.empty()) {
                fmt.href = href;
            }
            fmt.underline = true;
            fmt.hasColor = true;
            fmt.color = SwColor{0, 0, 238}; // standard link blue
        } else if (tagName == "mark") {
            fmt.hasBgColor = true;
            fmt.bgColor = SwColor{255, 255, 0}; // yellow highlight
        } else if (tagName == "small") {
            if (fmt.fontSize <= 0) {
                fmt.fontSize = 10;
            } else {
                fmt.fontSize = std::max(6, fmt.fontSize - 2);
            }
        } else if (tagName == "big") {
            if (fmt.fontSize <= 0) {
                fmt.fontSize = 16;
            } else {
                fmt.fontSize += 2;
            }
        } else if (tagName == "span") {
            const std::string styleAttr = extractAttribute(attrs, "style");
            fmt = parseInlineStyle(styleAttr, fmt);
        } else if (tagName == "font") {
            const std::string colorAttr = extractAttribute(attrs, "color");
            SwColor c{0, 0, 0};
            if (parseCssColor(colorAttr, c)) {
                fmt.hasColor = true;
                fmt.color = c;
            }
            const std::string sizeAttr = extractAttribute(attrs, "size");
            if (!sizeAttr.empty()) {
                int sz = 0;
                if (parseInt(sizeAttr, sz)) {
                    // HTML font size 1-7 to approximate pt
                    const int ptMap[] = {0, 8, 10, 12, 14, 18, 24, 36};
                    if (sz >= 1 && sz <= 7) {
                        fmt.fontSize = ptMap[sz];
                    }
                }
            }
            const std::string faceAttr = extractAttribute(attrs, "face");
            if (!faceAttr.empty()) {
                fmt.fontFamily = faceAttr;
            }
        }

        if (selfClosing) {
            popFmt();
        }
    }

    flush();
    // Push format for the last line
    lineFormats.push_back(currentLineFmt);

    return result;
}

inline void SwTextEdit::normalizeRuns() {
    std::vector<Run> merged;
    merged.reserve(m_runs.size());

    for (const Run& run : m_runs) {
        if (run.text.isEmpty()) {
            continue;
        }
        if (!merged.empty() && merged.back().format == run.format) {
            merged.back().text.append(run.text);
        } else {
            merged.push_back(run);
        }
    }

    m_runs.swap(merged);
}

inline void SwTextEdit::rebuildRunsFromPlainText() {
    m_runs.clear();
    if (documentIsEmpty_()) {
        return;
    }
    m_runs.push_back(Run{documentText_(), TextFormat{}});
}

inline bool SwTextEdit::runsMatchPlainText() const {
    size_t total = 0;
    for (const Run& run : m_runs) {
        total += run.text.size();
    }
    if (total != documentLength_()) {
        return false;
    }

    size_t pos = 0;
    for (const Run& run : m_runs) {
        if (run.text.isEmpty()) {
            continue;
        }
        if (m_pieceTable.substr(pos, run.text.size()) != run.text) {
            return false;
        }
        pos += run.text.size();
    }
    return true;
}

inline void SwTextEdit::ensureRunsInSync() const {
    auto* self = const_cast<SwTextEdit*>(this);
    if (documentIsEmpty_()) {
        self->m_runs.clear();
        return;
    }
    if (!m_runs.empty() && runsMatchPlainText()) {
        return;
    }
    self->rebuildRunsFromPlainText();
}

inline SwTextEdit::TextFormat SwTextEdit::formatForInsertionAt(size_t pos) const {
    if (documentIsEmpty_() || m_runs.empty()) {
        return TextFormat{};
    }
    if (pos == 0) {
        return formatAtChar(0);
    }
    return formatAtChar(std::min(pos - 1, documentLength_() - 1));
}

inline SwTextEdit::TextFormat SwTextEdit::formatAtChar(size_t pos) const {
    size_t cursor = 0;
    for (const Run& run : m_runs) {
        const size_t len = run.text.size();
        if (pos < cursor + len) {
            return run.format;
        }
        cursor += len;
    }
    return m_runs.empty() ? TextFormat{} : m_runs.back().format;
}

inline SwFont SwTextEdit::fontForFormat(const TextFormat& fmt) const {
    SwFont font = getFont();
    if (fmt.bold) {
        font.setWeight(Bold);
    }
    font.setItalic(font.isItalic() || fmt.italic);
    font.setUnderline(font.isUnderline() || fmt.underline);
    if (fmt.fontSize > 0) {
        font.setPointSize(fmt.fontSize);
    }
    if (!fmt.fontFamily.empty()) {
        font.setFamily(std::wstring(fmt.fontFamily.begin(), fmt.fontFamily.end()));
    }
    return font;
}

inline int SwTextEdit::headingFontSize(int headingLevel) const {
    switch (headingLevel) {
    case 1: return 24;
    case 2: return 18;
    case 3: return 14;
    case 4: return 12;
    case 5: return 10;
    case 6: return 8;
    default: return getFont().getPointSize();
    }
}

inline SwTextEdit::LineFormat SwTextEdit::lineFormatAt(int lineIndex) const {
    if (lineIndex >= 0 && lineIndex < static_cast<int>(m_lineFormats.size())) {
        return m_lineFormats[lineIndex];
    }
    return LineFormat{};
}

inline int SwTextEdit::lineHeightForLine(int lineIndex) const {
    const LineFormat lf = lineFormatAt(lineIndex);
    int pt = getFont().getPointSize();
    if (pt <= 0) pt = 12;
    if (lf.headingLevel > 0) {
        pt = headingFontSize(lf.headingLevel);
    }
    if (lf.isHr) {
        return 12; // thin line for HR
    }
    // Table line — return full table height
    const EmbeddedTable* tbl = tableForLine(lineIndex);
    if (tbl) {
        return tableHeight(*tbl) + 8; // +8 for margin
    }
    // Scan runs on this line to find the largest fontSize
    if (lineIndex >= 0 && lineIndex < logicalLineCount_()) {
        const size_t lineStart = logicalLineStart_(lineIndex);
        const size_t lineLen = logicalLineLength_(lineIndex);
        size_t runPos = 0;
        for (size_t r = 0; r < m_runs.size(); ++r) {
            size_t runEnd = runPos + m_runs[r].text.size();
            // Check if this run overlaps with this line
            if (runEnd > lineStart && runPos < lineStart + lineLen) {
                if (m_runs[r].format.fontSize > pt) {
                    pt = m_runs[r].format.fontSize;
                }
            }
            if (runPos >= lineStart + lineLen) break;
            runPos = runEnd;
        }
    }
    return std::max(18, pt + 10);
}

inline int SwTextEdit::yOffsetForLine(int lineIndex, int firstVisible) const {
    int y = 0;
    const int start = std::max(0, firstVisible);
    const int end = std::min(lineIndex, logicalLineCount_());
    for (int i = start; i < end; ++i) {
        y += lineHeightForLine(i);
    }
    return y;
}

inline int SwTextEdit::lineIndexAtY(int relativeY, int firstVisible) const {
    int y = 0;
    const int start = std::max(0, firstVisible);
    for (int i = start; i < logicalLineCount_(); ++i) {
        const int lh = lineHeightForLine(i);
        if (relativeY < y + lh) {
            return i;
        }
        y += lh;
    }
    return std::max(0, logicalLineCount_() - 1);
}

inline int SwTextEdit::visibleLineCount(int firstVisible, int availableHeight) const {
    int y = 0;
    int count = 0;
    const int start = std::max(0, firstVisible);
    for (int i = start; i < logicalLineCount_(); ++i) {
        y += lineHeightForLine(i);
        ++count;
        if (y >= availableHeight) {
            break;
        }
    }
    return std::max(1, count);
}

inline void SwTextEdit::insertIntoRuns(size_t pos, char ch, const TextFormat& fmt) {
    if (m_runs.empty()) {
        m_runs.push_back(Run{SwString(1, ch), fmt});
        return;
    }

    size_t cursor = 0;
    for (size_t i = 0; i < m_runs.size(); ++i) {
        const size_t len = m_runs[i].text.size();
        if (pos > cursor + len) {
            cursor += len;
            continue;
        }

        const size_t offset = pos - cursor;
        if (m_runs[i].format == fmt) {
            m_runs[i].text.insert(offset, 1, ch);
        } else if (offset == 0) {
            m_runs.insert(m_runs.begin() + static_cast<std::ptrdiff_t>(i), Run{SwString(1, ch), fmt});
        } else if (offset >= len) {
            m_runs.insert(m_runs.begin() + static_cast<std::ptrdiff_t>(i + 1), Run{SwString(1, ch), fmt});
        } else {
            Run right{m_runs[i].text.substr(offset), m_runs[i].format};
            m_runs[i].text = m_runs[i].text.substr(0, offset);
            m_runs.insert(m_runs.begin() + static_cast<std::ptrdiff_t>(i + 1), Run{SwString(1, ch), fmt});
            m_runs.insert(m_runs.begin() + static_cast<std::ptrdiff_t>(i + 2), right);
        }

        normalizeRuns();
        return;
    }

    if (!m_runs.empty() && m_runs.back().format == fmt) {
        m_runs.back().text.append(ch);
    } else {
        m_runs.push_back(Run{SwString(1, ch), fmt});
    }
    normalizeRuns();
}

inline void SwTextEdit::eraseFromRuns(size_t pos) {
    if (m_runs.empty()) {
        return;
    }

    size_t total = 0;
    for (const Run& run : m_runs) {
        total += run.text.size();
    }
    if (pos >= total) {
        return;
    }

    size_t cursor = 0;
    for (size_t i = 0; i < m_runs.size(); ++i) {
        const size_t len = m_runs[i].text.size();
        if (pos >= cursor + len) {
            cursor += len;
            continue;
        }

        const size_t offset = pos - cursor;
        m_runs[i].text.erase(offset, 1);
        if (m_runs[i].text.isEmpty()) {
            m_runs.erase(m_runs.begin() + static_cast<std::ptrdiff_t>(i));
        }
        normalizeRuns();
        return;
    }
}

inline void SwTextEdit::drawRichLine(SwPainter* painter,
                                     const SwRect& inner,
                                     int y,
                                     int lh,
                                     size_t lineStart,
                                     size_t lineLen,
                                     const SwColor& defaultTextColor) const {
    if (!painter || lineLen == 0) {
        return;
    }

    const int fallbackWidth = std::max(1, inner.width);
    int x = inner.x;

    size_t docCursor = 0;
    const size_t lineEnd = lineStart + lineLen;
    for (const Run& run : m_runs) {
        const size_t runStart = docCursor;
        const size_t runEnd = docCursor + run.text.size();
        docCursor = runEnd;

        if (runEnd <= lineStart) {
            continue;
        }
        if (runStart >= lineEnd) {
            break;
        }

        const size_t segStart = std::max(lineStart, runStart);
        const size_t segEnd = std::min(lineEnd, runEnd);
        if (segEnd <= segStart) {
            continue;
        }

        const size_t segOffset = segStart - runStart;
        const size_t segLen = segEnd - segStart;
        const SwString segText = run.text.substr(segOffset, segLen);
        if (segText.isEmpty()) {
            continue;
        }

        const SwColor color = (!getEnable()) ? defaultTextColor : (run.format.hasColor ? run.format.color : defaultTextColor);
        const SwFont font = fontForFormat(run.format);

        const int segWidth =
            SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(), segText, font, segText.size(), fallbackWidth);

        // Draw background highlight if present
        if (run.format.hasBgColor && getEnable()) {
            painter->fillRect(SwRect{x, y, segWidth, lh}, run.format.bgColor, run.format.bgColor, 0);
        }

        SwRect segRect{x, y, inner.width - (x - inner.x), lh};
        painter->drawText(segRect,
                          segText,
                          DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          color,
                          font);

        // Draw strikethrough line — position at text x-height center (≈40% from top)
        // Text is VCenter in lh, so visual midpoint of lowercase letters is slightly above lh/2
        if (run.format.strikethrough && getEnable()) {
            const int fontSize = font.getPointSize() > 0 ? font.getPointSize() : 12;
            const int strikeY = y + (lh - fontSize) / 2 + fontSize * 48 / 100;
            painter->drawLine(x, strikeY, x + segWidth, strikeY, color, 1);
        }

        x += segWidth;
        if (x > inner.x + inner.width) {
            break;
        }
    }
}

inline int SwTextEdit::richTextWidth(size_t start, size_t length, int defaultWidth) const {
    if (length == 0 || m_runs.empty()) {
        return 0;
    }

    const size_t end = start + length;
    size_t docCursor = 0;
    int width = 0;
    for (const Run& run : m_runs) {
        const size_t runStart = docCursor;
        const size_t runEnd = docCursor + run.text.size();
        docCursor = runEnd;

        if (runEnd <= start) {
            continue;
        }
        if (runStart >= end) {
            break;
        }

        const size_t segStart = std::max(start, runStart);
        const size_t segEnd = std::min(end, runEnd);
        if (segEnd <= segStart) {
            continue;
        }

        const size_t segOffset = segStart - runStart;
        const size_t segLen = segEnd - segStart;
        const SwString segText = run.text.substr(segOffset, segLen);
        const SwFont font = fontForFormat(run.format);
        width += SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(), segText, font, segText.size(), defaultWidth);
    }
    return width;
}

inline size_t SwTextEdit::richCharacterIndexAtPosition(size_t lineStart,
                                                       size_t lineLen,
                                                       int relativeX,
                                                       int defaultWidth) const {
    if (lineLen == 0 || relativeX <= 0 || m_runs.empty()) {
        return 0;
    }

    const size_t lineEnd = lineStart + lineLen;
    size_t col = 0;
    int x = 0;

    size_t docCursor = 0;
    for (const Run& run : m_runs) {
        const size_t runStart = docCursor;
        const size_t runEnd = docCursor + run.text.size();
        docCursor = runEnd;

        if (runEnd <= lineStart) {
            continue;
        }
        if (runStart >= lineEnd) {
            break;
        }

        const size_t segStart = std::max(lineStart, runStart);
        const size_t segEnd = std::min(lineEnd, runEnd);
        if (segEnd <= segStart) {
            continue;
        }

        const size_t segOffset = segStart - runStart;
        const size_t segLen = segEnd - segStart;
        const SwString segText = run.text.substr(segOffset, segLen);
        if (segText.isEmpty()) {
            continue;
        }

        const SwFont font = fontForFormat(run.format);
        const int segWidth =
            SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(), segText, font, segText.size(), defaultWidth);
        if (relativeX < x + segWidth) {
            const int localX = std::max(0, relativeX - x);
            const size_t idxInSeg =
                SwWidgetPlatformAdapter::characterIndexAtPosition(nativeWindowHandle(), segText, font, localX, defaultWidth);
            return col + std::min(idxInSeg, segText.size());
        }

        x += segWidth;
        col += segLen;
    }

    return lineLen;
}

inline SwTextEdit::SwTextEdit(SwWidget* parent)
    : SwPlainTextEdit(parent) {
}

inline void SwTextEdit::setPlainText(const SwString& text) {
    SwPlainTextEdit::setPlainText(text);
    rebuildRunsFromPlainText();
    m_lineFormats.clear();
}

inline void SwTextEdit::appendPlainText(const SwString& text) {
    SwPlainTextEdit::appendPlainText(text);
    rebuildRunsFromPlainText();
    m_lineFormats.clear();
}

inline void SwTextEdit::clear() {
    SwPlainTextEdit::clear();
    m_runs.clear();
    m_lineFormats.clear();
}

inline void SwTextEdit::setHtml(const SwString& html) {
    ParseResult result = parseHtmlFull(html);
    m_runs = result.runs;
    m_lineFormats = result.lineFormats;
    m_tables = result.tables;
    normalizeRuns();

    SwString plain;
    for (const Run& run : m_runs) {
        plain.append(run.text);
    }

    SwPlainTextEdit::setPlainText(plain);
    ensureRunsInSync();
}

inline SwString SwTextEdit::toHtml() const {
    ensureRunsInSync();

    SwString out;

    // --- Split text into logical lines (by '\n') ---
    std::vector<std::pair<size_t, size_t>> logLines; // (start, length)
    for (int i = 0; i < logicalLineCount_(); ++i) {
        logLines.push_back(std::make_pair(logicalLineStart_(i), logicalLineLength_(i)));
    }

    // --- Build cumulative run start offsets ---
    std::vector<size_t> runStarts;
    {
        size_t pos = 0;
        for (size_t r = 0; r < m_runs.size(); ++r) {
            runStarts.push_back(pos);
            pos += m_runs[r].text.size();
        }
    }

    // --- Helper: emit escaped text (no <br> conversion) ---
    auto escapeHtml = [](const SwString& text) -> SwString {
        SwString r;
        r.reserve(text.size() + 16);
        for (size_t i = 0; i < text.size(); ++i) {
            char ch = text[i];
            switch (ch) {
            case '&': r.append("&amp;"); break;
            case '<': r.append("&lt;"); break;
            case '>': r.append("&gt;"); break;
            case '"': r.append("&quot;"); break;
            case '\'': r.append("&#39;"); break;
            default: r.append(ch); break;
            }
        }
        return r;
    };

    // --- Helper: emit inline formatted content for a character range ---
    auto emitInline = [&](size_t lineStart, size_t lineLen) {
        size_t lineEnd = lineStart + lineLen;
        for (size_t ri = 0; ri < m_runs.size(); ++ri) {
            size_t rStart = runStarts[ri];
            size_t rEnd = rStart + m_runs[ri].text.size();

            size_t segStart = std::max(lineStart, rStart);
            size_t segEnd = std::min(lineEnd, rEnd);
            if (segEnd <= segStart) continue;

            size_t offInRun = segStart - rStart;
            size_t segLen = segEnd - segStart;
            SwString segText = m_runs[ri].text.substr(offInRun, segLen);

            const TextFormat& fmt = m_runs[ri].format;
            SwString prefix, suffix;

            bool needsSpan = false;
            SwString styleStr;
            if (fmt.hasColor) {
                styleStr.append("color: ");
                styleStr.append(colorToCss(fmt.color));
                styleStr.append("; ");
                needsSpan = true;
            }
            if (fmt.hasBgColor) {
                styleStr.append("background-color: ");
                styleStr.append(colorToCss(fmt.bgColor));
                styleStr.append("; ");
                needsSpan = true;
            }
            if (fmt.fontSize > 0) {
                std::ostringstream oss;
                oss << "font-size: " << fmt.fontSize << "pt; ";
                styleStr.append(SwString(oss.str()));
                needsSpan = true;
            }
            if (!fmt.fontFamily.empty()) {
                styleStr.append("font-family: ");
                styleStr.append(SwString(fmt.fontFamily));
                styleStr.append("; ");
                needsSpan = true;
            }
            if (needsSpan) {
                prefix.append("<span style=\"");
                prefix.append(styleStr);
                prefix.append("\">");
                suffix.prepend("</span>");
            }
            if (!fmt.href.empty()) {
                prefix.append("<a href=\"");
                prefix.append(SwString(fmt.href));
                prefix.append("\">");
                suffix.prepend("</a>");
            }
            if (fmt.bold) { prefix.append("<b>"); suffix.prepend("</b>"); }
            if (fmt.italic) { prefix.append("<i>"); suffix.prepend("</i>"); }
            if (fmt.underline && fmt.href.empty()) { prefix.append("<u>"); suffix.prepend("</u>"); }
            if (fmt.strikethrough) { prefix.append("<s>"); suffix.prepend("</s>"); }

            out.append(prefix);
            out.append(escapeHtml(segText));
            out.append(suffix);
        }
    };

    // --- Track list open/close state ---
    enum ActiveList { ALNone, ALUL, ALOL };
    ActiveList activeList = ALNone;
    bool lastWasEmpty = false;

    for (size_t li = 0; li < logLines.size(); ++li) {
        LineFormat lf;
        if (li < m_lineFormats.size()) lf = m_lineFormats[li];

        size_t lineStart = logLines[li].first;
        size_t lineLen = logLines[li].second;

        // --- Skip empty non-special lines (eliminates blank <p></p> from round-trip) ---
        bool isEmpty = (lineLen == 0) && lf.headingLevel == 0
                       && lf.listType == LineFormat::NoList && !lf.isHr;
        if (isEmpty) {
            // Check if this is a table placeholder line
            const EmbeddedTable* tbl = tableForLogicalLine(static_cast<int>(li));
            if (tbl && !tbl->rows.empty()) {
                if (activeList != ALNone) {
                    out.append(activeList == ALUL ? "</ul>" : "</ol>");
                    activeList = ALNone;
                }
                std::ostringstream toss;
                toss << "<table border=\"" << tbl->borderWidth << "\" cellpadding=\"" << tbl->cellPadding << "\" cellspacing=\"0\">";
                out.append(SwString(toss.str()));
                for (size_t ri = 0; ri < tbl->rows.size(); ++ri) {
                    out.append("<tr>");
                    for (size_t ci = 0; ci < tbl->rows[ri].size(); ++ci) {
                        const TableCell& tc = tbl->rows[ri][ci];
                        SwString tag = tc.isHeader ? "<th" : "<td";
                        if (tc.hasBg) {
                            std::ostringstream co;
                            co << " style=\"background-color: " << colorToCss(tc.bgColor).toStdString() << ";\"";
                            tag.append(SwString(co.str()));
                        }
                        tag.append(">");
                        out.append(tag);
                        if (tc.format.bold && !tc.isHeader) out.append("<b>");
                        out.append(escapeHtml(tc.text));
                        if (tc.format.bold && !tc.isHeader) out.append("</b>");
                        out.append(tc.isHeader ? "</th>" : "</td>");
                    }
                    out.append("</tr>");
                }
                out.append("</table>");
                continue;
            }
            continue; // skip all empty paragraphs — block spacing provides visual separation
        }

        // --- HR ---
        if (lf.isHr) {
            if (activeList != ALNone) {
                out.append(activeList == ALUL ? "</ul>" : "</ol>");
                activeList = ALNone;
            }
            out.append("<hr/>");
            continue;
        }

        // --- List transitions ---
        ActiveList needed = ALNone;
        if (lf.listType == LineFormat::Unordered) needed = ALUL;
        else if (lf.listType == LineFormat::Ordered) needed = ALOL;

        if (needed != activeList) {
            if (activeList != ALNone) out.append(activeList == ALUL ? "</ul>" : "</ol>");
            if (needed != ALNone) out.append(needed == ALUL ? "<ul>" : "<ol>");
            activeList = needed;
        }

        // --- Emit block-level tag + inline content ---
        if (lf.headingLevel > 0) {
            std::ostringstream oss;
            oss << "<h" << lf.headingLevel << ">";
            out.append(SwString(oss.str()));
            emitInline(lineStart, lineLen);
            oss.str("");
            oss << "</h" << lf.headingLevel << ">";
            out.append(SwString(oss.str()));
        } else if (activeList != ALNone) {
            out.append("<li>");
            emitInline(lineStart, lineLen);
            out.append("</li>");
        } else {
            out.append("<p>");
            emitInline(lineStart, lineLen);
            out.append("</p>");
        }
    }

    // Close any open list
    if (activeList != ALNone) {
        out.append(activeList == ALUL ? "</ul>" : "</ol>");
    }

    return out;
}

inline SwPlainTextEdit::EditState SwTextEdit::captureEditState() const {
    ensureRunsInSync();

    EditState st = SwPlainTextEdit::captureEditState();
    std::vector<Run> runs = m_runs;
    std::vector<LineFormat> lineFmts = m_lineFormats;
    std::vector<EmbeddedTable> tables = m_tables;
    st.applyExtra = [this, runs, lineFmts, tables]() mutable {
        m_runs = runs;
        const_cast<SwTextEdit*>(this)->m_lineFormats = lineFmts;
        const_cast<SwTextEdit*>(this)->m_tables = tables;
    };
    return st;
}

inline void SwTextEdit::paintEvent(PaintEvent* event) {
    ensureRunsInSync();

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

    // Compute how many lines fit in the visible area using actual line heights
    const int baseLh = lineHeightPx();
    {
        int totalH = 0;
        int fitCount = 0;
        for (int li = logicalLineCount_() - 1; li >= 0; --li) {
            totalH += lineHeightForLine(li);
            if (totalH > inner.height) break;
            fitCount++;
        }
        clampFirstVisibleLine(std::max(1, fitCount));
    }

    SwColor defaultTextColor = resolveTextColor(sheet, SwColor{24, 28, 36});
    if (!getEnable()) {
        defaultTextColor = SwColor{150, 150, 150};
    }

    painter->pushClipRect(inner);

    const bool hasSel = hasSelectedText();
    const size_t selMin = (std::min)(m_selectionStart, m_selectionEnd);
    const size_t selMax = (std::max)(m_selectionStart, m_selectionEnd);
    const SwColor selFill{219, 234, 254};
    const int selectionWidth = std::max(1, inner.width);

    const int first = std::max(0, m_firstVisibleLine);
    int yPos = inner.y;
    for (int i = first; i < logicalLineCount_(); ++i) {
        const int lh = lineHeightForLine(i);
        if (yPos >= inner.y + inner.height) {
            break;
        }

        const size_t lineStart = logicalLineStart_(i);
        const size_t lineLen = logicalLineLength_(i);
        const LineFormat lf = lineFormatAt(i);

        // --- Horizontal rule ---
        if (lf.isHr) {
            const int hrY = yPos + lh / 2;
            painter->drawLine(inner.x + 4, hrY, inner.x + inner.width - 4, hrY, SwColor{180, 180, 180}, 1);
            yPos += lh;
            continue;
        }

        // --- Embedded table ---
        {
            const EmbeddedTable* tbl = tableForLine(i);
            if (tbl && !tbl->rows.empty()) {
                int tblX = inner.x + 4;
                int tblY = yPos + 4;
                int availW = inner.width - 8;
                int numCols = tbl->columns();
                int bw = tbl->borderWidth;
                int pad = tbl->cellPadding;
                int fontSize = getFont().getPointSize() > 0 ? getFont().getPointSize() : 12;
                int cellW = numCols > 0 ? (availW - (numCols + 1) * bw) / numCols : availW;
                int rowH = fontSize + 2 * pad + 4;

                int curTblY = tblY;
                for (size_t ri = 0; ri < tbl->rows.size(); ++ri) {
                    for (size_t ci = 0; ci < tbl->rows[ri].size(); ++ci) {
                        const TableCell& tc = tbl->rows[ri][ci];
                        int cx = tblX + bw + static_cast<int>(ci) * (cellW + bw);
                        int cy = curTblY;
                        SwRect cellRect{cx, cy, cellW, rowH};

                        // Cell background
                        if (tc.hasBg) {
                            painter->fillRect(cellRect, tc.bgColor, tc.bgColor, 0);
                        } else if (tbl->hasBg) {
                            painter->fillRect(cellRect, tbl->bgColor, tbl->bgColor, 0);
                        }

                        // Cell border
                        painter->drawRect(cellRect, tbl->borderColor, bw);

                        // Cell text
                        SwRect textRect{cx + pad, cy, cellW - 2 * pad, rowH};
                        SwFont font = getFont();
                        SwColor color = defaultTextColor;
                        if (tc.format.bold) font.setWeight(Bold);
                        if (tc.format.italic) font.setItalic(true);
                        if (tc.format.fontSize > 0) font.setPointSize(tc.format.fontSize);
                        if (tc.format.hasColor) color = tc.format.color;
                        painter->drawText(textRect, tc.text,
                                          DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                          color, font);
                    }
                    curTblY += rowH + bw;
                }

                yPos += lh;
                continue;
            }
        }

        // Compute indentation
        int indent = 0;
        if (lf.indentLevel > 0) {
            indent = lf.indentLevel * listIndentPx();
        }

        // --- Draw list bullet / number ---
        if (lf.listType != LineFormat::NoList) {
            const int bulletX = inner.x + (lf.indentLevel - 1) * listIndentPx();
            if (lf.listType == LineFormat::Unordered) {
                // Draw bullet dot
                const SwString bullet("\xe2\x80\xa2"); // Unicode bullet •
                SwRect bulletRect{bulletX, yPos, listIndentPx(), lh};
                painter->drawText(bulletRect,
                                  bullet,
                                  DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                  defaultTextColor,
                                  getFont());
            } else if (lf.listType == LineFormat::Ordered && lf.listIndex > 0) {
                std::ostringstream oss;
                oss << lf.listIndex << ".";
                SwString numText(oss.str());
                SwRect numRect{bulletX, yPos, listIndentPx(), lh};
                painter->drawText(numRect,
                                  numText,
                                  DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                  defaultTextColor,
                                  getFont());
            }
        }

        // Adjust inner rect for indentation and alignment
        SwRect lineInner = inner;
        lineInner.x += indent;
        lineInner.width = std::max(0, lineInner.width - indent);

        // --- Selection highlight ---
        if (hasSel) {
            const size_t lineEnd = lineStart + lineLen;
            const size_t segStart = (std::max)(selMin, lineStart);
            const size_t segEnd = (std::min)(selMax, lineEnd);
            if (segStart < segEnd) {
                const size_t startCol = segStart - lineStart;
                const size_t endCol = segEnd - lineStart;

                const int x1 = lineInner.x + richTextWidth(lineStart, startCol, selectionWidth);
                const int x2 = lineInner.x + richTextWidth(lineStart, endCol, selectionWidth);
                const int left = (std::min)(x1, x2);
                const int right = (std::max)(x1, x2);
                if (right > left) {
                    painter->fillRect(SwRect{left, yPos, right - left, lh}, selFill, selFill, 0);
                }
            }
        }

        // --- Alignment offset ---
        int alignOffset = 0;
        if (lf.alignment != LineFormat::AlignLeft && lineLen > 0) {
            const int textW = richTextWidth(lineStart, lineLen, selectionWidth);
            if (lf.alignment == LineFormat::AlignCenter) {
                alignOffset = std::max(0, (lineInner.width - textW) / 2);
            } else if (lf.alignment == LineFormat::AlignRight) {
                alignOffset = std::max(0, lineInner.width - textW);
            }
        }
        SwRect drawInner = lineInner;
        drawInner.x += alignOffset;
        drawInner.width = std::max(0, drawInner.width - alignOffset);

        drawRichLine(painter, drawInner, yPos, lh, lineStart, lineLen, defaultTextColor);

        yPos += lh;
    }

    if (documentIsEmpty_() && !m_placeholder.isEmpty() && !getFocus()) {
        SwRect phRect = inner;
        phRect.height = baseLh;
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
        if (cursorLine >= first && cursorLine < logicalLineCount_()) {
            const int cursorLh = lineHeightForLine(cursorLine);
            const int cursorY = inner.y + yOffsetForLine(cursorLine, first);
            if (cursorY < inner.y + inner.height) {
                const SwString lineText = logicalLineText_(cursorLine);
                const size_t clampedCol = std::min(static_cast<size_t>(cursorCol), lineText.size());

                const size_t lineStart = logicalLineStart_(cursorLine);

                const LineFormat lf = lineFormatAt(cursorLine);
                const int indent = lf.indentLevel * listIndentPx();

                const int fallbackWidth = std::max(1, inner.width);
                const int caretDx = richTextWidth(lineStart, clampedCol, fallbackWidth);
                const int x = inner.x + indent + caretDx;
                const int top = cursorY + 4;
                const int bottom = cursorY + cursorLh - 4;
                SwColor caretColor = getEnable() ? SwColor{24, 28, 36} : SwColor{150, 150, 150};
                painter->drawLine(x, top, x, bottom, caretColor, 1);
            }
        }
    }

    painter->popClipRect();
    painter->finalize();
}

inline void SwTextEdit::wheelEvent(WheelEvent* event) {
    if (!event || !isPointInside(event->x(), event->y())) {
        SwPlainTextEdit::wheelEvent(event);
        return;
    }
    if (event->isShiftPressed()) {
        SwPlainTextEdit::wheelEvent(event);
        return;
    }

    const SwRect bounds = rect();
    const Padding pad = resolvePadding(getToolSheet());
    const int bw = 1;
    SwRect inner = bounds;
    inner.x += bw + pad.left;
    inner.y += bw + pad.top;
    inner.width = std::max(0, inner.width - 2 * bw - (pad.left + pad.right));
    inner.height = std::max(0, inner.height - 2 * bw - (pad.top + pad.bottom));

    // Compute visible lines using actual per-line heights (accounts for tables)
    int totalH = 0;
    int fitCount = 0;
    for (int li = logicalLineCount_() - 1; li >= 0; --li) {
        totalH += lineHeightForLine(li);
        if (totalH > inner.height) break;
        fitCount++;
    }
    int visibleLines = std::max(1, fitCount);
    clampFirstVisibleLine(visibleLines);
    const int maxFirst = std::max(0, logicalLineCount_() - visibleLines);

    int steps = event->delta() / 120;
    if (steps == 0) {
        steps = (event->delta() > 0) ? 1 : (event->delta() < 0 ? -1 : 0);
    }
    if (steps == 0) {
        SwPlainTextEdit::wheelEvent(event);
        return;
    }

    m_firstVisibleLine = clampInt(m_firstVisibleLine - steps, 0, maxFirst);
    event->accept();
    update();
}

inline void SwTextEdit::updateCursorFromPosition(int px, int py) {
    ensureRunsInSync();

    const SwRect bounds = rect();
    const Padding pad = resolvePadding(getToolSheet());
    const int borderWidth = 1;
    SwRect inner = bounds;
    inner.x += borderWidth + pad.left;
    inner.y += borderWidth + pad.top;
    inner.width = std::max(0, inner.width - 2 * borderWidth - (pad.left + pad.right));
    inner.height = std::max(0, inner.height - 2 * borderWidth - (pad.top + pad.bottom));

    const int relativeY = py - inner.y;
    const int lineIdx = lineIndexAtY(relativeY, m_firstVisibleLine);

    const LineFormat lf = lineFormatAt(lineIdx);
    const int indent = lf.indentLevel * listIndentPx();
    const int relativeX = px - inner.x - indent;
    const size_t lineStart = logicalLineStart_(lineIdx);
    const size_t lineLen = logicalLineLength_(lineIdx);
    const size_t col = richCharacterIndexAtPosition(lineStart, lineLen, relativeX, std::max(1, inner.width));

    m_cursorPos = std::min(lineStart + std::min(col, lineLen), documentLength_());
}

inline void SwTextEdit::insertTextAt(size_t pos, const SwString& text) {
    ensureRunsInSync();

    if (text.isEmpty()) {
        return;
    }

    const size_t clamped = std::min(pos, documentLength_());
    const TextFormat fmt = formatForInsertionAt(clamped);

    m_pieceTable.insert(clamped, text);

    size_t currentPos = clamped;
    for (size_t i = 0; i < text.size(); ++i) {
        insertIntoRuns(currentPos, text[i], fmt);
        ++currentPos;
    }

    normalizeRuns();
}

inline void SwTextEdit::eraseTextAt(size_t pos, size_t len) {
    ensureRunsInSync();

    if (len == 0 || documentIsEmpty_()) {
        return;
    }

    const size_t clampedPos = std::min(pos, documentLength_());
    if (clampedPos >= documentLength_()) {
        return;
    }

    const size_t clampedLen = std::min(len, documentLength_() - clampedPos);
    m_pieceTable.remove(clampedPos, clampedLen);
    for (size_t i = 0; i < clampedLen; ++i) {
        eraseFromRuns(clampedPos);
    }

    normalizeRuns();
}

// ---------------------------------------------------------------------------
// SwTextDocument integration
// ---------------------------------------------------------------------------

inline SwTextDocument* SwTextEdit::document() const {
    // Build a SwTextDocument from the current widget content.
    // Caller owns the returned pointer.
    SwTextDocument* doc = new SwTextDocument();
    SwString html = toHtml();
    if (!html.isEmpty()) {
        doc->setHtml(html);
    } else {
        doc->setPlainText(toPlainText());
    }
    doc->setDefaultFont(getFont());
    return doc;
}

inline void SwTextEdit::setDocument(SwTextDocument* doc) {
    if (!doc) return;
    SwString html = doc->toHtml();
    setHtml(html);
}

inline SwTextCursor SwTextEdit::textCursor() const {
    SwTextDocument* doc = document();
    SwTextCursor cursor(doc);
    cursor.setPosition(static_cast<int>(m_cursorPos));
    return cursor;
}

inline bool SwTextEdit::print(const SwString& pdfFilename, const SwFont& font) const {
    SwTextDocument* doc = document();
    SwFont f = font.getPointSize() > 0 ? font : getFont();
    bool ok = SwTextDocumentWriter::exportToPdf(doc, pdfFilename, f);
    delete doc;
    return ok;
}
