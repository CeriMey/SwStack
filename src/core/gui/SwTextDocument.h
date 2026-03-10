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

#include "SwTextFormat.h"
#include "SwTextBlockUserData.h"
#include "../object/SwObject.h"
#include "../types/SwList.h"
#include "../types/SwMap.h"
#include "../types/SwString.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

class SwTextDocument;
class SwTextCursor;
class SwTextDocumentLayout;

namespace swTextDocumentDetail {
inline int sizeToInt(size_t value) {
    return value <= static_cast<size_t>(std::numeric_limits<int>::max())
        ? static_cast<int>(value)
        : std::numeric_limits<int>::max();
}

inline bool parseCssRgbTriplet(const std::string& text, int& r, int& g, int& b) {
    std::istringstream stream(text);
    char firstComma = '\0';
    char secondComma = '\0';
    stream >> std::ws >> r >> std::ws >> firstComma
           >> std::ws >> g >> std::ws >> secondComma
           >> std::ws >> b >> std::ws;
    return stream && firstComma == ',' && secondComma == ',' && stream.eof();
}
}

struct SwTextLayoutFormatRange {
    int start{0};
    int length{0};
    SwTextCharFormat format;

    bool operator==(const SwTextLayoutFormatRange& other) const {
        return start == other.start &&
               length == other.length &&
               format == other.format;
    }

    bool operator!=(const SwTextLayoutFormatRange& other) const {
        return !(*this == other);
    }
};

// ---------------------------------------------------------------------------
// SwTextFragment — a run of text with uniform character format
// ---------------------------------------------------------------------------

class SwTextFragment {
public:
    SwTextFragment() = default;
    SwTextFragment(const SwString& text, const SwTextCharFormat& fmt)
        : m_text(text), m_format(fmt) {}

    SwString text() const             { return m_text; }
    void setText(const SwString& t)   { m_text = t; }

    SwTextCharFormat charFormat() const              { return m_format; }
    void setCharFormat(const SwTextCharFormat& fmt)  { m_format = fmt; }

    int length() const { return static_cast<int>(m_text.size()); }
    bool isEmpty() const { return m_text.isEmpty(); }

private:
    SwString m_text;
    SwTextCharFormat m_format;
};

// ---------------------------------------------------------------------------
// SwTextBlock — a paragraph (list of fragments + block format)
// ---------------------------------------------------------------------------

class SwTextList;

class SwTextBlock {
public:
    SwTextBlock() = default;

    // --- Text content ---
    SwString text() const {
        if (m_textCacheDirty) {
            m_textCache.clear();
            for (int i = 0; i < m_fragments.size(); ++i) {
                m_textCache.append(m_fragments[i].text());
            }
            m_textCacheDirty = false;
        }
        return m_textCache;
    }

    int length() const {
        int len = 0;
        for (int i = 0; i < m_fragments.size(); ++i) {
            len += m_fragments[i].length();
        }
        return len;
    }

    // --- Fragments ---
    int fragmentCount() const { return swTextDocumentDetail::sizeToInt(m_fragments.size()); }
    const SwTextFragment& fragmentAt(int index) const { return m_fragments[index]; }
    SwTextFragment& fragmentAt(int index) { return m_fragments[index]; }

    void appendFragment(const SwTextFragment& frag) { m_fragments.append(frag); m_textCacheDirty = true; }
    void insertFragment(int index, const SwTextFragment& frag) {
        m_fragments.insert(index, frag);
        m_textCacheDirty = true;
    }
    void removeFragment(int index) { m_fragments.removeAt(index); m_textCacheDirty = true; }
    void clearFragments() { m_fragments.clear(); m_textCacheDirty = true; }

    // --- Block format ---
    SwTextBlockFormat blockFormat() const                    { return m_blockFormat; }
    void setBlockFormat(const SwTextBlockFormat& fmt)        { m_blockFormat = fmt; }

    // --- Character format at a position within the block ---
    SwTextCharFormat charFormatAt(int pos) const {
        int cursor = 0;
        for (int i = 0; i < m_fragments.size(); ++i) {
            int len = m_fragments[i].length();
            if (pos < cursor + len) {
                return m_fragments[i].charFormat();
            }
            cursor += len;
        }
        if (!m_fragments.isEmpty()) {
            return m_fragments[m_fragments.size() - 1].charFormat();
        }
        return SwTextCharFormat();
    }

    // --- List association ---
    SwTextList* list() const       { return m_list; }
    void setList(SwTextList* list) { m_list = list; }

    // --- Block number (set by document) ---
    int blockNumber() const        { return m_blockNumber; }
    void setBlockNumber(int n)     { m_blockNumber = n; }

    bool isValid() const { return m_blockNumber >= 0; }

    int userState() const { return m_userState; }
    void setUserState(int state) { m_userState = state; }

    SwTextBlockUserData* userData() const { return m_userData.get(); }
    void setUserData(SwTextBlockUserData* data) { m_userData.reset(data); }
    void clearUserData() { m_userData.reset(); }

    const SwList<SwTextLayoutFormatRange>& additionalFormats() const { return m_additionalFormats; }
    void setAdditionalFormats(const SwList<SwTextLayoutFormatRange>& formats) { m_additionalFormats = formats; }
    void clearAdditionalFormats() { m_additionalFormats.clear(); }

    // --- Insert text at position within block ---
    void insertText(int pos, const SwString& text, const SwTextCharFormat& fmt) {
        m_textCacheDirty = true;
        if (m_fragments.isEmpty()) {
            m_fragments.append(SwTextFragment(text, fmt));
            return;
        }
        int cursor = 0;
        for (int i = 0; i < m_fragments.size(); ++i) {
            int len = m_fragments[i].length();
            if (pos <= cursor + len) {
                int offset = pos - cursor;
                if (m_fragments[i].charFormat() == fmt) {
                    SwString t = m_fragments[i].text();
                    t.insert(offset, text);
                    m_fragments[i].setText(t);
                } else if (offset == 0) {
                    m_fragments.insert(i, SwTextFragment(text, fmt));
                } else if (offset >= len) {
                    m_fragments.insert(i + 1, SwTextFragment(text, fmt));
                } else {
                    SwString before = m_fragments[i].text().substr(0, offset);
                    SwString after = m_fragments[i].text().substr(offset);
                    SwTextCharFormat origFmt = m_fragments[i].charFormat();
                    m_fragments[i].setText(before);
                    m_fragments.insert(i + 1, SwTextFragment(text, fmt));
                    m_fragments.insert(i + 2, SwTextFragment(after, origFmt));
                }
                normalize();
                return;
            }
            cursor += len;
        }
        // Append at end
        if (!m_fragments.isEmpty() && m_fragments[m_fragments.size() - 1].charFormat() == fmt) {
            SwString t = m_fragments[m_fragments.size() - 1].text();
            t.append(text);
            m_fragments[m_fragments.size() - 1].setText(t);
        } else {
            m_fragments.append(SwTextFragment(text, fmt));
        }
        normalize();
    }

    // --- Remove text range within block ---
    void removeText(int pos, int count) {
        if (count <= 0) return;
        m_textCacheDirty = true;
        int remaining = count;
        int cursor = 0;
        for (int i = 0; i < m_fragments.size() && remaining > 0;) {
            int len = m_fragments[i].length();
            if (pos >= cursor + len) {
                cursor += len;
                ++i;
                continue;
            }
            int offset = pos - cursor;
            int toRemove = std::min(remaining, len - offset);
            SwString t = m_fragments[i].text();
            t.erase(offset, toRemove);
            m_fragments[i].setText(t);
            remaining -= toRemove;
            if (m_fragments[i].isEmpty()) {
                m_fragments.removeAt(i);
            } else {
                cursor += m_fragments[i].length();
                ++i;
            }
        }
        normalize();
    }

    // --- Set char format on a range ---
    void setCharFormat(int pos, int count, const SwTextCharFormat& fmt) {
        if (count <= 0) return;
        // Split fragments at pos and pos+count boundaries, then apply format
        splitAt(pos);
        splitAt(pos + count);
        int cursor = 0;
        for (int i = 0; i < m_fragments.size(); ++i) {
            int len = m_fragments[i].length();
            if (cursor >= pos && cursor + len <= pos + count) {
                m_fragments[i].setCharFormat(fmt);
            }
            cursor += len;
        }
        normalize();
    }

