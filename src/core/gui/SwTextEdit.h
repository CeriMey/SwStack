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

#include "SwPlainTextEdit.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

class SwTextEdit : public SwPlainTextEdit {
    SW_OBJECT(SwTextEdit, SwPlainTextEdit)

public:
    explicit SwTextEdit(SwWidget* parent = nullptr);

    void setText(const SwString& text) { setPlainText(text); }

    void setPlainText(const SwString& text);
    void appendPlainText(const SwString& text);
    void clear();

    void setHtml(const SwString& html);
    SwString toHtml() const;

protected:
    EditState captureEditState() const override;

    void paintEvent(PaintEvent* event) override;
    void updateCursorFromPosition(int px, int py) override;

    void insertTextAt(size_t pos, const SwString& text) override;
    void eraseTextAt(size_t pos, size_t len) override;

private:
    struct TextFormat {
        bool bold{false};
        bool italic{false};
        bool underline{false};
        bool hasColor{false};
        SwColor color{0, 0, 0};

        bool operator==(const TextFormat& other) const;
        bool operator!=(const TextFormat& other) const { return !(*this == other); }
    };

    struct Run {
        SwString text;
        TextFormat format;
    };

    mutable std::vector<Run> m_runs;

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
};

inline bool SwTextEdit::TextFormat::operator==(const TextFormat& other) const {
    if (bold != other.bold || italic != other.italic || underline != other.underline || hasColor != other.hasColor) {
        return false;
    }
    if (!hasColor) {
        return true;
    }
    return color.r == other.color.r && color.g == other.color.g && color.b == other.color.b;
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
    if (c[0] == '#' && c.size() == 7) {
        auto hex = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
            if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
            return -1;
        };
        const int r1 = hex(c[1]), r2 = hex(c[2]);
        const int g1 = hex(c[3]), g2 = hex(c[4]);
        const int b1 = hex(c[5]), b2 = hex(c[6]);
        if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) {
            return false;
        }
        out = SwColor{r1 * 16 + r2, g1 * 16 + g2, b1 * 16 + b2};
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
    std::vector<Run> runs;

    TextFormat fmt{};
    std::vector<TextFormat> fmtStack;
    std::string s = html.toStdString();

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

    auto pushFmt = [&]() { fmtStack.push_back(fmt); };
    auto popFmt = [&]() {
        if (!fmtStack.empty()) {
            fmt = fmtStack.back();
            fmtStack.pop_back();
        }
    };

    for (size_t i = 0; i < s.size();) {
        if (s[i] != '<') {
            const size_t next = s.find('<', i);
            buffer.append(s.substr(i, (next == std::string::npos) ? std::string::npos : (next - i)));
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

        if (tagName == "br") {
            flush();
            runs.push_back(Run{SwString("\n"), fmt});
            continue;
        }

        if (tagName == "p" || tagName == "div") {
            flush();
            if (!runs.empty()) {
                const Run& last = runs.back();
                if (!last.text.isEmpty() && !last.text.endsWith(SwString("\n"))) {
                    runs.push_back(Run{SwString("\n"), fmt});
                }
            }
            if (isClosing) {
                runs.push_back(Run{SwString("\n"), fmt});
            }
            continue;
        }

        const bool isFmtTag =
            tagName == "b" || tagName == "strong" || tagName == "i" || tagName == "em" || tagName == "u" ||
            tagName == "span" || tagName == "font";

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
        }

        if (selfClosing) {
            popFmt();
        }
    }

    flush();
    return runs;
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
    if (m_text.isEmpty()) {
        return;
    }
    m_runs.push_back(Run{m_text, TextFormat{}});
}

inline bool SwTextEdit::runsMatchPlainText() const {
    size_t total = 0;
    for (const Run& run : m_runs) {
        total += run.text.size();
    }
    if (total != m_text.size()) {
        return false;
    }

    size_t pos = 0;
    for (const Run& run : m_runs) {
        if (run.text.isEmpty()) {
            continue;
        }
        if (m_text.substr(pos, run.text.size()) != run.text) {
            return false;
        }
        pos += run.text.size();
    }
    return true;
}

inline void SwTextEdit::ensureRunsInSync() const {
    auto* self = const_cast<SwTextEdit*>(this);
    if (m_text.isEmpty()) {
        self->m_runs.clear();
        return;
    }
    if (!m_runs.empty() && runsMatchPlainText()) {
        return;
    }
    self->rebuildRunsFromPlainText();
}

