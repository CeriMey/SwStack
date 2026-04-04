#include "RuntimeProfilerStackInspectorWidget.h"

#include "SwSyntaxHighlighter.h"
#include "SwTextFormat.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace {

static SwString timingKindName_(SwRuntimeTimingKind kind) {
    switch (kind) {
    case SwRuntimeTimingKind::PlatformPump:
        return "PlatformPump";
    case SwRuntimeTimingKind::PostedEvent:
        return "PostedEvent";
    case SwRuntimeTimingKind::ObjectEvent:
        return "ObjectEvent";
    case SwRuntimeTimingKind::Timer:
        return "Timer";
    case SwRuntimeTimingKind::FiberTask:
        return "FiberTask";
    case SwRuntimeTimingKind::ManualScope:
    default:
        return "ManualScope";
    }
}

static SwString laneName_(SwFiberLane lane) {
    switch (lane) {
    case SwFiberLane::Control:
        return "Control";
    case SwFiberLane::Input:
        return "Input";
    case SwFiberLane::Normal:
        return "Normal";
    case SwFiberLane::Background:
    default:
        return "Background";
    }
}

static SwString durationText_(long long durationUs) {
    const double durationMs = static_cast<double>(durationUs) / 1000.0;
    const int precision = durationMs >= 100.0 ? 0 : 1;
    return SwString::number(durationMs, 'f', precision) + " ms";
}

static SwString secondsSinceLaunchText_(long long sampleTimeNs, long long launchTimeNs) {
    if (sampleTimeNs <= 0 || launchTimeNs <= 0 || sampleTimeNs < launchTimeNs) {
        return "n/a";
    }
    const double seconds = static_cast<double>(sampleTimeNs - launchTimeNs) / 1000000000.0;
    return SwString::number(seconds, 'f', 3) + " s";
}

static SwString hexAddress_(unsigned long long value) {
    return "0x" + SwString::number(value, 16);
}

static SwString baseName_(const SwString& path) {
    if (path.isEmpty()) {
        return SwString();
    }
    const std::string value = path.toStdString();
    const size_t pos = value.find_last_of("\\/");
    if (pos == std::string::npos || pos + 1 >= value.size()) {
        return path;
    }
    return SwString::fromUtf8(value.c_str() + pos + 1);
}

static SwString sourceLocationText_(const SwRuntimeResolvedFrame& frame) {
    if (!frame.lineResolved || frame.sourceFile.isEmpty()) {
        return SwString();
    }
    return frame.sourceFile + ":" + SwString::number(frame.lineNumber);
}

static SwString frameHeadline_(const SwRuntimeResolvedFrame& frame) {
    if (frame.address == 0 && frame.symbolName.isEmpty() && frame.moduleName.isEmpty()) {
        return "<unresolved>";
    }

    const SwString moduleLabel = !frame.moduleName.isEmpty() ? frame.moduleName : baseName_(frame.modulePath);
    SwString headline;
    if (!moduleLabel.isEmpty()) {
        headline.append(moduleLabel);
        if (frame.symbolResolved) {
            headline.append('!');
        }
    }

    if (frame.symbolResolved) {
        headline.append(frame.symbolName);
        headline.append(" + 0x");
        headline.append(SwString::number(frame.displacement, 16));
    } else if (frame.moduleResolved && frame.moduleBase != 0 && frame.address >= frame.moduleBase) {
        if (headline.isEmpty()) {
            headline = moduleLabel;
        }
        headline.append(" + 0x");
        headline.append(SwString::number(frame.address - frame.moduleBase, 16));
    } else {
        headline = hexAddress_(frame.address);
    }

    if (frame.lineResolved) {
        headline.append(" (");
        headline.append(baseName_(frame.sourceFile));
        headline.append(":");
        headline.append(SwString::number(frame.lineNumber));
        headline.append(")");
    }
    return headline;
}