    void mergeCharFormat(int pos, int count, const SwTextCharFormat& fmt) {
        if (count <= 0) return;
        splitAt(pos);
        splitAt(pos + count);
        int cursor = 0;
        for (int i = 0; i < m_fragments.size(); ++i) {
            int len = m_fragments[i].length();
            if (cursor >= pos && cursor + len <= pos + count) {
                SwTextCharFormat merged = m_fragments[i].charFormat();
                merged.merge(fmt);
                m_fragments[i].setCharFormat(merged);
            }
            cursor += len;
        }
        normalize();
    }

private:
    SwList<SwTextFragment> m_fragments;
    SwTextBlockFormat m_blockFormat;
    SwTextList* m_list{nullptr};
    int m_blockNumber{-1};
    int m_userState{-1};
    std::shared_ptr<SwTextBlockUserData> m_userData;
    SwList<SwTextLayoutFormatRange> m_additionalFormats;
    mutable SwString m_textCache;
    mutable bool m_textCacheDirty{true};

    void splitAt(int pos) {
        int cursor = 0;
        for (int i = 0; i < m_fragments.size(); ++i) {
            int len = m_fragments[i].length();
            if (pos > cursor && pos < cursor + len) {
                int offset = pos - cursor;
                SwString before = m_fragments[i].text().substr(0, offset);
                SwString after = m_fragments[i].text().substr(offset);
                SwTextCharFormat fmt = m_fragments[i].charFormat();
                m_fragments[i].setText(before);
                m_fragments.insert(i + 1, SwTextFragment(after, fmt));
                return;
            }
            cursor += len;
        }
    }

    void normalize() {
        for (size_t i = m_fragments.size(); i > 0; --i) {
            const size_t index = i - 1;
            if (m_fragments[index].isEmpty()) {
                m_fragments.removeAt(index);
            }
        }
        for (size_t i = m_fragments.size(); i > 1; --i) {
            const size_t index = i - 1;
            if (m_fragments[index].charFormat() == m_fragments[index - 1].charFormat()) {
                SwString merged = m_fragments[index - 1].text();
                merged.append(m_fragments[index].text());
                m_fragments[index - 1].setText(merged);
                m_fragments.removeAt(index);
            }
        }
    }
};

// ---------------------------------------------------------------------------
// SwTextList — groups blocks that form a list
// ---------------------------------------------------------------------------

class SwTextList {
public:
    SwTextList() = default;
    explicit SwTextList(const SwTextListFormat& fmt) : m_format(fmt) {}

    SwTextListFormat format() const               { return m_format; }
    void setFormat(const SwTextListFormat& fmt)    { m_format = fmt; }

    int count() const { return swTextDocumentDetail::sizeToInt(m_blockIndices.size()); }
    void addBlockIndex(int idx) { m_blockIndices.append(idx); }
    int blockIndex(int i) const { return m_blockIndices[i]; }
    void removeBlockIndex(int idx) {
        for (int i = 0; i < m_blockIndices.size(); ++i) {
            if (m_blockIndices[i] == idx) {
                m_blockIndices.removeAt(i);
                return;
            }
        }
    }

    int itemNumber(int blockIndex) const {
        for (int i = 0; i < m_blockIndices.size(); ++i) {
            if (m_blockIndices[i] == blockIndex) return i;
        }
        return -1;
    }

private:
    SwTextListFormat m_format;
    SwList<int> m_blockIndices;
};

// ---------------------------------------------------------------------------
// SwTextTable — table within the document
// ---------------------------------------------------------------------------

class SwTextTable {
public:
    SwTextTable() = default;
    SwTextTable(int rows, int cols) { resize(rows, cols); }

    void resize(int rows, int cols) {
        m_rows = rows;
        m_cols = cols;
        m_cells.resize(rows * cols);
        m_cellFormats.resize(rows * cols);
    }

    int rows() const    { return m_rows; }
    int columns() const { return m_cols; }

    SwTextTableFormat tableFormat() const                 { return m_format; }
    void setTableFormat(const SwTextTableFormat& fmt)     { m_format = fmt; }

    // --- Cell access ---
    struct Cell {
        SwList<SwTextBlock> blocks;
    };

    Cell& cellAt(int row, int col) {
        return m_cells[row * m_cols + col];
    }
    const Cell& cellAt(int row, int col) const {
        return m_cells[row * m_cols + col];
    }

    SwTextTableCellFormat cellFormat(int row, int col) const {
        return m_cellFormats[row * m_cols + col];
    }
    void setCellFormat(int row, int col, const SwTextTableCellFormat& fmt) {
        m_cellFormats[row * m_cols + col] = fmt;
    }

    // --- Row/column manipulation ---
    void insertRow(int before) {
        std::vector<Cell> newRow(m_cols);
        std::vector<SwTextTableCellFormat> newFmts(m_cols);
        m_cells.insert(m_cells.begin() + before * m_cols, newRow.begin(), newRow.end());
        m_cellFormats.insert(m_cellFormats.begin() + before * m_cols, newFmts.begin(), newFmts.end());
        ++m_rows;
    }

    void insertColumn(int before) {
        for (int r = m_rows - 1; r >= 0; --r) {
            m_cells.insert(m_cells.begin() + r * m_cols + before, Cell());
            m_cellFormats.insert(m_cellFormats.begin() + r * m_cols + before, SwTextTableCellFormat());
        }
        ++m_cols;
    }

    void removeRow(int index) {
        if (index < 0 || index >= m_rows) return;
        m_cells.erase(m_cells.begin() + index * m_cols, m_cells.begin() + (index + 1) * m_cols);
        m_cellFormats.erase(m_cellFormats.begin() + index * m_cols, m_cellFormats.begin() + (index + 1) * m_cols);
        --m_rows;
    }

    void removeColumn(int index) {
        if (index < 0 || index >= m_cols) return;
        for (int r = m_rows - 1; r >= 0; --r) {
            m_cells.erase(m_cells.begin() + r * m_cols + index);
            m_cellFormats.erase(m_cellFormats.begin() + r * m_cols + index);
        }
        --m_cols;
    }

    // Position in document (block index where this table starts)
    int startBlock() const     { return m_startBlock; }
    void setStartBlock(int b)  { m_startBlock = b; }

private:
    int m_rows{0};
    int m_cols{0};
    SwTextTableFormat m_format;
    std::vector<Cell> m_cells;
    std::vector<SwTextTableCellFormat> m_cellFormats;
    int m_startBlock{-1};
};

// ---------------------------------------------------------------------------
// SwTextDocument — the top-level document model (Qt6 QTextDocument equivalent)
// ---------------------------------------------------------------------------

class SwTextDocument : public SwObject {
    SW_OBJECT(SwTextDocument, SwObject)
public:
    explicit SwTextDocument(SwObject* parent = nullptr)
        : SwObject(parent) {
        // Document always has at least one empty block
        SwTextBlock block;
        block.setBlockNumber(0);
        m_blocks.append(block);
    }

    ~SwTextDocument() {
        for (int i = 0; i < m_lists.size(); ++i) {
            delete m_lists[i];
        }
        for (int i = 0; i < m_tables.size(); ++i) {
            delete m_tables[i];
        }
    }

    // --- Block access ---
    int blockCount() const { return swTextDocumentDetail::sizeToInt(m_blocks.size()); }

    SwTextBlock& blockAt(int index) { return m_blocks[index]; }
    const SwTextBlock& blockAt(int index) const { return m_blocks[index]; }

    SwTextBlock& firstBlock() { return m_blocks[0]; }
    const SwTextBlock& firstBlock() const { return m_blocks[0]; }

    SwTextBlock& lastBlock() { return m_blocks[m_blocks.size() - 1]; }
    const SwTextBlock& lastBlock() const { return m_blocks[m_blocks.size() - 1]; }

    // --- Total character count (including block separators) ---
    int characterCount() const {
        int count = 0;
        for (int i = 0; i < m_blocks.size(); ++i) {
            count += m_blocks[i].length();
            if (i < m_blocks.size() - 1) ++count; // block separator
        }
        return count;
    }

