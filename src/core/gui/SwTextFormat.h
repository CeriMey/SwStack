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

#include "SwFont.h"
#include "graphics/SwImage.h"
#include "../types/Sw.h"
#include "../types/SwString.h"
#include "../types/SwList.h"
#include "../types/SwMap.h"
#include "../types/SwAny.h"

#include <string>

// ---------------------------------------------------------------------------
// SwTextCharFormat — character-level formatting (Qt6 QTextCharFormat equivalent)
// ---------------------------------------------------------------------------

class SwTextCharFormat {
public:
    enum UnderlineStyle {
        NoUnderline,
        SingleUnderline,
        WaveUnderline,
        SpellCheckUnderline
    };

    SwTextCharFormat() = default;

    // --- Font properties ---
    void setFontFamily(const SwString& family)   { m_fontFamily = family; m_hasFontFamily = true; }
    SwString fontFamily() const                   { return m_fontFamily; }
    bool hasFontFamily() const                    { return m_hasFontFamily; }

    void setFontPointSize(int size)               { m_fontPointSize = size; m_hasFontPointSize = true; }
    int fontPointSize() const                     { return m_fontPointSize; }
    bool hasFontPointSize() const                 { return m_hasFontPointSize; }

    void setFontWeight(FontWeight w)              { m_fontWeight = w; m_hasFontWeight = true; }
    FontWeight fontWeight() const                 { return m_fontWeight; }
    bool hasFontWeight() const                    { return m_hasFontWeight; }

    void setFontItalic(bool on)                   { m_italic = on; m_hasItalic = true; }
    bool fontItalic() const                       { return m_italic; }
    bool hasFontItalic() const                    { return m_hasItalic; }

    void setFontUnderline(bool on)                { setUnderlineStyle(on ? SingleUnderline : NoUnderline); }
    bool fontUnderline() const                    { return m_underlineStyle == SingleUnderline; }
    bool hasFontUnderline() const                 { return m_hasUnderlineStyle && m_underlineStyle == SingleUnderline; }

    void setUnderlineStyle(UnderlineStyle style)  { m_underlineStyle = style; m_hasUnderlineStyle = true; }
    UnderlineStyle underlineStyle() const         { return m_underlineStyle; }
    bool hasUnderlineStyle() const                { return m_hasUnderlineStyle; }

    void setUnderlineColor(const SwColor& c)      { m_underlineColor = c; m_hasUnderlineColor = true; }
    SwColor underlineColor() const                { return m_underlineColor; }
    bool hasUnderlineColor() const                { return m_hasUnderlineColor; }

    void setFontStrikeOut(bool on)                { m_strikeOut = on; m_hasStrikeOut = true; }
    bool fontStrikeOut() const                    { return m_strikeOut; }
    bool hasFontStrikeOut() const                 { return m_hasStrikeOut; }

    // --- Colors ---
    void setForeground(const SwColor& c)          { m_foreground = c; m_hasForeground = true; }
    SwColor foreground() const                    { return m_foreground; }
    bool hasForeground() const                    { return m_hasForeground; }

    void setBackground(const SwColor& c)          { m_background = c; m_hasBackground = true; }
    SwColor background() const                    { return m_background; }
    bool hasBackground() const                    { return m_hasBackground; }

    // --- Link ---
    void setAnchorHref(const SwString& href)      { m_href = href; }
    SwString anchorHref() const                   { return m_href; }
    bool isAnchor() const                         { return !m_href.isEmpty(); }

    void setAnchorName(const SwString& name)      { m_anchorName = name; }
    SwString anchorName() const                   { return m_anchorName; }

    // --- Vertical alignment ---
    enum VerticalAlignment { AlignNormal, AlignSuperScript, AlignSubScript };
    void setVerticalAlignment(VerticalAlignment a) { m_vertAlign = a; }
    VerticalAlignment verticalAlignment() const    { return m_vertAlign; }

