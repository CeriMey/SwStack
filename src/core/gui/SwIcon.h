#pragma once

/**
 * @file src/core/gui/SwIcon.h
 * @brief Multi-resolution icon container (QIcon equivalent).
 *
 * Supports .ico files (Windows icon format with multiple embedded sizes),
 * .bmp (32-bit ARGB via SwImage), and raw SwImage data.
 *
 * Usage:
 * @code
 *   SwIcon icon("app.ico");              // load .ico with all resolutions
 *   SwIcon icon2(SwImage(myPixels));      // from single SwImage
 *
 *   SwImage img = icon.pixmap(16);        // best match for 16x16
 *   HICON hicon = icon.toHICON(32);       // native Win32 handle
 * @endcode
 */

#include "graphics/SwImage.h"
#include "SwString.h"

#include <vector>
#include <algorithm>
#include <fstream>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

class SwIcon {
public:
    SwIcon() = default;

    explicit SwIcon(const SwString& filePath) {
        load(filePath);
    }

    explicit SwIcon(const SwImage& image) {
        if (!image.isNull()) {
            Entry e;
            e.image = image;
            e.size = image.width();
            m_entries.push_back(e);
        }
    }

    // -- Loading --

    /**
     * @brief Load from file. Auto-detects .ico vs .bmp by extension and magic bytes.
     */
    bool load(const SwString& filePath) {
        m_entries.clear();
        m_filePath = filePath;

        std::string path = filePath.toStdString();
        std::ifstream in(path.c_str(), std::ios::binary);
        if (!in.is_open()) return false;

        // Read first 4 bytes to detect format
        uint8_t magic[4] = {};
        in.read(reinterpret_cast<char*>(magic), 4);
        in.seekg(0);

        // ICO: 00 00 01 00
        if (magic[0] == 0 && magic[1] == 0 && magic[2] == 1 && magic[3] == 0) {
            return loadIco_(in);
        }

        // BMP: 'BM'
        if (magic[0] == 'B' && magic[1] == 'M') {
            in.close();
            SwImage img;
            if (img.load(filePath)) {
                Entry e; e.image = img; e.size = img.width();
                m_entries.push_back(e);
                return true;
            }
        }

        // PNG: 89 50 4E 47
        if (magic[0] == 0x89 && magic[1] == 0x50 && magic[2] == 0x4E && magic[3] == 0x47) {
            // PNG not supported by SwImage yet — skip
            return false;
        }

        return false;
    }

    /**
     * @brief Add an image for a specific size.
     */
    void addImage(const SwImage& image, int size = 0) {
        if (image.isNull()) return;
        Entry e;
        e.image = image;
        e.size = (size > 0) ? size : image.width();
        m_entries.push_back(e);
    }

    // -- Query --

    bool isNull() const { return m_entries.empty() && m_filePath.isEmpty() && !m_exeIcon; }

    /** @brief Number of available resolutions. */
    int availableSizeCount() const { return static_cast<int>(m_entries.size()); }

    /** @brief Get the available sizes. */
    std::vector<int> availableSizes() const {
        std::vector<int> sizes;
        for (const auto& e : m_entries) sizes.push_back(e.size);
        return sizes;
    }

    /**
     * @brief Get the best-matching image for the requested size.
     * Returns the closest match (prefers larger over smaller).
     */
    SwImage pixmap(int requestedSize = 0) const {
        if (m_entries.empty()) return {};
        if (requestedSize <= 0) return m_entries[0].image;

        // Find exact match first
        for (const auto& e : m_entries) {
            if (e.size == requestedSize) return e.image;
        }

        // Find closest larger
        const Entry* best = nullptr;
        int bestDiff = 999999;
        for (const auto& e : m_entries) {
            int diff = e.size - requestedSize;
            if (diff >= 0 && diff < bestDiff) {
                bestDiff = diff;
                best = &e;
            }
        }
        if (best) return best->image;

        // Fallback: closest smaller
        best = &m_entries[0];
        bestDiff = 999999;
        for (const auto& e : m_entries) {
            int diff = requestedSize - e.size;
            if (diff >= 0 && diff < bestDiff) {
                bestDiff = diff;
                best = &e;
            }
        }
        return best->image;
    }

