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

#pragma once

#include "SwString.h"
#include "SwVector.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstring>

/***************************************************************************************************
 * SwPieceTable — Piece Table text buffer for large-file editing.
 *
 * Two buffers:
 *   - Original buffer (read-only): either an mmap'd file or an SwString snapshot.
 *   - Add buffer (append-only SwString): receives all inserted text.
 *
 * Pieces are descriptors (buffer, start, length, newlineCount) stored in a flat
 * SwVector sorted by logical document order.  Edits split/trim/insert pieces
 * without copying existing text — O(pieces) per edit, not O(document_size).
 *
 * A line index (mapping line number → piece + local offset) can be built
 * incrementally or streamed from a background thread pool worker.
 **************************************************************************************************/

class SwPieceTable {
public:
    // ─── Buffer type ────────────────────────────────────────────────────
    enum BufferType { Original = 0, Add = 1 };

    struct Piece {
        BufferType buffer;
        size_t start;
        size_t length;
        int newlineCount;
    };

    // ─── Line index entry (built in background or on demand) ────────────
    struct LineEntry {
        size_t offset;          // absolute character offset in logical document
    };

    // ─── Construction / destruction ─────────────────────────────────────
    SwPieceTable() = default;

    ~SwPieceTable() {
        unmapOriginal_();
    }

    SwPieceTable(const SwPieceTable&) = delete;
    SwPieceTable& operator=(const SwPieceTable&) = delete;

    // ─── setText: set content from SwString (copies into original buffer) ─
    void setText(const SwString& text) {
        unmapOriginal_();
        m_originalOwned = text;
        m_originalData = m_originalOwned.isEmpty() ? nullptr : m_originalOwned.data();
        m_originalSize = m_originalOwned.size();
        m_isMapped = false;

        m_add.clear();
        m_pieces.clear();
        m_lineIndex.clear();
        m_lineIndexReady.store(false, std::memory_order_release);

        if (m_originalSize > 0) {
            Piece p{Original, 0, m_originalSize, countNewlines_(m_originalData, 0, m_originalSize)};
            m_pieces.push_back(p);
            m_totalLength = m_originalSize;
            m_totalNewlines = p.newlineCount;
        } else {
            m_totalLength = 0;
            m_totalNewlines = 0;
        }

        buildLineIndex_();
    }

    // ─── setMappedFile: memory-map a file as original buffer (zero-copy) ─
#ifdef _WIN32
    bool setMappedFile(const SwString& filePath) {
        unmapOriginal_();
        m_originalOwned.clear();
        m_add.clear();
        m_pieces.clear();
        m_lineIndex.clear();
        m_lineIndexReady.store(false, std::memory_order_release);
        m_totalLength = 0;
        m_totalNewlines = 0;

        const std::string pathA = filePath.toStdString();
        HANDLE hFile = ::CreateFileA(pathA.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            return false;
        }
        LARGE_INTEGER fileSize;
        if (!::GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart == 0) {
            ::CloseHandle(hFile);
            return false;
        }
        HANDLE hMapping = ::CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!hMapping) {
            ::CloseHandle(hFile);
            return false;
        }
        void* ptr = ::MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
        if (!ptr) {
            ::CloseHandle(hMapping);
            ::CloseHandle(hFile);
            return false;
        }

        m_mmapFile = hFile;
        m_mmapMapping = hMapping;
        m_originalData = static_cast<const char*>(ptr);
        m_originalSize = static_cast<size_t>(fileSize.QuadPart);
        m_isMapped = true;

        Piece p{Original, 0, m_originalSize, 0};
        // Newline count will be computed by background line index builder
        m_pieces.push_back(p);
        m_totalLength = m_originalSize;
        m_totalNewlines = 0;  // Updated after line index build

        return true;
    }