    int revision() const { return m_revision; }
    int lastEditBlockHint() const { return m_lastEditBlockHint; }
    void clearEditBlockHint() { m_lastEditBlockHint = -1; }

    void beginBatchEdit() {
        m_batchEditing = true;
        m_batchOldBlockCount = blockCount();
        m_batchContentsChangePos = 0;
        m_batchContentsChangeRemoved = 0;
        m_batchContentsChangeAdded = 0;
        m_batchHasChanges = false;
        m_lastEditBlockHint = -1;
    }

    void endBatchEdit() {
        if (!m_batchEditing) return;
        const int oldBlockCount = m_batchOldBlockCount;
        m_batchEditing = false;
        renumberBlocks();
        if (blockCount() != oldBlockCount) {
            emit blockCountChanged();
        }
        if (m_batchHasChanges) {
            emit contentsChange(m_batchContentsChangePos,
                                m_batchContentsChangeRemoved,
                                m_batchContentsChangeAdded);
            emit contentsChanged();
        }
    }

    // --- Position mapping: absolute position <-> (block, offset) ---
    struct BlockPosition {
        int blockIndex{-1};
        int offset{0};
    };

    BlockPosition blockPositionFromAbsolute(int absPos) const {
        int cursor = 0;
        for (int i = 0; i < m_blocks.size(); ++i) {
            int blockLen = m_blocks[i].length();
            if (absPos <= cursor + blockLen) {
                return {i, absPos - cursor};
            }
            cursor += blockLen + 1; // +1 for block separator
        }
        return {static_cast<int>(m_blocks.size()) - 1, m_blocks[m_blocks.size() - 1].length()};
    }

    int absolutePosition(int blockIndex, int offset) const {
        int pos = 0;
        for (int i = 0; i < blockIndex && i < m_blocks.size(); ++i) {
            pos += m_blocks[i].length() + 1; // +1 for separator
        }
        return pos + offset;
    }

    // --- Plain text ---
    SwString toPlainText() const {
        SwString result;
        for (int i = 0; i < m_blocks.size(); ++i) {
            if (i > 0) result.append('\n');
            result.append(m_blocks[i].text());
        }
        return result;
    }

    void setPlainText(const SwString& text) {
        const int oldCharCount = characterCount();
        const int oldBlockCount = blockCount();
        m_blocks.clear();
        for (int i = 0; i < m_lists.size(); ++i) delete m_lists[i];
        m_lists.clear();
        for (int i = 0; i < m_tables.size(); ++i) delete m_tables[i];
        m_tables.clear();
        m_imageResources.clear();
        // Split on newlines
        SwList<SwString> lines;
        size_t start = 0;
        for (size_t i = 0; i <= text.size(); ++i) {
            if (i == text.size() || text[i] == '\n') {
                lines.append(text.substr(start, i - start));
                start = i + 1;
            }
        }
        m_blocks.clear();
        for (int i = 0; i < lines.size(); ++i) {
            SwTextBlock block;
            block.setBlockNumber(i);
            if (!lines[i].isEmpty()) {
                block.appendFragment(SwTextFragment(lines[i], SwTextCharFormat()));
            }
            m_blocks.append(block);
        }
        if (m_blocks.isEmpty()) {
            SwTextBlock block;
            block.setBlockNumber(0);
            m_blocks.append(block);
        }
        ++m_revision;
        if (blockCount() != oldBlockCount) {
            emit blockCountChanged();
        }
        emit contentsChange(0, oldCharCount, characterCount());
        emit contentsChanged();
    }

    // --- HTML import/export ---
    void setHtml(const SwString& html);
    SwString toHtml() const;

    // --- Modification API (used by SwTextCursor) ---
    void insertBlock(int absPos, const SwTextBlockFormat& blockFmt, const SwTextCharFormat& charFmt) {
        const int oldBlockCount = blockCount();
        BlockPosition bp = blockPositionFromAbsolute(absPos);
        SwTextBlock& current = m_blocks[bp.blockIndex];

        // Split the current block at bp.offset
        SwTextBlock newBlock;
        newBlock.setBlockFormat(blockFmt);

        // Move fragments after the split point to the new block
        int cursor = 0;
        for (int i = 0; i < current.fragmentCount(); ++i) {
            int fragLen = current.fragmentAt(i).length();
            if (bp.offset <= cursor) {
                // This entire fragment goes to new block
                newBlock.appendFragment(current.fragmentAt(i));
            } else if (bp.offset < cursor + fragLen) {
                // Split this fragment
                int offset = bp.offset - cursor;
                SwString after = current.fragmentAt(i).text().substr(offset);
                if (!after.isEmpty()) {
                    newBlock.appendFragment(SwTextFragment(after, current.fragmentAt(i).charFormat()));
                }
                current.fragmentAt(i).setText(current.fragmentAt(i).text().substr(0, offset));
            }
            cursor += fragLen;
        }

        // Remove moved fragments from current block
        while (current.fragmentCount() > 0) {
            int lastLen = 0;
            int total = 0;
            for (int i = 0; i < current.fragmentCount(); ++i) {
                total += current.fragmentAt(i).length();
            }
            if (total <= bp.offset) break;
            // Remove last fragment if it was moved
            int c = 0;
            bool removed = false;
            for (int i = current.fragmentCount() - 1; i >= 0; --i) {
                c = 0;
                for (int j = 0; j <= i; ++j) c += current.fragmentAt(j).length();
                if (c > bp.offset) {
                    // Check if this fragment was fully moved
                    int fragStart = c - current.fragmentAt(i).length();
                    if (fragStart >= bp.offset) {
                        current.removeFragment(i);
                        removed = true;
                    } else {
                        break;
                    }
                }
            }
            if (!removed) break;
        }

        // Insert the new block after current
        int insertIdx = bp.blockIndex + 1;
        m_blocks.insert(insertIdx, newBlock);
        m_modified = true;
        ++m_revision;
        if (m_batchEditing) {
            recordBatchChange_(absPos, 0, 1, bp.blockIndex);
            return;
        }
        renumberBlocks();
        if (blockCount() != oldBlockCount) {
            emit blockCountChanged();
        }
        emit contentsChange(absPos, 0, 1);
        emit contentsChanged();
    }

    void insertBlockDirect(int blockIndex,
                           int offset,
                           const SwTextBlockFormat& blockFmt,
                           const SwTextCharFormat& charFmt) {
        if (blockIndex < 0 || blockIndex >= m_blocks.size()) return;
        const int oldBlockCount = blockCount();
        SwTextBlock& current = m_blocks[blockIndex];

        SwTextBlock newBlock;
        newBlock.setBlockFormat(blockFmt);

        int cursor = 0;
        for (int i = 0; i < current.fragmentCount(); ++i) {
            int fragLen = current.fragmentAt(i).length();
            if (offset <= cursor) {
                newBlock.appendFragment(current.fragmentAt(i));
            } else if (offset < cursor + fragLen) {
                int splitOffset = offset - cursor;
                SwString after = current.fragmentAt(i).text().substr(splitOffset);
                if (!after.isEmpty()) {
                    newBlock.appendFragment(SwTextFragment(after, current.fragmentAt(i).charFormat()));
                }
                current.fragmentAt(i).setText(current.fragmentAt(i).text().substr(0, splitOffset));
            }
            cursor += fragLen;
        }

        while (current.fragmentCount() > 0) {
            int total = 0;
            for (int i = 0; i < current.fragmentCount(); ++i) {
                total += current.fragmentAt(i).length();
            }
            if (total <= offset) break;
            bool removed = false;
            for (int i = current.fragmentCount() - 1; i >= 0; --i) {
                int c = 0;
                for (int j = 0; j <= i; ++j) c += current.fragmentAt(j).length();
                if (c > offset) {
                    const int fragStart = c - current.fragmentAt(i).length();
                    if (fragStart >= offset) {
                        current.removeFragment(i);
                        removed = true;
                    } else {
                        break;
                    }
                }
            }
            if (!removed) break;
        }

        const int insertIdx = blockIndex + 1;
        m_blocks.insert(insertIdx, newBlock);
        m_modified = true;
        ++m_revision;

        const int absPos = absolutePosition(blockIndex, offset);
        if (m_batchEditing) {
            recordBatchChange_(absPos, 0, 1, blockIndex);
            return;
        }

        renumberBlocksFrom(insertIdx);
        if (blockCount() != oldBlockCount) {
            emit blockCountChanged();
        }
        m_lastEditBlockHint = blockIndex;
        emit contentsChange(absPos, 0, 1);
        emit contentsChanged();
    }

