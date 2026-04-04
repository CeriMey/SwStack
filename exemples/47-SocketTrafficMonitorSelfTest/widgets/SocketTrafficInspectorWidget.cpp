#include "SocketTrafficInspectorWidget.h"

#include "SwSyntaxHighlighter.h"
#include "SwTextFormat.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <string>

namespace {

struct ClassDisplay_ {
    SwString shortName;
    SwString namespaceName;
};

static SwString humanBytes_(unsigned long long bytes) {
    const double value = static_cast<double>(bytes);
    if (value >= 1024.0 * 1024.0 * 1024.0) {
        return SwString::number(value / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
    }
    if (value >= 1024.0 * 1024.0) {
        return SwString::number(value / (1024.0 * 1024.0), 'f', 2) + " MB";
    }
    if (value >= 1024.0) {
        return SwString::number(value / 1024.0, 'f', 1) + " KB";
    }
    return SwString::number(bytes) + " B";
}

static SwString humanRate_(unsigned long long bytesPerSecond) {
    return humanBytes_(bytesPerSecond) + "/s";
}

static SwString secondsSinceLaunchText_(long long sampleTimeNs, long long launchTimeNs) {
    if (sampleTimeNs <= 0 || launchTimeNs <= 0 || sampleTimeNs < launchTimeNs) {
        return "n/a";
    }
    return SwString::number(static_cast<double>(sampleTimeNs - launchTimeNs) / 1000000000.0, 'f', 3) + " s";
}

static SwString activityAgeText_(long long sampleTimeNs, long long lastActivityNs) {
    if (sampleTimeNs <= 0 || lastActivityNs <= 0) {
        return "n/a";
    }
    const long long deltaNs = std::max(0LL, sampleTimeNs - lastActivityNs);
    return SwString::number(static_cast<double>(deltaNs) / 1000000000.0, 'f', deltaNs < 10000000000LL ? 2 : 1) + " s ago";
}

static SwString endpointText_(const SwSocketTrafficSocketSnapshot& socket) {
    if (!socket.endpointSummary.isEmpty()) {
        return socket.endpointSummary;
    }
    return socket.transportName;
}

static ClassDisplay_ classDisplay_(const SwString& rawClassName) {
    ClassDisplay_ display;
    std::string full = rawClassName.toStdString();
    if (full.empty()) {
        display.shortName = "<unknown>";
        return display;
    }

    const std::string anonymousNamespace = "anonymous namespace::";
    size_t anonymousPos = full.find(anonymousNamespace);
    while (anonymousPos != std::string::npos) {
        full.erase(anonymousPos, anonymousNamespace.size());
        anonymousPos = full.find(anonymousNamespace);
    }

    const std::string classPrefix = "class ";
    if (full.rfind(classPrefix, 0) == 0) {
        full.erase(0, classPrefix.size());
    }
    const std::string structPrefix = "struct ";
    if (full.rfind(structPrefix, 0) == 0) {
        full.erase(0, structPrefix.size());
    }

    const size_t separator = full.rfind("::");
    if (separator == std::string::npos) {
        display.shortName = SwString(full);
        return display;
    }

    display.namespaceName = SwString(full.substr(0, separator));
    display.shortName = SwString(full.substr(separator + 2));
    return display;
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

static bool isDigitOrSign_(char ch) {
    return std::isdigit(static_cast<unsigned char>(ch)) != 0 || ch == '+' || ch == '-';
}

class SocketTrafficReportHighlighter : public SwSyntaxHighlighter {
    SW_OBJECT(SocketTrafficReportHighlighter, SwSyntaxHighlighter)

public:
    explicit SocketTrafficReportHighlighter(SwTextDocument* document = nullptr)
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

        if (trimmed == "NETWORK CONSUMER REPORT") {
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
            highlightSocketLine_(line);
            return;
        }

        const size_t colon = line.find(':');
        if (colon != std::string::npos) {
            highlightKeyValueLine_(line, colon);
            return;
        }

        if (trimmed.rfind("- ", 0) == 0) {
            setFormat(0, static_cast<int>(text.size()), noteFormat_);
        }
    }

private:
    void highlightSocketLine_(const std::string& line) {
        const size_t closeBracket = line.find(']');
        if (closeBracket != std::string::npos) {
            setFormat(0, static_cast<int>(closeBracket + 1), socketIndexFormat_);
        }

        size_t segmentStart = (closeBracket == std::string::npos) ? 0 : closeBracket + 2;
        size_t separator = line.find(" | ", segmentStart);
        if (separator != std::string::npos && separator > segmentStart) {
            setFormat(static_cast<int>(segmentStart),
                      static_cast<int>(separator - segmentStart),
                      socketNameFormat_);
            segmentStart = separator + 3;
        }

        separator = line.find(" | ", segmentStart);
        if (separator != std::string::npos && separator > segmentStart) {
            setFormat(static_cast<int>(segmentStart),
                      static_cast<int>(separator - segmentStart),
                      transportFormat_);
            setFormat(static_cast<int>(separator + 3),
                      static_cast<int>(line.size() - separator - 3),
                      statusFormat_);
            highlightStatusTokens_(line, separator + 3);
            return;
        }

        if (segmentStart < line.size()) {
            setFormat(static_cast<int>(segmentStart),
                      static_cast<int>(line.size() - segmentStart),
                      socketNameFormat_);
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
        if (valueStart >= line.size()) {
            return;
        }

        setFormat(static_cast<int>(valueStart),
                  static_cast<int>(line.size() - valueStart),
                  valueFormat_);

        highlightRateTokens_(line, valueStart);
        highlightPercentTokens_(line, valueStart);
        highlightStatusTokens_(line, valueStart);
        highlightTransportTokens_(line, valueStart);
        highlightEndpointValue_(line, valueStart);
    }

    void highlightRateTokens_(const std::string& line, size_t start) {
        size_t pos = start;
        while (pos < line.size()) {
            if (!isDigitOrSign_(line[pos])) {
                ++pos;
                continue;
            }

            const size_t begin = pos;
            if (line[pos] == '+' || line[pos] == '-') {
                ++pos;
            }
            bool hasDigits = false;
            while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])) != 0) {
                hasDigits = true;
                ++pos;
            }
            if (pos < line.size() && line[pos] == '.') {
                ++pos;
                while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])) != 0) {
                    hasDigits = true;
                    ++pos;
                }
            }
            if (!hasDigits) {
                continue;
            }
            if (pos < line.size() && line[pos] == ' ') {
                ++pos;
            }

            const char* const units[] = {"B/s", "KB/s", "MB/s", "GB/s", "B", "KB", "MB", "GB", "%", "s ago", "s since launch"};
            bool matchedUnit = false;
            for (size_t i = 0; i < sizeof(units) / sizeof(units[0]); ++i) {
                const std::string unit(units[i]);
                if (line.compare(pos, unit.size(), unit) == 0) {
                    pos += unit.size();
                    setFormat(static_cast<int>(begin), static_cast<int>(pos - begin), metricFormat_);
                    matchedUnit = true;
                    break;
                }
            }
            if (!matchedUnit && pos > begin) {
                if (begin == 0 || std::isspace(static_cast<unsigned char>(line[begin - 1])) != 0) {
                    setFormat(static_cast<int>(begin), static_cast<int>(pos - begin), metricFormat_);
                }
            }
        }
    }

    void highlightPercentTokens_(const std::string& line, size_t start) {
        size_t pos = line.find('%', start);
        while (pos != std::string::npos) {
            size_t begin = pos;
            while (begin > start &&
                   (std::isdigit(static_cast<unsigned char>(line[begin - 1])) != 0 ||
                    line[begin - 1] == '.' || line[begin - 1] == ' ')) {
                --begin;
            }
            setFormat(static_cast<int>(begin), static_cast<int>(pos - begin + 1), metricFormat_);
            pos = line.find('%', pos + 1);
        }
    }

    void highlightStatusTokens_(const std::string& line, size_t start) {
        highlightToken_(line, start, "idle", mutedValueFormat_);
        highlightToken_(line, start, "open", statusFormat_);
        highlightToken_(line, start, "closed", criticalFormat_);
        highlightToken_(line, start, "waiting for input", mutedValueFormat_);
        highlightToken_(line, start, "<none>", mutedValueFormat_);
        highlightToken_(line, start, "<unnamed>", mutedValueFormat_);
        highlightToken_(line, start, "<unknown>", mutedValueFormat_);
        highlightToken_(line, start, "n/a", mutedValueFormat_);
    }

    void highlightTransportTokens_(const std::string& line, size_t start) {
        highlightToken_(line, start, "TCP", transportFormat_);
        highlightToken_(line, start, "UDP", transportFormat_);
        highlightToken_(line, start, "TLS", transportFormat_);
    }

    void highlightEndpointValue_(const std::string& line, size_t start) {
        if (line.find("endpoint", 0) == std::string::npos) {
            return;
        }

        size_t pos = start;
        while (pos < line.size()) {
            if (std::isdigit(static_cast<unsigned char>(line[pos])) == 0 &&
                line[pos] != ':' && line[pos] != '.' && line[pos] != '-' && line[pos] != '>' &&
                line[pos] != '[' && line[pos] != ']' && line[pos] != '*') {
                ++pos;
                continue;
            }

            const size_t begin = pos;
            while (pos < line.size()) {
                const unsigned char ch = static_cast<unsigned char>(line[pos]);
                if (std::isdigit(ch) != 0 || line[pos] == '.' || line[pos] == ':' ||
                    line[pos] == '-' || line[pos] == '>' || line[pos] == '[' ||
                    line[pos] == ']' || line[pos] == '*') {
                    ++pos;
                    continue;
                }
                break;
            }
            if (pos > begin) {
                setFormat(static_cast<int>(begin), static_cast<int>(pos - begin), endpointFormat_);
            }
        }
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
    SwTextCharFormat socketIndexFormat_{makeFormat_(SwColor{86, 156, 214}, Medium)};
    SwTextCharFormat socketNameFormat_{makeFormat_(SwColor{220, 220, 170})};
    SwTextCharFormat transportFormat_{makeFormat_(SwColor{78, 201, 176}, Medium)};
    SwTextCharFormat statusFormat_{makeFormat_(SwColor{220, 220, 220}, Medium)};
    SwTextCharFormat endpointFormat_{makeFormat_(SwColor{106, 115, 125})};
    SwTextCharFormat noteFormat_{makeFormat_(SwColor{142, 142, 146})};
};

