/***************************************************************************************************
 * 38-DocumentEditor — Example: HTML document display + PDF export
 *
 * Demonstrates the Qt-like document architecture:
 *   SwTextDocument  → document model (blocks, fragments, lists, tables)
 *   SwTextCursor    → programmatic editing
 *   SwTextEdit      → widget display with document() / setDocument() / print()
 *   SwPdfWriter     → PDF 1.4 generation
 *
 * This example builds a rich document two ways:
 *   1) Via SwTextCursor (programmatic, like Qt)
 *   2) Via setHtml()   (from HTML string)
 * Then displays it in a SwTextEdit and exports to PDF on button click.
 ***************************************************************************************************/

#include "SwCodeEditor.h"
#include "SwCompleter.h"
#include "SwCppCompletionIndex.h"
#include "SwCppDiagnosticsProvider.h"
#include "SwCppSyntaxHighlighter.h"
#include "SwDir.h"
#include "SwFileDialog.h"
#include "SwFile.h"
#include "SwGuiApplication.h"
#include "SwLabel.h"
#include "SwLayout.h"
#include "SwMainWindow.h"
#include "SwMessageBox.h"
#include "SwPushButton.h"
#include "SwStandardItemModel.h"
#include "editorTheme/SwCodeEditorVisualTheme.h"
#include "core/runtime/SwCrashHandler.h"
#include "core/types/SwDebug.h"
#include "core/types/SwMap.h"
#include "SwString.h"
#include "SwTabWidget.h"
#include "SwTextCursor.h"
#include "SwTextDocument.h"
#include "SwTextDocumentWriter.h"
#include "SwTextEdit.h"

#include <algorithm>
#include <cstdlib>
#include <cctype>

// ---------------------------------------------------------------------------
// Build a document programmatically (the "Qt way")
// ---------------------------------------------------------------------------
static SwTextDocument* buildDocumentWithCursor() {
    SwTextDocument* doc = new SwTextDocument();
    SwTextCursor cursor(doc);

    // ── Title ──
    SwTextCharFormat titleFmt;
    titleFmt.setFontPointSize(24);
    titleFmt.setFontWeight(Bold);
    titleFmt.setForeground(SwColor{30, 60, 120});
    cursor.insertText("SwStack Document Editor", titleFmt);
    cursor.insertBlock();

    // ── Subtitle ──
    SwTextCharFormat subtitleFmt;
    subtitleFmt.setFontPointSize(14);
    subtitleFmt.setFontItalic(true);
    subtitleFmt.setForeground(SwColor{100, 100, 100});
    cursor.insertText("A Qt-like document model for the SwStack framework", subtitleFmt);
    cursor.insertBlock();
    cursor.insertBlock();

    // ── Body paragraph ──
    SwTextCharFormat bodyFmt;
    bodyFmt.setFontPointSize(11);
    cursor.insertText("This document was built ", bodyFmt);

    SwTextCharFormat boldFmt;
    boldFmt.setFontPointSize(11);
    boldFmt.setFontWeight(Bold);
    cursor.insertText("programmatically", boldFmt);

    cursor.insertText(" using ", bodyFmt);

    SwTextCharFormat codeFmt;
    codeFmt.setFontPointSize(11);
    codeFmt.setFontFamily(SwString("Courier New"));
    codeFmt.setBackground(SwColor{240, 240, 240});
    cursor.insertText("SwTextCursor", codeFmt);

    cursor.insertText(", just like you would with ", bodyFmt);

    SwTextCharFormat italicFmt;
    italicFmt.setFontPointSize(11);
    italicFmt.setFontItalic(true);
    cursor.insertText("QTextCursor", italicFmt);

    cursor.insertText(" in Qt.", bodyFmt);
    cursor.insertBlock();
    cursor.insertBlock();

    // ── Heading ──
    SwTextCharFormat h2Fmt;
    h2Fmt.setFontPointSize(18);
    h2Fmt.setFontWeight(Bold);
    cursor.insertText("Features", h2Fmt);
    cursor.insertBlock();

    // ── Bullet list ──
    SwTextListFormat listFmt;
    listFmt.setStyle(SwTextListFormat::ListDisc);

    SwTextList* list = cursor.insertList(listFmt);
    cursor.insertText("Rich text with ", bodyFmt);
    { SwTextCharFormat bf = bodyFmt; bf.setFontWeight(Bold); cursor.insertText("bold", bf); }
    cursor.insertText(", ", bodyFmt);
    { SwTextCharFormat itf = bodyFmt; itf.setFontItalic(true); cursor.insertText("italic", itf); }
    cursor.insertText(", ", bodyFmt);
    { SwTextCharFormat uf = bodyFmt; uf.setFontUnderline(true); cursor.insertText("underline", uf); }
    cursor.insertText(", ", bodyFmt);
    { SwTextCharFormat sf = bodyFmt; sf.setFontStrikeOut(true); cursor.insertText("strikethrough", sf); }

    cursor.insertListItem(list);
    cursor.insertText("Headings h1 through h6 with proper font sizes", bodyFmt);

    cursor.insertListItem(list);
    cursor.insertText("Ordered and unordered lists with nesting", bodyFmt);

    cursor.insertListItem(list);
    cursor.insertText("Tables with cell formatting", bodyFmt);

    cursor.insertListItem(list);
    cursor.insertText("Background colors and text highlights", bodyFmt);

    cursor.insertListItem(list);

    SwTextCharFormat linkFmt;
    linkFmt.setFontPointSize(11);
    linkFmt.setAnchorHref(SwString("https://github.com"));
    linkFmt.setForeground(SwColor{0, 0, 238});
    linkFmt.setFontUnderline(true);
    cursor.insertText("Hyperlinks", linkFmt);
    cursor.insertText(" with proper styling", bodyFmt);

    cursor.insertBlock();
    cursor.insertBlock();

    // ── Heading ──
    cursor.insertText("Color Palette", h2Fmt);
    cursor.insertBlock();

    // ── Colored text demo ──
    struct ColorEntry { const char* name; SwColor color; };
    ColorEntry colors[] = {
        {"Red",     {220, 50, 50}},
        {"Green",   {50, 160, 50}},
        {"Blue",    {50, 50, 220}},
        {"Orange",  {230, 140, 20}},
        {"Purple",  {140, 50, 180}},
    };
    for (int ci = 0; ci < 5; ++ci) {
        SwTextCharFormat colorFmt;
        colorFmt.setFontPointSize(12);
        colorFmt.setFontWeight(Bold);
        colorFmt.setForeground(colors[ci].color);
        cursor.insertText(SwString(colors[ci].name), colorFmt);
        cursor.insertText("  ", bodyFmt);
    }
    cursor.insertBlock();
    cursor.insertBlock();

    // ── Strikethrough demo ──
    SwTextCharFormat strikeFmt;
    strikeFmt.setFontPointSize(11);
    strikeFmt.setFontStrikeOut(true);
    cursor.insertText("This text is struck through", strikeFmt);
    cursor.insertText("  —  ", bodyFmt);

    SwTextCharFormat highlightFmt;
    highlightFmt.setFontPointSize(11);
    highlightFmt.setBackground(SwColor{255, 255, 100});
    cursor.insertText("This text is highlighted", highlightFmt);
    cursor.insertBlock();
    cursor.insertBlock();

    // ── Table demo ──
    cursor.insertText("Sample Table", h2Fmt);
    cursor.insertBlock();

    SwTextTableFormat tableFmt;
    tableFmt.setBorderWidth(1);
    tableFmt.setCellPadding(4);
    tableFmt.setBorderColor(SwColor{100, 100, 100});

    SwTextTable* table = doc->insertTable(cursor.position(), 3, 3, tableFmt);

    // Header row
    SwTextCharFormat headerFmt;
    headerFmt.setFontWeight(Bold);
    headerFmt.setFontPointSize(11);
    SwTextBlock hb0; hb0.appendFragment(SwTextFragment(SwString("Feature"), headerFmt));
    SwTextBlock hb1; hb1.appendFragment(SwTextFragment(SwString("Status"), headerFmt));
    SwTextBlock hb2; hb2.appendFragment(SwTextFragment(SwString("Notes"), headerFmt));
    table->cellAt(0, 0).blocks.clear(); table->cellAt(0, 0).blocks.append(hb0);
    table->cellAt(0, 1).blocks.clear(); table->cellAt(0, 1).blocks.append(hb1);
    table->cellAt(0, 2).blocks.clear(); table->cellAt(0, 2).blocks.append(hb2);

    // Header bg
    SwTextTableCellFormat hdrCellFmt;
    hdrCellFmt.setBackground(SwColor{220, 230, 245});
    hdrCellFmt.setPadding(4);
    table->setCellFormat(0, 0, hdrCellFmt);
    table->setCellFormat(0, 1, hdrCellFmt);
    table->setCellFormat(0, 2, hdrCellFmt);

    // Data rows
    auto setCell = [&](int r, int c, const char* text) {
        SwTextBlock b; b.appendFragment(SwTextFragment(SwString(text), bodyFmt));
        table->cellAt(r, c).blocks.clear(); table->cellAt(r, c).blocks.append(b);
    };
    setCell(1, 0, "Rich text");    setCell(1, 1, "Done");   setCell(1, 2, "Bold, italic, underline");
    setCell(2, 0, "PDF export");   setCell(2, 1, "Done");   setCell(2, 2, "With AFM glyph widths");

    // Green background for "Done" cells
    SwTextTableCellFormat doneFmt;
    doneFmt.setBackground(SwColor{220, 245, 220});
    doneFmt.setPadding(4);
    table->setCellFormat(1, 1, doneFmt);
    table->setCellFormat(2, 1, doneFmt);

    cursor.insertBlock();
    cursor.insertBlock();

    // ── Footer ──
    SwTextCharFormat footerFmt;
    footerFmt.setFontPointSize(9);
    footerFmt.setForeground(SwColor{150, 150, 150});
    footerFmt.setFontItalic(true);
    cursor.insertText("Generated by SwStack SwTextDocument + SwTextCursor", footerFmt);

    return doc;
}

// ---------------------------------------------------------------------------
// Build the same kind of document from HTML
// ---------------------------------------------------------------------------
static const char* sampleHtml = R"(
<h1 style="color: rgb(30,60,120);">SwStack Document Editor</h1>
<p><i style="color: gray; font-size: 14pt;">A Qt-like document model for the SwStack framework</i></p>

<p style="font-size: 11pt;">
  This document was loaded from <b>HTML</b> using
  <code>SwTextEdit::setHtml()</code>, just like
  <i>QTextEdit</i> in Qt.
</p>