static SwString firstResolvedFrame_(const RuntimeProfilerStackInspectorData& data) {
    for (size_t i = 0; i < data.resolvedFrames.size(); ++i) {
        const SwRuntimeResolvedFrame& frame = data.resolvedFrames[i];
        if (frame.symbolResolved || frame.lineResolved || frame.moduleResolved) {
            return frameHeadline_(frame);
        }
    }
    for (size_t i = 0; i < data.symbols.size(); ++i) {
        if (!data.symbols[i].isEmpty()) {
            return data.symbols[i];
        }
    }
    if (!data.frames.isEmpty()) {
        return hexAddress_(data.frames[0]);
    }
    return "<none>";
}

static int resolvedSymbolCount_(const RuntimeProfilerStackInspectorData& data) {
    if (!data.resolvedFrames.isEmpty()) {
        int count = 0;
        for (size_t i = 0; i < data.resolvedFrames.size(); ++i) {
            if (data.resolvedFrames[i].symbolResolved) {
                ++count;
            }
        }
        return count;
    }

    int count = 0;
    for (size_t i = 0; i < data.symbols.size(); ++i) {
        if (!data.symbols[i].isEmpty()) {
            ++count;
        }
    }
    return count;
}

static int resolvedSourceCount_(const RuntimeProfilerStackInspectorData& data) {
    int count = 0;
    for (size_t i = 0; i < data.resolvedFrames.size(); ++i) {
        if (data.resolvedFrames[i].lineResolved) {
            ++count;
        }
    }
    return count;
}

static int resolvedModuleCount_(const RuntimeProfilerStackInspectorData& data) {
    int count = 0;
    SwList<SwString> uniqueModules;
    for (size_t i = 0; i < data.resolvedFrames.size(); ++i) {
        const SwString moduleName = !data.resolvedFrames[i].moduleName.isEmpty()
                                        ? data.resolvedFrames[i].moduleName
                                        : data.resolvedFrames[i].modulePath;
        if (moduleName.isEmpty()) {
            continue;
        }
        bool alreadySeen = false;
        for (size_t j = 0; j < uniqueModules.size(); ++j) {
            if (uniqueModules[j] == moduleName) {
                alreadySeen = true;
                break;
            }
        }
        if (!alreadySeen) {
            uniqueModules.append(moduleName);
            ++count;
        }
    }
    return count;
}

static SwString captureQuality_(const RuntimeProfilerStackInspectorData& data) {
    const int frameCount = static_cast<int>(data.frames.size());
    const int symbolCount = resolvedSymbolCount_(data);
    const int sourceCount = resolvedSourceCount_(data);
    if (frameCount <= 0) {
        return "no frames captured";
    }
    if (sourceCount >= frameCount && frameCount > 0) {
        return "file + line for all frames";
    }
    if (sourceCount > 0) {
        return "mixed symbols with source lines";
    }
    if (symbolCount >= frameCount) {
        return "symbols for all frames";
    }
    if (symbolCount > 0) {
        return "partially symbolized";
    }
    return "raw addresses only";
}

static SwString severityText_(const RuntimeProfilerStackInspectorData& data) {
    const long long thresholdUs = std::max(1LL, data.thresholdUs);
    if (data.elapsedUs >= thresholdUs * 4) {
        return "CRITICAL";
    }
    if (data.elapsedUs >= thresholdUs * 2) {
        return "SEVERE";
    }
    if (data.elapsedUs >= thresholdUs) {
        return "ELEVATED";
    }
    return "WITHIN THRESHOLD";
}

