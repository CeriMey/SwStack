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

#include <algorithm>

// ---------------------------------------------------------------------------
// SwTextCursor — navigation and editing API (Qt6 QTextCursor equivalent)
// ---------------------------------------------------------------------------

class SwTextCursor {
public:
    SwTextCursor() : m_document(nullptr), m_position(0), m_anchor(0) {}

    explicit SwTextCursor(SwTextDocument* doc)
        : m_document(doc), m_position(0), m_anchor(0) {}

    // --- Position ---
    int position() const  { return m_position; }
    int anchor() const    { return m_anchor; }

    void setPosition(int pos, bool keepAnchor = false) {
        m_position = clampPos(pos);
        if (!keepAnchor) m_anchor = m_position;
    }

    // --- Selection ---
    bool hasSelection() const { return m_position != m_anchor; }

    int selectionStart() const { return std::min(m_position, m_anchor); }
    int selectionEnd() const   { return std::max(m_position, m_anchor); }

    SwString selectedText() const {
        if (!m_document || !hasSelection()) return SwString();
        int start = selectionStart();
        int end = selectionEnd();
        SwString full = m_document->toPlainText();
        if (start >= static_cast<int>(full.size())) return SwString();
        return full.substr(start, std::min(end - start, static_cast<int>(full.size()) - start));
    }

    void select(int start, int end) {
        m_anchor = clampPos(start);
        m_position = clampPos(end);
    }

    void clearSelection() { m_anchor = m_position; }

    void selectAll() {
        if (!m_document) return;
        m_anchor = 0;
        m_position = m_document->characterCount();
    }

    // --- Movement ---
    enum MoveOperation {
        NoMove,
        Start,          // beginning of document
        End,            // end of document
        Left,           // one character left
        Right,          // one character right
        Up,             // one line up
        Down,           // one line down
        StartOfLine,
        EndOfLine,
        StartOfBlock,
        EndOfBlock,
        NextBlock,
        PreviousBlock,
        WordLeft,
        WordRight
    };

    enum MoveMode {
        MoveAnchor = 0,  // moves anchor with cursor
        KeepAnchor = 1   // keeps anchor = creates/extends selection
    };

    bool movePosition(MoveOperation op, MoveMode mode = MoveAnchor, int n = 1) {
        if (!m_document) return false;

        int newPos = m_position;
        int charCount = m_document->characterCount();

        for (int step = 0; step < n; ++step) {
            switch (op) {
            case Start:
                newPos = 0;
                break;
            case End:
                newPos = charCount;
                break;
            case Left:
                if (newPos > 0) --newPos;
                break;
            case Right:
                if (newPos < charCount) ++newPos;
                break;
            case StartOfBlock: {
                auto bp = m_document->blockPositionFromAbsolute(newPos);
                newPos = m_document->absolutePosition(bp.blockIndex, 0);
                break;
            }
            case EndOfBlock: {
                auto bp = m_document->blockPositionFromAbsolute(newPos);
                newPos = m_document->absolutePosition(bp.blockIndex, m_document->blockAt(bp.blockIndex).length());
                break;
            }
            case StartOfLine:
                // Same as StartOfBlock for now (no word-wrap awareness)
                return movePosition(StartOfBlock, mode, 1);
            case EndOfLine:
                return movePosition(EndOfBlock, mode, 1);
            case NextBlock: {
                auto bp = m_document->blockPositionFromAbsolute(newPos);
                if (bp.blockIndex + 1 < m_document->blockCount()) {
                    newPos = m_document->absolutePosition(bp.blockIndex + 1, 0);
                }
                break;
            }
            case PreviousBlock: {
                auto bp = m_document->blockPositionFromAbsolute(newPos);
                if (bp.blockIndex > 0) {
                    newPos = m_document->absolutePosition(bp.blockIndex - 1, 0);
                }
                break;
            }
            case Up: {
                auto bp = m_document->blockPositionFromAbsolute(newPos);
                if (bp.blockIndex > 0) {
                    int offset = std::min(bp.offset, m_document->blockAt(bp.blockIndex - 1).length());
                    newPos = m_document->absolutePosition(bp.blockIndex - 1, offset);
                }
                break;
            }
            case Down: {
                auto bp = m_document->blockPositionFromAbsolute(newPos);
                if (bp.blockIndex + 1 < m_document->blockCount()) {
                    int offset = std::min(bp.offset, m_document->blockAt(bp.blockIndex + 1).length());
                    newPos = m_document->absolutePosition(bp.blockIndex + 1, offset);
                }
                break;
            }
            case WordLeft: {
                SwString text = m_document->toPlainText();
                int p = newPos;
                // Skip spaces backwards
                while (p > 0 && (p - 1 < static_cast<int>(text.size())) && std::isspace(static_cast<unsigned char>(text[p - 1]))) --p;
                // Skip word chars backwards
                while (p > 0 && (p - 1 < static_cast<int>(text.size())) && !std::isspace(static_cast<unsigned char>(text[p - 1]))) --p;
                newPos = p;
                break;
            }
            case WordRight: {
                SwString text = m_document->toPlainText();
                int p = newPos;
                int sz = static_cast<int>(text.size());
                // Skip word chars forward
                while (p < sz && !std::isspace(static_cast<unsigned char>(text[p]))) ++p;
                // Skip spaces forward
                while (p < sz && std::isspace(static_cast<unsigned char>(text[p]))) ++p;
                newPos = p;
                break;
            }
            case NoMove:
                break;
            }
        }

        m_position = clampPos(newPos);
        if (mode == MoveAnchor) {
            m_anchor = m_position;
        }
        return true;
    }