    void insertText(int absPos, const SwString& text, const SwTextCharFormat& fmt) {
        if (text.isEmpty()) return;
        BlockPosition bp = blockPositionFromAbsolute(absPos);
        m_blocks[bp.blockIndex].insertText(bp.offset, text, fmt);
        m_modified = true;
        ++m_revision;
        if (m_batchEditing) {
            recordBatchChange_(absPos, 0, static_cast<int>(text.size()), bp.blockIndex);
            return;
        }
        emit contentsChange(absPos, 0, static_cast<int>(text.size()));
        emit contentsChanged();
    }

    void insertTextDirect(int blockIndex, int offset, const SwString& text, const SwTextCharFormat& fmt) {
        if (text.isEmpty() || blockIndex < 0 || blockIndex >= m_blocks.size()) return;
        m_blocks[blockIndex].insertText(offset, text, fmt);
        m_modified = true;
        ++m_revision;
        m_lastEditBlockHint = blockIndex;
        if (m_batchEditing) {
            recordBatchChange_(absolutePosition(blockIndex, offset),
                               0,
                               static_cast<int>(text.size()),
                               blockIndex);
            return;
        }
        // pos arg is not used by the highlighter (it uses m_lastEditBlockHint
        // for O(1) block lookup). Passing blockIndex avoids an O(N) scan.
        emit contentsChange(blockIndex, 0, static_cast<int>(text.size()));
        emit contentsChanged();
    }

    void removeTextDirect(int blockIndex, int offset, int count) {
        if (count <= 0 || blockIndex < 0 || blockIndex >= m_blocks.size()) return;
        m_blocks[blockIndex].removeText(offset, count);
        m_modified = true;
        ++m_revision;
        m_lastEditBlockHint = blockIndex;
        if (m_batchEditing) {
            recordBatchChange_(absolutePosition(blockIndex, offset), count, 0, blockIndex);
            return;
        }
        emit contentsChange(blockIndex, count, 0);
        emit contentsChanged();
    }

    void removeText(int absPos, int count) {
        if (count <= 0) return;
        const int oldBlockCount = blockCount();
        const int requested = count;
        int remaining = count;
        while (remaining > 0) {
            BlockPosition bp = blockPositionFromAbsolute(absPos);
            if (bp.blockIndex < 0 || bp.blockIndex >= m_blocks.size()) break;

            int blockLen = m_blocks[bp.blockIndex].length();
            int avail = blockLen - bp.offset;

            if (avail > 0 && remaining <= avail) {
                // Remove within current block
                m_blocks[bp.blockIndex].removeText(bp.offset, remaining);
                remaining = 0;
            } else if (avail > 0) {
                // Remove rest of current block
                m_blocks[bp.blockIndex].removeText(bp.offset, avail);
                remaining -= avail;
            } else if (bp.blockIndex + 1 < m_blocks.size()) {
                // At block boundary - merge with next block
                mergeBlockWithNext(bp.blockIndex);
                --remaining; // block separator consumed
            } else {
                break;
            }
        }
        renumberBlocks();
        m_modified = true;
        ++m_revision;
        if (m_batchEditing) {
            recordBatchChange_(absPos, requested - remaining, 0, blockPositionFromAbsolute(absPos).blockIndex);
            return;
        }
        if (blockCount() != oldBlockCount) {
            emit blockCountChanged();
        }
        emit contentsChange(absPos, requested - remaining, 0);
        emit contentsChanged();
    }

    void setCharFormat(int absPos, int count, const SwTextCharFormat& fmt) {
        if (count <= 0) return;
        int remaining = count;
        int pos = absPos;
        while (remaining > 0) {
            BlockPosition bp = blockPositionFromAbsolute(pos);
            if (bp.blockIndex >= m_blocks.size()) break;
            int blockLen = m_blocks[bp.blockIndex].length();
            int avail = blockLen - bp.offset;
            int toFormat = std::min(remaining, avail);
            if (toFormat > 0) {
                m_blocks[bp.blockIndex].setCharFormat(bp.offset, toFormat, fmt);
            }
            pos += toFormat + 1; // +1 for separator
            remaining -= toFormat + 1;
        }
        m_modified = true;
    }

    void mergeCharFormat(int absPos, int count, const SwTextCharFormat& fmt) {
        if (count <= 0) return;
        int remaining = count;
        int pos = absPos;
        while (remaining > 0) {
            BlockPosition bp = blockPositionFromAbsolute(pos);
            if (bp.blockIndex >= m_blocks.size()) break;
            int blockLen = m_blocks[bp.blockIndex].length();
            int avail = blockLen - bp.offset;
            int toFormat = std::min(remaining, avail);
            if (toFormat > 0) {
                m_blocks[bp.blockIndex].mergeCharFormat(bp.offset, toFormat, fmt);
            }
            pos += toFormat + 1;
            remaining -= toFormat + 1;
        }
        m_modified = true;
    }

    void setBlockFormat(int blockIndex, const SwTextBlockFormat& fmt) {
        if (blockIndex >= 0 && blockIndex < m_blocks.size()) {
            m_blocks[blockIndex].setBlockFormat(fmt);
            m_modified = true;
        }
    }

    // --- Lists ---
    SwTextList* createList(const SwTextListFormat& fmt) {
        SwTextList* list = new SwTextList(fmt);
        m_lists.append(list);
        return list;
    }

    int listCount() const { return swTextDocumentDetail::sizeToInt(m_lists.size()); }
    SwTextList* listAt(int index) const { return m_lists[index]; }

    // --- Tables ---
    SwTextTable* insertTable(int absPos, int rows, int cols, const SwTextTableFormat& fmt = SwTextTableFormat()) {
        SwTextTable* table = new SwTextTable(rows, cols);
        table->setTableFormat(fmt);
        BlockPosition bp = blockPositionFromAbsolute(absPos);
        table->setStartBlock(bp.blockIndex);
        m_tables.append(table);

        // Insert placeholder blocks for each cell
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                SwTextBlock cellBlock;
                table->cellAt(r, c).blocks.append(cellBlock);
            }
        }

        m_modified = true;
        emit contentsChanged();
        return table;
    }

    int tableCount() const { return swTextDocumentDetail::sizeToInt(m_tables.size()); }
    SwTextTable* tableAt(int index) const { return m_tables[index]; }

    // Find table at a given block
    SwTextTable* tableForBlock(int blockIndex) const {
        for (int i = 0; i < m_tables.size(); ++i) {
            if (m_tables[i]->startBlock() == blockIndex) return m_tables[i];
        }
        return nullptr;
    }

    // --- Image resources ---
    void addResource(const SwString& name, const SwImage& image) {
        m_imageResources.insert(name.toStdString(), image);
    }

    SwImage resource(const SwString& name) const {
        std::string key = name.toStdString();
        if (m_imageResources.contains(key)) {
            return m_imageResources.value(key);
        }
        return SwImage();
    }

    // --- Document properties ---
    void setPageSize(int widthPt, int heightPt) { m_pageWidth = widthPt; m_pageHeight = heightPt; }
    int pageWidth() const  { return m_pageWidth; }
    int pageHeight() const { return m_pageHeight; }

    void setDefaultFont(const SwFont& font) { m_defaultFont = font; }
    SwFont defaultFont() const { return m_defaultFont; }

    void setDefaultCharFormat(const SwTextCharFormat& fmt) { m_defaultCharFormat = fmt; }
    SwTextCharFormat defaultCharFormat() const { return m_defaultCharFormat; }

    bool isModified() const { return m_modified; }
    void setModified(bool m) { m_modified = m; }

    // --- Undo/Redo support (simplified) ---
    void setUndoRedoEnabled(bool on) { m_undoRedoEnabled = on; }
    bool isUndoRedoEnabled() const { return m_undoRedoEnabled; }

    // --- Clear ---
    void clear() {
        const int oldCharCount = characterCount();
        const int oldBlockCount = blockCount();
        m_blocks.clear();
        for (int i = 0; i < m_lists.size(); ++i) delete m_lists[i];
        m_lists.clear();
        for (int i = 0; i < m_tables.size(); ++i) delete m_tables[i];
        m_tables.clear();
        m_imageResources.clear();

        SwTextBlock block;
        block.setBlockNumber(0);
        m_blocks.append(block);
        m_modified = false;
        ++m_revision;
        if (blockCount() != oldBlockCount) {
            emit blockCountChanged();
        }
        if (oldCharCount > 0) {
            emit contentsChange(0, oldCharCount, 0);
        }
        emit contentsChanged();
    }

    // --- Signals ---
    DECLARE_SIGNAL_VOID(contentsChanged)
    DECLARE_SIGNAL(contentsChange, int, int, int)
    DECLARE_SIGNAL_VOID(blockCountChanged)