static bool containsAnyToken_(const SwString& value, const char* const* tokens, int tokenCount) {
    const std::string lowered = value.toStdString();
    std::string haystack(lowered);
    std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    for (int i = 0; i < tokenCount; ++i) {
        std::string needle(tokens[i]);
        std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (haystack.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

static SwString blockingHint_(const RuntimeProfilerStackInspectorData& data) {
    static const char* const kDelayTokens[] = {"sleep", "delay", "wait", "stall", "poll"};
    static const char* const kIoTokens[] = {"recv", "send", "socket", "readfile", "writefile", "epoll", "select"};
    static const char* const kLockTokens[] = {"mutex", "criticalsection", "semaphore", "conditionvariable", "lock"};

    for (size_t i = 0; i < data.resolvedFrames.size(); ++i) {
        const SwRuntimeResolvedFrame& frame = data.resolvedFrames[i];
        const SwString candidates[] = {frame.symbolName, frame.moduleName, frame.sourceFile};
        for (size_t j = 0; j < sizeof(candidates) / sizeof(candidates[0]); ++j) {
            const SwString& value = candidates[j];
            if (value.isEmpty()) {
                continue;
            }
            if (containsAnyToken_(value, kDelayTokens,
                                  static_cast<int>(sizeof(kDelayTokens) / sizeof(kDelayTokens[0])))) {
                return "explicit delay or wait path";
            }
            if (containsAnyToken_(value, kIoTokens,
                                  static_cast<int>(sizeof(kIoTokens) / sizeof(kIoTokens[0])))) {
                return "blocking IO path";
            }
            if (containsAnyToken_(value, kLockTokens,
                                  static_cast<int>(sizeof(kLockTokens) / sizeof(kLockTokens[0])))) {
                return "contention or lock path";
            }
        }
    }

    for (size_t i = 0; i < data.symbols.size(); ++i) {
        const SwString& symbol = data.symbols[i];
        if (symbol.isEmpty()) {
            continue;
        }
        if (containsAnyToken_(symbol, kDelayTokens, static_cast<int>(sizeof(kDelayTokens) / sizeof(kDelayTokens[0])))) {
            return "explicit delay or wait path";
        }
        if (containsAnyToken_(symbol, kIoTokens, static_cast<int>(sizeof(kIoTokens) / sizeof(kIoTokens[0])))) {
            return "blocking IO path";
        }
        if (containsAnyToken_(symbol, kLockTokens, static_cast<int>(sizeof(kLockTokens) / sizeof(kLockTokens[0])))) {
            return "contention or lock path";
        }
    }

    if (!data.frames.isEmpty()) {
        return "native call stack captured";
    }
    return "no blocking signature available";
}

static std::string trimCopy_(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(begin, end - begin);
}

static bool isUniformLine_(const std::string& value, char token) {
    if (value.size() < 4) {
        return false;
    }
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != token) {
            return false;
        }
    }
    return true;
}

static bool isSectionHeading_(const std::string& value) {
    if (value.empty()) {
        return false;
    }

    bool hasLetter = false;
    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if (std::isalpha(ch) != 0) {
            hasLetter = true;
            if (std::toupper(ch) != ch) {
                return false;
            }
            continue;
        }
        if (value[i] != ' ' && value[i] != '_' && value[i] != '/') {
            return false;
        }
    }
    return hasLetter;
}

static SwTextCharFormat makeFormat_(const SwColor& color,
                                    FontWeight weight = Normal,
                                    bool italic = false) {
    SwTextCharFormat format;
    format.setForeground(color);
    if (weight != Normal) {
        format.setFontWeight(weight);
    }
    if (italic) {
        format.setFontItalic(true);
    }
    return format;
}

class RuntimeProfilerStackReportHighlighter : public SwSyntaxHighlighter {
    SW_OBJECT(RuntimeProfilerStackReportHighlighter, SwSyntaxHighlighter)

public:
    explicit RuntimeProfilerStackReportHighlighter(SwTextDocument* document = nullptr)
        : SwSyntaxHighlighter(document) {
        setDocument(document);
    }

protected:
    void highlightBlock(const SwString& text) override {
        const std::string line = text.toStdString();
        const std::string trimmed = trimCopy_(line);
        if (trimmed.empty()) {
            return;
        }

        if (trimmed == "STALL REPORT") {
            setFormat(0, static_cast<int>(text.size()), titleFormat_);
            return;
        }

        if (isUniformLine_(trimmed, '=') || isUniformLine_(trimmed, '-')) {
            setFormat(0, static_cast<int>(text.size()), dividerFormat_);
            return;
        }

        if (isSectionHeading_(trimmed)) {
            setFormat(0, static_cast<int>(text.size()), sectionFormat_);
            return;
        }

        if (!line.empty() && line[0] == '[') {
            highlightFrameLine_(line);
            return;
        }

        const size_t colon = line.find(':');
        if (colon != std::string::npos) {
            highlightKeyValueLine_(line, colon);
            return;
        }

        if (trimmed.rfind("- ", 0) == 0) {
            setFormat(0, static_cast<int>(text.size()), noteFormat_);
            return;
        }
    }

private:
    void highlightFrameLine_(const std::string& line) {
        const size_t closeBracket = line.find(']');
        if (closeBracket != std::string::npos) {
            setFormat(0, static_cast<int>(closeBracket + 1), frameIndexFormat_);
        }

        const size_t atPos = line.find(" @ ");
        if (atPos != std::string::npos) {
            if (closeBracket != std::string::npos && atPos > closeBracket + 2) {
                setFormat(static_cast<int>(closeBracket + 2),
                          static_cast<int>(atPos - closeBracket - 2),
                          frameSymbolFormat_);
            }
            setFormat(static_cast<int>(atPos + 3),
                      static_cast<int>(line.size() - atPos - 3),
                      addressFormat_);
        } else if (closeBracket != std::string::npos && closeBracket + 2 < line.size()) {
            setFormat(static_cast<int>(closeBracket + 2),
                      static_cast<int>(line.size() - closeBracket - 2),
                      frameSymbolFormat_);
        }
    }

