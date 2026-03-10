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

class SwSyntaxHighlighter : public SwObject {
    SW_OBJECT(SwSyntaxHighlighter, SwObject)

public:
    explicit SwSyntaxHighlighter(SwTextDocument* parent = nullptr)
        : SwObject(parent) {}

    virtual ~SwSyntaxHighlighter() = default;

    void setDocument(SwTextDocument* doc) {
        if (m_document == doc) {
            return;
        }
        if (m_document) {
            SwObject::disconnect(m_document, this);
        }
        m_document = doc;
        clearDeferredRehighlight_();
        if (m_document) {
            if (!parent()) {
                setParent(m_document);
            }
            SwObject::connect(m_document, &SwTextDocument::contentsChange, this, [this](int pos, int /*removed*/, int /*added*/) {
                if (!m_rehighlighting) {
                    int startBlock = -1;
                    if (m_document->lastEditBlockHint() >= 0) {
                        startBlock = m_document->lastEditBlockHint();
                        m_document->clearEditBlockHint();
                    } else {
                        startBlock = blockIndexForPosition_(pos);
                    }
                    if (!m_hasPendingRehighlight) {
                        m_pendingRehighlightStartBlock = startBlock;
                        m_hasPendingRehighlight = true;
                    } else {
                        m_pendingRehighlightStartBlock = std::min(m_pendingRehighlightStartBlock, startBlock);
                    }
                }
            });
            SwObject::connect(m_document, &SwTextDocument::contentsChanged, this, [this]() {
                if (!m_rehighlighting) {
                    const int startBlock = m_hasPendingRehighlight ? m_pendingRehighlightStartBlock : 0;
                    m_hasPendingRehighlight = false;
                    m_pendingRehighlightStartBlock = 0;
                    if (m_autoRehighlightSuspended) {
                        queueDeferredRehighlight_(startBlock);
                    } else {
                        rehighlightFrom(startBlock);
                    }
                }
            });
        }
        rehighlight();
    }

    SwTextDocument* document() const { return m_document; }

    void rehighlight() {
        if (!m_document || m_rehighlighting) {
            return;
        }
        clearDeferredRehighlight_();
        rehighlightRangeInternal_(0, m_document->blockCount() - 1, false);
    }

    void rehighlightBlock(int blockIndex) {
        if (!m_document || blockIndex < 0 || blockIndex >= m_document->blockCount()) {
            return;
        }

        SwTextBlock& block = m_document->blockAt(blockIndex);
        m_currentBlockIndex = blockIndex;
        m_pendingFormats.clear();
        m_pendingUserState = block.userState();
        m_pendingUserData = nullptr;
        m_hasPendingUserState = false;
        m_hasPendingUserData = false;

        highlightBlock(block.text());

        block.setAdditionalFormats(m_pendingFormats);
        if (m_hasPendingUserState) {
            block.setUserState(m_pendingUserState);
        }
        if (m_hasPendingUserData) {
            block.setUserData(m_pendingUserData);
            m_pendingUserData = nullptr;
        }

        m_currentBlockIndex = -1;
    }

    SwList<SwTextLayoutFormatRange> formatRanges(int blockIndex) const {
        if (!m_document || blockIndex < 0 || blockIndex >= m_document->blockCount()) {
            return SwList<SwTextLayoutFormatRange>();
        }
        return m_document->blockAt(blockIndex).additionalFormats();
    }

    void setAutoRehighlightSuspended(bool suspended) {
        m_autoRehighlightSuspended = suspended;
    }

    bool autoRehighlightSuspended() const {
        return m_autoRehighlightSuspended;
    }

    bool hasDeferredRehighlight() const {
        return m_hasDeferredRehighlight;
    }

    int deferredRehighlightStartBlock() const {
        return m_hasDeferredRehighlight ? m_deferredRehighlightStartBlock : -1;
    }

    bool processDeferredRehighlight(int maxBlocks) {
        if (!m_document || !m_hasDeferredRehighlight || maxBlocks <= 0) {
            return false;
        }
        const int count = m_document->blockCount();
        if (count <= 0) {
            clearDeferredRehighlight_();
            return false;
        }
        const int startBlock = std::max(0, m_deferredRehighlightStartBlock);
        const int endBlock = std::min(count - 1, startBlock + maxBlocks - 1);
        rehighlightRangeInternal_(startBlock, endBlock, false);
        if (endBlock >= count - 1) {
            clearDeferredRehighlight_();
            return false;
        }
        m_deferredRehighlightStartBlock = endBlock + 1;
        return true;
    }

    bool processDeferredRehighlightUpTo(int targetBlock) {
        if (!m_document || !m_hasDeferredRehighlight) {
            return false;
        }
        const int count = m_document->blockCount();
        if (count <= 0) {
            clearDeferredRehighlight_();
            return false;
        }
        const int startBlock = std::max(0, m_deferredRehighlightStartBlock);
        const int endBlock = std::min(count - 1, std::max(startBlock, targetBlock));
        rehighlightRangeInternal_(startBlock, endBlock, false);
        if (endBlock >= count - 1) {
            clearDeferredRehighlight_();
            return false;
        }
        m_deferredRehighlightStartBlock = endBlock + 1;
        return true;
    }