private:
    SwList<SwTextBlock> m_blocks;
    SwList<SwTextList*> m_lists;
    SwList<SwTextTable*> m_tables;
    SwMap<std::string, SwImage> m_imageResources;

    int m_pageWidth{595};   // A4 in points (72 dpi)
    int m_pageHeight{842};
    SwFont m_defaultFont;
    SwTextCharFormat m_defaultCharFormat;
    bool m_modified{false};
    bool m_undoRedoEnabled{true};
    bool m_batchEditing{false};
    int m_batchOldBlockCount{0};
    int m_batchContentsChangePos{0};
    int m_batchContentsChangeRemoved{0};
    int m_batchContentsChangeAdded{0};
    bool m_batchHasChanges{false};
    int m_revision{0};
    int m_lastEditBlockHint{-1};

    void recordBatchChange_(int pos, int removed, int added, int blockHint) {
        if (!m_batchHasChanges) {
            m_batchContentsChangePos = pos;
            m_batchHasChanges = true;
        } else {
            m_batchContentsChangePos = std::min(m_batchContentsChangePos, pos);
        }
        m_batchContentsChangeRemoved += removed;
        m_batchContentsChangeAdded += added;
        if (blockHint >= 0) {
            if (m_lastEditBlockHint < 0 || blockHint < m_lastEditBlockHint) {
                m_lastEditBlockHint = blockHint;
            }
        }
    }

    void renumberBlocks() {
        renumberBlocksFrom(0);
    }

    void renumberBlocksFrom(int startIndex) {
        for (int i = startIndex; i < m_blocks.size(); ++i) {
            if (m_blocks[i].blockNumber() == i) {
                // All blocks from here are already correctly numbered.
                break;
            }
            m_blocks[i].setBlockNumber(i);
        }
    }

    void mergeBlockWithNext(int blockIndex) {
        if (blockIndex + 1 >= m_blocks.size()) return;
        SwTextBlock& current = m_blocks[blockIndex];
        SwTextBlock& next = m_blocks[blockIndex + 1];
        // Move all fragments from next to current
        for (int i = 0; i < next.fragmentCount(); ++i) {
            current.appendFragment(next.fragmentAt(i));
        }
        m_blocks.removeAt(blockIndex + 1);
    }
};

// ---------------------------------------------------------------------------
// SwTextDocument — HTML import / export (inline implementations)
// ---------------------------------------------------------------------------