<h2>Features</h2>
<ul>
  <li>Rich text with <b>bold</b>, <i>italic</i>, <u>underline</u>, <s>strikethrough</s></li>
  <li>Headings h1 through h6 with proper font sizes</li>
  <li>Ordered and unordered lists with nesting</li>
  <li>Tables with cell formatting</li>
  <li>Background colors and <mark>text highlights</mark></li>
  <li><a href="https://github.com">Hyperlinks</a> with proper styling</li>
</ul>

<h2>Numbered List</h2>
<ol>
  <li>First item</li>
  <li>Second item</li>
  <li>Third item</li>
</ol>

<h2>Color Palette</h2>
<p>
  <span style="color: rgb(220,50,50); font-size: 12pt;"><b>Red</b></span>
  <span style="color: rgb(50,160,50); font-size: 12pt;"><b>Green</b></span>
  <span style="color: rgb(50,50,220); font-size: 12pt;"><b>Blue</b></span>
  <span style="color: rgb(230,140,20); font-size: 12pt;"><b>Orange</b></span>
  <span style="color: rgb(140,50,180); font-size: 12pt;"><b>Purple</b></span>
</p>

<p>
  <s>This text is struck through</s> —
  <span style="background-color: rgb(255,255,100);">This text is highlighted</span>
</p>

<h2>Sample Table</h2>
<table border="1" cellpadding="4" cellspacing="0">
  <tr>
    <td style="background-color: rgb(220,230,245);"><b>Feature</b></td>
    <td style="background-color: rgb(220,230,245);"><b>Status</b></td>
    <td style="background-color: rgb(220,230,245);"><b>Notes</b></td>
  </tr>
  <tr>
    <td>Rich text</td>
    <td style="background-color: rgb(220,245,220);">Done</td>
    <td>Bold, italic, underline</td>
  </tr>
  <tr>
    <td>PDF export</td>
    <td style="background-color: rgb(220,245,220);">Done</td>
    <td>With AFM glyph widths</td>
  </tr>
</table>

<hr/>

<p><i style="color: gray; font-size: 9pt;">Generated by SwStack SwTextDocument + SwTextEdit::setHtml()</i></p>
)";

static const char* sampleCode = R"(#include <array>
#include <iostream>
#include <string_view>
#include <vector>

#define SW_TRACE(value) std::cout << #value << " = " << (value) << std::endl

namespace demo {

class Greeter {
public:
    explicit Greeter(std::string_view name)
        : m_name(name)
        , m_magic(0x2A) {}

    void run() const {
        constexpr double kPi = 3.1415926535;
        const std::array<int, 4> values{{1, 2, 3, 4}};
        const char marker = '\n';
        const std::string banner = "SwCodeEditor with VS Code style";

        // Dark+ inspired C++ syntax highlighting.
        for (int value : values) {
            SW_TRACE(value);
        }

        /*
         * Multi-line comments should stay green
         * across the whole block.
         */
        if (m_magic > 0 && marker == '\n') {
            std::cout << banner << " for " << m_name << " (" << kPi << ')' << std::endl;
        }
    }

private:
    std::string_view m_name;
    int m_magic;
};

} // namespace demo

int main() {
    demo::Greeter greeter("SwStack");
    greeter.run();
    return 0;
}
)";

struct ParsedClassRange {
    SwString name;
    size_t openBrace{static_cast<size_t>(-1)};
    size_t closeBrace{static_cast<size_t>(-1)};
};

enum class CompletionMemberKind {
    Method,
    Field
};

enum class CompletionMemberAccess {
    Public,
    Protected,
    Private
};

struct CompletionMemberInfo {
    SwString name;
    CompletionMemberKind kind{CompletionMemberKind::Field};
    CompletionMemberAccess access{CompletionMemberAccess::Public};
};

struct CompletionTypeInfo {
    SwString qualifiedName;
    SwString leafName;
    SwList<CompletionMemberInfo> members;
};

struct CompletionTypeTable {
    SwList<SwString> typeNames;
    SwMap<SwString, CompletionTypeInfo> typeInfos;
};

struct CompletionVariableInfo {
    SwString typeName;
    bool isPointer{false};
};

struct MemberAccessContext {
    bool valid{false};
    bool usesArrow{false};
    size_t operatorPos{static_cast<size_t>(-1)};
    SwString baseName;
    SwString memberPrefix;
};

struct CompletionItem {
    SwString displayText;
    SwString insertText;
    SwString toolTip;
};

static bool isCompletionIdentifierStart(char ch) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    return std::isalpha(uch) != 0 || uch == static_cast<unsigned char>('_');
}

static bool isCompletionIdentifierPart(char ch) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    return std::isalnum(uch) != 0 || uch == static_cast<unsigned char>('_');
}

static bool isKeywordBoundary_(const SwString& text, size_t pos, size_t length) {
    if (pos > 0 && isCompletionIdentifierPart(text[pos - 1])) {
        return false;
    }
    const size_t end = pos + length;
    return end >= text.size() || !isCompletionIdentifierPart(text[end]);
}

static bool startsWithKeywordAt_(const SwString& text, size_t pos, const char* keyword) {
    if (!keyword) {
        return false;
    }

    const SwString needle(keyword);
    if (pos + needle.size() > text.size()) {
        return false;
    }
    if (text.substr(pos, needle.size()) != needle) {
        return false;
    }
    return isKeywordBoundary_(text, pos, needle.size());
}

static size_t skipSpaces_(const SwString& text, size_t pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
        ++pos;
    }
    return pos;
}

static SwString readIdentifier_(const SwString& text, size_t& pos) {
    pos = skipSpaces_(text, pos);
    if (pos >= text.size() || !isCompletionIdentifierStart(text[pos])) {
        return SwString();
    }

    const size_t start = pos;
    ++pos;
    while (pos < text.size() && isCompletionIdentifierPart(text[pos])) {
        ++pos;
    }
    return text.substr(start, pos - start);
}

static size_t findMatchingBrace_(const SwString& text, size_t openBrace) {
    if (openBrace >= text.size() || text[openBrace] != '{') {
        return static_cast<size_t>(-1);
    }

    int depth = 1;
    for (size_t i = openBrace + 1; i < text.size(); ++i) {
        if (text[i] == '{') {
            ++depth;
        } else if (text[i] == '}') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return static_cast<size_t>(-1);
}

static SwString sanitizeCodeForParsing_(const SwString& text) {
    SwString sanitized = text;

    enum class ParseState {
        Normal,
        LineComment,
        BlockComment,
        StringLiteral,
        CharLiteral
    };

    ParseState state = ParseState::Normal;
    bool escaped = false;

    for (size_t i = 0; i < sanitized.size(); ++i) {
        const char ch = sanitized[i];
        const char next = (i + 1 < sanitized.size()) ? sanitized[i + 1] : '\0';

        switch (state) {
            case ParseState::Normal:
                if (ch == '/' && next == '/') {
                    sanitized[i] = ' ';
                    sanitized[i + 1] = ' ';
                    ++i;
                    state = ParseState::LineComment;
                } else if (ch == '/' && next == '*') {
                    sanitized[i] = ' ';
                    sanitized[i + 1] = ' ';
                    ++i;
                    state = ParseState::BlockComment;
                } else if (ch == '"') {
                    sanitized[i] = ' ';
                    state = ParseState::StringLiteral;
                    escaped = false;
                } else if (ch == '\'') {
                    sanitized[i] = ' ';
                    state = ParseState::CharLiteral;
                    escaped = false;
                }
                break;

            case ParseState::LineComment:
                if (ch != '\n') {
                    sanitized[i] = ' ';
                } else {
                    state = ParseState::Normal;
                }
                break;

            case ParseState::BlockComment:
                if (ch == '*' && next == '/') {
                    sanitized[i] = ' ';
                    sanitized[i + 1] = ' ';
                    ++i;
                    state = ParseState::Normal;
                } else if (ch != '\n') {
                    sanitized[i] = ' ';
                }
                break;

            case ParseState::StringLiteral:
                if (ch != '\n') {
                    sanitized[i] = ' ';
                }
                if (!escaped && ch == '"') {
                    state = ParseState::Normal;
                }
                escaped = (!escaped && ch == '\\');
                break;

            case ParseState::CharLiteral:
                if (ch != '\n') {
                    sanitized[i] = ' ';
                }
                if (!escaped && ch == '\'') {
                    state = ParseState::Normal;
                }
                escaped = (!escaped && ch == '\\');
                break;
        }
    }

    return sanitized;
}

static SwString collapseWhitespace_(const SwString& text) {
    SwString collapsed;
    collapsed.reserve(text.size());

    bool pendingSpace = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (std::isspace(ch) != 0) {
            pendingSpace = !collapsed.isEmpty();
            continue;
        }
        if (pendingSpace) {
            collapsed.append(' ');
            pendingSpace = false;
        }
        collapsed.append(text[i]);
    }

    return collapsed.trimmed();
}

static bool isIgnoredDeclarationHead_(const SwString& token) {
    return token == SwString("if") ||
           token == SwString("for") ||
           token == SwString("while") ||
           token == SwString("switch") ||
           token == SwString("catch") ||
           token == SwString("return") ||
           token == SwString("class") ||
           token == SwString("struct") ||
           token == SwString("enum") ||
           token == SwString("namespace") ||
           token == SwString("using") ||
           token == SwString("typedef") ||
           token == SwString("template") ||
           token == SwString("friend") ||
           token == SwString("static_assert");
}

static bool shouldKeepCompletionWord_(const SwString& word) {
    if (word.size() < 2 || !isCompletionIdentifierStart(word[0])) {
        return false;
    }
    return !isIgnoredDeclarationHead_(word);
}

static void appendUniqueCompletionWord_(SwList<SwString>& words, const SwString& word) {
    if (!shouldKeepCompletionWord_(word)) {
        return;
    }

    for (int i = 0; i < words.size(); ++i) {
        if (words[i] == word) {
            return;
        }
    }
    words.append(word);
}

static bool containsString_(const SwList<SwString>& values, const SwString& value) {
    for (int i = 0; i < values.size(); ++i) {
        if (values[i] == value) {
            return true;
        }
    }
    return false;
}

static void appendUniqueString_(SwList<SwString>& values, const SwString& value) {
    if (value.isEmpty() || containsString_(values, value)) {
        return;
    }
    values.append(value);
}

static SwString extractIdentifierBefore_(const SwString& text, size_t pos) {
    size_t end = std::min(pos, text.size());
    while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    if (end == 0) {
        return SwString();
    }

    size_t start = end;
    if (!isCompletionIdentifierPart(text[end - 1])) {
        return SwString();
    }
    while (start > 0 && isCompletionIdentifierPart(text[start - 1])) {
        --start;
    }
    if (start > 0 && text[start - 1] == '~') {
        --start;
    }

    return text.substr(start, end - start);
}