static SwScrollBar* verticalScrollBarFor_(SwCodeEditor* editor) {
    if (!editor) {
        return nullptr;
    }
    for (SwObject* childObject : editor->children()) {
        SwScrollBar* scrollBar = dynamic_cast<SwScrollBar*>(childObject);
        if (scrollBar && scrollBar->orientation() == SwScrollBar::Orientation::Vertical) {
            return scrollBar;
        }
    }
    return nullptr;
}

} // namespace

SocketTrafficInspectorWidget::SocketTrafficInspectorWidget(SwWidget* parent)
    : SwWidget(parent) {
    buildUi_();
    clearEntry();
}

void SocketTrafficInspectorWidget::showConsumer(const SocketTrafficInspectorData& data) {
    if (!editor_) {
        return;
    }

    const SwString report = reportTextFor_(data);
    if (report == lastReportText_) {
        return;
    }

    const bool preserveViewport = (lastConsumerId_ != 0 && lastConsumerId_ == data.consumer.consumerId);
    const int firstVisibleLine = preserveViewport ? editor_->firstVisibleLine() : 0;

    lastReportText_ = report;
    lastConsumerId_ = data.consumer.consumerId;
    editor_->setPlainText(report);
    if (preserveViewport) {
        restoreViewport_(firstVisibleLine);
    }
}