    void highlightKeyValueLine_(const std::string& line, size_t colon) {
        size_t keyStart = 0;
        while (keyStart < colon && std::isspace(static_cast<unsigned char>(line[keyStart])) != 0) {
            ++keyStart;
        }
        if (keyStart < colon) {
            setFormat(static_cast<int>(keyStart),
                      static_cast<int>(colon - keyStart + 1),
                      keyFormat_);
        }

        size_t valueStart = colon + 1;
        while (valueStart < line.size() && std::isspace(static_cast<unsigned char>(line[valueStart])) != 0) {
            ++valueStart;
        }

        highlightHexTokens_(line, valueStart);
        highlightDurationTokens_(line, valueStart);
        highlightStatusTokens_(line, valueStart);
        highlightFrameKinds_(line, valueStart);
    }

    void highlightHexTokens_(const std::string& line, size_t start) {
        size_t pos = start;
        while ((pos = line.find("0x", pos)) != std::string::npos) {
            size_t end = pos + 2;
            while (end < line.size() && std::isxdigit(static_cast<unsigned char>(line[end])) != 0) {
                ++end;
            }
            setFormat(static_cast<int>(pos), static_cast<int>(end - pos), addressFormat_);
            pos = end;
        }
    }

    void highlightDurationTokens_(const std::string& line, size_t start) {
        size_t pos = start;
        while (pos < line.size()) {
            if (std::isdigit(static_cast<unsigned char>(line[pos])) == 0 &&
                !(line[pos] == '+' && pos + 1 < line.size() &&
                  std::isdigit(static_cast<unsigned char>(line[pos + 1])) != 0)) {
                ++pos;
                continue;
            }

            const size_t begin = pos;
            if (line[pos] == '+') {
                ++pos;
            }
            while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])) != 0) {
                ++pos;
            }
            if (pos < line.size() && line[pos] == '.') {
                ++pos;
                while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])) != 0) {
                    ++pos;
                }
            }
            if (pos < line.size() && line[pos] == ' ') {
                ++pos;
            }

            if (pos + 1 < line.size() &&
                ((line[pos] == 'm' && line[pos + 1] == 's') ||
                 (line[pos] == 'u' && line[pos + 1] == 's'))) {
                pos += 2;
                setFormat(static_cast<int>(begin), static_cast<int>(pos - begin), metricFormat_);
                continue;
            }
        }
    }

    void highlightStatusTokens_(const std::string& line, size_t start) {
        highlightToken_(line, start, "CRITICAL", criticalFormat_);
        highlightToken_(line, start, "SEVERE", criticalFormat_);
        highlightToken_(line, start, "ELEVATED", metricFormat_);
        highlightToken_(line, start, "WITHIN THRESHOLD", mutedValueFormat_);
    }

    void highlightFrameKinds_(const std::string& line, size_t start) {
        highlightToken_(line, start, "PostedEvent", valueFormat_);
        highlightToken_(line, start, "ObjectEvent", valueFormat_);
        highlightToken_(line, start, "ManualScope", valueFormat_);
        highlightToken_(line, start, "Timer", valueFormat_);
        highlightToken_(line, start, "FiberTask", valueFormat_);
        highlightToken_(line, start, "PlatformPump", valueFormat_);
        highlightToken_(line, start, "Control", valueFormat_);
        highlightToken_(line, start, "Input", valueFormat_);
        highlightToken_(line, start, "Normal", valueFormat_);
        highlightToken_(line, start, "Background", valueFormat_);
    }

    void highlightToken_(const std::string& line,
                         size_t start,
                         const char* token,
                         const SwTextCharFormat& format) {
        if (!token || !*token) {
            return;
        }

        const std::string needle(token);
        size_t pos = line.find(needle, start);
        while (pos != std::string::npos) {
            setFormat(static_cast<int>(pos), static_cast<int>(needle.size()), format);
            pos = line.find(needle, pos + needle.size());
        }
    }

    SwTextCharFormat titleFormat_{makeFormat_(SwColor{255, 255, 255}, SemiBold)};
    SwTextCharFormat sectionFormat_{makeFormat_(SwColor{78, 201, 176}, Medium)};
    SwTextCharFormat dividerFormat_{makeFormat_(SwColor{97, 97, 97})};
    SwTextCharFormat keyFormat_{makeFormat_(SwColor{156, 163, 175})};
    SwTextCharFormat valueFormat_{makeFormat_(SwColor{220, 220, 220})};
    SwTextCharFormat mutedValueFormat_{makeFormat_(SwColor{142, 142, 146})};
    SwTextCharFormat metricFormat_{makeFormat_(SwColor{255, 203, 107}, Medium)};
    SwTextCharFormat criticalFormat_{makeFormat_(SwColor{244, 71, 71}, Medium)};
    SwTextCharFormat frameIndexFormat_{makeFormat_(SwColor{86, 156, 214}, Medium)};
    SwTextCharFormat frameSymbolFormat_{makeFormat_(SwColor{220, 220, 170})};
    SwTextCharFormat addressFormat_{makeFormat_(SwColor{106, 115, 125})};
    SwTextCharFormat noteFormat_{makeFormat_(SwColor{142, 142, 146})};
};

} // namespace