static SwList<SwString> splitTopLevelComma_(const SwString& text) {
    SwList<SwString> parts;
    size_t segmentStart = 0;
    int angleDepth = 0;
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;

    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '<') {
            ++angleDepth;
        } else if (ch == '>' && angleDepth > 0) {
            --angleDepth;
        } else if (ch == '(') {
            ++parenDepth;
        } else if (ch == ')' && parenDepth > 0) {
            --parenDepth;
        } else if (ch == '[') {
            ++bracketDepth;
        } else if (ch == ']' && bracketDepth > 0) {
            --bracketDepth;
        } else if (ch == '{') {
            ++braceDepth;
        } else if (ch == '}' && braceDepth > 0) {
            --braceDepth;
        } else if (ch == ',' && angleDepth == 0 && parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
            parts.append(text.substr(segmentStart, i - segmentStart));
            segmentStart = i + 1;
        }
    }

    if (segmentStart <= text.size()) {
        parts.append(text.substr(segmentStart));
    }
    return parts;
}

static SwString joinQualifiedName_(const SwList<SwString>& scope, const SwString& leaf) {
    if (scope.isEmpty()) {
        return leaf;
    }

    SwString qualified = scope[0];
    for (int i = 1; i < scope.size(); ++i) {
        qualified += "::";
        qualified += scope[i];
    }

    if (!leaf.isEmpty()) {
        qualified += "::";
        qualified += leaf;
    }
    return qualified;
}

static SwString leafNameOfQualified_(const SwString& qualifiedName) {
    const size_t separator = qualifiedName.lastIndexOf(SwString("::"));
    if (separator == static_cast<size_t>(-1)) {
        return qualifiedName;
    }
    return qualifiedName.substr(separator + 2);
}