void SocketTrafficInspectorWidget::clearEntry() {
    if (!editor_) {
        return;
    }

    lastReportText_ =
        "NETWORK CONSUMER REPORT\n"
        "=======================\n"
        "\n"
        "OVERVIEW\n"
        "--------\n"
        "selection     : <none>\n"
        "status        : waiting for input\n"
        "\n"
        "NOTES\n"
        "-----\n"
        "- Select a consumer in the table to inspect socket activity.\n"
        "- This inspector uses the same sober read-only editor surface as example 41.\n";
    lastConsumerId_ = 0;
    editor_->setPlainText(lastReportText_);
}

SwSize SocketTrafficInspectorWidget::minimumSizeHint() const {
    return SwSize{360, 180};
}

void SocketTrafficInspectorWidget::buildUi_() {
    setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");

    editor_ = new SwCodeEditor(this);
    editor_->setReadOnly(true);
    editor_->setWordWrapEnabled(false);
    editor_->setLineNumbersVisible(false);
    editor_->setCodeFoldingEnabled(false);
    editor_->setAutoCompletionEnabled(false);
    editor_->setPlaceholderText("Select a consumer to inspect the traffic report.");
    editor_->setSyntaxHighlighter(new SocketTrafficReportHighlighter(editor_->document()));
    applyEditorTheme_();

    SwVerticalLayout* layout = new SwVerticalLayout();
    layout->setMargin(0);
    layout->setSpacing(0);
    layout->addWidget(editor_, 1, 240);
    setLayout(layout);
}