    void rehighlightWindow(int firstBlock, int lastBlock, int lookBehind = 256) {
        if (!m_document || m_rehighlighting) {
            return;
        }
        const int count = m_document->blockCount();
        if (count <= 0) {
            return;
        }
        const int clampedFirst = std::max(0, std::min(firstBlock, count - 1));
        const int clampedLast = std::max(clampedFirst, std::min(lastBlock, count - 1));

        int safeStart = clampedFirst;
        int steps = 0;
        while (safeStart > 0 &&
               m_document->blockAt(safeStart - 1).userState() < 0 &&
               steps < std::max(0, lookBehind)) {
            --safeStart;
            ++steps;
        }
        rehighlightRangeInternal_(safeStart, clampedLast, false);
    }

    void rehighlightFrom(int startBlock) {
        if (!m_document || m_rehighlighting) {
            return;
        }
        clearDeferredRehighlight_();
        rehighlightRangeInternal_(startBlock, m_document->blockCount() - 1, true);
    }

    DECLARE_SIGNAL_VOID(formattingChanged)

protected:
    virtual void highlightBlock(const SwString& text) = 0;

    void setFormat(int start, int count, const SwTextCharFormat& format) {
        if (start < 0 || count <= 0) {
            return;
        }
        SwTextLayoutFormatRange range;
        range.start = start;
        range.length = count;
        range.format = format;
        m_pendingFormats.append(range);
    }

    int previousBlockState() const {
        if (!m_document || m_currentBlockIndex <= 0) {
            return -1;
        }
        return m_document->blockAt(m_currentBlockIndex - 1).userState();
    }

    int currentBlockState() const {
        return m_pendingUserState;
    }

    void setCurrentBlockState(int newState) {
        m_pendingUserState = newState;
        m_hasPendingUserState = true;
    }

    void setCurrentBlockUserData(SwTextBlockUserData* data) {
        if (m_pendingUserData == data) {
            return;
        }
        delete m_pendingUserData;
        m_pendingUserData = data;
        m_hasPendingUserData = true;
    }

    SwTextBlockUserData* currentBlockUserData() const {
        if (!m_document || m_currentBlockIndex < 0 || m_currentBlockIndex >= m_document->blockCount()) {
            return nullptr;
        }
        return m_document->blockAt(m_currentBlockIndex).userData();
    }

private:
    int blockIndexForPosition_(int absPos) const {
        if (!m_document) {
            return 0;
        }
        int pos = 0;
        for (int i = 0; i < m_document->blockCount(); ++i) {
            const int next = pos + m_document->blockAt(i).length() + 1;
            if (absPos < next) {
                return i;
            }
            pos = next;
        }
        return std::max(0, m_document->blockCount() - 1);
    }

    void clearDeferredRehighlight_() {
        m_hasPendingRehighlight = false;
        m_pendingRehighlightStartBlock = 0;
        m_hasDeferredRehighlight = false;
        m_deferredRehighlightStartBlock = 0;
    }

    void queueDeferredRehighlight_(int startBlock) {
        const int clampedStart = std::max(0, startBlock);
        if (!m_hasDeferredRehighlight) {
            m_deferredRehighlightStartBlock = clampedStart;
            m_hasDeferredRehighlight = true;
            return;
        }
        m_deferredRehighlightStartBlock = std::min(m_deferredRehighlightStartBlock, clampedStart);
    }

    void rehighlightRangeInternal_(int startBlock, int endBlock, bool allowEarlyStop) {
        if (!m_document || m_rehighlighting) {
            return;
        }
        const int count = m_document->blockCount();
        if (count <= 0) {
            m_hasPendingRehighlight = false;
            m_pendingRehighlightStartBlock = 0;
            formattingChanged();
            return;
        }

        const int clampedStart = std::max(0, startBlock);
        const int clampedEnd = std::min(endBlock, count - 1);
        if (clampedStart > clampedEnd) {
            m_hasPendingRehighlight = false;
            m_pendingRehighlightStartBlock = 0;
            return;
        }

        m_rehighlighting = true;
        for (int i = clampedStart; i <= clampedEnd; ++i) {
            const int prevState = m_document->blockAt(i).userState();
            rehighlightBlock(i);
            if (allowEarlyStop &&
                i > clampedStart &&
                prevState >= 0 &&
                m_document->blockAt(i).userState() == prevState) {
                break;
            }
        }
        m_rehighlighting = false;
        m_hasPendingRehighlight = false;
        m_pendingRehighlightStartBlock = 0;
        formattingChanged();
    }

    SwTextDocument* m_document{nullptr};
    int m_currentBlockIndex{-1};
    SwList<SwTextLayoutFormatRange> m_pendingFormats;
    int m_pendingUserState{-1};
    SwTextBlockUserData* m_pendingUserData{nullptr};
    bool m_hasPendingUserState{false};
    bool m_hasPendingUserData{false};
    bool m_rehighlighting{false};
    int m_pendingRehighlightStartBlock{0};
    bool m_hasPendingRehighlight{false};
    bool m_autoRehighlightSuspended{false};
    int m_deferredRehighlightStartBlock{0};
    bool m_hasDeferredRehighlight{false};
};