inline void SwTextDocument::setHtml(const SwString& html) {
    clear();
    m_blocks.clear();

    std::string s = html.toStdString();

    SwTextCharFormat fmt;
    std::vector<SwTextCharFormat> fmtStack;
    SwTextBlockFormat blockFmt;
    SwTextBlock currentBlock;
    currentBlock.setBlockNumber(0);
    bool hasContent = false;

    struct ListCtx { SwTextList* list; int counter; };
    std::vector<ListCtx> listStack;

    auto toLower = [](std::string str) -> std::string {
        for (auto& c : str) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return str;
    };
    auto trim = [](std::string str) -> std::string {
        while (!str.empty() && std::isspace(static_cast<unsigned char>(str.front()))) str.erase(str.begin());
        while (!str.empty() && std::isspace(static_cast<unsigned char>(str.back()))) str.pop_back();
        return str;
    };
    auto extractAttr = [&](const std::string& attrs, const std::string& name) -> std::string {
        std::string needle = toLower(name) + "=";
        std::string lower = toLower(attrs);
        size_t pos = lower.find(needle);
        if (pos == std::string::npos) return "";
        pos += needle.size();
        if (pos >= attrs.size()) return "";
        char q = attrs[pos];
        if (q == '\'' || q == '\"') {
            size_t end = attrs.find(q, pos + 1);
            return (end == std::string::npos) ? "" : attrs.substr(pos + 1, end - pos - 1);
        }
        size_t end = attrs.find_first_of(" \t>", pos);
        return attrs.substr(pos, (end == std::string::npos) ? std::string::npos : end - pos);
    };
    auto parseCssColor = [](const std::string& c, SwColor& out) -> bool {
        if (c.empty()) return false;
        if (c[0] == '#' && c.size() == 7) {
            auto hex = [](char ch) -> int {
                if (ch >= '0' && ch <= '9') return ch - '0';
                if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
                if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
                return -1;
            };
            int r = hex(c[1]) * 16 + hex(c[2]);
            int g = hex(c[3]) * 16 + hex(c[4]);
            int b = hex(c[5]) * 16 + hex(c[6]);
            if (r >= 0 && g >= 0 && b >= 0) { out = {r, g, b}; return true; }
        }
        // Handle rgb(r,g,b) format
        if (c.size() > 4 && c.substr(0, 4) == "rgb(") {
            size_t start = 4;
            size_t end = c.find(')', start);
            if (end != std::string::npos) {
                std::string inner = c.substr(start, end - start);
                int r = 0, g = 0, b = 0;
                if (swTextDocumentDetail::parseCssRgbTriplet(inner, r, g, b)) {
                    out = {r, g, b};
                    return true;
                }
            }
        }
        if (c == "red")   { out = {255,0,0}; return true; }
        if (c == "green") { out = {0,128,0}; return true; }
        if (c == "blue")  { out = {0,0,255}; return true; }
        if (c == "black") { out = {0,0,0}; return true; }
        if (c == "white") { out = {255,255,255}; return true; }
        if (c == "gray" || c == "grey") { out = {128,128,128}; return true; }
        return false;
    };

    auto newBlock = [&]() {
        if (hasContent || currentBlock.fragmentCount() > 0) {
            m_blocks.append(currentBlock);
        }
        currentBlock = SwTextBlock();
        currentBlock.setBlockNumber(swTextDocumentDetail::sizeToInt(m_blocks.size()));
        currentBlock.setBlockFormat(blockFmt);
        blockFmt = SwTextBlockFormat();
        hasContent = false;
    };

    auto flushText = [&](const std::string& text) {
        if (text.empty()) return;
        currentBlock.appendFragment(SwTextFragment(SwString(text), fmt));
        hasContent = true;
    };

    std::string buffer;
    for (size_t i = 0; i < s.size();) {
        if (s[i] != '<') {
            size_t next = s.find('<', i);
            buffer.append(s.substr(i, (next == std::string::npos) ? std::string::npos : (next - i)));
            i = (next == std::string::npos) ? s.size() : next;
            continue;
        }

        flushText(buffer);
        buffer.clear();

        size_t close = s.find('>', i + 1);
        if (close == std::string::npos) { buffer.push_back(s[i]); ++i; continue; }

        std::string tag = trim(s.substr(i + 1, close - i - 1));
        i = close + 1;
        if (tag.empty()) continue;

        bool isClosing = tag[0] == '/';
        if (isClosing) tag = trim(tag.substr(1));
        bool selfClosing = !tag.empty() && tag.back() == '/';
        if (selfClosing) tag = trim(tag.substr(0, tag.size() - 1));

        std::string tagName, attrs;
        size_t ws = tag.find_first_of(" \t\r\n");
        if (ws == std::string::npos) { tagName = toLower(tag); }
        else { tagName = toLower(tag.substr(0, ws)); attrs = tag.substr(ws + 1); }

        if (tagName == "br") { buffer.push_back('\n'); continue; }

        if (tagName == "hr") {
            newBlock();
            // HR is represented as an empty block with heading level 0 and a special top margin
            // The layout engine recognizes empty blocks with small height as HR
            newBlock();
            continue;
        }

        if (tagName == "p" || tagName == "div") {
            if (!isClosing) {
                newBlock();
                std::string align = toLower(extractAttr(attrs, "align"));
                if (align == "center")     blockFmt.setAlignment(SwTextBlockFormat::AlignCenter);
                else if (align == "right") blockFmt.setAlignment(SwTextBlockFormat::AlignRight);
                currentBlock.setBlockFormat(blockFmt);
            } else {
                newBlock();
            }
            continue;
        }

        if (tagName.size() == 2 && tagName[0] == 'h' && tagName[1] >= '1' && tagName[1] <= '6') {
            int level = tagName[1] - '0';
            if (!isClosing) {
                newBlock();
                blockFmt.setHeadingLevel(level);
                currentBlock.setBlockFormat(blockFmt);
                fmtStack.push_back(fmt);
                fmt.setFontWeight(Bold);
                int sizes[] = {0, 24, 18, 14, 12, 10, 8};
                fmt.setFontPointSize(sizes[level]);
            } else {
                if (!fmtStack.empty()) { fmt = fmtStack.back(); fmtStack.pop_back(); }
                newBlock();
            }
            continue;
        }

        if (tagName == "ul" || tagName == "ol") {
            if (!isClosing) {
                newBlock();
                SwTextListFormat listFmt;
                listFmt.setStyle(tagName == "ol" ? SwTextListFormat::ListDecimal : SwTextListFormat::ListDisc);
                listFmt.setIndent(static_cast<int>(listStack.size()) + 1);
                SwTextList* list = createList(listFmt);
                listStack.push_back({list, 0});
            } else {
                if (!listStack.empty()) listStack.pop_back();
                newBlock();
            }
            continue;
        }

        if (tagName == "li") {
            if (!isClosing) {
                newBlock();
                if (!listStack.empty()) {
                    auto& ctx = listStack.back();
                    currentBlock.setList(ctx.list);
                    ctx.list->addBlockIndex(currentBlock.blockNumber());
                    ctx.counter++;
                    SwTextBlockFormat liFmt;
                    liFmt.setIndent(static_cast<int>(listStack.size()));
                    currentBlock.setBlockFormat(liFmt);
                }
            } else {
                newBlock();
            }
            continue;
        }

        if (tagName == "b" || tagName == "strong") {
            if (!isClosing) { fmtStack.push_back(fmt); fmt.setFontWeight(Bold); }
            else if (!fmtStack.empty()) { fmt = fmtStack.back(); fmtStack.pop_back(); }
            continue;
        }
        if (tagName == "i" || tagName == "em") {
            if (!isClosing) { fmtStack.push_back(fmt); fmt.setFontItalic(true); }
            else if (!fmtStack.empty()) { fmt = fmtStack.back(); fmtStack.pop_back(); }
            continue;
        }
        if (tagName == "u") {
            if (!isClosing) { fmtStack.push_back(fmt); fmt.setFontUnderline(true); }
            else if (!fmtStack.empty()) { fmt = fmtStack.back(); fmtStack.pop_back(); }
            continue;
        }
        if (tagName == "s" || tagName == "del" || tagName == "strike") {
            if (!isClosing) { fmtStack.push_back(fmt); fmt.setFontStrikeOut(true); }
            else if (!fmtStack.empty()) { fmt = fmtStack.back(); fmtStack.pop_back(); }
            continue;
        }
        if (tagName == "a") {
            if (!isClosing) {
                fmtStack.push_back(fmt);
                fmt.setAnchorHref(SwString(extractAttr(attrs, "href")));
                fmt.setFontUnderline(true);
                fmt.setForeground(SwColor{0, 0, 238});
            }
            else if (!fmtStack.empty()) { fmt = fmtStack.back(); fmtStack.pop_back(); }
            continue;
        }
        if (tagName == "span") {
            if (!isClosing) {
                fmtStack.push_back(fmt);
                std::string style = extractAttr(attrs, "style");
                // Parse inline style
                std::istringstream ss(style);
                std::string decl;
                while (std::getline(ss, decl, ';')) {
                    decl = trim(decl);
                    size_t colon = decl.find(':');
                    if (colon == std::string::npos) continue;
                    std::string key = toLower(trim(decl.substr(0, colon)));
                    std::string val = trim(decl.substr(colon + 1));
                    if (key == "color") {
                        SwColor c{0,0,0};
                        if (parseCssColor(toLower(val), c)) fmt.setForeground(c);
                    } else if (key == "background-color") {
                        SwColor c{255,255,255};
                        if (parseCssColor(toLower(val), c)) fmt.setBackground(c);
                    } else if (key == "font-weight") {
                        if (toLower(val).find("bold") != std::string::npos) fmt.setFontWeight(Bold);
                    } else if (key == "font-style") {
                        if (toLower(val).find("italic") != std::string::npos) fmt.setFontItalic(true);
                    } else if (key == "font-size") {
                        try { int sz = std::stoi(val); if (sz > 0) fmt.setFontPointSize(sz); } catch (...) {}
                    } else if (key == "text-decoration") {
                        if (toLower(val).find("underline") != std::string::npos) fmt.setFontUnderline(true);
                        if (toLower(val).find("line-through") != std::string::npos) fmt.setFontStrikeOut(true);
                    }
                }
            }
            else if (!fmtStack.empty()) { fmt = fmtStack.back(); fmtStack.pop_back(); }
            continue;
        }
        if (tagName == "font") {
            if (!isClosing) {
                fmtStack.push_back(fmt);
                std::string colorAttr = extractAttr(attrs, "color");
                SwColor c{0,0,0};
                if (parseCssColor(toLower(colorAttr), c)) fmt.setForeground(c);
                std::string faceAttr = extractAttr(attrs, "face");
                if (!faceAttr.empty()) fmt.setFontFamily(SwString(faceAttr));
            }
            else if (!fmtStack.empty()) { fmt = fmtStack.back(); fmtStack.pop_back(); }
            continue;
        }
        if (tagName == "code" || tagName == "pre") {
            if (!isClosing) { fmtStack.push_back(fmt); fmt.setFontFamily(SwString("Courier New")); }
            else if (!fmtStack.empty()) { fmt = fmtStack.back(); fmtStack.pop_back(); }
            continue;
        }
        if (tagName == "mark") {
            if (!isClosing) { fmtStack.push_back(fmt); fmt.setBackground(SwColor{255,255,0}); }
            else if (!fmtStack.empty()) { fmt = fmtStack.back(); fmtStack.pop_back(); }
            continue;
        }
        if (tagName == "img") {
            std::string alt = extractAttr(attrs, "alt");
            if (alt.empty()) alt = "[image]";
            currentBlock.appendFragment(SwTextFragment(SwString(alt), fmt));
            hasContent = true;
            continue;
        }

        // --- Table parsing ---
        if (tagName == "table") {
            if (!isClosing) {
                newBlock(); // flush current content

                // Parse table attributes
                SwTextTableFormat tableFmt;
                std::string borderAttr = extractAttr(attrs, "border");
                if (!borderAttr.empty()) { try { tableFmt.setBorderWidth(std::stoi(borderAttr)); } catch (...) {} }
                std::string cellpadAttr = extractAttr(attrs, "cellpadding");
                if (!cellpadAttr.empty()) { try { tableFmt.setCellPadding(std::stoi(cellpadAttr)); } catch (...) {} }
                std::string cellspAttr = extractAttr(attrs, "cellspacing");
                if (!cellspAttr.empty()) { try { tableFmt.setCellSpacing(std::stoi(cellspAttr)); } catch (...) {} }
                // Parse style for background, width
                std::string style = extractAttr(attrs, "style");
                if (!style.empty()) {
                    std::istringstream tss(style);
                    std::string tdecl;
                    while (std::getline(tss, tdecl, ';')) {
                        tdecl = trim(tdecl);
                        size_t colon = tdecl.find(':');
                        if (colon == std::string::npos) continue;
                        std::string key = toLower(trim(tdecl.substr(0, colon)));
                        std::string val = trim(tdecl.substr(colon + 1));
                        if (key == "background-color") {
                            SwColor bgc{255,255,255};
                            if (parseCssColor(toLower(val), bgc)) tableFmt.setBackground(bgc);
                        }
                    }
                }
                std::string bgAttr = extractAttr(attrs, "bgcolor");
                if (!bgAttr.empty()) {
                    SwColor bgc{255,255,255};
                    if (parseCssColor(toLower(bgAttr), bgc)) tableFmt.setBackground(bgc);
                }

                // Collect all cell data by pre-scanning until </table>
                struct HtmlCell { std::string content; bool isHeader; std::string bgColor; std::string align; };
                std::vector<std::vector<HtmlCell>> tableRows;
                std::vector<HtmlCell> curRow;
                HtmlCell curCell;
                curCell.isHeader = false;
                bool inCell = false;
                bool inRow = false;
                int tableDepth = 1; // track nested tables

                while (i < s.size() && tableDepth > 0) {
                    if (s[i] != '<') {
                        if (inCell) curCell.content += s[i];
                        ++i;
                        continue;
                    }
                    size_t tc = s.find('>', i + 1);
                    if (tc == std::string::npos) { ++i; continue; }
                    std::string itag = trim(s.substr(i + 1, tc - i - 1));
                    i = tc + 1;
                    if (itag.empty()) continue;
                    bool iClose = itag[0] == '/';
                    if (iClose) itag = trim(itag.substr(1));
                    bool iSelf = !itag.empty() && itag.back() == '/';
                    if (iSelf) itag = trim(itag.substr(0, itag.size() - 1));
                    std::string iName, iAttrs;
                    size_t iws = itag.find_first_of(" \t\r\n");
                    if (iws == std::string::npos) { iName = toLower(itag); }
                    else { iName = toLower(itag.substr(0, iws)); iAttrs = itag.substr(iws + 1); }

                    if (iName == "table" && !iClose) { tableDepth++; if (inCell) curCell.content += "<table " + iAttrs + ">"; continue; }
                    if (iName == "table" && iClose) {
                        tableDepth--;
                        if (tableDepth > 0 && inCell) { curCell.content += "</table>"; continue; }
                        // Close table
                        if (inCell) { curRow.push_back(curCell); inCell = false; }
                        if (inRow && !curRow.empty()) { tableRows.push_back(curRow); }
                        break;
                    }
                    if (tableDepth > 1 && inCell) {
                        // Inside nested table, accumulate raw HTML
                        if (iClose) curCell.content += "</" + iName + ">";
                        else curCell.content += "<" + iName + (iAttrs.empty() ? "" : " " + iAttrs) + (iSelf ? "/" : "") + ">";
                        continue;
                    }
                    if (iName == "tr") {
                        if (!iClose) { curRow.clear(); inRow = true; }
                        else {
                            if (inCell) { curRow.push_back(curCell); inCell = false; }
                            if (!curRow.empty()) tableRows.push_back(curRow);
                            inRow = false;
                        }
                        continue;
                    }
                    if (iName == "td" || iName == "th") {
                        if (!iClose) {
                            if (inCell) curRow.push_back(curCell); // flush prev cell
                            curCell = HtmlCell();
                            curCell.isHeader = (iName == "th");
                            curCell.bgColor = extractAttr(iAttrs, "bgcolor");
                            curCell.align = extractAttr(iAttrs, "align");
                            // Parse style for bg
                            std::string cstyle = extractAttr(iAttrs, "style");
                            if (!cstyle.empty()) {
                                std::istringstream css(cstyle);
                                std::string cd;
                                while (std::getline(css, cd, ';')) {
                                    cd = trim(cd);
                                    size_t cc = cd.find(':');
                                    if (cc == std::string::npos) continue;
                                    std::string ck = toLower(trim(cd.substr(0, cc)));
                                    std::string cv = trim(cd.substr(cc + 1));
                                    if (ck == "background-color") curCell.bgColor = cv;
                                }
                            }
                            inCell = true;
                        } else {
                            if (inCell) curRow.push_back(curCell);
                            inCell = false;
                        }
                        continue;
                    }
                    // Pass through inline formatting tags as raw html into cell content
                    if (inCell) {
                        if (iClose) curCell.content += "</" + iName + ">";
                        else if (iSelf) curCell.content += "<" + iName + (iAttrs.empty() ? "" : " " + iAttrs) + "/>";
                        else curCell.content += "<" + iName + (iAttrs.empty() ? "" : " " + iAttrs) + ">";
                    }
                }

                // Build SwTextTable from collected data
                if (!tableRows.empty()) {
                    int numRows = static_cast<int>(tableRows.size());
                    int numCols = 0;
                    for (auto& row : tableRows) numCols = std::max(numCols, static_cast<int>(row.size()));
                    if (numCols > 0) {
                        if (tableFmt.borderWidth() == 0) tableFmt.setBorderWidth(1);
                        if (tableFmt.cellPadding() == 0) tableFmt.setCellPadding(4);
                        SwTextTable* tbl = new SwTextTable(numRows, numCols);
                        tbl->setTableFormat(tableFmt);
                        tbl->setStartBlock(swTextDocumentDetail::sizeToInt(m_blocks.size()));
                        m_tables.append(tbl);

                        for (int r = 0; r < numRows; ++r) {
                            for (int c = 0; c < numCols; ++c) {
                                if (c < static_cast<int>(tableRows[r].size())) {
                                    const HtmlCell& hc = tableRows[r][c];
                                    // Parse cell content as mini-HTML for formatting
                                    SwTextCharFormat cellFmt;
                                    if (hc.isHeader) cellFmt.setFontWeight(Bold);
                                    // Parse inline tags in cell content
                                    SwTextBlock cellBlock;
                                    std::string cbuf;
                                    SwTextCharFormat cfmt = cellFmt;
                                    std::vector<SwTextCharFormat> cfmtStack;
                                    for (size_t ci = 0; ci < hc.content.size(); ) {
                                        if (hc.content[ci] != '<') {
                                            cbuf += hc.content[ci]; ++ci; continue;
                                        }
                                        // flush text
                                        if (!cbuf.empty()) {
                                            cellBlock.appendFragment(SwTextFragment(SwString(cbuf), cfmt));
                                            cbuf.clear();
                                        }
                                        size_t ce = hc.content.find('>', ci + 1);
                                        if (ce == std::string::npos) { cbuf += hc.content[ci]; ++ci; continue; }
                                        std::string ct = trim(hc.content.substr(ci + 1, ce - ci - 1));
                                        ci = ce + 1;
                                        bool cc = ct[0] == '/';
                                        if (cc) ct = trim(ct.substr(1));
                                        std::string cn = toLower(ct.substr(0, ct.find_first_of(" \t\r\n/")));
                                        if (cn == "b" || cn == "strong") {
                                            if (!cc) { cfmtStack.push_back(cfmt); cfmt.setFontWeight(Bold); }
                                            else if (!cfmtStack.empty()) { cfmt = cfmtStack.back(); cfmtStack.pop_back(); }
                                        } else if (cn == "i" || cn == "em") {
                                            if (!cc) { cfmtStack.push_back(cfmt); cfmt.setFontItalic(true); }
                                            else if (!cfmtStack.empty()) { cfmt = cfmtStack.back(); cfmtStack.pop_back(); }
                                        } else if (cn == "u") {
                                            if (!cc) { cfmtStack.push_back(cfmt); cfmt.setFontUnderline(true); }
                                            else if (!cfmtStack.empty()) { cfmt = cfmtStack.back(); cfmtStack.pop_back(); }
                                        } else if (cn == "s" || cn == "del") {
                                            if (!cc) { cfmtStack.push_back(cfmt); cfmt.setFontStrikeOut(true); }
                                            else if (!cfmtStack.empty()) { cfmt = cfmtStack.back(); cfmtStack.pop_back(); }
                                        } else if (cn == "br") {
                                            cbuf += '\n';
                                        }
                                    }
                                    if (!cbuf.empty()) {
                                        cellBlock.appendFragment(SwTextFragment(SwString(cbuf), cfmt));
                                    }
                                    tbl->cellAt(r, c).blocks.clear();
                                    tbl->cellAt(r, c).blocks.append(cellBlock);

                                    // Cell format (bg color)
                                    if (!hc.bgColor.empty()) {
                                        SwColor bgc{255,255,255};
                                        if (parseCssColor(toLower(hc.bgColor), bgc)) {
                                            SwTextTableCellFormat ccf;
                                            ccf.setBackground(bgc);
                                            ccf.setPadding(tableFmt.cellPadding());
                                            tbl->setCellFormat(r, c, ccf);
                                        }
                                    }
                                }
                            }
                        }
                        // Insert a placeholder block in the document
                        SwTextBlock tableBlock;
                        tableBlock.setBlockNumber(swTextDocumentDetail::sizeToInt(m_blocks.size()));
                        m_blocks.append(tableBlock);
                        hasContent = false;
                        currentBlock = SwTextBlock();
                        currentBlock.setBlockNumber(swTextDocumentDetail::sizeToInt(m_blocks.size()));
                    }
                }
            }
            // </table> is consumed by the inner loop
            continue;
        }
    }

    flushText(buffer);
    m_blocks.append(currentBlock);
    renumberBlocks();

    // Remove leading empty block if document has content
    if (m_blocks.size() > 1 && m_blocks[0].length() == 0 && m_blocks[0].fragmentCount() == 0) {
        m_blocks.removeAt(0);
        renumberBlocks();
    }

    m_modified = false;
    emit contentsChanged();
}