static size_t findMatchingParen_(const SwString& text, size_t openParen) {
    if (openParen >= text.size() || text[openParen] != '(') {
        return static_cast<size_t>(-1);
    }

    int depth = 1;
    for (size_t i = openParen + 1; i < text.size(); ++i) {
        if (text[i] == '(') {
            ++depth;
        } else if (text[i] == ')') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return static_cast<size_t>(-1);
}

static size_t skipSpacesBackward_(const SwString& text, size_t pos) {
    if (text.isEmpty()) {
        return static_cast<size_t>(-1);
    }

    size_t cursor = std::min(pos, text.size());
    while (cursor > 0 && std::isspace(static_cast<unsigned char>(text[cursor - 1])) != 0) {
        --cursor;
    }
    return (cursor == 0) ? static_cast<size_t>(-1) : (cursor - 1);
}

static bool isCompletionTypeDeclarationHead_(const SwString& text, size_t pos) {
    return startsWithKeywordAt_(text, pos, "class") ||
           startsWithKeywordAt_(text, pos, "struct") ||
           startsWithKeywordAt_(text, pos, "enum");
}

static bool isCompletionTypeDeclarationHeaderText_(const SwString& header) {
    return header.startsWith("class ") ||
           header.startsWith("struct ") ||
           header.startsWith("enum ") ||
           header.startsWith("enum class ");
}

static SwString extractDeclaredNameFromDeclarator_(const SwString& declarator) {
    SwString part = collapseWhitespace_(declarator).trimmed();
    while (!part.isEmpty() && (part[0] == '*' || part[0] == '&')) {
        part.remove(0, 1);
        part = part.trimmed();
    }

    const size_t equalPos = part.firstIndexOf('=');
    if (equalPos != static_cast<size_t>(-1)) {
        part = part.substr(0, equalPos).trimmed();
    }

    const size_t bracePos = part.firstIndexOf('{');
    if (bracePos != static_cast<size_t>(-1)) {
        part = part.substr(0, bracePos).trimmed();
    }

    const size_t bracketPos = part.firstIndexOf('[');
    if (bracketPos != static_cast<size_t>(-1)) {
        return extractIdentifierBefore_(part, bracketPos);
    }

    const size_t parenPos = part.firstIndexOf('(');
    if (parenPos != static_cast<size_t>(-1)) {
        return extractIdentifierBefore_(part, parenPos);
    }

    return extractIdentifierBefore_(part, part.size());
}

static bool isPointerDeclarator_(const SwString& declarator, const SwString& declaredName) {
    if (declaredName.isEmpty()) {
        return false;
    }

    const SwString compact = collapseWhitespace_(declarator).trimmed();
    const size_t namePos = compact.lastIndexOf(declaredName);
    if (namePos == static_cast<size_t>(-1)) {
        return compact.firstIndexOf('*') != static_cast<size_t>(-1);
    }

    return compact.substr(0, namePos).firstIndexOf('*') != static_cast<size_t>(-1);
}

static SwString readQualifiedIdentifier_(const SwString& text, size_t& pos) {
    pos = skipSpaces_(text, pos);
    if (pos >= text.size() || !isCompletionIdentifierStart(text[pos])) {
        return SwString();
    }

    const size_t start = pos;
    ++pos;
    while (pos < text.size() && isCompletionIdentifierPart(text[pos])) {
        ++pos;
    }

    while (pos + 1 < text.size() && text[pos] == ':' && text[pos + 1] == ':') {
        const size_t separatorPos = pos;
        pos += 2;
        if (pos >= text.size() || !isCompletionIdentifierStart(text[pos])) {
            pos = separatorPos;
            break;
        }
        ++pos;
        while (pos < text.size() && isCompletionIdentifierPart(text[pos])) {
            ++pos;
        }
    }

    return text.substr(start, pos - start);
}

static size_t skipTemplateArguments_(const SwString& text, size_t pos) {
    if (pos >= text.size() || text[pos] != '<') {
        return pos;
    }

    int depth = 0;
    for (size_t i = pos; i < text.size(); ++i) {
        if (text[i] == '<') {
            ++depth;
        } else if (text[i] == '>' && depth > 0) {
            --depth;
            if (depth == 0) {
                return i + 1;
            }
        }
    }

    return pos;
}

static bool isCompletionLeadingDeclarationQualifier_(const SwString& token) {
    return token == SwString("const") ||
           token == SwString("constexpr") ||
           token == SwString("static") ||
           token == SwString("inline") ||
           token == SwString("volatile") ||
           token == SwString("mutable") ||
           token == SwString("typename") ||
           token == SwString("extern") ||
           token == SwString("friend");
}

static bool isCompletionPostTypeQualifier_(const SwString& token) {
    return token == SwString("const") ||
           token == SwString("volatile") ||
           token == SwString("constexpr");
}

static void registerCompletionType_(CompletionTypeTable& table, const SwString& qualifiedTypeName) {
    if (qualifiedTypeName.isEmpty() || table.typeInfos.contains(qualifiedTypeName)) {
        return;
    }

    CompletionTypeInfo info;
    info.qualifiedName = qualifiedTypeName;
    info.leafName = leafNameOfQualified_(qualifiedTypeName);
    table.typeInfos.insert(qualifiedTypeName, info);
    appendUniqueString_(table.typeNames, qualifiedTypeName);
}

static void appendCompletionMember_(CompletionTypeTable& table,
                                    const SwString& qualifiedTypeName,
                                    const CompletionMemberInfo& member) {
    if (qualifiedTypeName.isEmpty() || member.name.isEmpty()) {
        return;
    }

    registerCompletionType_(table, qualifiedTypeName);
    CompletionTypeInfo info = table.typeInfos.value(qualifiedTypeName);
    for (int i = 0; i < info.members.size(); ++i) {
        if (info.members[i].name == member.name && info.members[i].kind == member.kind) {
            return;
        }
    }
    info.members.append(member);
    table.typeInfos.insert(qualifiedTypeName, info);
}

static SwString resolveKnownCompletionTypeName_(const CompletionTypeTable& table, const SwString& candidate) {
    if (candidate.isEmpty()) {
        return SwString();
    }
    if (table.typeInfos.contains(candidate)) {
        return candidate;
    }

    SwString resolved;
    for (int i = 0; i < table.typeNames.size(); ++i) {
        const SwString& typeName = table.typeNames[i];
        if (typeName == candidate || typeName.endsWith(SwString("::") + candidate)) {
            if (!resolved.isEmpty() && resolved != typeName) {
                return SwString();
            }
            resolved = typeName;
        }
    }
    return resolved;
}

static SwString resolveCompletionTypeNameAt_(const SwString& statement,
                                             const CompletionTypeTable& table,
                                             size_t& typeEnd) {
    size_t pos = skipSpaces_(statement, 0);
    while (pos < statement.size()) {
        size_t tokenPos = pos;
        const SwString token = readIdentifier_(statement, tokenPos);
        if (token.isEmpty() || !isCompletionLeadingDeclarationQualifier_(token)) {
            break;
        }
        pos = skipSpaces_(statement, tokenPos);
    }

    size_t candidatePos = pos;
    const SwString candidate = readQualifiedIdentifier_(statement, candidatePos);
    if (candidate.isEmpty()) {
        typeEnd = static_cast<size_t>(-1);
        return SwString();
    }

    size_t afterType = skipSpaces_(statement, candidatePos);
    const size_t afterTemplate = skipTemplateArguments_(statement, afterType);
    if (afterTemplate != afterType) {
        afterType = skipSpaces_(statement, afterTemplate);
    }

    while (afterType < statement.size()) {
        size_t tokenPos = afterType;
        const SwString token = readIdentifier_(statement, tokenPos);
        if (token.isEmpty() || !isCompletionPostTypeQualifier_(token)) {
            break;
        }
        afterType = skipSpaces_(statement, tokenPos);
    }

    typeEnd = afterType;
    return resolveKnownCompletionTypeName_(table, candidate);
}

static void registerDeclaredVariablesFromStatement_(const SwString& statement,
                                                    const CompletionTypeTable& table,
                                                    SwMap<SwString, CompletionVariableInfo>& variables) {
    const SwString compact = collapseWhitespace_(statement);
    if (compact.isEmpty()) {
        return;
    }

    size_t firstTokenPos = 0;
    const SwString firstToken = readIdentifier_(compact, firstTokenPos);
    if (isIgnoredDeclarationHead_(firstToken)) {
        return;
    }

    size_t typeEnd = static_cast<size_t>(-1);
    const SwString resolvedTypeName = resolveCompletionTypeNameAt_(compact, table, typeEnd);
    if (resolvedTypeName.isEmpty() || typeEnd == static_cast<size_t>(-1) || typeEnd >= compact.size()) {
        return;
    }

    const SwString declaratorsText = compact.substr(typeEnd);
    const SwList<SwString> declarators = splitTopLevelComma_(declaratorsText);
    for (int i = 0; i < declarators.size(); ++i) {
        const SwString variableName = extractDeclaredNameFromDeclarator_(declarators[i]);
        if (variableName.isEmpty()) {
            continue;
        }

        CompletionVariableInfo info;
        info.typeName = resolvedTypeName;
        info.isPointer = isPointerDeclarator_(declarators[i], variableName);
        variables.insert(variableName, info);
    }
}

static void registerFunctionParametersFromHeader_(const SwString& header,
                                                  const CompletionTypeTable& table,
                                                  SwMap<SwString, CompletionVariableInfo>& variables) {
    const SwString compact = collapseWhitespace_(header);
    const size_t openParen = compact.firstIndexOf('(');
    if (openParen == static_cast<size_t>(-1)) {
        return;
    }

    const size_t closeParen = findMatchingParen_(compact, openParen);
    if (closeParen == static_cast<size_t>(-1) || closeParen <= openParen + 1) {
        return;
    }

    const SwString paramsText = compact.substr(openParen + 1, closeParen - openParen - 1).trimmed();
    if (paramsText.isEmpty() || paramsText == SwString("void")) {
        return;
    }

    const SwList<SwString> params = splitTopLevelComma_(paramsText);
    for (int i = 0; i < params.size(); ++i) {
        registerDeclaredVariablesFromStatement_(params[i], table, variables);
    }
}

static void collectCompletionMembersFromBody_(const SwString& sanitizedBody,
                                              const SwString& qualifiedTypeName,
                                              bool publicByDefault,
                                              CompletionTypeTable& table) {
    const SwString typeLeaf = leafNameOfQualified_(qualifiedTypeName);
    CompletionMemberAccess currentAccess =
        publicByDefault ? CompletionMemberAccess::Public : CompletionMemberAccess::Private;
    size_t statementStart = 0;
    size_t i = 0;

    while (i < sanitizedBody.size()) {
        const char ch = sanitizedBody[i];

        if (startsWithKeywordAt_(sanitizedBody, i, "public") ||
            startsWithKeywordAt_(sanitizedBody, i, "private") ||
            startsWithKeywordAt_(sanitizedBody, i, "protected")) {
            size_t pos = i;
            const SwString access = readIdentifier_(sanitizedBody, pos);
            pos = skipSpaces_(sanitizedBody, pos);
            if (!access.isEmpty() && pos < sanitizedBody.size() && sanitizedBody[pos] == ':') {
                if (access == SwString("public")) {
                    currentAccess = CompletionMemberAccess::Public;
                } else if (access == SwString("protected")) {
                    currentAccess = CompletionMemberAccess::Protected;
                } else {
                    currentAccess = CompletionMemberAccess::Private;
                }
                i = pos + 1;
                statementStart = i;
                continue;
            }
        }

        if (isCompletionTypeDeclarationHead_(sanitizedBody, i) || startsWithKeywordAt_(sanitizedBody, i, "namespace")) {
            size_t pos = i;
            while (pos < sanitizedBody.size() && sanitizedBody[pos] != '{' && sanitizedBody[pos] != ';') {
                ++pos;
            }
            if (pos < sanitizedBody.size() && sanitizedBody[pos] == '{') {
                const size_t closeBrace = findMatchingBrace_(sanitizedBody, pos);
                if (closeBrace == static_cast<size_t>(-1)) {
                    break;
                }
                i = closeBrace + 1;
                while (i < sanitizedBody.size() && std::isspace(static_cast<unsigned char>(sanitizedBody[i])) != 0) {
                    ++i;
                }
                if (i < sanitizedBody.size() && sanitizedBody[i] == ';') {
                    ++i;
                }
                statementStart = i;
                continue;
            }

            i = (pos < sanitizedBody.size()) ? (pos + 1) : sanitizedBody.size();
            statementStart = i;
            continue;
        }

        if (ch == '{') {
            const SwString header = collapseWhitespace_(sanitizedBody.substr(statementStart, i - statementStart));
            if (!header.isEmpty() && !isCompletionTypeDeclarationHeaderText_(header) && !header.startsWith("namespace ")) {
                const size_t parenPos = header.firstIndexOf('(');
                if (parenPos != static_cast<size_t>(-1)) {
                    const SwString memberName = extractIdentifierBefore_(header, parenPos);
                    if (!memberName.isEmpty() &&
                        memberName != typeLeaf &&
                        memberName != (SwString("~") + typeLeaf) &&
                        memberName != SwString("operator")) {
                        CompletionMemberInfo member;
                        member.name = memberName;
                        member.kind = CompletionMemberKind::Method;
                        member.access = currentAccess;
                        appendCompletionMember_(table, qualifiedTypeName, member);
                    }
                }
            }

            const size_t closeBrace = findMatchingBrace_(sanitizedBody, i);
            if (closeBrace == static_cast<size_t>(-1)) {
                break;
            }

            i = closeBrace + 1;
            while (i < sanitizedBody.size() && std::isspace(static_cast<unsigned char>(sanitizedBody[i])) != 0) {
                ++i;
            }
            if (i < sanitizedBody.size() && sanitizedBody[i] == ';') {
                ++i;
            }
            statementStart = i;
            continue;
        }

        if (ch == ';') {
            const SwString statement = collapseWhitespace_(sanitizedBody.substr(statementStart, i - statementStart));
            if (!statement.isEmpty()) {
                const size_t parenPos = statement.firstIndexOf('(');
                if (parenPos != static_cast<size_t>(-1)) {
                    const SwString memberName = extractIdentifierBefore_(statement, parenPos);
                    if (!memberName.isEmpty() &&
                        memberName != typeLeaf &&
                        memberName != (SwString("~") + typeLeaf) &&
                        memberName != SwString("operator")) {
                        CompletionMemberInfo member;
                        member.name = memberName;
                        member.kind = CompletionMemberKind::Method;
                        member.access = currentAccess;
                        appendCompletionMember_(table, qualifiedTypeName, member);
                    }
                } else {
                    const SwList<SwString> declarators = splitTopLevelComma_(statement);
                    for (int partIndex = 0; partIndex < declarators.size(); ++partIndex) {
                        const SwString memberName = extractDeclaredNameFromDeclarator_(declarators[partIndex]);
                        if (memberName.isEmpty() ||
                            memberName == typeLeaf ||
                            memberName == (SwString("~") + typeLeaf)) {
                            continue;
                        }

                        CompletionMemberInfo member;
                        member.name = memberName;
                        member.kind = CompletionMemberKind::Field;
                        member.access = currentAccess;
                        appendCompletionMember_(table, qualifiedTypeName, member);
                    }
                }
            }

            statementStart = i + 1;
        }

        ++i;
    }
}

static void collectCompletionDeclaredTypes_(const SwString& sanitized,
                                            size_t begin,
                                            size_t end,
                                            const SwList<SwString>& scope,
                                            CompletionTypeTable& table) {
    size_t i = begin;

    while (i < end) {
        if (startsWithKeywordAt_(sanitized, i, "namespace")) {
            size_t pos = i + SwString("namespace").size();
            const SwString namespaceName = readIdentifier_(sanitized, pos);
            const bool hasName = !namespaceName.isEmpty();

            while (pos < end && sanitized[pos] != '{' && sanitized[pos] != ';') {
                ++pos;
            }
            if (pos < end && sanitized[pos] == '{') {
                const size_t closeBrace = findMatchingBrace_(sanitized, pos);
                if (closeBrace != static_cast<size_t>(-1) && closeBrace < end) {
                    SwList<SwString> nestedScope = scope;
                    if (hasName) {
                        nestedScope.append(namespaceName);
                    }
                    collectCompletionDeclaredTypes_(sanitized, pos + 1, closeBrace, nestedScope, table);
                    i = closeBrace + 1;
                    continue;
                }
            }

            i = (pos < end) ? (pos + 1) : end;
            continue;
        }

        if (isCompletionTypeDeclarationHead_(sanitized, i)) {
            size_t pos = i;
            SwString kindToken;
            if (startsWithKeywordAt_(sanitized, i, "enum")) {
                kindToken = SwString("enum");
                pos += SwString("enum").size();
                pos = skipSpaces_(sanitized, pos);
                if (startsWithKeywordAt_(sanitized, pos, "class")) {
                    pos += SwString("class").size();
                }
            } else if (startsWithKeywordAt_(sanitized, i, "class")) {
                kindToken = SwString("class");
                pos += SwString("class").size();
            } else {
                kindToken = SwString("struct");
                pos += SwString("struct").size();
            }

            const SwString typeName = readIdentifier_(sanitized, pos);
            const bool hasName = !typeName.isEmpty();
            const SwString qualifiedTypeName = hasName ? joinQualifiedName_(scope, typeName) : SwString();
            if (!qualifiedTypeName.isEmpty()) {
                registerCompletionType_(table, qualifiedTypeName);
            }

            while (pos < end && sanitized[pos] != '{' && sanitized[pos] != ';') {
                ++pos;
            }
            if (pos < end && sanitized[pos] == '{') {
                const size_t closeBrace = findMatchingBrace_(sanitized, pos);
                if (closeBrace != static_cast<size_t>(-1) && closeBrace < end) {
                    if (!qualifiedTypeName.isEmpty() && kindToken != SwString("enum")) {
                        collectCompletionMembersFromBody_(sanitized.substr(pos + 1, closeBrace - pos - 1),
                                                          qualifiedTypeName,
                                                          kindToken == SwString("struct"),
                                                          table);
                    }

                    SwList<SwString> nestedScope = scope;
                    if (hasName) {
                        nestedScope.append(typeName);
                    }
                    collectCompletionDeclaredTypes_(sanitized, pos + 1, closeBrace, nestedScope, table);
                    i = closeBrace + 1;
                    continue;
                }
            }

            i = (pos < end) ? (pos + 1) : end;
            continue;
        }

        if (sanitized[i] == '{') {
            const size_t closeBrace = findMatchingBrace_(sanitized, i);
            if (closeBrace == static_cast<size_t>(-1) || closeBrace >= end) {
                break;
            }
            collectCompletionDeclaredTypes_(sanitized, i + 1, closeBrace, scope, table);
            i = closeBrace + 1;
            continue;
        }

        ++i;
    }
}

static void collectCompletionTypesFromText_(const SwString& text, CompletionTypeTable& table) {
    const SwString sanitized = sanitizeCodeForParsing_(text);
    const SwList<SwString> rootScope;
    collectCompletionDeclaredTypes_(sanitized, 0, sanitized.size(), rootScope, table);
}

static void mergeCompletionTypeTables_(CompletionTypeTable& destination, const CompletionTypeTable& source) {
    for (int i = 0; i < source.typeNames.size(); ++i) {
        const SwString& typeName = source.typeNames[i];
        if (!destination.typeInfos.contains(typeName)) {
            destination.typeInfos.insert(typeName, source.typeInfos.value(typeName));
            appendUniqueString_(destination.typeNames, typeName);
            continue;
        }

        const CompletionTypeInfo sourceInfo = source.typeInfos.value(typeName);
        for (int memberIndex = 0; memberIndex < sourceInfo.members.size(); ++memberIndex) {
            appendCompletionMember_(destination, typeName, sourceInfo.members[memberIndex]);
        }
    }
}

static void collectFunctionNameFromStatement_(const SwString& statement, SwList<SwString>& words) {
    const SwString compact = collapseWhitespace_(statement);
    if (compact.isEmpty()) {
        return;
    }

    size_t firstTokenPos = 0;
    const SwString firstToken = readIdentifier_(compact, firstTokenPos);
    if (isIgnoredDeclarationHead_(firstToken)) {
        return;
    }

    const size_t parenPos = compact.firstIndexOf('(');
    if (parenPos == static_cast<size_t>(-1)) {
        return;
    }

    const SwString name = extractIdentifierBefore_(compact, parenPos);
    if (name == SwString("operator")) {
        return;
    }
    appendUniqueCompletionWord_(words, name);
}

static void collectDeclaratorNamesFromStatement_(const SwString& statement, SwList<SwString>& words) {
    const SwString compact = collapseWhitespace_(statement);
    if (compact.isEmpty()) {
        return;
    }

    size_t firstTokenPos = 0;
    const SwString firstToken = readIdentifier_(compact, firstTokenPos);
    if (isIgnoredDeclarationHead_(firstToken)) {
        return;
    }

    const SwList<SwString> declarators = splitTopLevelComma_(compact);
    for (int i = 0; i < declarators.size(); ++i) {
        SwString part = collapseWhitespace_(declarators[i]);
        const size_t equalPos = part.firstIndexOf('=');
        if (equalPos != static_cast<size_t>(-1)) {
            part = part.substr(0, equalPos);
        }
        const size_t bracePos = part.firstIndexOf('{');
        if (bracePos != static_cast<size_t>(-1)) {
            part = part.substr(0, bracePos);
        }
        part = part.trimmed();
        appendUniqueCompletionWord_(words, extractIdentifierBefore_(part, part.size()));
    }
}

static bool parseMacroLine_(const SwString& line, SwList<SwString>& words) {
    const SwString compact = collapseWhitespace_(line);
    if (!compact.startsWith("#define ")) {
        return false;
    }

    size_t pos = SwString("#define ").size();
    const SwString macroName = readIdentifier_(compact, pos);
    appendUniqueCompletionWord_(words, macroName);
    return true;
}

static void collectScopeSymbols_(const SwString& original,
                                 const SwString& sanitized,
                                 size_t begin,
                                 size_t end,
                                 SwList<SwString>& words) {
    size_t statementStart = begin;
    size_t i = begin;

    while (i < end) {
        const char ch = sanitized[i];

        if (ch == '#') {
            size_t lineEnd = i;
            while (lineEnd < end && sanitized[lineEnd] != '\n') {
                ++lineEnd;
            }
            parseMacroLine_(original.substr(i, lineEnd - i), words);
            i = (lineEnd < end) ? (lineEnd + 1) : end;
            statementStart = i;
            continue;
        }

        if (startsWithKeywordAt_(sanitized, i, "namespace")) {
            size_t pos = i + SwString("namespace").size();
            const SwString namespaceName = readIdentifier_(sanitized, pos);
            appendUniqueCompletionWord_(words, namespaceName);

            while (pos < end && sanitized[pos] != '{' && sanitized[pos] != ';') {
                ++pos;
            }
            if (pos < end && sanitized[pos] == '{') {
                const size_t closeBrace = findMatchingBrace_(sanitized, pos);
                if (closeBrace != static_cast<size_t>(-1) && closeBrace < end) {
                    collectScopeSymbols_(original, sanitized, pos + 1, closeBrace, words);
                    i = closeBrace + 1;
                    statementStart = i;
                    continue;
                }
            }

            i = pos + 1;
            statementStart = i;
            continue;
        }

        if (startsWithKeywordAt_(sanitized, i, "class") ||
            startsWithKeywordAt_(sanitized, i, "struct") ||
            startsWithKeywordAt_(sanitized, i, "enum")) {
            size_t pos = i;
            if (startsWithKeywordAt_(sanitized, i, "enum")) {
                pos += SwString("enum").size();
                pos = skipSpaces_(sanitized, pos);
                if (startsWithKeywordAt_(sanitized, pos, "class")) {
                    pos += SwString("class").size();
                }
            } else if (startsWithKeywordAt_(sanitized, i, "class")) {
                pos += SwString("class").size();
            } else {
                pos += SwString("struct").size();
            }

            const SwString typeName = readIdentifier_(sanitized, pos);
            appendUniqueCompletionWord_(words, typeName);

            while (pos < end && sanitized[pos] != '{' && sanitized[pos] != ';') {
                ++pos;
            }
            if (pos < end && sanitized[pos] == '{') {
                const size_t closeBrace = findMatchingBrace_(sanitized, pos);
                i = (closeBrace != static_cast<size_t>(-1)) ? (closeBrace + 1) : (pos + 1);
            } else {
                i = pos + 1;
            }
            statementStart = i;
            continue;
        }

        if (ch == '{') {
            const SwString header = original.substr(statementStart, i - statementStart);
            collectFunctionNameFromStatement_(header, words);
            const size_t closeBrace = findMatchingBrace_(sanitized, i);
            if (closeBrace == static_cast<size_t>(-1)) {
                break;
            }
            collectScopeSymbols_(original, sanitized, i + 1, closeBrace, words);
            i = closeBrace + 1;
            statementStart = i;
            continue;
        }

        if (ch == ';') {
            const SwString statement = original.substr(statementStart, i - statementStart);
            collectFunctionNameFromStatement_(statement, words);
            collectDeclaratorNamesFromStatement_(statement, words);
            statementStart = i + 1;
        }

        ++i;
    }
}

static bool findEnclosingClassRange_(const SwString& text, size_t cursorPos, ParsedClassRange& result) {
    const SwString sanitized = sanitizeCodeForParsing_(text);
    bool found = false;
    size_t bestSpan = static_cast<size_t>(-1);

    for (size_t i = 0; i < sanitized.size(); ++i) {
        if (!startsWithKeywordAt_(sanitized, i, "class") && !startsWithKeywordAt_(sanitized, i, "struct")) {
            continue;
        }

        size_t pos = i + (startsWithKeywordAt_(sanitized, i, "class") ? SwString("class").size() : SwString("struct").size());
        const SwString className = readIdentifier_(sanitized, pos);
        if (className.isEmpty()) {
            continue;
        }

        while (pos < sanitized.size() && sanitized[pos] != '{' && sanitized[pos] != ';') {
            ++pos;
        }
        if (pos >= sanitized.size() || sanitized[pos] != '{') {
            continue;
        }

        const size_t closeBrace = findMatchingBrace_(sanitized, pos);
        if (closeBrace == static_cast<size_t>(-1)) {
            continue;
        }

        if (cursorPos > pos && cursorPos <= closeBrace) {
            const size_t span = closeBrace - pos;
            if (!found || span < bestSpan) {
                found = true;
                bestSpan = span;
                result.name = className;
                result.openBrace = pos;
                result.closeBrace = closeBrace;
            }
        }
    }

    return found;
}

static void collectClassMembers_(const SwString& text, const ParsedClassRange& classRange, SwList<SwString>& words) {
    if (classRange.openBrace == static_cast<size_t>(-1) ||
        classRange.closeBrace == static_cast<size_t>(-1) ||
        classRange.closeBrace <= classRange.openBrace + 1) {
        return;
    }

    const SwString body = text.substr(classRange.openBrace + 1, classRange.closeBrace - classRange.openBrace - 1);
    const SwString sanitizedBody = sanitizeCodeForParsing_(body);

    size_t statementStart = 0;
    size_t i = 0;

    while (i < sanitizedBody.size()) {
        const char ch = sanitizedBody[i];

        if (startsWithKeywordAt_(sanitizedBody, i, "public") ||
            startsWithKeywordAt_(sanitizedBody, i, "private") ||
            startsWithKeywordAt_(sanitizedBody, i, "protected")) {
            size_t pos = i;
            const SwString access = readIdentifier_(sanitizedBody, pos);
            pos = skipSpaces_(sanitizedBody, pos);
            if (!access.isEmpty() && pos < sanitizedBody.size() && sanitizedBody[pos] == ':') {
                i = pos + 1;
                statementStart = i;
                continue;
            }
        }

        if (ch == '{') {
            const SwString header = body.substr(statementStart, i - statementStart);
            const SwString compactHeader = collapseWhitespace_(header);
            if (!compactHeader.startsWith("class ") &&
                !compactHeader.startsWith("struct ") &&
                !compactHeader.startsWith("enum ") &&
                !compactHeader.startsWith("namespace ")) {
                collectFunctionNameFromStatement_(header, words);
            }

            const size_t closeBrace = findMatchingBrace_(sanitizedBody, i);
            if (closeBrace == static_cast<size_t>(-1)) {
                break;
            }

            i = closeBrace + 1;
            while (i < sanitizedBody.size() && std::isspace(static_cast<unsigned char>(sanitizedBody[i])) != 0) {
                ++i;
            }
            if (i < sanitizedBody.size() && sanitizedBody[i] == ';') {
                ++i;
            }
            statementStart = i;
            continue;
        }

        if (ch == ';') {
            const SwString statement = body.substr(statementStart, i - statementStart);
            if (statement.firstIndexOf('(') != static_cast<size_t>(-1)) {
                collectFunctionNameFromStatement_(statement, words);
            } else {
                collectDeclaratorNamesFromStatement_(statement, words);
            }
            statementStart = i + 1;
        }

        ++i;
    }
}

static SwString normalizePath_(const SwString& path) {
    SwString normalized = path;
    normalized.replace("\\", "/");
    while (normalized.endsWith("/")) {
        normalized.remove(static_cast<int>(normalized.size() - 1), 1);
    }
    return normalized;
}

static SwString parentPath_(const SwString& path) {
    const SwString normalized = normalizePath_(path);
    const size_t slash = normalized.toStdString().find_last_of('/');
    if (slash == std::string::npos) {
        return normalized;
    }
    return normalized.substr(0, slash);
}

static SwString repositoryRoot_() {
    static SwString cachedRoot;
    if (!cachedRoot.isEmpty()) {
        return cachedRoot;
    }

    SwString current = normalizePath_(SwDir::currentPath());
    for (int i = 0; i < 8 && !current.isEmpty(); ++i) {
        const SwString marker = current + "/src/core/gui/SwCodeEditor.h";
        if (SwFile::isFile(marker)) {
            cachedRoot = current;
            return cachedRoot;
        }

        const SwString parent = parentPath_(current);
        if (parent == current) {
            break;
        }
        current = parent;
    }

    cachedRoot = normalizePath_(SwDir::currentPath());
    return cachedRoot;
}

static SwList<SwString> includeSearchRoots_() {
    static SwList<SwString> roots;
    if (!roots.isEmpty()) {
        return roots;
    }

    const SwString root = repositoryRoot_();
    roots.append(root);
    roots.append(root + "/src");
    roots.append(root + "/src/core");
    roots.append(root + "/src/core/gui");
    roots.append(root + "/src/core/types");
    roots.append(root + "/src/core/io");
    roots.append(root + "/src/core/fs");
    roots.append(root + "/src/core/runtime");
    return roots;
}

static SwString resolveIncludePath_(const SwString& includePath) {
    if (includePath.isEmpty()) {
        return SwString();
    }
    if (SwFile::isFile(includePath)) {
        return includePath;
    }

    const SwList<SwString> roots = includeSearchRoots_();
    for (int i = 0; i < roots.size(); ++i) {
        const SwString candidate = normalizePath_(roots[i]) + "/" + includePath;
        if (SwFile::isFile(candidate)) {
            return candidate;
        }
    }

    if (includePath.startsWith("core/")) {
        const SwString candidate = repositoryRoot_() + "/src/" + includePath;
        if (SwFile::isFile(candidate)) {
            return candidate;
        }
    }

    return SwString();
}

static SwString readFileText_(const SwString& filePath) {
    SwFile file(filePath);
    if (!file.open(SwFile::Read)) {
        return SwString();
    }
    return file.readAll();
}

static void collectIncludedHeaderCompletionTypes_(const SwString& text, CompletionTypeTable& table) {
    static SwMap<SwString, CompletionTypeTable> headerCache;

    const SwList<SwString> lines = text.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        const SwString trimmed = lines[i].trimmed();
        if (!trimmed.startsWith("#include")) {
            continue;
        }

        size_t pathStart = trimmed.firstIndexOf('"');
        char closing = '"';
        if (pathStart == static_cast<size_t>(-1)) {
            pathStart = trimmed.firstIndexOf('<');
            closing = '>';
        }
        if (pathStart == static_cast<size_t>(-1)) {
            continue;
        }

        size_t pathEnd = pathStart + 1;
        while (pathEnd < trimmed.size() && trimmed[pathEnd] != closing) {
            ++pathEnd;
        }
        if (pathEnd >= trimmed.size() || pathEnd <= pathStart + 1) {
            continue;
        }

        const SwString includePath = trimmed.substr(pathStart + 1, pathEnd - pathStart - 1);
        const SwString resolved = resolveIncludePath_(includePath);
        if (resolved.isEmpty()) {
            continue;
        }

        if (!headerCache.contains(resolved)) {
            CompletionTypeTable cachedTable;
            collectCompletionTypesFromText_(readFileText_(resolved), cachedTable);
            headerCache.insert(resolved, cachedTable);
        }

        mergeCompletionTypeTables_(table, headerCache.value(resolved));
    }
}