RuntimeProfilerStackInspectorWidget::RuntimeProfilerStackInspectorWidget(SwWidget* parent)
    : SwWidget(parent) {
    buildUi_();
    clearEntry();
}

void RuntimeProfilerStackInspectorWidget::showEntry(const RuntimeProfilerStackInspectorData& data) {
    if (!editor_) {
        return;
    }

    const SwString report = reportTextFor_(data);
    if (data.sequence == lastSequence_ && report == lastReportText_) {
        return;
    }

    lastSequence_ = data.sequence;
    lastReportText_ = report;
    editor_->setPlainText(report);
}

void RuntimeProfilerStackInspectorWidget::clearEntry() {
    if (editor_) {
        lastSequence_ = 0;
        lastReportText_ =
            "STALL REPORT\n"
            "============\n"
            "\n"
            "OVERVIEW\n"
            "--------\n"
            "selection    : <none>\n"
            "status       : waiting for input\n"
            "\n"
            "NOTES\n"
            "-----\n"
            "- Select a row in the stall table to open a detailed capture report.\n"
            "- This inspector uses a read-only code editor surface for precise stack reading.\n";
        editor_->setPlainText(lastReportText_);
    }
}

bool RuntimeProfilerStackInspectorWidget::isPinnedToTop() const {
    return !editor_ || editor_->firstVisibleLine() <= 0;
}

SwSize RuntimeProfilerStackInspectorWidget::minimumSizeHint() const {
    return SwSize{250, 180};
}