inline SwString SwTextDocument::toHtml() const {
    SwString out;
    out.append("<!DOCTYPE html><html><body>");

    auto escapeHtml = [](const SwString& text) -> SwString {
        SwString escaped;
        for (size_t c = 0; c < text.size(); ++c) {
            char ch = text[c];
            switch (ch) {
            case '&': escaped.append("&amp;"); break;
            case '<': escaped.append("&lt;"); break;
            case '>': escaped.append("&gt;"); break;
            case '\"': escaped.append("&quot;"); break;
            case '\n': escaped.append("<br/>"); break;
            default: escaped.append(ch); break;
            }
        }
        return escaped;
    };

    auto fragmentsToHtml = [&](const SwTextBlock& blk) -> SwString {
        SwString result;
        for (int f = 0; f < blk.fragmentCount(); ++f) {
            const SwTextFragment& frag = blk.fragmentAt(f);
            const SwTextCharFormat& cf = frag.charFormat();
            SwString prefix, suffix;
            bool needsSpan = false;
            SwString styleStr;
            if (cf.hasForeground()) {
                std::ostringstream oss;
                oss << "color: rgb(" << cf.foreground().r << "," << cf.foreground().g << "," << cf.foreground().b << "); ";
                styleStr.append(SwString(oss.str())); needsSpan = true;
            }
            if (cf.hasBackground()) {
                std::ostringstream oss;
                oss << "background-color: rgb(" << cf.background().r << "," << cf.background().g << "," << cf.background().b << "); ";
                styleStr.append(SwString(oss.str())); needsSpan = true;
            }
            if (cf.hasFontPointSize() && cf.fontPointSize() > 0) {
                std::ostringstream oss;
                oss << "font-size: " << cf.fontPointSize() << "pt; ";
                styleStr.append(SwString(oss.str())); needsSpan = true;
            }
            if (needsSpan) { prefix.append("<span style=\""); prefix.append(styleStr); prefix.append("\">"); suffix.prepend("</span>"); }
            if (cf.isAnchor()) { prefix.append("<a href=\""); prefix.append(cf.anchorHref()); prefix.append("\">"); suffix.prepend("</a>"); }
            if (cf.hasFontWeight() && cf.fontWeight() == Bold) { prefix.append("<b>"); suffix.prepend("</b>"); }
            if (cf.fontItalic()) { prefix.append("<i>"); suffix.prepend("</i>"); }
            if (cf.fontUnderline() && !cf.isAnchor()) { prefix.append("<u>"); suffix.prepend("</u>"); }
            if (cf.fontStrikeOut()) { prefix.append("<s>"); suffix.prepend("</s>"); }
            result.append(prefix);
            result.append(escapeHtml(frag.text()));
            result.append(suffix);
        }
        return result;
    };

    for (int i = 0; i < m_blocks.size(); ++i) {
        const SwTextBlock& block = m_blocks[i];
        const SwTextBlockFormat& bf = block.blockFormat();

        // --- Table export ---
        SwTextTable* tbl = tableForBlock(i);
        if (tbl) {
            const SwTextTableFormat& tf = tbl->tableFormat();
            std::ostringstream toss;
            toss << "<table border=\"" << tf.borderWidth() << "\" cellpadding=\"" << tf.cellPadding()
                 << "\" cellspacing=\"" << tf.cellSpacing() << "\"";
            if (tf.hasBackground()) {
                toss << " bgcolor=\"#" << std::hex << std::setfill('0')
                     << std::setw(2) << tf.background().r
                     << std::setw(2) << tf.background().g
                     << std::setw(2) << tf.background().b << "\"" << std::dec;
            }
            toss << ">";
            out.append(SwString(toss.str()));
            for (int r = 0; r < tbl->rows(); ++r) {
                out.append("<tr>");
                for (int c = 0; c < tbl->columns(); ++c) {
                    SwTextTableCellFormat ccf = tbl->cellFormat(r, c);
                    SwString cellTag = "<td";
                    if (ccf.hasBackground()) {
                        std::ostringstream coss;
                        coss << " bgcolor=\"#" << std::hex << std::setfill('0')
                             << std::setw(2) << ccf.background().r
                             << std::setw(2) << ccf.background().g
                             << std::setw(2) << ccf.background().b << "\"" << std::dec;
                        cellTag.append(SwString(coss.str()));
                    }
                    cellTag.append(">");
                    out.append(cellTag);
                    const SwTextTable::Cell& cell = tbl->cellAt(r, c);
                    for (int bi = 0; bi < cell.blocks.size(); ++bi) {
                        if (bi > 0) out.append("<br/>");
                        out.append(fragmentsToHtml(cell.blocks[bi]));
                    }
                    out.append("</td>");
                }
                out.append("</tr>");
            }
            out.append("</table>");
            continue; // skip normal block rendering for table placeholder
        }

        // Determine block-level tag
        SwString openTag, closeTag;
        if (bf.headingLevel() >= 1 && bf.headingLevel() <= 6) {
            std::ostringstream oss;
            oss << "<h" << bf.headingLevel() << ">";
            openTag = SwString(oss.str());
            oss.str(""); oss << "</h" << bf.headingLevel() << ">";
            closeTag = SwString(oss.str());
        } else {
            SwString style;
            if (bf.alignment() == SwTextBlockFormat::AlignCenter) {
                style = " style=\"text-align: center;\"";
            } else if (bf.alignment() == SwTextBlockFormat::AlignRight) {
                style = " style=\"text-align: right;\"";
            }
            openTag = SwString("<p") + style + SwString(">");
            closeTag = SwString("</p>");
        }

        // List handling
        if (block.list()) {
            SwTextList* list = block.list();
            int itemNum = list->itemNumber(i);
            if (itemNum == 0) {
                if (list->format().isOrdered()) out.append("<ol>");
                else out.append("<ul>");
            }
            openTag = SwString("<li>");
            closeTag = SwString("</li>");
        }

        out.append(openTag);
        out.append(fragmentsToHtml(block));

        out.append(closeTag);

        // Close list if this is the last item
        if (block.list()) {
            SwTextList* list = block.list();
            int itemNum = list->itemNumber(i);
            bool isLast = (itemNum == list->count() - 1);
            if (isLast) {
                if (list->format().isOrdered()) out.append("</ol>");
                else out.append("</ul>");
            }
        }
    }

    out.append("</body></html>");
    return out;
}