static void appendBuiltInIncludeWords_(const SwString& includePath, SwList<SwString>& words) {
    if (includePath == SwString("array")) {
        appendUniqueCompletionWord_(words, SwString("array"));
    } else if (includePath == SwString("iostream")) {
        appendUniqueCompletionWord_(words, SwString("cout"));
        appendUniqueCompletionWord_(words, SwString("cin"));
        appendUniqueCompletionWord_(words, SwString("cerr"));
        appendUniqueCompletionWord_(words, SwString("endl"));
    } else if (includePath == SwString("memory")) {
        appendUniqueCompletionWord_(words, SwString("unique_ptr"));
        appendUniqueCompletionWord_(words, SwString("shared_ptr"));
        appendUniqueCompletionWord_(words, SwString("make_unique"));
        appendUniqueCompletionWord_(words, SwString("make_shared"));
    } else if (includePath == SwString("string")) {
        appendUniqueCompletionWord_(words, SwString("string"));
        appendUniqueCompletionWord_(words, SwString("to_string"));
    } else if (includePath == SwString("string_view")) {
        appendUniqueCompletionWord_(words, SwString("string_view"));
    } else if (includePath == SwString("vector")) {
        appendUniqueCompletionWord_(words, SwString("vector"));
    }

    appendUniqueCompletionWord_(words, SwString("std"));
}