void RuntimeProfilerStackInspectorWidget::buildUi_() {
    setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");

    editor_ = new SwCodeEditor(this);
    editor_->setReadOnly(true);
    editor_->setWordWrapEnabled(false);
    editor_->setLineNumbersVisible(false);
    editor_->setCodeFoldingEnabled(false);
    editor_->setAutoCompletionEnabled(false);
    editor_->setPlaceholderText("Select a stall to inspect the capture report.");
    editor_->setSyntaxHighlighter(new RuntimeProfilerStackReportHighlighter(editor_->document()));
    applyEditorTheme_();

    SwVerticalLayout* layout = new SwVerticalLayout();
    layout->setMargin(0);
    layout->setSpacing(0);
    layout->addWidget(editor_, 1, 240);
    setLayout(layout);
}

void RuntimeProfilerStackInspectorWidget::applyEditorTheme_() {
    if (!editor_) {
        return;
    }

    SwCodeEditorTheme theme = swCodeEditorVsCodeDarkTheme();
    theme.backgroundColor = SwColor{30, 30, 30};
    theme.borderColor = SwColor{62, 62, 66};
    theme.focusBorderColor = SwColor{0, 122, 204};
    theme.textColor = SwColor{220, 220, 220};
    theme.disabledTextColor = SwColor{112, 112, 112};
    theme.gutterBackgroundColor = SwColor{30, 30, 30};
    theme.gutterTextColor = SwColor{132, 132, 136};
    theme.currentLineNumberColor = SwColor{198, 198, 198};
    theme.gutterSeparatorColor = SwColor{51, 51, 55};
    theme.currentLineBackgroundColor = SwColor{37, 37, 38};
    theme.selectionBackgroundColor = SwColor{9, 71, 113};
    theme.placeholderColor = SwColor{106, 115, 125};
    theme.scrollBarTrackColor = SwColor{37, 37, 38};
    theme.scrollBarThumbColor = SwColor{96, 103, 112};
    theme.scrollBarThumbHoverColor = SwColor{122, 128, 138};
    theme.borderRadius = 8;
    theme.scrollBarWidth = 14;
    editor_->setTheme(theme);
}