void SocketTrafficInspectorWidget::applyEditorTheme_() {
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

void SocketTrafficInspectorWidget::restoreViewport_(int firstVisibleLine) {
    if (!editor_) {
        return;
    }

    SwScrollBar* verticalScrollBar = verticalScrollBarFor_(editor_);
    if (!verticalScrollBar) {
        return;
    }

    const int clampedLine = std::max(verticalScrollBar->minimum(),
                                     std::min(firstVisibleLine, verticalScrollBar->maximum()));
    verticalScrollBar->setValue(clampedLine);
}

SwString SocketTrafficInspectorWidget::reportTextFor_(const SocketTrafficInspectorData& data) const {
    const SwSocketTrafficTelemetryConsumerSnapshot& consumer = data.consumer;
    const ClassDisplay_ consumerClass = classDisplay_(consumer.consumerClassName);

    auto appendKeyValue = [](SwString& out, const char* key, const SwString& value) {
        SwString keyText(key ? key : "");
        while (keyText.size() < 14) {
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
    appendLine(report, "NETWORK CONSUMER REPORT");
    appendLine(report, "=======================");
    appendLine(report, "");

    appendLine(report, "OVERVIEW");
    appendLine(report, "--------");
    appendKeyValue(report, "consumer", consumer.consumerLabel.isEmpty() ? SwString("<unnamed>") : consumer.consumerLabel);
    appendKeyValue(report, "class", consumerClass.shortName);
    if (!consumerClass.namespaceName.isEmpty()) {
        appendKeyValue(report, "namespace", consumerClass.namespaceName);
    }
    appendKeyValue(report, "sample", secondsSinceLaunchText_(data.sampleTimeNs, data.launchTimeNs) + " since launch");
    appendKeyValue(report, "state", consumer.stateLabel);
    appendKeyValue(report,
                   "sockets",
                   SwString::number(consumer.socketCount) + " (" + SwString::number(consumer.openSocketCount) + " open)");
    appendKeyValue(report, "last_active", activityAgeText_(data.sampleTimeNs, consumer.lastActivityNs));
    appendKeyValue(report, "share", SwString::number(consumer.sharePercentOfTotal, 'f', 2) + " %");
    appendLine(report, "");

    appendLine(report, "TRAFFIC");
    appendLine(report, "-------");
    appendKeyValue(report, "rx_now", humanRate_(consumer.rxRateBytesPerSecond));
    appendKeyValue(report, "tx_now", humanRate_(consumer.txRateBytesPerSecond));
    appendKeyValue(report, "total_load", humanRate_(consumer.totalRateBytesPerSecond));
    appendKeyValue(report, "rx_total", humanBytes_(consumer.rxBytesTotal));
    appendKeyValue(report, "tx_total", humanBytes_(consumer.txBytesTotal));
    appendLine(report, "");

    appendLine(report, "SOCKETS");
    appendLine(report, "-------");
    if (consumer.sockets.size() == 0) {
        appendLine(report, "No sockets currently attached to this consumer.");
    } else {
        for (size_t i = 0; i < consumer.sockets.size(); ++i) {
            const SwSocketTrafficSocketSnapshot& socket = consumer.sockets[i];
            const ClassDisplay_ socketClass = classDisplay_(socket.socketClassName);
            SwString index = SwString::number(static_cast<long long>(i + 1));
            if (index.size() < 2) {
                index = "0" + index;
            }

            appendLine(report, "[" + index + "] " + socketClass.shortName + " | " + socket.transportName + " | " + socket.stateLabel);
            appendKeyValue(report, "  endpoint", endpointText_(socket));
            appendKeyValue(report, "  activity", activityAgeText_(data.sampleTimeNs, socket.lastActivityNs));
            appendKeyValue(report, "  rx_now", humanRate_(socket.rxRateBytesPerSecond));
            appendKeyValue(report, "  tx_now", humanRate_(socket.txRateBytesPerSecond));
            appendKeyValue(report, "  rx_total", humanBytes_(socket.rxBytesTotal));
            appendKeyValue(report, "  tx_total", humanBytes_(socket.txBytesTotal));
            if (socket.transportKind == SwSocketTrafficTransportKind::Udp) {
                appendKeyValue(report,
                               "  datagrams",
                               SwString::number(socket.totalReceivedDatagrams) + " rx / " +
                                   SwString::number(socket.totalSentDatagrams) + " tx");
                appendKeyValue(report,
                               "  queue",
                               "pending " + SwString::number(socket.pendingDatagramCount) +
                                   " | drops " + SwString::number(socket.droppedDatagrams) +
                                   " | high-water " + SwString::number(socket.queueHighWatermark));
            }
            appendLine(report, "");
        }
    }

    appendLine(report, "NOTES");
    appendLine(report, "-----");
    appendLine(report, "- Rates are sampled from socket lifetime counters inside the application.");
    appendLine(report, "- Consumer ownership is resolved from the first non-socket SwObject parent.");
    appendLine(report, "- TLS traffic is reported as application bytes, not encrypted wire overhead.");

    return report;
}