    // --- Insertion ---
    void insertText(const SwString& text) {
        insertText(text, m_charFormat);
    }

    void insertText(const SwString& text, const SwTextCharFormat& fmt) {
        if (!m_document) return;
        removeSelectedText();

        // Split text on newlines — each \n means insert a new block
        size_t start = 0;
        for (size_t i = 0; i <= text.size(); ++i) {
            if (i == text.size() || text[i] == '\n') {
                if (i > start) {
                    SwString segment = text.substr(start, i - start);
                    m_document->insertText(m_position, segment, fmt);
                    m_position += static_cast<int>(segment.size());
                }
                if (i < text.size() && text[i] == '\n') {
                    insertBlock();
                }
                start = i + 1;
            }
        }
        m_anchor = m_position;
    }

    void insertBlock() {
        insertBlock(m_blockFormat, m_charFormat);
    }

    void insertBlock(const SwTextBlockFormat& blockFmt, const SwTextCharFormat& charFmt = SwTextCharFormat()) {
        if (!m_document) return;
        removeSelectedText();
        m_document->insertBlock(m_position, blockFmt, charFmt);
        ++m_position; // move past block separator
        m_anchor = m_position;
    }

    void insertHtml(const SwString& html) {
        if (!m_document) return;
        removeSelectedText();
        // Create a temporary document, parse HTML, then insert blocks/fragments
        SwTextDocument temp;
        temp.setHtml(html);
        // Insert content from temp into this document at current position
        for (int i = 0; i < temp.blockCount(); ++i) {
            if (i > 0) insertBlock();
            const SwTextBlock& block = temp.blockAt(i);
            for (int f = 0; f < block.fragmentCount(); ++f) {
                const SwTextFragment& frag = block.fragmentAt(f);
                m_document->insertText(m_position, frag.text(), frag.charFormat());
                m_position += frag.length();
            }
        }
        m_anchor = m_position;
    }

    // --- Tables ---
    SwTextTable* insertTable(int rows, int cols, const SwTextTableFormat& fmt = SwTextTableFormat()) {
        if (!m_document) return nullptr;
        removeSelectedText();
        return m_document->insertTable(m_position, rows, cols, fmt);
    }

    // --- Lists ---
    SwTextList* insertList(const SwTextListFormat& fmt) {
        if (!m_document) return nullptr;
        removeSelectedText();
        insertBlock();
        auto bp = m_document->blockPositionFromAbsolute(m_position);
        SwTextList* list = m_document->createList(fmt);
        m_document->blockAt(bp.blockIndex).setList(list);
        list->addBlockIndex(bp.blockIndex);
        SwTextBlockFormat bf;
        bf.setIndent(fmt.indent());
        m_document->setBlockFormat(bp.blockIndex, bf);
        return list;
    }

    SwTextList* createList(const SwTextListFormat& fmt) {
        return insertList(fmt);
    }

    // Add a new item to the current list (inserts a new block attached to the list)
    void insertListItem(SwTextList* list) {
        if (!m_document || !list) return;
        insertBlock();
        auto bp = m_document->blockPositionFromAbsolute(m_position);
        if (bp.blockIndex >= 0 && bp.blockIndex < m_document->blockCount()) {
            m_document->blockAt(bp.blockIndex).setList(list);
            list->addBlockIndex(bp.blockIndex);
        }
    }