static void collectSymbolsFromText_(const SwString& text, SwList<SwString>& words) {
    const SwString sanitized = sanitizeCodeForParsing_(text);
    collectScopeSymbols_(text, sanitized, 0, sanitized.size(), words);
}

static void collectIncludedHeaderSymbols_(const SwString& text, SwList<SwString>& words) {
    static SwMap<SwString, SwList<SwString>> headerCache;

    const SwList<SwString> lines = text.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        const SwString trimmed = lines[i].trimmed();
        if (!trimmed.startsWith("#include")) {
            continue;
        }

        size_t pathStart = trimmed.firstIndexOf('"');
        char closing = '"';
        if (pathStart == static_cast<size_t>(-1)) {
            pathStart = trimmed.firstIndexOf('<');
            closing = '>';
        }
        if (pathStart == static_cast<size_t>(-1)) {
            continue;
        }

        size_t pathEnd = pathStart + 1;
        while (pathEnd < trimmed.size() && trimmed[pathEnd] != closing) {
            ++pathEnd;
        }
        if (pathEnd >= trimmed.size() || pathEnd <= pathStart + 1) {
            continue;
        }

        const SwString includePath = trimmed.substr(pathStart + 1, pathEnd - pathStart - 1);
        appendBuiltInIncludeWords_(includePath, words);

        const SwString resolved = resolveIncludePath_(includePath);
        if (resolved.isEmpty()) {
            continue;
        }

        if (!headerCache.contains(resolved)) {
            SwList<SwString> symbols;
            collectSymbolsFromText_(readFileText_(resolved), symbols);
            headerCache.insert(resolved, symbols);
        }

        const SwList<SwString> headerWords = headerCache.value(resolved);
        for (int w = 0; w < headerWords.size(); ++w) {
            appendUniqueCompletionWord_(words, headerWords[w]);
        }
    }
}

static bool isClassOrStructHeader_(const SwString& header) {
    return header.startsWith("class ") || header.startsWith("struct ");
}

static void collectVisibleVariablesUntilCursor_(const SwString& sanitized,
                                                size_t begin,
                                                size_t end,
                                                size_t cursorPos,
                                                const CompletionTypeTable& table,
                                                SwMap<SwString, CompletionVariableInfo>& variables);

static void collectVisibleClassMembersUntilCursor_(const SwString& sanitized,
                                                   size_t begin,
                                                   size_t end,
                                                   size_t cursorPos,
                                                   const CompletionTypeTable& table,
                                                   SwMap<SwString, CompletionVariableInfo>& variables) {
    size_t statementStart = begin;
    size_t i = begin;
    const size_t limit = std::min(end, cursorPos);

    while (i < limit) {
        if (startsWithKeywordAt_(sanitized, i, "public") ||
            startsWithKeywordAt_(sanitized, i, "private") ||
            startsWithKeywordAt_(sanitized, i, "protected")) {
            size_t pos = i;
            const SwString access = readIdentifier_(sanitized, pos);
            pos = skipSpaces_(sanitized, pos);
            if (!access.isEmpty() && pos < limit && sanitized[pos] == ':') {
                i = pos + 1;
                statementStart = i;
                continue;
            }
        }

        if (isCompletionTypeDeclarationHead_(sanitized, i) || startsWithKeywordAt_(sanitized, i, "namespace")) {
            size_t pos = i;
            while (pos < end && sanitized[pos] != '{' && sanitized[pos] != ';') {
                ++pos;
            }
            if (pos < end && sanitized[pos] == '{') {
                const size_t closeBrace = findMatchingBrace_(sanitized, pos);
                if (closeBrace == static_cast<size_t>(-1) || closeBrace >= end) {
                    break;
                }
                const SwString header = collapseWhitespace_(sanitized.substr(i, pos - i));
                if (cursorPos > pos && cursorPos <= closeBrace && isClassOrStructHeader_(header)) {
                    collectVisibleClassMembersUntilCursor_(sanitized, pos + 1, closeBrace, cursorPos, table, variables);
                    return;
                }
                i = closeBrace + 1;
                statementStart = i;
                continue;
            }

            i = (pos < end) ? (pos + 1) : end;
            statementStart = i;
            continue;
        }

        if (sanitized[i] == '{') {
            const size_t closeBrace = findMatchingBrace_(sanitized, i);
            if (closeBrace == static_cast<size_t>(-1) || closeBrace >= end) {
                break;
            }

            const SwString header = collapseWhitespace_(sanitized.substr(statementStart, i - statementStart));
            if (cursorPos > i && cursorPos <= closeBrace) {
                if (!header.isEmpty() && !isCompletionTypeDeclarationHeaderText_(header) && !header.startsWith("namespace ")) {
                    registerFunctionParametersFromHeader_(header, table, variables);
                    collectVisibleVariablesUntilCursor_(sanitized, i + 1, closeBrace, cursorPos, table, variables);
                }
                return;
            }

            i = closeBrace + 1;
            statementStart = i;
            continue;
        }

        if (sanitized[i] == ';') {
            const SwString statement = sanitized.substr(statementStart, i - statementStart);
            if (statement.firstIndexOf('(') == static_cast<size_t>(-1)) {
                registerDeclaredVariablesFromStatement_(statement, table, variables);
            }
            statementStart = i + 1;
        }

        ++i;
    }
}

static void collectVisibleVariablesUntilCursor_(const SwString& sanitized,
                                                size_t begin,
                                                size_t end,
                                                size_t cursorPos,
                                                const CompletionTypeTable& table,
                                                SwMap<SwString, CompletionVariableInfo>& variables) {
    size_t statementStart = begin;
    size_t i = begin;
    const size_t limit = std::min(end, cursorPos);

    while (i < limit) {
        if (startsWithKeywordAt_(sanitized, i, "namespace")) {
            size_t pos = i + SwString("namespace").size();
            readIdentifier_(sanitized, pos);
            while (pos < end && sanitized[pos] != '{' && sanitized[pos] != ';') {
                ++pos;
            }
            if (pos < end && sanitized[pos] == '{') {
                const size_t closeBrace = findMatchingBrace_(sanitized, pos);
                if (closeBrace == static_cast<size_t>(-1) || closeBrace >= end) {
                    break;
                }
                if (cursorPos > pos && cursorPos <= closeBrace) {
                    collectVisibleVariablesUntilCursor_(sanitized, pos + 1, closeBrace, cursorPos, table, variables);
                    return;
                }
                i = closeBrace + 1;
                statementStart = i;
                continue;
            }

            i = (pos < end) ? (pos + 1) : end;
            statementStart = i;
            continue;
        }

        if (isCompletionTypeDeclarationHead_(sanitized, i)) {
            size_t pos = i;
            while (pos < end && sanitized[pos] != '{' && sanitized[pos] != ';') {
                ++pos;
            }
            if (pos < end && sanitized[pos] == '{') {
                const size_t closeBrace = findMatchingBrace_(sanitized, pos);
                if (closeBrace == static_cast<size_t>(-1) || closeBrace >= end) {
                    break;
                }
                const SwString header = collapseWhitespace_(sanitized.substr(i, pos - i));
                if (cursorPos > pos && cursorPos <= closeBrace && isClassOrStructHeader_(header)) {
                    collectVisibleClassMembersUntilCursor_(sanitized, pos + 1, closeBrace, cursorPos, table, variables);
                    return;
                }
                i = closeBrace + 1;
                statementStart = i;
                continue;
            }

            i = (pos < end) ? (pos + 1) : end;
            statementStart = i;
            continue;
        }

        if (sanitized[i] == '{') {
            const size_t closeBrace = findMatchingBrace_(sanitized, i);
            if (closeBrace == static_cast<size_t>(-1) || closeBrace >= end) {
                break;
            }

            const SwString header = collapseWhitespace_(sanitized.substr(statementStart, i - statementStart));
            if (cursorPos > i && cursorPos <= closeBrace) {
                if (!header.isEmpty() && !isCompletionTypeDeclarationHeaderText_(header) && !header.startsWith("namespace ")) {
                    registerFunctionParametersFromHeader_(header, table, variables);
                    collectVisibleVariablesUntilCursor_(sanitized, i + 1, closeBrace, cursorPos, table, variables);
                }
                return;
            }

            i = closeBrace + 1;
            statementStart = i;
            continue;
        }

        if (sanitized[i] == ';') {
            registerDeclaredVariablesFromStatement_(sanitized.substr(statementStart, i - statementStart),
                                                    table,
                                                    variables);
            statementStart = i + 1;
        }

        ++i;
    }
}