    // --- Tooltip ---
    void setToolTip(const SwString& tip)          { m_toolTip = tip; }
    SwString toolTip() const                      { return m_toolTip; }

    // --- Custom properties (extensible, like Qt) ---
    void setProperty(int id, const SwAny& value)  { m_properties.insert(id, value); }
    SwAny property(int id) const {
        if (m_properties.contains(id)) return m_properties.value(id);
        return SwAny();
    }

    // --- Build a SwFont from this format ---
    SwFont toFont(const SwFont& base = SwFont()) const {
        SwFont f = base;
        if (m_hasFontFamily) {
            f.setFamily(std::wstring(m_fontFamily.begin(), m_fontFamily.end()));
        }
        if (m_hasFontPointSize) {
            f.setPointSize(m_fontPointSize);
        }
        if (m_hasFontWeight) {
            f.setWeight(m_fontWeight);
        }
        if (m_hasItalic) {
            f.setItalic(m_italic);
        }
        if (m_hasUnderlineStyle) {
            f.setUnderline(m_underlineStyle == SingleUnderline);
        }
        return f;
    }

    // --- Merge another format into this one (only set fields override) ---
    void merge(const SwTextCharFormat& other) {
        if (other.m_hasFontFamily)    { m_fontFamily = other.m_fontFamily; m_hasFontFamily = true; }
        if (other.m_hasFontPointSize) { m_fontPointSize = other.m_fontPointSize; m_hasFontPointSize = true; }
        if (other.m_hasFontWeight)    { m_fontWeight = other.m_fontWeight; m_hasFontWeight = true; }
        if (other.m_hasItalic)        { m_italic = other.m_italic; m_hasItalic = true; }
        if (other.m_hasUnderlineStyle) {
            m_underlineStyle = other.m_underlineStyle;
            m_hasUnderlineStyle = true;
        }
        if (other.m_hasUnderlineColor) {
            m_underlineColor = other.m_underlineColor;
            m_hasUnderlineColor = true;
        }
        if (other.m_hasStrikeOut)     { m_strikeOut = other.m_strikeOut; m_hasStrikeOut = true; }
        if (other.m_hasForeground)    { m_foreground = other.m_foreground; m_hasForeground = true; }
        if (other.m_hasBackground)    { m_background = other.m_background; m_hasBackground = true; }
        if (!other.m_href.isEmpty())  { m_href = other.m_href; }
        if (other.m_vertAlign != AlignNormal) { m_vertAlign = other.m_vertAlign; }
    }

    bool operator==(const SwTextCharFormat& o) const {
        return m_hasFontFamily == o.m_hasFontFamily && m_fontFamily == o.m_fontFamily &&
               m_hasFontPointSize == o.m_hasFontPointSize && m_fontPointSize == o.m_fontPointSize &&
               m_hasFontWeight == o.m_hasFontWeight && m_fontWeight == o.m_fontWeight &&
               m_hasItalic == o.m_hasItalic && m_italic == o.m_italic &&
               m_hasUnderlineStyle == o.m_hasUnderlineStyle && m_underlineStyle == o.m_underlineStyle &&
               m_hasUnderlineColor == o.m_hasUnderlineColor &&
               (!m_hasUnderlineColor || (m_underlineColor.r == o.m_underlineColor.r &&
                                         m_underlineColor.g == o.m_underlineColor.g &&
                                         m_underlineColor.b == o.m_underlineColor.b)) &&
               m_hasStrikeOut == o.m_hasStrikeOut && m_strikeOut == o.m_strikeOut &&
               m_hasForeground == o.m_hasForeground &&
               (!m_hasForeground || (m_foreground.r == o.m_foreground.r && m_foreground.g == o.m_foreground.g && m_foreground.b == o.m_foreground.b)) &&
               m_hasBackground == o.m_hasBackground &&
               (!m_hasBackground || (m_background.r == o.m_background.r && m_background.g == o.m_background.g && m_background.b == o.m_background.b)) &&
               m_href == o.m_href && m_vertAlign == o.m_vertAlign;
    }
    bool operator!=(const SwTextCharFormat& o) const { return !(*this == o); }

private:
    SwString m_fontFamily;
    int m_fontPointSize{0};
    FontWeight m_fontWeight{Normal};
    bool m_italic{false};
    UnderlineStyle m_underlineStyle{NoUnderline};
    SwColor m_underlineColor{0, 0, 0};
    bool m_strikeOut{false};
    SwColor m_foreground{0, 0, 0};
    SwColor m_background{255, 255, 255};
    SwString m_href;
    SwString m_anchorName;
    SwString m_toolTip;
    VerticalAlignment m_vertAlign{AlignNormal};
    SwMap<int, SwAny> m_properties;

