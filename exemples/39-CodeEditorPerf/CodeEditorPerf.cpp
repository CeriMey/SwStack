/***************************************************************************************************
 * 39-CodeEditorPerf
 *
 * Dedicated benchmark for large-file editing, highlighting, and completion metadata.
 ***************************************************************************************************/

#include "SwCodeEditor.h"
#include "SwCompleter.h"
#include "SwCppCompletionIndex.h"
#include "SwCppDiagnosticsProvider.h"
#include "SwCppSyntaxHighlighter.h"
#include "SwGuiApplication.h"
#include "SwStandardItemModel.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>

namespace {

using Clock = std::chrono::steady_clock;

struct GeneratedDocument {
    SwString text;
    SwVector<size_t> lineStarts;
};

class PerfCodeEditor : public SwCodeEditor {
public:
    explicit PerfCodeEditor(SwWidget* parent = nullptr)
        : SwCodeEditor(parent) {}

    void benchTypeAt(size_t pos, char ch) {
        m_cursorPos = std::min(pos, m_pieceTable.totalLength());
        m_selectionStart = m_selectionEnd = m_cursorPos;
        insertChar(ch);
    }

    void benchPasteAt(size_t pos, const SwString& text) {
        m_cursorPos = std::min(pos, m_pieceTable.totalLength());
        m_selectionStart = m_selectionEnd = m_cursorPos;
        insertTextAt(m_cursorPos, text);
    }
};

static void appendLine_(GeneratedDocument& doc, const SwString& line) {
    doc.lineStarts.push_back(doc.text.size());
    doc.text.append(line);
    doc.text.append('\n');
}

static GeneratedDocument buildDocument_(int targetLineCount) {
    GeneratedDocument doc;
    doc.lineStarts.reserve(targetLineCount);
    doc.text.reserve(static_cast<size_t>(targetLineCount) * 48);

    appendLine_(doc, "namespace perfbench {");
    appendLine_(doc, "class BigRecord {");
    appendLine_(doc, "public:");
    appendLine_(doc, "    int value;");
    appendLine_(doc, "    int other;");
    appendLine_(doc, "    void touch();");
    appendLine_(doc, "    void reset();");
    appendLine_(doc, "};");
    appendLine_(doc, "void BigRecord::touch() { value += 1; }");
    appendLine_(doc, "void BigRecord::reset() { value = 0; other = 0; }");

    int functionIndex = 0;
    while (static_cast<int>(doc.lineStarts.size()) + 6 <= targetLineCount) {
        appendLine_(doc, SwString("int fn_") + SwString::number(functionIndex) + "() {");
        appendLine_(doc, "    BigRecord entry;");
        appendLine_(doc, SwString("    entry.value = ") + SwString::number(functionIndex) + ";");
        appendLine_(doc, "    entry.");
        appendLine_(doc, "    return entry.value;");
        appendLine_(doc, "}");
        ++functionIndex;
    }

    while (static_cast<int>(doc.lineStarts.size()) < targetLineCount) {
        appendLine_(doc, SwString("int filler_") + SwString::number(functionIndex++) + " = 0;");
    }

    return doc;
}

template<typename Fn>
static long long measureMs_(Fn&& fn) {
    const auto start = Clock::now();
    fn();
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

template<typename Fn>
static long long measureUs_(Fn&& fn) {
    const auto start = Clock::now();
    fn();
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
}

static void printMetric_(const char* name, long long value, const char* unit) {
    std::cout << name << ": " << value << " " << unit << std::endl;
}

} // namespace

int main() {
    SwGuiApplication app;

    const int targetLine = 17888;
    const int totalLines = 100000;
    const int bulkPasteLines = 80000;
    const GeneratedDocument generated = buildDocument_(totalLines);
    const GeneratedDocument bulkPaste = buildDocument_(bulkPasteLines);

    PerfCodeEditor editor;
    editor.resize(1280, 900);

    SwCompleter* completer = new SwCompleter(&editor);
    SwStandardItemModel* model = new SwStandardItemModel(0, 1, completer);
    completer->setCaseSensitivity(Sw::CaseInsensitive);
    completer->setCompletionRole(SwItemDataRole::EditRole);
    completer->setMaxVisibleItems(8);
    completer->setModel(model);
    editor.setCompleter(completer);
    editor.setAutoCompletionEnabled(true);
    editor.setAutoCompletionMinPrefixLength(1);

    SwCppSyntaxHighlighter* highlighter = new SwCppSyntaxHighlighter(editor.document());
    editor.setSyntaxHighlighter(highlighter);

    SwCppDiagnosticsProvider* diagnostics = new SwCppDiagnosticsProvider(editor.document());
    diagnostics->setDebounceInterval(220);
    editor.setDiagnosticsProvider(diagnostics);

    SwCppCompletionIndex index(&editor);
    index.setAsyncEnabled(false);
    index.setTextProvider([&editor]() {
        return editor.toPlainText();
    });
    editor.setCompletionProvider([&index](SwCodeEditor*,
                                          const SwString& prefix,
                                          size_t cursorPos,
                                          bool) {
        SwList<SwCodeEditor::CompletionEntry> entries;
        const SwList<SwCppCompletionItem> items = index.completionItems(cursorPos, prefix, 96);
        for (int i = 0; i < items.size(); ++i) {
            SwCodeEditor::CompletionEntry entry;
            entry.displayText = items[i].displayText;
            entry.insertText = items[i].insertText;
            entry.toolTip = items[i].toolTip;
            entries.append(entry);
        }
        return entries;
    });

    const long long setTextMs = measureMs_([&]() {
        editor.setPlainText(generated.text);
    });
    const long long highlightMs = measureMs_([&]() {
        highlighter->rehighlight();
    });
    const long long indexMs = measureMs_([&]() {
        index.rebuildNow();
    });

    const size_t completionLineStart = generated.lineStarts[static_cast<size_t>(targetLine - 1)];
    const size_t completionPos = completionLineStart + SwString("    entry.").size();

    SwVector<long long> completionQueryUs;
    completionQueryUs.reserve(200);
    for (int i = 0; i < 200; ++i) {
        completionQueryUs.push_back(measureUs_([&]() {
            index.completionItems(completionPos, "v", 32);
        }));
    }

    SwVector<long long> typingUs;
    typingUs.reserve(64);
    size_t typePos = completionPos;
    for (int i = 0; i < 64; ++i) {
        typingUs.push_back(measureUs_([&]() {
            editor.benchTypeAt(typePos, 'x');
        }));
        ++typePos;
    }

    const long long reindexAfterTypingMs = measureMs_([&]() {
        index.rebuildNow();
    });

    const size_t pastePos = generated.lineStarts[static_cast<size_t>(totalLines / 2)];
    const long long bulkPasteMs = measureMs_([&]() {
        editor.benchPasteAt(pastePos, bulkPaste.text);
    });

    const long long completionAvgUs =
        std::accumulate(completionQueryUs.begin(), completionQueryUs.end(), 0LL) /
        std::max(1, static_cast<int>(completionQueryUs.size()));
    const long long completionMaxUs =
        *std::max_element(completionQueryUs.begin(), completionQueryUs.end());
    const long long typingAvgUs =
        std::accumulate(typingUs.begin(), typingUs.end(), 0LL) /
        std::max(1, static_cast<int>(typingUs.size()));
    const long long typingMaxUs =
        *std::max_element(typingUs.begin(), typingUs.end());

    const SwCppCompletionIndex::Stats stats = index.stats();

    std::cout << "CodeEditorPerf benchmark" << std::endl;
    std::cout << "lines: " << totalLines << std::endl;
    std::cout << "target-line: " << targetLine << std::endl;
    printMetric_("set-plain-text", setTextMs, "ms");
    printMetric_("rehighlight", highlightMs, "ms");
    printMetric_("index-build", indexMs, "ms");
    printMetric_("completion-query-avg", completionAvgUs, "us");
    printMetric_("completion-query-max", completionMaxUs, "us");
    printMetric_("typing-avg", typingAvgUs, "us");
    printMetric_("typing-max", typingMaxUs, "us");
    printMetric_("reindex-after-typing", reindexAfterTypingMs, "ms");
    printMetric_("bulk-paste", bulkPasteMs, "ms");
    std::cout << "index-scopes: " << stats.scopeCount << std::endl;
    std::cout << "index-types: " << stats.typeCount << std::endl;
    std::cout << "index-symbols: " << stats.symbolCount << std::endl;

    const bool fluent =
        typingAvgUs <= 2500 &&
        typingMaxUs <= 8000 &&
        completionAvgUs <= 1500 &&
        completionMaxUs <= 5000;
    std::cout << "verdict: " << (fluent ? "PASS" : "FAIL") << std::endl;

    return fluent ? 0 : 2;
}