#else
    bool setMappedFile(const SwString& filePath) {
        unmapOriginal_();
        m_originalOwned.clear();
        m_add.clear();
        m_pieces.clear();
        m_lineIndex.clear();
        m_lineIndexReady.store(false, std::memory_order_release);
        m_totalLength = 0;
        m_totalNewlines = 0;

        const std::string pathStr = filePath.toStdString();
        int fd = ::open(pathStr.c_str(), O_RDONLY);
        if (fd < 0) {
            return false;
        }
        struct stat st;
        if (::fstat(fd, &st) != 0 || st.st_size == 0) {
            ::close(fd);
            return false;
        }
        void* ptr = ::mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
        ::close(fd);
        if (ptr == MAP_FAILED) {
            return false;
        }

        m_mmapPtr = ptr;
        m_mmapSize = static_cast<size_t>(st.st_size);
        m_originalData = static_cast<const char*>(ptr);
        m_originalSize = m_mmapSize;
        m_isMapped = true;

        Piece p{Original, 0, m_originalSize, 0};
        m_pieces.push_back(p);
        m_totalLength = m_originalSize;
        m_totalNewlines = 0;

        return true;
    }
#endif

    // ─── Core queries ───────────────────────────────────────────────────
    size_t totalLength() const { return m_totalLength; }
    bool isEmpty() const { return m_totalLength == 0; }

    int lineCount() const {
        if (m_lineIndexReady.load(std::memory_order_acquire)) {
            return static_cast<int>(m_lineIndex.size());
        }
        return m_totalNewlines + 1;
    }

    bool isLineIndexReady() const {
        return m_lineIndexReady.load(std::memory_order_acquire);
    }

    // ─── Character access ───────────────────────────────────────────────
    char charAt(size_t pos) const {
        if (pos >= m_totalLength) {
            return '\0';
        }
        size_t cumulative = 0;
        for (int i = 0; i < m_pieces.size(); ++i) {
            const Piece& p = m_pieces[i];
            if (pos < cumulative + p.length) {
                return bufferChar_(p.buffer, p.start + (pos - cumulative));
            }
            cumulative += p.length;
        }
        return '\0';
    }

    // ─── Substring extraction ───────────────────────────────────────────
    SwString substr(size_t pos, size_t length) const {
        if (length == 0 || pos >= m_totalLength) {
            return SwString();
        }
        const size_t actualEnd = std::min(pos + length, m_totalLength);
        const size_t actualLen = actualEnd - pos;

        SwString result;
        result.reserve(actualLen);

        size_t cumulative = 0;
        int pieceIdx = 0;
        while (pieceIdx < m_pieces.size() && cumulative + m_pieces[pieceIdx].length <= pos) {
            cumulative += m_pieces[pieceIdx].length;
            ++pieceIdx;
        }

        size_t remaining = actualLen;
        size_t localOffset = pos - cumulative;
        while (remaining > 0 && pieceIdx < m_pieces.size()) {
            const Piece& p = m_pieces[pieceIdx];
            const size_t available = p.length - localOffset;
            const size_t take = std::min(available, remaining);
            const char* src = bufferPtr_(p.buffer) + p.start + localOffset;
            result.append(src, take);
            remaining -= take;
            ++pieceIdx;
            localOffset = 0;
        }
        return result;
    }

    // ─── Full text extraction ───────────────────────────────────────────
    SwString toPlainText() const {
        if (m_totalLength == 0) {
            return SwString();
        }
        // Fast path: single original piece, no edits
        if (m_pieces.size() == 1 && m_pieces[0].buffer == Original && !m_isMapped) {
            return m_originalOwned;
        }
        SwString result;
        result.reserve(m_totalLength);
        for (int i = 0; i < m_pieces.size(); ++i) {
            const Piece& p = m_pieces[i];
            result.append(bufferPtr_(p.buffer) + p.start, p.length);
        }
        return result;
    }

    bool equals(const SwString& text) const {
        if (text.size() != m_totalLength) {
            return false;
        }
        size_t textPos = 0;
        for (int i = 0; i < m_pieces.size(); ++i) {
            const Piece& p = m_pieces[i];
            const char* src = bufferPtr_(p.buffer) + p.start;
            if (std::memcmp(src, text.data() + textPos, p.length) != 0) {
                return false;
            }
            textPos += p.length;
        }
        return true;
    }

    bool endsWith(const SwString& suffix) const {
        if (suffix.size() > m_totalLength) {
            return false;
        }
        return substr(m_totalLength - suffix.size(), suffix.size()) == suffix;
    }

    // ─── Insert ─────────────────────────────────────────────────────────
    void insert(size_t pos, const SwString& text) {
        if (text.isEmpty()) {
            return;
        }
        const size_t clamped = std::min(pos, m_totalLength);

        // Append to add buffer
        const size_t addStart = m_add.size();
        m_add.append(text);

        const int newNewlines = countNewlines_(m_add.data(), addStart, text.size());
        Piece newPiece{Add, addStart, text.size(), newNewlines};

        if (m_pieces.isEmpty()) {
            m_pieces.push_back(newPiece);
        } else {
            const int splitIdx = splitPieceAt_(clamped);
            m_pieces.insert(m_pieces.begin() + splitIdx, newPiece);
        }

        m_totalLength += text.size();
        m_totalNewlines += newNewlines;
        updateLineIndexAfterInsert_(clamped, text.size(), newNewlines);
    }

    void insert(size_t pos, const char* data, size_t length) {
        if (!data || length == 0) {
            return;
        }
        insert(pos, SwString(data, length));
    }

    // ─── Remove ─────────────────────────────────────────────────────────
    void remove(size_t pos, size_t length) {
        if (length == 0 || isEmpty()) {
            return;
        }
        const size_t removeStart = std::min(pos, m_totalLength);
        const size_t removeEnd = std::min(pos + length, m_totalLength);
        if (removeEnd <= removeStart) {
            return;
        }
        const size_t removeLen = removeEnd - removeStart;

        // Split at boundaries so they align with piece boundaries
        const int endIdx = splitPieceAt_(removeEnd);
        const int startIdx = splitPieceAt_(removeStart);

        // Count newlines being removed
        int removedNewlines = 0;
        for (int i = startIdx; i < endIdx; ++i) {
            removedNewlines += m_pieces[i].newlineCount;
        }

        // Erase the pieces
        if (startIdx < endIdx) {
            m_pieces.erase(m_pieces.begin() + startIdx, m_pieces.begin() + endIdx);
        }

        m_totalLength -= removeLen;
        m_totalNewlines -= removedNewlines;
        updateLineIndexAfterRemove_(removeStart, removeLen, removedNewlines);
    }

    // ─── Line access (uses line index when ready, otherwise computes) ───

    int lineForOffset(size_t pos) const {
        if (m_lineIndexReady.load(std::memory_order_acquire) && !m_lineIndex.isEmpty()) {
            return lineForOffsetFromIndex_(pos);
        }
        return lineForOffsetFromPieces_(pos);
    }

    size_t lineStart(int lineIdx) const {
        if (lineIdx <= 0) {
            return 0;
        }
        if (m_lineIndexReady.load(std::memory_order_acquire)) {
            if (lineIdx < static_cast<int>(m_lineIndex.size())) {
                return m_lineIndex[lineIdx].offset;
            }
            return m_totalLength;
        }
        return lineStartFromPieces_(lineIdx);
    }

    size_t lineLength(int lineIdx) const {
        const size_t start = lineStart(lineIdx);
        const size_t nextStart = lineStart(lineIdx + 1);
        if (nextStart <= start) {
            // Last line: from start to end of document
            return m_totalLength - start;
        }
        // Subtract 1 for the '\n' between this line and next
        return (nextStart > start) ? (nextStart - start - 1) : 0;
    }

    SwString lineContent(int lineIdx) const {
        const size_t start = lineStart(lineIdx);
        const size_t len = lineLength(lineIdx);
        if (len == 0 && start >= m_totalLength) {
            return SwString();
        }
        return substr(start, len);
    }

    // ─── Line index management ──────────────────────────────────────────

    // Build line index synchronously (call from main thread for small files)
    void buildLineIndex() {
        buildLineIndex_();
    }

    // Build line index from external data (call from thread pool worker)
    // The worker scans the original buffer and produces a SwVector<LineEntry>.
    // After edits, the index is updated incrementally.
    void setLineIndex(SwVector<LineEntry>&& index, int totalNewlines) {
        m_lineIndex = std::move(index);
        m_totalNewlines = totalNewlines;
        // Fix piece newline counts for original buffer (only if single original piece)
        if (m_pieces.size() == 1 && m_pieces[0].buffer == Original) {
            m_pieces[0].newlineCount = totalNewlines;
        }
        m_lineIndexReady.store(true, std::memory_order_release);
    }

    // Get raw access for background scanner
    const char* originalData() const { return m_originalData; }
    size_t originalSize() const { return m_originalSize; }

    // ─── Piece access (for debugging / testing) ─────────────────────────
    int pieceCount() const { return m_pieces.size(); }
    const Piece& pieceAt(int idx) const { return m_pieces[idx]; }