    bool m_hasFontFamily{false};
    bool m_hasFontPointSize{false};
    bool m_hasFontWeight{false};
    bool m_hasItalic{false};
    bool m_hasUnderlineStyle{false};
    bool m_hasUnderlineColor{false};
    bool m_hasStrikeOut{false};
    bool m_hasForeground{false};
    bool m_hasBackground{false};
};

// ---------------------------------------------------------------------------
// SwTextBlockFormat — block/paragraph-level formatting (Qt6 QTextBlockFormat)
// ---------------------------------------------------------------------------

class SwTextBlockFormat {
public:
    SwTextBlockFormat() = default;

    enum Alignment { AlignLeft = 0, AlignCenter = 1, AlignRight = 2, AlignJustify = 3 };

    void setAlignment(Alignment a)        { m_alignment = a; }
    Alignment alignment() const           { return m_alignment; }

    void setIndent(int level)             { m_indent = level; }
    int indent() const                    { return m_indent; }

    void setHeadingLevel(int level)       { m_headingLevel = level; } // 0=normal, 1-6
    int headingLevel() const              { return m_headingLevel; }

    void setTopMargin(int m)              { m_topMargin = m; }
    int topMargin() const                 { return m_topMargin; }

    void setBottomMargin(int m)           { m_bottomMargin = m; }
    int bottomMargin() const              { return m_bottomMargin; }

    void setLeftMargin(int m)             { m_leftMargin = m; }
    int leftMargin() const                { return m_leftMargin; }

    void setRightMargin(int m)            { m_rightMargin = m; }
    int rightMargin() const               { return m_rightMargin; }

    void setLineHeight(int h)             { m_lineHeight = h; }
    int lineHeight() const                { return m_lineHeight; }

    void setTextIndent(int px)            { m_textIndent = px; }
    int textIndent() const                { return m_textIndent; }

    void setBackground(const SwColor& c)  { m_background = c; m_hasBackground = true; }
    SwColor background() const            { return m_background; }
    bool hasBackground() const            { return m_hasBackground; }

    // Page break control (for PDF/print)
    void setPageBreakBefore(bool on)      { m_pageBreakBefore = on; }
    bool pageBreakBefore() const          { return m_pageBreakBefore; }

    void setPageBreakAfter(bool on)       { m_pageBreakAfter = on; }
    bool pageBreakAfter() const           { return m_pageBreakAfter; }

    bool operator==(const SwTextBlockFormat& o) const {
        return m_alignment == o.m_alignment && m_indent == o.m_indent &&
               m_headingLevel == o.m_headingLevel && m_topMargin == o.m_topMargin &&
               m_bottomMargin == o.m_bottomMargin && m_leftMargin == o.m_leftMargin &&
               m_rightMargin == o.m_rightMargin && m_lineHeight == o.m_lineHeight &&
               m_textIndent == o.m_textIndent;
    }
    bool operator!=(const SwTextBlockFormat& o) const { return !(*this == o); }

private:
    Alignment m_alignment{AlignLeft};
    int m_indent{0};
    int m_headingLevel{0};
    int m_topMargin{0};
    int m_bottomMargin{0};
    int m_leftMargin{0};
    int m_rightMargin{0};
    int m_lineHeight{0};   // 0 = auto
    int m_textIndent{0};
    SwColor m_background{255, 255, 255};
    bool m_hasBackground{false};
    bool m_pageBreakBefore{false};
    bool m_pageBreakAfter{false};
};