    /** @brief Original file path (empty if constructed from SwImage). */
    SwString filePath() const { return m_filePath; }

    // -- Native handles --

#ifdef _WIN32
    /**
     * @brief Create a Win32 HICON from the best-matching resolution.
     * Caller does NOT own the handle — it is cached internally.
     */
    HICON toHICON(int size = 0) const {
        if (m_exeIcon) {
            const int iconSize = (size > 0) ? size : GetSystemMetrics(SM_CXSMICON);
            HICON h = loadApplicationHIcon_(iconSize);
            if (h) return h;
        }

        // If loaded from .ico file, use LoadImage for best quality
        if (!m_filePath.isEmpty()) {
            int cx = (size > 0) ? size : GetSystemMetrics(SM_CXSMICON);
            int cy = (size > 0) ? size : GetSystemMetrics(SM_CYSMICON);
            std::wstring wpath(m_filePath.toStdString().begin(), m_filePath.toStdString().end());
            HICON h = (HICON)LoadImageW(nullptr, wpath.c_str(), IMAGE_ICON, cx, cy, LR_LOADFROMFILE);
            if (h) return h;
        }

        // Fallback: create from SwImage pixel data
        SwImage img = pixmap(size > 0 ? size : 16);
        if (img.isNull()) return LoadIconW(nullptr, MAKEINTRESOURCEW(32512));

        return createHIconFromImage_(img);
    }
#endif

    // -- Static constructors --

    static SwIcon fromImage(const SwImage& img) { return SwIcon(img); }

    static SwIcon applicationIcon() {
#ifdef _WIN32
        SwIcon icon;
        icon.m_exeIcon = true;
        return icon;
#else
        return SwIcon();
#endif
    }

private:
    struct Entry {
        SwImage image;
        int size = 0;
    };

    std::vector<Entry> m_entries;
    SwString m_filePath;
    bool m_exeIcon = false;

    // -- ICO format parser --

    bool loadIco_(std::ifstream& in) {
        // ICONDIR header
        uint16_t reserved = readU16_(in);
        uint16_t type = readU16_(in);
        uint16_t count = readU16_(in);
        (void)reserved;

        if (type != 1 || count == 0 || count > 256) return false;

        // ICONDIRENTRY array
        struct IcoEntry {
            uint8_t  width;
            uint8_t  height;
            uint8_t  colorCount;
            uint8_t  reserved;
            uint16_t planes;
            uint16_t bitCount;
            uint32_t bytesInRes;
            uint32_t imageOffset;
        };

        std::vector<IcoEntry> entries(count);
        for (int i = 0; i < count; i++) {
            entries[i].width      = readU8_(in);
            entries[i].height     = readU8_(in);
            entries[i].colorCount = readU8_(in);
            entries[i].reserved   = readU8_(in);
            entries[i].planes     = readU16_(in);
            entries[i].bitCount   = readU16_(in);
            entries[i].bytesInRes = readU32_(in);
            entries[i].imageOffset = readU32_(in);
        }

        // Parse each embedded image
        for (int i = 0; i < count; i++) {
            int w = entries[i].width == 0 ? 256 : entries[i].width;
            int h = entries[i].height == 0 ? 256 : entries[i].height;

            in.seekg(entries[i].imageOffset);
            if (!in.good()) continue;

            // Check if embedded image is PNG (89 50 4E 47) or BMP DIB
            uint8_t sig[4];
            in.read(reinterpret_cast<char*>(sig), 4);
            in.seekg(entries[i].imageOffset);

            if (sig[0] == 0x89 && sig[1] == 0x50) {
                // PNG embedded — skip (SwImage doesn't support PNG yet)
                continue;
            }

            // BMP DIB (BITMAPINFOHEADER without file header)
            SwImage img = loadIcoDib_(in, w, h, entries[i].bytesInRes);
            if (!img.isNull()) {
                Entry e; e.image = img; e.size = w;
                m_entries.push_back(e);
            }
        }

        return !m_entries.empty();
    }