SwString RuntimeProfilerStackInspectorWidget::reportTextFor_(const RuntimeProfilerStackInspectorData& data) const {
    const long long overshootUs = std::max(0LL, data.elapsedUs - data.thresholdUs);
    const int frameCount = static_cast<int>(std::max(data.frames.size(), data.resolvedFrames.size()));
    const int symbolCount = resolvedSymbolCount_(data);
    const int sourceCount = resolvedSourceCount_(data);
    const int moduleCount = resolvedModuleCount_(data);
    SwString topSource;
    SwString topModulePath;
    for (size_t i = 0; i < data.resolvedFrames.size(); ++i) {
        if (topSource.isEmpty() && data.resolvedFrames[i].lineResolved) {
            topSource = sourceLocationText_(data.resolvedFrames[i]);
        }
        if (topModulePath.isEmpty() && !data.resolvedFrames[i].modulePath.isEmpty()) {
            topModulePath = data.resolvedFrames[i].modulePath;
        }
        if (!topSource.isEmpty() && !topModulePath.isEmpty()) {
            break;
        }
    }

    auto appendKeyValue = [](SwString& out, const char* key, const SwString& value) {
        SwString keyText(key ? key : "");
        while (keyText.size() < 13) {
            keyText.append(' ');
        }
        out.append(keyText);
        out.append(": ");
        out.append(value);
        out.append("\n");
    };

    auto appendLine = [](SwString& out, const SwString& line) {
        out.append(line);
        out.append("\n");
    };

    SwString report;
    appendLine(report, "STALL REPORT");
    appendLine(report, "============");
    appendLine(report, "");

    appendLine(report, "OVERVIEW");
    appendLine(report, "--------");
    appendKeyValue(report, "sequence", "#" + SwString::number(data.sequence));
    appendKeyValue(report, "kind", timingKindName_(data.kind));
    appendKeyValue(report, "scope", data.label.isEmpty() ? SwString("<unnamed>") : data.label);
    appendKeyValue(report, "captured_at", secondsSinceLaunchText_(data.sampleTimeNs, data.launchTimeNs) + " since launch");
    appendKeyValue(report, "duration", durationText_(data.elapsedUs));
    appendKeyValue(report, "threshold", durationText_(data.thresholdUs));
    appendKeyValue(report, "overshoot", overshootUs > 0 ? ("+" + durationText_(overshootUs)) : SwString("within threshold"));
    appendKeyValue(report, "severity", severityText_(data));
    appendKeyValue(report, "lane", laneName_(data.lane));
    if (!data.applicationLabel.isEmpty()) {
        appendKeyValue(report, "runtime", data.applicationLabel);
    } else if (data.applicationId != 0ULL) {
        appendKeyValue(report, "runtime", "#" + SwString::number(data.applicationId));
    }
    appendKeyValue(report, "thread", SwString::number(data.threadId));
    appendLine(report, "");

    appendLine(report, "CAPTURE");
    appendLine(report, "-------");
    appendKeyValue(report, "frames", SwString::number(frameCount));
    appendKeyValue(report, "symbols", SwString::number(symbolCount) + " / " + SwString::number(frameCount));
    appendKeyValue(report, "sources", SwString::number(sourceCount) + " / " + SwString::number(frameCount));
    appendKeyValue(report, "modules", SwString::number(moduleCount));
    appendKeyValue(report, "top_frame", firstResolvedFrame_(data));
    appendKeyValue(report, "top_source", topSource.isEmpty() ? SwString("<none>") : topSource);
    appendKeyValue(report, "module_path", topModulePath.isEmpty() ? SwString("<none>") : topModulePath);
    appendKeyValue(report, "backend", data.symbolBackend.isEmpty() ? SwString("<default>") : data.symbolBackend);
    appendKeyValue(report, "quality", captureQuality_(data));
    appendKeyValue(report, "heuristic", blockingHint_(data));
    if (!data.symbolSearchPath.isEmpty()) {
        appendKeyValue(report, "search_path", data.symbolSearchPath);
    }
    appendLine(report, "");

    appendLine(report, "STACK TRACE");
    appendLine(report, "-----------");
    if (frameCount == 0 && data.symbols.isEmpty()) {
        appendLine(report, "No stack frames were captured for this stall.");
    } else if (!data.resolvedFrames.isEmpty()) {
        for (size_t i = 0; i < data.resolvedFrames.size(); ++i) {
            const SwRuntimeResolvedFrame& frame = data.resolvedFrames[i];
            SwString index = SwString::number(static_cast<long long>(i + 1));
            if (index.size() < 2) {
                index = "0" + index;
            }

            appendLine(report, "[" + index + "] " + frameHeadline_(frame) + " @ " + hexAddress_(frame.address));
            if (frame.lineResolved) {
                appendKeyValue(report, "  source", sourceLocationText_(frame));
            }
            if (!frame.modulePath.isEmpty()) {
                appendKeyValue(report, "  module", frame.modulePath);
            }
            if (!frame.symbolResolved && !frame.moduleResolved) {
                appendKeyValue(report, "  status", "unresolved address");
            }
            appendLine(report, "");
        }
    } else {
        const size_t lineCount = std::max(data.frames.size(), data.symbols.size());
        for (size_t i = 0; i < lineCount; ++i) {
            SwString index = SwString::number(static_cast<long long>(i + 1));
            if (index.size() < 2) {
                index = "0" + index;
            }

            SwString symbol = (i < data.symbols.size() && !data.symbols[i].isEmpty())
                                  ? data.symbols[i]
                                  : SwString("<unresolved>");
            std::string padded = symbol.toStdString();
            if (padded.size() < 48) {
                padded.append(48 - padded.size(), ' ');
            }
            const SwString address = (i < data.frames.size()) ? hexAddress_(data.frames[i]) : SwString("n/a");
            appendLine(report, "[" + index + "] " + SwString(padded.c_str()) + " @ " + address);
        }
    }

    appendLine(report, "");
    appendLine(report, "NOTES");
    appendLine(report, "-----");
    appendLine(report, "- Overshoot is computed against the active stall threshold.");
    appendLine(report, "- Stack quality reflects how many frames resolved to symbols and source lines.");
    appendLine(report, "- Module and source paths come from the active PDB + loaded module state on the sampled thread.");

    return report;
}