// ---------------------------------------------------------------------------
// SwTextListFormat — list formatting (Qt6 QTextListFormat)
// ---------------------------------------------------------------------------

class SwTextListFormat {
public:
    SwTextListFormat() = default;

    enum Style {
        ListDisc,           // •
        ListCircle,         // ○
        ListSquare,         // ■
        ListDecimal,        // 1. 2. 3.
        ListLowerAlpha,     // a. b. c.
        ListUpperAlpha,     // A. B. C.
        ListLowerRoman,     // i. ii. iii.
        ListUpperRoman      // I. II. III.
    };

    void setStyle(Style s)                { m_style = s; }
    Style style() const                   { return m_style; }

    void setIndent(int level)             { m_indent = level; }
    int indent() const                    { return m_indent; }

    void setPrefix(const SwString& p)     { m_prefix = p; }
    SwString prefix() const               { return m_prefix; }

    void setSuffix(const SwString& s)     { m_suffix = s; }
    SwString suffix() const               { return m_suffix; }

    void setStart(int n)                  { m_start = n; }
    int start() const                     { return m_start; }

    bool isOrdered() const {
        return m_style >= ListDecimal;
    }

    SwString bulletText(int index) const {
        SwString result;
        result.append(m_prefix);
        switch (m_style) {
        case ListDisc:       result.append("\xe2\x80\xa2"); break; // •
        case ListCircle:     result.append("\xe2\x97\x8b"); break; // ○
        case ListSquare:     result.append("\xe2\x96\xaa"); break; // ▪
        case ListDecimal:    result.append(SwString::number(m_start + index)); break;
        case ListLowerAlpha: result.append(SwString(1, static_cast<char>('a' + ((m_start - 1 + index) % 26)))); break;
        case ListUpperAlpha: result.append(SwString(1, static_cast<char>('A' + ((m_start - 1 + index) % 26)))); break;
        case ListLowerRoman: result.append(toRoman(m_start + index, false)); break;
        case ListUpperRoman: result.append(toRoman(m_start + index, true)); break;
        }
        // Only append suffix for ordered list styles (not for bullet symbols)
        if (m_style != ListDisc && m_style != ListCircle && m_style != ListSquare) {
            result.append(m_suffix);
        }
        return result;
    }

private:
    Style m_style{ListDisc};
    int m_indent{1};
    SwString m_prefix;
    SwString m_suffix{"."};
    int m_start{1};

    static SwString toRoman(int value, bool upper) {
        if (value <= 0 || value > 3999) return SwString::number(value);
        struct RomanPair { int val; const char* str; };
        static const RomanPair table[] = {
            {1000,"m"},{900,"cm"},{500,"d"},{400,"cd"},{100,"c"},{90,"xc"},
            {50,"l"},{40,"xl"},{10,"x"},{9,"ix"},{5,"v"},{4,"iv"},{1,"i"}
        };
        SwString result;
        for (const auto& p : table) {
            while (value >= p.val) {
                result.append(SwString(p.str));
                value -= p.val;
            }
        }
        if (upper) {
            std::string s = result.toStdString();
            for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return SwString(s);
        }
        return result;
    }
};

// ---------------------------------------------------------------------------
// SwTextTableFormat — table formatting (Qt6 QTextTableFormat)
// ---------------------------------------------------------------------------

class SwTextTableFormat {
public:
    SwTextTableFormat() = default;

    void setBorderWidth(int w)            { m_borderWidth = w; }
    int borderWidth() const               { return m_borderWidth; }

    void setBorderColor(const SwColor& c) { m_borderColor = c; }
    SwColor borderColor() const           { return m_borderColor; }

