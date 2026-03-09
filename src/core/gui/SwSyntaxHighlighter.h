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
        if (m_document) {
            if (!parent()) {
                setParent(m_document);
            }
            SwObject::connect(m_document, &SwTextDocument::contentsChanged, this, [this]() {
                if (!m_rehighlighting) {
                    rehighlight();
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
        m_rehighlighting = true;
        for (int i = 0; i < m_document->blockCount(); ++i) {
            rehighlightBlock(i);
        }
        m_rehighlighting = false;
        formattingChanged();
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
    SwTextDocument* m_document{nullptr};
    int m_currentBlockIndex{-1};
    SwList<SwTextLayoutFormatRange> m_pendingFormats;
    int m_pendingUserState{-1};
    SwTextBlockUserData* m_pendingUserData{nullptr};
    bool m_hasPendingUserState{false};
    bool m_hasPendingUserData{false};
    bool m_rehighlighting{false};
};
