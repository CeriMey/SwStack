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
#include "../runtime/SwTimer.h"

#include <algorithm>

struct SwTextDiagnosticRange {
    int start{0};
    int length{0};

    int end() const {
        return start + std::max(0, length);
    }

    bool isValid() const {
        return start >= 0 && length > 0;
    }

    bool contains(int position) const {
        return position >= start && position < end();
    }

    bool operator==(const SwTextDiagnosticRange& other) const {
        return start == other.start && length == other.length;
    }

    bool operator!=(const SwTextDiagnosticRange& other) const {
        return !(*this == other);
    }
};

enum class SwTextDiagnosticSeverity {
    Hint,
    Information,
    Warning,
    Error
};

struct SwTextDiagnostic {
    SwTextDiagnosticRange range;
    SwTextDiagnosticSeverity severity{SwTextDiagnosticSeverity::Error};
    SwString message;
    SwTextCharFormat format;

    bool operator==(const SwTextDiagnostic& other) const {
        return range == other.range &&
               severity == other.severity &&
               message == other.message &&
               format == other.format;
    }

    bool operator!=(const SwTextDiagnostic& other) const {
        return !(*this == other);
    }
};

class SwTextDiagnosticsProvider : public SwObject {
    SW_OBJECT(SwTextDiagnosticsProvider, SwObject)

public:
    explicit SwTextDiagnosticsProvider(SwTextDocument* parent = nullptr)
        : SwObject(parent)
        , m_document(nullptr)
        , m_reanalysisTimer(new SwTimer(this)) {
        m_reanalysisTimer->setSingleShot(true);
        m_reanalysisTimer->setInterval(m_debounceIntervalMs);
        SwObject::connect(m_reanalysisTimer, &SwTimer::timeout, this, [this]() {
            reanalyzeNow_();
        });
    }

    virtual ~SwTextDiagnosticsProvider() = default;

    void setDocument(SwTextDocument* doc) {
        if (m_document == doc) {
            return;
        }
        if (m_document) {
            SwObject::disconnect(m_document, this);
        }
        m_document = doc;
        if (m_document) {
            if (!parent()) {
                setParent(m_document);
            }
            SwObject::connect(m_document, &SwTextDocument::contentsChanged, this, [this]() {
                scheduleReanalysis();
            });
        }
        reanalyze();
    }

    SwTextDocument* document() const {
        return m_document;
    }

    void setDebounceInterval(int ms) {
        m_debounceIntervalMs = std::max(0, ms);
        m_reanalysisTimer->setInterval(std::max(1, m_debounceIntervalMs));
    }

    int debounceInterval() const {
        return m_debounceIntervalMs;
    }

    void setMaxDiagnostics(int maxDiagnostics) {
        m_maxDiagnostics = std::max(1, maxDiagnostics);
        scheduleReanalysis();
    }

    int maxDiagnostics() const {
        return m_maxDiagnostics;
    }

    void scheduleReanalysis() {
        if (!m_document) {
            if (!m_diagnostics.isEmpty()) {
                m_diagnostics.clear();
                diagnosticsChanged();
            }
            return;
        }
        if (m_reanalyzing) {
            m_reanalysisPending = true;
            return;
        }
        if (m_debounceIntervalMs <= 0) {
            reanalyzeNow_();
            return;
        }
        m_reanalysisTimer->start(m_debounceIntervalMs);
    }

    void reanalyze() {
        if (m_reanalysisTimer->isActive()) {
            m_reanalysisTimer->stop();
        }
        m_reanalysisPending = false;
        reanalyzeNow_();
    }

    SwList<SwTextDiagnostic> diagnostics() const {
        return m_diagnostics;
    }

    SwList<SwTextDiagnostic> diagnosticsForBlock(int blockIndex) const {
        SwList<SwTextDiagnostic> blockDiagnostics;
        if (!m_document || blockIndex < 0 || blockIndex >= m_document->blockCount()) {
            return blockDiagnostics;
        }

        const int blockStart = m_document->absolutePosition(blockIndex, 0);
        const int blockLength = m_document->blockAt(blockIndex).length();
        const int blockEnd = blockStart + blockLength;

        for (int i = 0; i < m_diagnostics.size(); ++i) {
            const SwTextDiagnostic& diagnostic = m_diagnostics[i];
            const int diagnosticStart = diagnostic.range.start;
            const int diagnosticEnd = diagnostic.range.end();
            const int overlapStart = std::max(blockStart, diagnosticStart);
            const int overlapEnd = std::min(blockEnd, diagnosticEnd);
            if (overlapStart >= overlapEnd) {
                continue;
            }

            SwTextDiagnostic clipped = diagnostic;
            clipped.range.start = overlapStart;
            clipped.range.length = overlapEnd - overlapStart;
            blockDiagnostics.append(clipped);
        }
        return blockDiagnostics;
    }

    DECLARE_SIGNAL_VOID(diagnosticsChanged)

protected:
    virtual void analyzeDocument(const SwString& text, SwList<SwTextDiagnostic>& diagnostics) = 0;

private:
    void reanalyzeNow_() {
        if (!m_document || m_reanalyzing) {
            return;
        }

        m_reanalysisPending = false;
        m_reanalyzing = true;
        SwList<SwTextDiagnostic> updatedDiagnostics;
        analyzeDocument(m_document->toPlainText(), updatedDiagnostics);
        normalizeDiagnostics_(updatedDiagnostics);
        m_reanalyzing = false;

        if (updatedDiagnostics != m_diagnostics) {
            m_diagnostics = updatedDiagnostics;
            diagnosticsChanged();
        }

        if (m_reanalysisPending) {
            if (m_debounceIntervalMs <= 0) {
                reanalyzeNow_();
            } else {
                m_reanalysisTimer->start(m_debounceIntervalMs);
            }
        }
    }

    void normalizeDiagnostics_(SwList<SwTextDiagnostic>& diagnostics) const {
        if (!m_document) {
            diagnostics.clear();
            return;
        }

        const int characterCount = m_document->characterCount();
        SwList<SwTextDiagnostic> normalized;
        normalized.reserve(diagnostics.size());

        for (int i = 0; i < diagnostics.size(); ++i) {
            SwTextDiagnostic diagnostic = diagnostics[i];
            diagnostic.range.start = std::max(0, diagnostic.range.start);
            if (diagnostic.range.start >= characterCount) {
                continue;
            }

            const int maxLength = std::max(1, characterCount - diagnostic.range.start);
            diagnostic.range.length = std::max(1, diagnostic.range.length);
            diagnostic.range.length = std::min(diagnostic.range.length, maxLength);
            normalized.append(diagnostic);

            if (static_cast<int>(normalized.size()) >= m_maxDiagnostics) {
                break;
            }
        }

        diagnostics = normalized;
    }

    SwTextDocument* m_document;
    SwList<SwTextDiagnostic> m_diagnostics;
    SwTimer* m_reanalysisTimer{nullptr};
    int m_debounceIntervalMs{90};
    int m_maxDiagnostics{256};
    bool m_reanalyzing{false};
    bool m_reanalysisPending{false};
};