    void setCellPadding(int p)            { m_cellPadding = p; }
    int cellPadding() const               { return m_cellPadding; }

    void setCellSpacing(int s)            { m_cellSpacing = s; }
    int cellSpacing() const               { return m_cellSpacing; }

    void setWidth(int w)                  { m_width = w; m_widthIsPercent = false; }
    void setWidthPercent(int pct)         { m_width = pct; m_widthIsPercent = true; }
    int width() const                     { return m_width; }
    bool widthIsPercent() const           { return m_widthIsPercent; }

    void setAlignment(SwTextBlockFormat::Alignment a) { m_alignment = a; }
    SwTextBlockFormat::Alignment alignment() const    { return m_alignment; }

    void setBackground(const SwColor& c)  { m_background = c; m_hasBackground = true; }
    SwColor background() const            { return m_background; }
    bool hasBackground() const            { return m_hasBackground; }

    void setColumnWidthConstraints(const SwList<int>& widths) { m_colWidths = widths; }
    SwList<int> columnWidthConstraints() const { return m_colWidths; }

private:
    int m_borderWidth{1};
    SwColor m_borderColor{0, 0, 0};
    int m_cellPadding{2};
    int m_cellSpacing{2};
    int m_width{0};
    bool m_widthIsPercent{true};
    SwTextBlockFormat::Alignment m_alignment{SwTextBlockFormat::AlignLeft};
    SwColor m_background{255, 255, 255};
    bool m_hasBackground{false};
    SwList<int> m_colWidths;
};

// ---------------------------------------------------------------------------
// SwTextTableCellFormat — table cell formatting (Qt6 QTextTableCellFormat)
// ---------------------------------------------------------------------------

class SwTextTableCellFormat {
public:
    SwTextTableCellFormat() = default;

    void setBackground(const SwColor& c)  { m_background = c; m_hasBackground = true; }
    SwColor background() const            { return m_background; }
    bool hasBackground() const            { return m_hasBackground; }

    void setPadding(int p)                { m_topPad = m_bottomPad = m_leftPad = m_rightPad = p; }
    void setTopPadding(int p)             { m_topPad = p; }
    void setBottomPadding(int p)          { m_bottomPad = p; }
    void setLeftPadding(int p)            { m_leftPad = p; }
    void setRightPadding(int p)           { m_rightPad = p; }
    int topPadding() const                { return m_topPad; }
    int bottomPadding() const             { return m_bottomPad; }
    int leftPadding() const               { return m_leftPad; }
    int rightPadding() const              { return m_rightPad; }

    enum VerticalAlignment { AlignTop, AlignMiddle, AlignBottom };
    void setVerticalAlignment(VerticalAlignment a) { m_vertAlign = a; }
    VerticalAlignment verticalAlignment() const    { return m_vertAlign; }

private:
    SwColor m_background{255, 255, 255};
    bool m_hasBackground{false};
    int m_topPad{2};
    int m_bottomPad{2};
    int m_leftPad{2};
    int m_rightPad{2};
    VerticalAlignment m_vertAlign{AlignTop};
};

// ---------------------------------------------------------------------------
// SwTextImageFormat — inline image formatting (Qt6 QTextImageFormat)
// ---------------------------------------------------------------------------

class SwTextImageFormat {
public:
    SwTextImageFormat() = default;

    void setName(const SwString& name)    { m_name = name; }
    SwString name() const                 { return m_name; }

    void setWidth(int w)                  { m_width = w; }
    int width() const                     { return m_width; }

    void setHeight(int h)                 { m_height = h; }
    int height() const                    { return m_height; }

    void setImage(const SwImage& img)     { m_image = img; }
    SwImage image() const                 { return m_image; }
    bool hasImage() const                 { return !m_image.isNull(); }

private:
    SwString m_name;
    int m_width{0};   // 0 = natural size
    int m_height{0};
    SwImage m_image;
};