private:
    // ─── Buffer access helpers ──────────────────────────────────────────
    const char* bufferPtr_(BufferType type) const {
        return (type == Original) ? m_originalData : m_add.data();
    }

    char bufferChar_(BufferType type, size_t offset) const {
        if (type == Original) {
            return (offset < m_originalSize) ? m_originalData[offset] : '\0';
        }
        return (offset < m_add.size()) ? m_add[offset] : '\0';
    }

    // ─── Newline counting ───────────────────────────────────────────────
    static int countNewlines_(const char* data, size_t start, size_t length) {
        if (!data) {
            return 0;
        }
        int count = 0;
        const char* ptr = data + start;
        const char* end = ptr + length;
        while (ptr < end) {
            // Use memchr for fast scanning
            const void* found = std::memchr(ptr, '\n', static_cast<size_t>(end - ptr));
            if (!found) {
                break;
            }
            ++count;
            ptr = static_cast<const char*>(found) + 1;
        }
        return count;
    }

    // ─── Split piece at absolute position ───────────────────────────────
    // Returns the index of the piece that starts at `pos`.
    // If pos is at an existing boundary, no split is needed.
    int splitPieceAt_(size_t pos) {
        size_t cumulative = 0;
        for (int i = 0; i < m_pieces.size(); ++i) {
            if (cumulative == pos) {
                return i;
            }
            const Piece& p = m_pieces[i];
            if (cumulative + p.length > pos) {
                // Split this piece
                const size_t localOffset = pos - cumulative;
                Piece after{p.buffer, p.start + localOffset, p.length - localOffset, 0};
                after.newlineCount = countNewlines_(bufferPtr_(after.buffer), after.start, after.length);

                m_pieces[i].length = localOffset;
                m_pieces[i].newlineCount = countNewlines_(bufferPtr_(m_pieces[i].buffer),
                                                          m_pieces[i].start,
                                                          m_pieces[i].length);
                m_pieces.insert(m_pieces.begin() + i + 1, after);
                return i + 1;
            }
            cumulative += p.length;
        }
        return m_pieces.size();
    }

    // ─── Line index: build from scratch ─────────────────────────────────
    void buildLineIndex_() {
        m_lineIndex.clear();
        m_lineIndex.push_back(LineEntry{0}); // Line 0 starts at offset 0

        size_t cumulative = 0;
        int totalNL = 0;
        for (int i = 0; i < m_pieces.size(); ++i) {
            const Piece& p = m_pieces[i];
            const char* data = bufferPtr_(p.buffer) + p.start;
            for (size_t j = 0; j < p.length; ++j) {
                if (data[j] == '\n') {
                    ++totalNL;
                    m_lineIndex.push_back(LineEntry{cumulative + j + 1});
                }
            }
            cumulative += p.length;
        }

        m_totalNewlines = totalNL;
        m_lineIndexReady.store(true, std::memory_order_release);
    }

    // ─── Line index: incremental update after insert ────────────────────
    void updateLineIndexAfterInsert_(size_t pos, size_t insertedLen, int insertedNewlines) {
        if (!m_lineIndexReady.load(std::memory_order_acquire)) {
            return; // No index to update
        }

        // Find the line containing pos via binary search
        const int lineIdx = lineForOffsetFromIndex_(pos);

        if (insertedNewlines == 0) {
            // No new lines: just shift offsets after pos
            for (int i = lineIdx + 1; i < static_cast<int>(m_lineIndex.size()); ++i) {
                m_lineIndex[i].offset += insertedLen;
            }
            return;
        }

        // Insert new line entries for each newline in the inserted text
        // Scan the inserted text (it's at the end of m_add)
        const char* insertedData = m_add.data() + (m_add.size() - insertedLen);
        SwVector<LineEntry> newEntries;
        newEntries.reserve(static_cast<size_t>(insertedNewlines));
        for (size_t j = 0; j < insertedLen; ++j) {
            if (insertedData[j] == '\n') {
                newEntries.push_back(LineEntry{pos + j + 1});
            }
        }

        // Insert new entries after lineIdx
        const int insertAt = lineIdx + 1;
        // Rebuild once to avoid O(newlines * trailing_lines) repeated vector inserts.
        SwVector<LineEntry> updatedIndex;
        updatedIndex.reserve(static_cast<size_t>(m_lineIndex.size() + newEntries.size()));
        for (int i = 0; i < insertAt; ++i) {
            updatedIndex.push_back(m_lineIndex[i]);
        }
        for (int i = 0; i < static_cast<int>(newEntries.size()); ++i) {
            updatedIndex.push_back(newEntries[i]);
        }
        for (int i = insertAt; i < static_cast<int>(m_lineIndex.size()); ++i) {
            updatedIndex.push_back(LineEntry{m_lineIndex[i].offset + insertedLen});
        }
        m_lineIndex = std::move(updatedIndex);
    }

    // ─── Line index: incremental update after remove ────────────────────
    void updateLineIndexAfterRemove_(size_t pos, size_t removedLen, int removedNewlines) {
        if (!m_lineIndexReady.load(std::memory_order_acquire)) {
            return;
        }

        if (removedNewlines == 0) {
            // No lines removed: just shift offsets
            const int lineIdx = lineForOffsetFromIndex_(pos);
            for (int i = lineIdx + 1; i < static_cast<int>(m_lineIndex.size()); ++i) {
                m_lineIndex[i].offset -= removedLen;
            }
            return;
        }

        // Find lines that start within the removed range [pos+1, pos+removedLen]
        // These lines no longer exist
        const size_t removeEnd = pos + removedLen;
        int firstRemoved = -1;
        int lastRemoved = -1;
        for (int i = 0; i < static_cast<int>(m_lineIndex.size()); ++i) {
            const size_t off = m_lineIndex[i].offset;
            if (off > pos && off <= removeEnd) {
                if (firstRemoved < 0) {
                    firstRemoved = i;
                }
                lastRemoved = i;
            }
        }

        if (firstRemoved >= 0 && lastRemoved >= firstRemoved) {
            m_lineIndex.erase(m_lineIndex.begin() + firstRemoved,
                              m_lineIndex.begin() + lastRemoved + 1);
        }

        // Shift remaining lines after the removal point
        const int lineIdx = lineForOffsetFromIndex_(pos);
        for (int i = lineIdx + 1; i < static_cast<int>(m_lineIndex.size()); ++i) {
            m_lineIndex[i].offset -= removedLen;
        }
    }

    // ─── Line lookup from index (binary search) ─────────────────────────
    int lineForOffsetFromIndex_(size_t pos) const {
        const size_t clamped = std::min(pos, m_totalLength);
        // Binary search: find the last line whose offset <= clamped
        int lo = 0;
        int hi = static_cast<int>(m_lineIndex.size()) - 1;
        int result = 0;
        while (lo <= hi) {
            const int mid = lo + (hi - lo) / 2;
            if (m_lineIndex[mid].offset <= clamped) {
                result = mid;
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
        return result;
    }

    // ─── Line lookup from pieces (no index, O(P)) ──────────────────────
    int lineForOffsetFromPieces_(size_t pos) const {
        if (m_pieces.isEmpty()) {
            return 0;
        }
        const size_t clamped = std::min(pos, m_totalLength);
        int newlineCount = 0;
        size_t cumulative = 0;
        for (int i = 0; i < m_pieces.size(); ++i) {
            const Piece& p = m_pieces[i];
            if (cumulative + p.length <= clamped) {
                newlineCount += p.newlineCount;
                cumulative += p.length;
            } else {
                // Count newlines within this piece up to the position
                const size_t localEnd = clamped - cumulative;
                const char* data = bufferPtr_(p.buffer) + p.start;
                for (size_t j = 0; j < localEnd; ++j) {
                    if (data[j] == '\n') {
                        ++newlineCount;
                    }
                }
                break;
            }
        }
        return newlineCount;
    }

    // ─── Line start from pieces (no index, O(P)) ───────────────────────
    size_t lineStartFromPieces_(int lineIdx) const {
        if (lineIdx <= 0) {
            return 0;
        }
        int newlinesSeen = 0;
        size_t cumulative = 0;
        for (int i = 0; i < m_pieces.size(); ++i) {
            const Piece& p = m_pieces[i];
            if (newlinesSeen + p.newlineCount < lineIdx) {
                newlinesSeen += p.newlineCount;
                cumulative += p.length;
                continue;
            }
            // The target newline is within this piece
            const char* data = bufferPtr_(p.buffer) + p.start;
            for (size_t j = 0; j < p.length; ++j) {
                if (data[j] == '\n') {
                    ++newlinesSeen;
                    if (newlinesSeen == lineIdx) {
                        return cumulative + j + 1;
                    }
                }
            }
            cumulative += p.length;
        }
        return m_totalLength;
    }

    // ─── Unmap original buffer ──────────────────────────────────────────
    void unmapOriginal_() {
        if (!m_isMapped) {
            return;
        }
#ifdef _WIN32
        if (m_originalData) {
            ::UnmapViewOfFile(m_originalData);
        }
        if (m_mmapMapping) {
            ::CloseHandle(m_mmapMapping);
            m_mmapMapping = NULL;
        }
        if (m_mmapFile != INVALID_HANDLE_VALUE) {
            ::CloseHandle(m_mmapFile);
            m_mmapFile = INVALID_HANDLE_VALUE;
        }
#else
        if (m_mmapPtr && m_mmapPtr != MAP_FAILED) {
            ::munmap(m_mmapPtr, m_mmapSize);
            m_mmapPtr = nullptr;
            m_mmapSize = 0;
        }
#endif
        m_originalData = nullptr;
        m_originalSize = 0;
        m_isMapped = false;
    }

    // ─── Data members ───────────────────────────────────────────────────
    // Original buffer (either owned SwString or mmap'd pointer)
    SwString m_originalOwned;
    const char* m_originalData{nullptr};
    size_t m_originalSize{0};
    bool m_isMapped{false};

    // mmap handles
#ifdef _WIN32
    HANDLE m_mmapFile{INVALID_HANDLE_VALUE};
    HANDLE m_mmapMapping{NULL};
#else
    void* m_mmapPtr{nullptr};
    size_t m_mmapSize{0};
#endif

    // Add buffer (append-only)
    SwString m_add;

    // Piece descriptors
    SwVector<Piece> m_pieces;

    // Cached totals
    size_t m_totalLength{0};
    int m_totalNewlines{0};

    // Line index (built synchronously or by background worker)
    SwVector<LineEntry> m_lineIndex;
    std::atomic<bool> m_lineIndexReady{false};
};

/***************************************************************************************************
 * SwPieceTableLineIndexBuilder — Background line index scanner.
 *
 * Usage with SwThreadPool:
 *   auto* builder = new SwPieceTableLineIndexBuilder(&pieceTable);
 *   SwThreadPool::globalInstance()->start(builder);
 *   // builder auto-deletes; pieceTable.isLineIndexReady() becomes true when done.
 **************************************************************************************************/

#include "SwThreadPool.h"

class SwPieceTableLineIndexBuilder : public SwRunnable {
public:
    explicit SwPieceTableLineIndexBuilder(SwPieceTable* table)
        : m_table(table) {
        setAutoDelete(true);
    }

    void run() override {
        if (!m_table) {
            return;
        }
        const char* data = m_table->originalData();
        const size_t size = m_table->originalSize();
        if (!data || size == 0) {
            SwVector<SwPieceTable::LineEntry> index;
            index.push_back(SwPieceTable::LineEntry{0});
            m_table->setLineIndex(std::move(index), 0);
            return;
        }

        SwVector<SwPieceTable::LineEntry> index;
        index.reserve(static_cast<size_t>(size / 40 + 1)); // Estimate ~40 chars per line
        index.push_back(SwPieceTable::LineEntry{0}); // Line 0

        int totalNewlines = 0;
        const char* ptr = data;
        const char* end = data + size;
        while (ptr < end) {
            const void* found = std::memchr(ptr, '\n', static_cast<size_t>(end - ptr));
            if (!found) {
                break;
            }
            ++totalNewlines;
            const size_t nlPos = static_cast<size_t>(static_cast<const char*>(found) - data);
            index.push_back(SwPieceTable::LineEntry{nlPos + 1});
            ptr = static_cast<const char*>(found) + 1;
        }

        m_table->setLineIndex(std::move(index), totalNewlines);
    }

private:
    SwPieceTable* m_table{nullptr};
};