    // --- Images ---
    void insertImage(const SwTextImageFormat& imgFmt) {
        if (!m_document) return;
        removeSelectedText();
        // Store image as resource and insert placeholder
        if (imgFmt.hasImage()) {
            m_document->addResource(imgFmt.name(), imgFmt.image());
        }
        SwTextCharFormat fmt = m_charFormat;
        // Use a special property to mark as image
        fmt.setProperty(10000, SwAny()); // sentinel for image
        SwString placeholder = imgFmt.name().isEmpty() ? SwString("[image]") : imgFmt.name();
        m_document->insertText(m_position, placeholder, fmt);
        m_position += static_cast<int>(placeholder.size());
        m_anchor = m_position;
    }

    // --- Deletion ---
    void removeSelectedText() {
        if (!m_document || !hasSelection()) return;
        int start = selectionStart();
        int count = selectionEnd() - start;
        m_document->removeText(start, count);
        m_position = start;
        m_anchor = start;
    }

    void deleteChar() {
        if (!m_document) return;
        if (hasSelection()) {
            removeSelectedText();
        } else if (m_position < m_document->characterCount()) {
            m_document->removeText(m_position, 1);
        }
    }

    void deletePreviousChar() {
        if (!m_document) return;
        if (hasSelection()) {
            removeSelectedText();
        } else if (m_position > 0) {
            --m_position;
            m_document->removeText(m_position, 1);
            m_anchor = m_position;
        }
    }

    // --- Formatting ---
    void setCharFormat(const SwTextCharFormat& fmt) {
        m_charFormat = fmt;
        if (hasSelection()) {
            m_document->setCharFormat(selectionStart(), selectionEnd() - selectionStart(), fmt);
        }
    }

    void mergeCharFormat(const SwTextCharFormat& fmt) {
        m_charFormat.merge(fmt);
        if (hasSelection()) {
            m_document->mergeCharFormat(selectionStart(), selectionEnd() - selectionStart(), fmt);
        }
    }

    SwTextCharFormat charFormat() const {
        if (m_document && !hasSelection()) {
            auto bp = m_document->blockPositionFromAbsolute(m_position);
            if (bp.blockIndex >= 0 && bp.blockIndex < m_document->blockCount()) {
                return m_document->blockAt(bp.blockIndex).charFormatAt(bp.offset);
            }
        }
        return m_charFormat;
    }

    void setBlockFormat(const SwTextBlockFormat& fmt) {
        m_blockFormat = fmt;
        if (m_document) {
            auto bp = m_document->blockPositionFromAbsolute(m_position);
            m_document->setBlockFormat(bp.blockIndex, fmt);
        }
    }

    SwTextBlockFormat blockFormat() const {
        if (m_document) {
            auto bp = m_document->blockPositionFromAbsolute(m_position);
            if (bp.blockIndex >= 0 && bp.blockIndex < m_document->blockCount()) {
                return m_document->blockAt(bp.blockIndex).blockFormat();
            }
        }
        return m_blockFormat;
    }

    // --- Current block info ---
    SwTextBlock currentBlock() const {
        if (!m_document) return SwTextBlock();
        auto bp = m_document->blockPositionFromAbsolute(m_position);
        return m_document->blockAt(bp.blockIndex);
    }

    int blockNumber() const {
        if (!m_document) return -1;
        auto bp = m_document->blockPositionFromAbsolute(m_position);
        return bp.blockIndex;
    }

    int positionInBlock() const {
        if (!m_document) return 0;
        auto bp = m_document->blockPositionFromAbsolute(m_position);
        return bp.offset;
    }

    // --- Document ---
    SwTextDocument* document() const { return m_document; }

    // --- Comparison ---
    bool operator==(const SwTextCursor& o) const { return m_document == o.m_document && m_position == o.m_position; }
    bool operator!=(const SwTextCursor& o) const { return !(*this == o); }
    bool operator<(const SwTextCursor& o) const  { return m_position < o.m_position; }

    bool atStart() const { return m_position == 0; }
    bool atEnd() const   { return m_document && m_position >= m_document->characterCount(); }
    bool isNull() const  { return m_document == nullptr; }

private:
    SwTextDocument* m_document;
    int m_position;
    int m_anchor;
    SwTextCharFormat m_charFormat;
    SwTextBlockFormat m_blockFormat;

    int clampPos(int pos) const {
        if (!m_document) return 0;
        return std::max(0, std::min(pos, m_document->characterCount()));
    }
};