static SwMap<SwString, CompletionVariableInfo> buildVisibleCompletionVariables_(const SwString& text,
                                                                                size_t cursorPos,
                                                                                const CompletionTypeTable& table) {
    SwMap<SwString, CompletionVariableInfo> variables;
    const SwString sanitized = sanitizeCodeForParsing_(text);
    collectVisibleVariablesUntilCursor_(sanitized, 0, sanitized.size(), cursorPos, table, variables);

    ParsedClassRange currentClass;
    if (findEnclosingClassRange_(text, cursorPos, currentClass)) {
        CompletionVariableInfo selfInfo;
        selfInfo.typeName = resolveKnownCompletionTypeName_(table, currentClass.name);
        if (selfInfo.typeName.isEmpty()) {
            selfInfo.typeName = currentClass.name;
        }
        selfInfo.isPointer = true;
        variables.insert(SwString("this"), selfInfo);
    }

    return variables;
}

static MemberAccessContext findMemberAccessContext_(const SwString& text, size_t cursorPos) {
    MemberAccessContext context;
    if (text.isEmpty()) {
        return context;
    }

    const size_t cursor = std::min(cursorPos, text.size());
    size_t prefixStart = cursor;
    while (prefixStart > 0 && isCompletionIdentifierPart(text[prefixStart - 1])) {
        --prefixStart;
    }
    context.memberPrefix = text.substr(prefixStart, cursor - prefixStart);

    size_t scan = prefixStart;
    while (scan > 0 && std::isspace(static_cast<unsigned char>(text[scan - 1])) != 0) {
        --scan;
    }

    if (scan >= 2 && text[scan - 2] == '-' && text[scan - 1] == '>') {
        context.usesArrow = true;
        context.operatorPos = scan - 2;
    } else if (scan >= 1 && text[scan - 1] == '.') {
        context.usesArrow = false;
        context.operatorPos = scan - 1;
    } else {
        return context;
    }

    context.baseName = extractIdentifierBefore_(text, context.operatorPos);
    if (context.baseName.isEmpty()) {
        return context;
    }

    const size_t previousTokenPos = skipSpacesBackward_(text, context.operatorPos - context.baseName.size());
    if (previousTokenPos != static_cast<size_t>(-1)) {
        const char previous = text[previousTokenPos];
        if (previous == '.' || previous == '>' || previous == ':' || previous == ')' || previous == ']') {
            return context;
        }
    }

    context.valid = true;
    return context;
}

static SwString completionMemberAccessLabel_(CompletionMemberAccess access) {
    switch (access) {
        case CompletionMemberAccess::Public:
            return SwString("public");
        case CompletionMemberAccess::Protected:
            return SwString("protected");
        case CompletionMemberAccess::Private:
            return SwString("private");
    }
    return SwString("public");
}

static SwString completionMemberKindLabel_(CompletionMemberKind kind) {
    return (kind == CompletionMemberKind::Method) ? SwString("method") : SwString("attribute");
}

static SwString completionMemberDisplayText_(const CompletionMemberInfo& member) {
    SwString display = member.name;
    if (member.kind == CompletionMemberKind::Method) {
        display += "()";
    }
    display += "  ";
    display += completionMemberAccessLabel_(member.access);
    display += " ";
    display += completionMemberKindLabel_(member.kind);
    return display;
}

static SwString completionMemberToolTip_(const CompletionTypeInfo& typeInfo, const CompletionMemberInfo& member) {
    SwString toolTip = typeInfo.qualifiedName;
    toolTip += "::";
    toolTip += member.name;
    if (member.kind == CompletionMemberKind::Method) {
        toolTip += "()";
    }
    toolTip += " - ";
    toolTip += completionMemberAccessLabel_(member.access);
    toolTip += " ";
    toolTip += completionMemberKindLabel_(member.kind);
    return toolTip;
}

static CompletionTypeTable buildCompletionTypeTable_(const SwString& text) {
    CompletionTypeTable table;
    collectCompletionTypesFromText_(text, table);
    collectIncludedHeaderCompletionTypes_(text, table);
    return table;
}

static SwList<CompletionItem> buildMemberCompletionItems_(const SwString& text, size_t cursorPos) {
    SwList<CompletionItem> items;
    const CompletionTypeTable typeTable = buildCompletionTypeTable_(text);
    const MemberAccessContext context = findMemberAccessContext_(text, cursorPos);
    if (!context.valid) {
        return items;
    }

    const SwMap<SwString, CompletionVariableInfo> variables =
        buildVisibleCompletionVariables_(text, cursorPos, typeTable);
    if (!variables.contains(context.baseName)) {
        return items;
    }

    const CompletionVariableInfo variableInfo = variables.value(context.baseName);
    const SwString resolvedTypeName = resolveKnownCompletionTypeName_(typeTable, variableInfo.typeName);
    if (resolvedTypeName.isEmpty() || !typeTable.typeInfos.contains(resolvedTypeName)) {
        return items;
    }

    CompletionTypeInfo typeInfo = typeTable.typeInfos.value(resolvedTypeName);
    std::stable_sort(typeInfo.members.begin(),
                     typeInfo.members.end(),
                     [](const CompletionMemberInfo& lhs, const CompletionMemberInfo& rhs) {
                         if (lhs.kind != rhs.kind) {
                             return lhs.kind == CompletionMemberKind::Method;
                         }
                         return lhs.name < rhs.name;
                     });

    for (int i = 0; i < typeInfo.members.size(); ++i) {
        CompletionItem item;
        item.displayText = completionMemberDisplayText_(typeInfo.members[i]);
        item.insertText = typeInfo.members[i].name;
        item.toolTip = completionMemberToolTip_(typeInfo, typeInfo.members[i]);
        items.append(item);
    }

    return items;
}

static SwList<SwString> buildCodeCompletionWords(SwCodeEditor* editor) {
    static const char* kWords[] = {
        "auto", "bool", "break", "case", "catch", "char", "class", "concept",
        "const", "constexpr", "continue", "double", "else", "enum", "explicit",
        "false", "float", "for", "if", "include", "inline", "int", "namespace",
        "noexcept", "nullptr", "override", "private", "protected", "public", "return",
        "static", "std", "string", "string_view", "struct", "switch", "template",
        "this", "throw", "true", "typename", "using", "vector", "virtual", "void",
        "while"
    };

    SwList<SwString> words;
    words.reserve(sizeof(kWords) / sizeof(kWords[0]) + 32);
    for (size_t i = 0; i < sizeof(kWords) / sizeof(kWords[0]); ++i) {
        appendUniqueCompletionWord_(words, SwString(kWords[i]));
    }

    if (!editor) {
        return words;
    }

    const SwString text = editor->toPlainText();
    collectSymbolsFromText_(text, words);
    collectIncludedHeaderSymbols_(text, words);

    const size_t cursorPos = static_cast<size_t>(std::max(0, editor->textCursor().position()));
    ParsedClassRange currentClass;
    if (findEnclosingClassRange_(text, cursorPos, currentClass)) {
        collectClassMembers_(text, currentClass, words);
    }

    return words;
}

static SwList<CompletionItem> buildCodeCompletionItems(SwCodeEditor* editor) {
    SwList<CompletionItem> items;
    if (!editor) {
        return items;
    }

    const SwString text = editor->toPlainText();
    const size_t cursorPos = static_cast<size_t>(std::max(0, editor->textCursor().position()));
    items = buildMemberCompletionItems_(text, cursorPos);
    if (!items.isEmpty()) {
        return items;
    }

    const SwList<SwString> words = buildCodeCompletionWords(editor);
    for (int i = 0; i < words.size(); ++i) {
        CompletionItem item;
        item.displayText = words[i];
        item.insertText = words[i];
        items.append(item);
    }
    return items;
}

static SwCppCompletionIndex* configureCodeCompleter(SwCodeEditor* editor) {
    if (!editor) {
        return nullptr;
    }

    SwCompleter* completer = new SwCompleter(editor);
    SwStandardItemModel* model = new SwStandardItemModel(0, 1, completer);
    completer->setCaseSensitivity(Sw::CaseInsensitive);
    completer->setCompletionRole(SwItemDataRole::EditRole);
    completer->setMaxVisibleItems(6);
    completer->setModel(model);

    editor->setCompleter(completer);
    editor->setAutoCompletionEnabled(true);
    editor->setAutoCompletionMinPrefixLength(2);
    swApplyCodeCompletionTheme(completer, swCodeCompletionThemeDefaultLight());

    SwCppCompletionIndex* index = new SwCppCompletionIndex(editor);
    index->setTextProvider([editor]() {
        return editor->toPlainText();
    });
    index->setDebounceInterval(80);
    index->setAsyncEnabled(true);
    index->rebuildNow();

    editor->setCompletionProvider([index](SwCodeEditor*,
                                          const SwString& prefix,
                                          size_t cursorPos,
                                          bool) {
        SwList<SwCodeEditor::CompletionEntry> entries;
        const SwList<SwCppCompletionItem> items = index->completionItems(cursorPos, prefix, 96);
        for (int i = 0; i < items.size(); ++i) {
            SwCodeEditor::CompletionEntry entry;
            entry.displayText = items[i].displayText;
            entry.insertText = items[i].insertText;
            entry.toolTip = items[i].toolTip;
            entries.append(entry);
        }
        return entries;
    });
    SwObject::connect(editor, &SwPlainTextEdit::textChanged, index, [index]() {
        index->scheduleRebuild();
    });
    SwObject::connect(index, &SwCppCompletionIndex::indexUpdated, editor, [editor]() {
        if (editor->completer() && editor->completer()->popupVisible()) {
            editor->triggerCompletion();
        }
    });
    return index;
}