inline SwTextEdit::TextFormat SwTextEdit::formatForInsertionAt(size_t pos) const {
    if (m_text.isEmpty() || m_runs.empty()) {
        return TextFormat{};
    }
    if (pos == 0) {
        return formatAtChar(0);
    }
    return formatAtChar(std::min(pos - 1, m_text.size() - 1));
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
    return font;
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

        SwRect segRect{x, y, inner.width - (x - inner.x), lh};
        painter->drawText(segRect,
                          segText,
                          DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          color,
                          font);

        const int segWidth =
            SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(), segText, font, segText.size(), fallbackWidth);
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
    setStyleSheet(R"(
        SwTextEdit {
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

inline void SwTextEdit::setPlainText(const SwString& text) {
    SwPlainTextEdit::setPlainText(text);
    rebuildRunsFromPlainText();
}

inline void SwTextEdit::appendPlainText(const SwString& text) {
    SwPlainTextEdit::appendPlainText(text);
    rebuildRunsFromPlainText();
}

inline void SwTextEdit::clear() {
    SwPlainTextEdit::clear();
    m_runs.clear();
}

inline void SwTextEdit::setHtml(const SwString& html) {
    m_runs = parseHtml(html);
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
    for (const Run& run : m_runs) {
        if (run.text.isEmpty()) {
            continue;
        }

        SwString prefix;
        SwString suffix;
        if (run.format.hasColor) {
            prefix.append("<span style=\"color: ");
            prefix.append(colorToCss(run.format.color));
            prefix.append(";\">");
            suffix.prepend("</span>");
        }
        if (run.format.bold) {
            prefix.append("<b>");
            suffix.prepend("</b>");
        }
        if (run.format.italic) {
            prefix.append("<i>");
            suffix.prepend("</i>");
        }
        if (run.format.underline) {
            prefix.append("<u>");
            suffix.prepend("</u>");
        }

        out.append(prefix);
        out.append(escapeHtmlWithLineBreaks(run.text));
        out.append(suffix);
    }
    return out;
}

inline SwPlainTextEdit::EditState SwTextEdit::captureEditState() const {
    ensureRunsInSync();

    EditState st = SwPlainTextEdit::captureEditState();
    st.applyExtra = [this, runs = m_runs]() mutable {
        m_runs = runs;
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

    const SwRect bounds = getRect();
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
    const int last = std::min(first + visibleLines + 1, m_lines.size());
    for (int i = first; i < last; ++i) {
        const int row = i - first;
        const int y = inner.y + row * lh;

        const size_t lineStart = (i < m_lineStarts.size()) ? m_lineStarts[i] : 0;
        const size_t lineLen = m_lines[i].size();

        if (hasSel) {
            const size_t lineEnd = lineStart + lineLen;
            const size_t segStart = (std::max)(selMin, lineStart);
            const size_t segEnd = (std::min)(selMax, lineEnd);
            if (segStart < segEnd) {
                const size_t startCol = segStart - lineStart;
                const size_t endCol = segEnd - lineStart;

                const int x1 = inner.x + richTextWidth(lineStart, startCol, selectionWidth);
                const int x2 = inner.x + richTextWidth(lineStart, endCol, selectionWidth);
                const int left = (std::min)(x1, x2);
                const int right = (std::max)(x1, x2);
                if (right > left) {
                    painter->fillRect(SwRect{left, y, right - left, lh}, selFill, selFill, 0);
                }
            }
        }

        drawRichLine(painter, inner, y, lh, lineStart, lineLen, defaultTextColor);
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

            const size_t lineStart = (cursorLine < m_lineStarts.size()) ? m_lineStarts[cursorLine] : 0;

            const int fallbackWidth = std::max(1, inner.width);
            const int caretDx = richTextWidth(lineStart, clampedCol, fallbackWidth);
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

inline void SwTextEdit::updateCursorFromPosition(int px, int py) {
    ensureRunsInSync();

    const SwRect bounds = getRect();
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
    const size_t lineStart = (lineIdx < m_lineStarts.size()) ? m_lineStarts[lineIdx] : 0;
    const size_t lineLen = (lineIdx >= 0 && lineIdx < m_lines.size()) ? m_lines[lineIdx].size() : 0;
    const size_t col = richCharacterIndexAtPosition(lineStart, lineLen, relativeX, std::max(1, inner.width));

    m_cursorPos = std::min(lineStart + std::min(col, lineLen), m_text.size());
}

inline void SwTextEdit::insertTextAt(size_t pos, const SwString& text) {
    ensureRunsInSync();

    if (text.isEmpty()) {
        return;
    }

    const size_t clamped = std::min(pos, m_text.size());
    const TextFormat fmt = formatForInsertionAt(clamped);

    m_text.insert(clamped, text);

    size_t currentPos = clamped;
    for (size_t i = 0; i < text.size(); ++i) {
        insertIntoRuns(currentPos, text[i], fmt);
        ++currentPos;
    }

    normalizeRuns();
}

inline void SwTextEdit::eraseTextAt(size_t pos, size_t len) {
    ensureRunsInSync();

    if (len == 0 || m_text.isEmpty()) {
        return;
    }

    const size_t clampedPos = std::min(pos, m_text.size());
    if (clampedPos >= m_text.size()) {
        return;
    }

    const size_t clampedLen = std::min(len, m_text.size() - clampedPos);
    m_text.erase(clampedPos, clampedLen);
    for (size_t i = 0; i < clampedLen; ++i) {
        eraseFromRuns(clampedPos);
    }

    normalizeRuns();
}