    SwImage loadIcoDib_(std::ifstream& in, int expectedW, int expectedH, uint32_t dataSize) {
        // Read BITMAPINFOHEADER
        uint32_t headerSize = readU32_(in);
        if (headerSize < 40) return {};

        int32_t  w = static_cast<int32_t>(readU32_(in));
        int32_t  h = static_cast<int32_t>(readU32_(in));
        uint16_t planes = readU16_(in);
        uint16_t bpp = readU16_(in);
        uint32_t compression = readU32_(in);
        /* skip rest of header */
        for (uint32_t skip = 24; skip < headerSize - 16; skip++) readU8_(in);

        (void)planes; (void)compression;

        // ICO DIB height is double (includes mask)
        int realH = h / 2;
        if (w <= 0 || realH <= 0) return {};

        if (bpp != 32) {
            // Only 32-bit ARGB supported for now
            return {};
        }

        SwImage img(w, realH, SwImage::Format_ARGB32);
        if (img.isNull()) return {};

        // Read pixel data (bottom-up)
        int rowBytes = w * 4;
        for (int y = realH - 1; y >= 0; y--) {
            uint8_t* dst = reinterpret_cast<uint8_t*>(img.scanLine(y));
            in.read(reinterpret_cast<char*>(dst), rowBytes);
        }

        // Skip AND mask (1-bit, we use alpha channel instead)

        return img;
    }

#ifdef _WIN32
    static HICON loadApplicationHIcon_(int size) {
        wchar_t exePath[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) {
            return nullptr;
        }

        HICON extracted = nullptr;
        const UINT extractedCount = PrivateExtractIconsW(exePath,
                                                         0,
                                                         size,
                                                         size,
                                                         &extracted,
                                                         nullptr,
                                                         1,
                                                         0);
        if (extractedCount > 0 && extracted) {
            return extracted;
        }

        HICON largeIcon = nullptr;
        HICON smallIcon = nullptr;
        const UINT iconCount = ExtractIconExW(exePath, 0, &largeIcon, &smallIcon, 1);
        if (iconCount == 0) {
            return nullptr;
        }

        const int smallMetric = GetSystemMetrics(SM_CXSMICON);
        if (size <= smallMetric && smallIcon) {
            return smallIcon;
        }
        return largeIcon ? largeIcon : smallIcon;
    }

    static HICON createHIconFromImage_(const SwImage& img) {
        int w = img.width();
        int h = img.height();

        // Create DIB section
        BITMAPV5HEADER bi = {};
        bi.bV5Size = sizeof(bi);
        bi.bV5Width = w;
        bi.bV5Height = -h; // top-down
        bi.bV5Planes = 1;
        bi.bV5BitCount = 32;
        bi.bV5Compression = BI_BITFIELDS;
        bi.bV5RedMask   = 0x00FF0000;
        bi.bV5GreenMask = 0x0000FF00;
        bi.bV5BlueMask  = 0x000000FF;
        bi.bV5AlphaMask = 0xFF000000;

        HDC hdc = GetDC(nullptr);
        void* bits = nullptr;
        HBITMAP hBitmap = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&bi),
                                           DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, hdc);
        if (!hBitmap || !bits) return LoadIconW(nullptr, MAKEINTRESOURCEW(32512));

        std::memcpy(bits, img.constBits(), static_cast<size_t>(w * h * 4));

        HBITMAP hMask = CreateBitmap(w, h, 1, 1, nullptr);

        ICONINFO ii = {};
        ii.fIcon = TRUE;
        ii.hbmMask = hMask;
        ii.hbmColor = hBitmap;
        HICON hIcon = CreateIconIndirect(&ii);

        DeleteObject(hBitmap);
        DeleteObject(hMask);

        return hIcon ? hIcon : LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    }
#endif

    // -- Binary readers --
    static uint8_t  readU8_(std::ifstream& in)  { return static_cast<uint8_t>(in.get()); }
    static uint16_t readU16_(std::ifstream& in) { uint8_t b[2]; in.read(reinterpret_cast<char*>(b), 2); return b[0] | (uint16_t(b[1]) << 8); }
    static uint32_t readU32_(std::ifstream& in) { uint8_t b[4]; in.read(reinterpret_cast<char*>(b), 4); return b[0] | (uint32_t(b[1])<<8) | (uint32_t(b[2])<<16) | (uint32_t(b[3])<<24); }
};