class DocumentEditorCodeEditor : public SwCodeEditor {
public:
    explicit DocumentEditorCodeEditor(SwWidget* parent = nullptr)
        : SwCodeEditor(parent) {}

    void setCompletionIndex(SwCppCompletionIndex* index) {
        m_completionIndex = index;
    }

protected:
    void keyPressEvent(KeyEvent* event) override {
        const bool ctrlSpace = event && event->isCtrlPressed() && (event->key() == 32 || event->text() == L' ');
        if (ctrlSpace) {
            rewritePointerMemberAccessForCompletion_();
        }
        SwCodeEditor::keyPressEvent(event);
    }

private:
    void rewritePointerMemberAccessForCompletion_() {
        const size_t cursorPos = static_cast<size_t>(std::max(0, textCursor().position()));
        size_t operatorPos = static_cast<size_t>(-1);
        const SwString text = toPlainText();
        const MemberAccessContext context = findMemberAccessContext_(text, cursorPos);
        if (!context.valid || context.usesArrow) {
            return;
        }
        operatorPos = context.operatorPos;

        bool shouldRewrite = false;
        if (m_completionIndex && m_completionIndex->isReady()) {
            shouldRewrite = m_completionIndex->shouldPreferArrowAccess(cursorPos);
        } else {
            const CompletionTypeTable typeTable = buildCompletionTypeTable_(text);
            const SwMap<SwString, CompletionVariableInfo> variables =
                buildVisibleCompletionVariables_(text, cursorPos, typeTable);
            shouldRewrite = variables.contains(context.baseName) && variables.value(context.baseName).isPointer;
        }
        if (!shouldRewrite) {
            return;
        }

        recordUndoState_();
        eraseTextAt(operatorPos, 1);
        insertTextAt(operatorPos, SwString("->"));
        m_cursorPos = std::min(m_cursorPos + 1, m_pieceTable.totalLength());
        m_selectionStart = m_selectionEnd = m_cursorPos;
        textChanged();
        ensureCursorVisible();
        update();
    }

    SwCppCompletionIndex* m_completionIndex{nullptr};
};

static void ensureCrashDumpsEnabled_() {
#if defined(_WIN32)
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, "SW_CRASH_DUMPS") == 0 && value) {
        std::free(value);
        return;
    }
    _putenv_s("SW_CRASH_DUMPS", "1");
#else
    const char* value = std::getenv("SW_CRASH_DUMPS");
    if (value) {
        return;
    }
    (void)setenv("SW_CRASH_DUMPS", "1", 0);
#endif
}

// ---------------------------------------------------------------------------
// Main application
// ---------------------------------------------------------------------------
int main() {
    ensureCrashDumpsEnabled_();
    SwCrashHandler::install("DocumentEditor");

    const SwString crashDir = SwCrashHandler::crashDirectory();
    if (!crashDir.isEmpty()) {
        SwString runtimeLog = crashDir;
        if (!runtimeLog.endsWith("/") && !runtimeLog.endsWith("\\")) {
            runtimeLog += "/";
        }
        runtimeLog += "DocumentEditor_runtime.log";
        SwDebug::setAppName("DocumentEditor");
        SwDebug::setFilePath(runtimeLog);
        SwDebug::setFileEnabled(true);
    }

    SwGuiApplication app;

    SwCrashReport report;
    if (SwCrashHandler::takeLastCrashReport(report)) {
        swCError("exemples.38.documenteditor") << "[CrashReport] dir=" << report.crashDir
                                               << " log=" << report.logPath
                                               << " dump=" << report.dumpPath;
    }

    // ── Main window ──
    SwMainWindow window("SwStack Document Editor");
    window.resize(950, 700);

    SwWidget* central = window.centralWidget();
    central->setStyleSheet("SwWidget { background-color: rgb(230, 232, 236); border-width: 0px; }");

    // ── Button styles ──
    SwString btnStyle = R"(
        SwPushButton {
            background-color: rgb(56, 118, 255);
            color: #FFFFFF;
            border-radius: 6px;
            border-width: 0px;
            padding: 4px 12px;
        }
    )";

    SwString btnExportStyle = R"(
        SwPushButton {
            background-color: rgb(40, 167, 69);
            color: #FFFFFF;
            border-radius: 6px;
            border-width: 0px;
            padding: 4px 12px;
        }
    )";

    // ── Toolbar widget with horizontal layout ──
    SwWidget* toolbar = new SwWidget(central);

    SwPushButton* btnCursor = new SwPushButton("Cursor Doc", toolbar);
    btnCursor->setStyleSheet(btnStyle);

    SwPushButton* btnHtml = new SwPushButton("HTML Doc", toolbar);
    btnHtml->setStyleSheet(btnStyle);

    SwPushButton* btnPdf = new SwPushButton("Export PDF", toolbar);
    btnPdf->setStyleSheet(btnExportStyle);

    SwPushButton* btnHtmlExport = new SwPushButton("Export HTML", toolbar);
    btnHtmlExport->setStyleSheet(btnExportStyle);

    SwLabel* statusLabel = new SwLabel(toolbar);
    statusLabel->setText("Ready");
    statusLabel->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(80,80,80); font-size: 12px; }");

    SwHorizontalLayout* tbLayout = new SwHorizontalLayout();
    tbLayout->setSpacing(4);
    tbLayout->setMargin(2);
    tbLayout->addWidget(btnCursor,     1, 100);
    tbLayout->addWidget(btnHtml,       1, 90);
    tbLayout->addWidget(btnPdf,        1, 100);
    tbLayout->addWidget(btnHtmlExport, 1, 110);
    tbLayout->addWidget(statusLabel,   2);       // stretch=2, takes remaining space
    toolbar->setLayout(tbLayout);

    // ── Text editor ──
    SwTabWidget* tabs = new SwTabWidget(central);
    tabs->setTabStyle(SwTabWidget::TabStyle::Underline);
    tabs->setStyleSheet(R"(
        SwTabWidget {
            background-color: rgb(245, 247, 250);
            border-color: rgb(213, 218, 226);
            border-width: 1px;
            border-radius: 10px;
        }
    )");

    SwWidget* documentPage = new SwWidget();
    documentPage->setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
    SwVerticalLayout* documentLayout = new SwVerticalLayout();
    documentLayout->setSpacing(0);
    documentLayout->setMargin(8);
    SwTextEdit* editor = new SwTextEdit(documentPage);
    documentLayout->addWidget(editor, 1);
    documentPage->setLayout(documentLayout);

    SwWidget* codePage = new SwWidget();
    const SwCodeEditorVisualTheme codeTheme =
        swCodeEditorVisualThemeById(SwCodeEditorVisualThemeId::VsCodeDark);
    codePage->setStyleSheet(swCodeEditorVisualThemeContainerStyleSheet(codeTheme));
    SwVerticalLayout* codeLayout = new SwVerticalLayout();
    codeLayout->setSpacing(0);
    codeLayout->setMargin(0);
    SwCodeEditor* codeEditor = new DocumentEditorCodeEditor(codePage);
    codeEditor->setPlainText(SwString(sampleCode));
    SwCppSyntaxHighlighter* cppHighlighter = new SwCppSyntaxHighlighter(codeEditor->document());
    codeEditor->setSyntaxHighlighter(cppHighlighter);
    SwCppDiagnosticsProvider* cppDiagnostics = new SwCppDiagnosticsProvider(codeEditor->document());
    cppDiagnostics->setDebounceInterval(220);
    codeEditor->setDiagnosticsProvider(cppDiagnostics);
    SwCppCompletionIndex* completionIndex = configureCodeCompleter(codeEditor);
    if (DocumentEditorCodeEditor* typedCodeEditor = dynamic_cast<DocumentEditorCodeEditor*>(codeEditor)) {
        typedCodeEditor->setCompletionIndex(completionIndex);
    }
    swApplyCodeEditorVisualTheme(codeEditor, codeTheme);
    codeLayout->addWidget(codeEditor, 1);
    codePage->setLayout(codeLayout);

    tabs->addTab(documentPage, "Document");
    tabs->addTab(codePage, "Code");
    tabs->setCurrentIndex(0);

    // ── Main vertical layout ──
    SwVerticalLayout* mainLayout = new SwVerticalLayout();
    mainLayout->setSpacing(4);
    mainLayout->setMargin(8);
    mainLayout->addWidget(toolbar, 0, 40);   // fixed height 40
    mainLayout->addWidget(tabs,    1);       // stretch=1, fills remaining space
    central->setLayout(mainLayout);

    // ── Load cursor-built document by default ──
    SwTextDocument* cursorDoc = buildDocumentWithCursor();

    editor->setDocument(cursorDoc);
    delete cursorDoc;

    // ── Button: Load cursor-built document ──
    SwObject::connect(btnCursor, &SwPushButton::clicked, [&]() {
        SwTextDocument* doc = buildDocumentWithCursor();
        editor->setDocument(doc);
        delete doc;
        statusLabel->setText("Loaded: Cursor-built document");
    });

    // ── Button: Load HTML document ──
    SwObject::connect(btnHtml, &SwPushButton::clicked, [&]() {
        editor->setHtml(SwString(sampleHtml));
        statusLabel->setText("Loaded: HTML document");
    });

    // ── Button: Export PDF ──
    SwObject::connect(btnPdf, &SwPushButton::clicked, [&]() {
        SwString filename = SwFileDialog::getSaveFileName(
            &window,
            SwString("Export PDF"),
            SwString(""),
            SwString("PDF Files (*.pdf)")
        );
        if (!filename.isEmpty()) {
            bool ok = editor->print(filename);
            if (ok) {
                statusLabel->setText(SwString("PDF exported: ") + filename);
            } else {
                statusLabel->setText("PDF export failed!");
            }
        }
    });

    // ── Button: Export HTML ──
    SwObject::connect(btnHtmlExport, &SwPushButton::clicked, [&]() {
        SwString filename = SwFileDialog::getSaveFileName(
            &window,
            SwString("Export HTML"),
            SwString(""),
            SwString("HTML Files (*.html)")
        );
        if (!filename.isEmpty()) {
            SwTextDocument* doc = editor->document();
            SwTextDocumentWriter writer;
            bool ok = writer.writeHtml(doc, filename);
            delete doc;
            if (ok) {
                statusLabel->setText(SwString("HTML exported: ") + filename);
            } else {
                statusLabel->setText("HTML export failed!");
            }
        }
    });

    window.show();
    return app.exec();
}
