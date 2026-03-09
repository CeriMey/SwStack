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

/**
 * @file SwFileDialog.h
 * @ingroup core_gui
 * @brief Declares `SwFileDialog`, a custom modal file-system browser for the Sw GUI toolkit.
 *
 * @details
 * This dialog provides a toolkit-native alternative to operating-system file choosers when the
 * application needs deterministic rendering, snapshot testing, or styling consistency with the rest
 * of the Sw widget set. The implementation combines several responsibilities in a single reusable
 * component:
 * - a navigation toolbar with back, forward, up, home, refresh, and editable path entry,
 * - a sidebar for well-known locations and drive shortcuts,
 * - a custom-painted central file list optimized for toolkit-controlled rendering,
 * - optional filter parsing compatible with common desktop dialog patterns,
 * - platform-aware icon lookup with caching to avoid repeated shell queries.
 *
 * The class keeps the public API intentionally close to conventional desktop file dialogs through
 * `FileMode`, `AcceptMode`, and the static convenience helpers such as `getOpenFileName()`.
 */

#include "SwDialog.h"
#include "SwComboBox.h"
#include "SwLabel.h"
#include "SwLineEdit.h"
#include "SwPushButton.h"
#include "SwScrollBar.h"
#include "SwToolButton.h"
#include "SwTreeWidget.h"

#include "platform/SwPlatformSelector.h"
#include "core/types/SwDateTime.h"
#include "core/types/SwVector.h"

#include "graphics/SwImage.h"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <string>
#include <unordered_map>

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#endif

/**
 * @class SwFileDialog
 * @brief Modal file dialog that browses directories and returns a file or directory selection.
 *
 * @details
 * `SwFileDialog` owns its full browsing experience. Rather than delegating to a native dialog, it
 * composes ordinary Sw widgets and a custom list view to provide predictable behavior across
 * platforms. Its internal workflow is organized around these steps:
 * - navigation methods update `m_directory` and optional history stacks,
 * - `refreshEntries()` enumerates directories and files for the active location,
 * - filter expressions are parsed into explicit wildcard patterns,
 * - the custom `FileListView` paints the current directory content and exposes activation signals,
 * - `acceptSelection()` normalizes the final path according to the selected mode.
 *
 * This makes the class suitable for both interactive desktop usage and environments where a fully
 * native dialog would be difficult to theme or capture reliably.
 */
class SwFileDialog : public SwDialog {
    SW_OBJECT(SwFileDialog, SwDialog)

public:
    /**
     * @enum FileMode
     * @brief Describes what kind of selection the dialog is expected to return.
     */
    enum FileMode {
        AnyFile,       ///< Accept any file path, including a path that does not yet exist.
        ExistingFile,  ///< Restrict selection to an existing file.
        Directory,     ///< Restrict selection to a directory.
        ExistingFiles  ///< Reserved for multi-file workflows; the current API still exposes one path.
    };

    /**
     * @enum AcceptMode
     * @brief Controls the user-facing intent of the confirmation action.
     */
    enum AcceptMode {
        AcceptOpen, ///< Dialog confirms an "open" workflow.
        AcceptSave  ///< Dialog confirms a "save" workflow.
    };

    /**
     * @brief Constructs the dialog and builds its browsing UI.
     * @param parent Optional parent widget.
     *
     * @details
     * The constructor initializes the non-native widget layout, enables modal behavior, requests a
     * native host window when available, and focuses the file list or path editor when the dialog is
     * shown so keyboard navigation works immediately.
     */
    explicit SwFileDialog(SwWidget* parent = nullptr)
        : SwDialog(parent) {
        setWindowTitle("Open file");
        resize(900, 560);
        buildUi();
        setModal(true);
        setUseNativeWindow(true);

        SwObject::connect(this, &SwDialog::shown, this, [this]() {
            if (m_view) {
                m_view->setFocus(true);
            } else if (m_pathEdit) {
                m_pathEdit->setFocus(true);
            }
        });
    }

    /**
     * @brief Navigates the dialog to a new directory without recording a history entry.
     * @param dir Target directory or synthetic location token such as `"Computer"`.
     */
    void setDirectory(const SwString& dir) {
        navigateTo(dir, false);
    }

    /**
     * @brief Returns the directory currently displayed by the dialog.
     * @return Active browsing location.
     */
    SwString directory() const { return m_directory; }

    /**
     * @brief Installs the name-filter expression used to reduce visible files.
     * @param filter Human-readable filter string such as `"Images (*.png *.jpg);;All Files (*)"`.
     *
     * @details
     * The dialog parses the provided string into filter entries and immediately refreshes the file
     * list so only matching file names remain visible for file-selection modes.
     */
    void setNameFilter(const SwString& filter) {
        m_filter = filter;
        rebuildFilterOptions();
        refreshEntries();
    }

    /**
     * @brief Returns the original filter expression configured for the dialog.
     * @return Filter string previously supplied through `setNameFilter()`.
     */
    SwString nameFilter() const { return m_filter; }

    /**
     * @brief Changes the semantic type of the selection expected from the user.
     * @param mode New file mode.
     *
     * @details
     * The mode updates labels, button text, and selection validation rules, then refreshes the
     * directory listing so file-only filters can be applied or relaxed as needed.
     */
    void setFileMode(FileMode mode) {
        m_fileMode = mode;
        updateUiTexts();
        refreshEntries();
    }

    /**
     * @brief Returns the current file mode.
     * @return Active `FileMode` value.
     */
    FileMode fileMode() const { return m_fileMode; }

    /**
     * @brief Changes whether the dialog behaves like an open or save dialog.
     * @param mode New accept mode.
     */
    void setAcceptMode(AcceptMode mode) {
        m_acceptMode = mode;
        updateUiTexts();
    }

    /**
     * @brief Returns the current accept mode.
     * @return Active `AcceptMode` value.
     */
    AcceptMode acceptMode() const { return m_acceptMode; }

    /**
     * @brief Preselects a path in the file-name editor.
     * @param file File or directory path to expose as the current selection.
     *
     * @details
     * This method does not force navigation on its own; it updates the selection model and mirrors
     * the provided text into the editable path field when present.
     */
    void selectFile(const SwString& file) {
        m_selectedFile = file;
        if (m_pathEdit) {
            m_pathEdit->setText(file);
        }
    }

    /**
     * @brief Returns the path currently stored as the dialog result.
     * @return Normalized selection path.
     */
    SwString selectedFile() const { return m_selectedFile; }

    /**
     * @brief Opens a modal dialog configured for choosing an existing file.
     * @param parent Optional parent widget.
     * @param caption Dialog title.
     * @param dir Initial directory.
     * @param filter Optional name-filter expression.
     * @return Selected file path, or an empty string when cancelled.
     */
    static SwString getOpenFileName(SwWidget* parent,
                                    const SwString& caption = "Open file",
                                    const SwString& dir = SwString(),
                                    const SwString& filter = SwString()) {
        SwFileDialog dlg(parent);
        dlg.setWindowTitle(caption);
        dlg.setDirectory(dir);
        dlg.setNameFilter(filter);
        dlg.setFileMode(ExistingFile);
        dlg.setAcceptMode(AcceptOpen);
        const int res = dlg.exec();
        if (res == Accepted) {
            return dlg.selectedFile();
        }
        return {};
    }

    /**
     * @brief Opens a modal dialog configured for save-style file selection.
     * @param parent Optional parent widget.
     * @param caption Dialog title.
     * @param dir Initial directory.
     * @param filter Optional name-filter expression.
     * @return Selected output path, or an empty string when cancelled.
     */
    static SwString getSaveFileName(SwWidget* parent,
                                    const SwString& caption = "Save file",
                                    const SwString& dir = SwString(),
                                    const SwString& filter = SwString()) {
        SwFileDialog dlg(parent);
        dlg.setWindowTitle(caption);
        dlg.setDirectory(dir);
        dlg.setNameFilter(filter);
        dlg.setFileMode(AnyFile);
        dlg.setAcceptMode(AcceptSave);
        const int res = dlg.exec();
        if (res == Accepted) {
            return dlg.selectedFile();
        }
        return {};
    }

    /**
     * @brief Opens a modal dialog restricted to directory selection.
     * @param parent Optional parent widget.
     * @param caption Dialog title.
     * @param dir Initial directory.
     * @return Selected directory path, or an empty string when cancelled.
     */
    static SwString getExistingDirectory(SwWidget* parent,
                                         const SwString& caption = "Select directory",
                                         const SwString& dir = SwString()) {
        SwFileDialog dlg(parent);
        dlg.setWindowTitle(caption);
        dlg.setDirectory(dir);
        dlg.setFileMode(Directory);
        dlg.setAcceptMode(AcceptOpen);
        const int res = dlg.exec();
        if (res == Accepted) {
            return dlg.selectedFile();
        }
        return {};
    }

protected:
    /**
     * @brief Recomputes the layout of toolbar, sidebar, list view, and footer controls.
     * @param event Resize event forwarded by the base dialog.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwDialog::resizeEvent(event);
        updateLayout();
    }

private:
    enum SortKey {
        SortByName = 0,
        SortByType = 1,
        SortByModified = 2,
        SortBySize = 3
    };

    /**
     * @brief Internal representation of one visible row in the browser.
     *
     * @details
     * Each entry contains the display name, resolved path, type information, optional size, and a
     * precomputed icon so the custom list view can paint rows without re-querying the file system.
     */
    struct BrowserEntry {
        SwString name;
        SwString path;
        bool isDir{false};
        bool isDrive{false};
        SwString typeName;
        size_t size{0};
        SwDateTime modifiedAt{static_cast<std::time_t>(0)};
        bool hasModifiedAt{false};
        SwImage icon;
    };

    /**
     * @brief Parsed form of one user-selectable filter combo-box entry.
     *
     * @details
     * The label is the human-readable caption shown to the user, while `patterns` stores the
     * wildcard expressions that `refreshEntries()` applies when listing files.
     */
    struct FilterOption {
        SwString label;
        SwStringList patterns; // ex: "*.png", "*.jpg" (empty => all)
    };

    /**
     * @brief Small cache that resolves platform-specific icons for files, folders, and drives.
     *
     * @details
     * On Windows the cache asks the shell for real icons and memoizes the resulting `SwImage`
     * objects by semantic key. On other platforms it falls back to generated placeholder graphics so
     * the dialog remains usable without native shell integration.
     */
    class SystemIconCache {
    public:
        /**
         * @brief Performs the `iconFor` operation.
         * @param isDir Value passed to the method.
         * @param isDrive Value passed to the method.
         * @param name Value passed to the method.
         * @param path Path used by the operation.
         * @param sizePx Value passed to the method.
         * @return The requested icon For.
         */
        SwImage iconFor(bool isDir,
                        bool isDrive,
                        const SwString& name,
                        const SwString& path,
                        int sizePx) {
            const SwString key = cacheKey(isDir, isDrive, name, path, sizePx);
            const std::string k = key.toStdString();
            auto it = m_cache.find(k);
            if (it != m_cache.end()) {
                return it->second;
            }

            SwImage img;
#if defined(_WIN32)
            if (isDrive && !path.trimmed().isEmpty()) {
                img = loadShellIconForPath(path, sizePx);
            } else if (isDir) {
                img = loadShellIconForAttributes("folder", FILE_ATTRIBUTE_DIRECTORY, sizePx);
            } else {
                const SwString ext = extensionLower(name);
                if (!ext.isEmpty()) {
                    img = loadShellIconForAttributes(SwString("file") + ext, FILE_ATTRIBUTE_NORMAL, sizePx);
                } else {
                    img = loadShellIconForAttributes("file", FILE_ATTRIBUTE_NORMAL, sizePx);
                }
            }
#else
            img = makeFallbackIcon(isDir, sizePx);
#endif

            if (img.isNull()) {
                img = makeFallbackIcon(isDir, sizePx);
            }

            m_cache.emplace(k, img);
            return img;
        }

        /**
         * @brief Clears the current object state.
         */
        void clear() { m_cache.clear(); }

        // Load the shell icon for the actual path (gives specific icons for Desktop, Documentsâ€¦)
        /**
         * @brief Performs the `iconForActualPath` operation.
         * @param path Path used by the operation.
         * @param sizePx Value passed to the method.
         * @return The requested icon For Actual Path.
         */
        SwImage iconForActualPath(const SwString& path, int sizePx) {
#if defined(_WIN32)
            if (!path.isEmpty()) {
                SwImage img = loadShellIconForPath(path, sizePx);
                if (!img.isNull()) return img;
            }
#endif
            return makeFallbackIcon(true, sizePx);
        }

        // Load the house icon for the user home folder
        /**
         * @brief Performs the `iconForHome` operation.
         * @param sizePx Value passed to the method.
         * @return The requested icon For Home.
         */
        SwImage iconForHome(int sizePx) {
#if defined(_WIN32)
            // Primary: shell32.dll,-51380 = Windows 11 Home orange house icon
            // (registered under CLSID {f874310e-b6b7-47dc-bc84-b9e6b38f5903} DefaultIcon)
            {
                wchar_t dllPath[MAX_PATH] = {};
                GetSystemDirectoryW(dllPath, MAX_PATH);
                PathAppendW(dllPath, L"shell32.dll");
                HICON hLarge = nullptr, hSmall = nullptr;
                if (SHDefExtractIconW(dllPath, -51380, 0,
                                      &hLarge, &hSmall,
                                      MAKELONG(sizePx, sizePx)) == S_OK) {
                    HICON icon = (sizePx <= 16 && hSmall) ? hSmall : hLarge;
                    SwImage img = iconToImage(icon, sizePx);
                    if (hLarge) DestroyIcon(hLarge);
                    if (hSmall && hSmall != hLarge) DestroyIcon(hSmall);
                    if (!img.isNull()) return img;
                }
            }
            // Fallback: PIDL of user profile folder
            {
                PIDLIST_ABSOLUTE pidl = nullptr;
                if (SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_Profile, 0, nullptr, &pidl))) {
                    SHFILEINFOW sfi;
                    std::memset(&sfi, 0, sizeof(sfi));
                    const UINT flags = SHGFI_PIDL | SHGFI_ICON
                                     | ((sizePx <= 16) ? SHGFI_SMALLICON : SHGFI_LARGEICON);
                    const DWORD_PTR ok = SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl),
                                                        0, &sfi, sizeof(sfi), flags);
                    ILFree(pidl);
                    if (ok) {
                        SwImage img = iconToImage(sfi.hIcon, sizePx);
                        if (sfi.hIcon) DestroyIcon(sfi.hIcon);
                        if (!img.isNull()) return img;
                    }
                }
            }
#endif
            return makeFallbackIcon(true, sizePx);
        }

        // Load the icon for "This PC" (Computer)
        /**
         * @brief Performs the `iconForComputer` operation.
         * @param sizePx Value passed to the method.
         * @return The requested icon For Computer.
         */
        SwImage iconForComputer(int sizePx) {
#if defined(_WIN32)
            // Primary: SHGetStockIconInfo with SIID_MYCOMPUTER=104 (most reliable)
            SHSTOCKICONINFO sii;
            std::memset(&sii, 0, sizeof(sii));
            sii.cbSize = sizeof(sii);
            const UINT siFlags = SHGSI_ICON | ((sizePx <= 16) ? SHGSI_SMALLICON : SHGSI_LARGEICON);
            if (SUCCEEDED(SHGetStockIconInfo(static_cast<SHSTOCKICONID>(104), siFlags, &sii))) {
                SwImage img = iconToImage(sii.hIcon, sizePx);
                if (sii.hIcon) DestroyIcon(sii.hIcon);
                if (!img.isNull()) return img;
            }
            // Fallback: CLSID virtual folder path
            SHFILEINFOW sfi;
            std::memset(&sfi, 0, sizeof(sfi));
            if (SHGetFileInfoW(L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}",
                               0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON) != 0) {
                SwImage img = iconToImage(sfi.hIcon, sizePx);
                if (sfi.hIcon) DestroyIcon(sfi.hIcon);
                if (!img.isNull()) return img;
            }
#endif
            return makeFallbackIcon(false, sizePx);
        }

    private:
        static SwString cacheKey(bool isDir,
                                 bool isDrive,
                                 const SwString& name,
                                 const SwString& path,
                                 int sizePx) {
            if (isDrive) {
                return SwString("drive:%1:%2").arg(path.trimmed()).arg(sizePx);
            }
            if (isDir) {
                return SwString("dir:%1").arg(sizePx);
            }
            const SwString ext = extensionLower(name);
            return SwString("file:%1:%2").arg(ext.isEmpty() ? SwString("none") : ext).arg(sizePx);
        }

        static SwString extensionLower(const SwString& name) {
            const size_t dot = name.lastIndexOf('.');
            if (dot == static_cast<size_t>(-1)) {
                return {};
            }
            return name.substr(dot).toLower();
        }

#if defined(_WIN32)
        static SwImage loadShellIconForAttributes(const SwString& pseudoPath, DWORD attrs, int sizePx) {
            SHFILEINFOW sfi;
            std::memset(&sfi, 0, sizeof(sfi));

            UINT flags = SHGFI_ICON | SHGFI_USEFILEATTRIBUTES;
            flags |= (sizePx <= 16) ? SHGFI_SMALLICON : SHGFI_LARGEICON;

            const std::wstring w = pseudoPath.toStdWString();
            if (SHGetFileInfoW(w.c_str(), attrs, &sfi, sizeof(sfi), flags) == 0) {
                return SwImage();
            }
            SwImage img = iconToImage(sfi.hIcon, sizePx);
            if (sfi.hIcon) {
                DestroyIcon(sfi.hIcon);
            }
            return img;
        }

        static SwImage loadShellIconForPath(const SwString& path, int sizePx) {
            SHFILEINFOW sfi;
            std::memset(&sfi, 0, sizeof(sfi));

            UINT flags = SHGFI_ICON;
            flags |= (sizePx <= 16) ? SHGFI_SMALLICON : SHGFI_LARGEICON;

            const std::wstring w = path.toStdWString();

            // For network drives, NEVER let SHGetFileInfoW touch the path physically â€”
            // it will block or crash on disconnected shares even with SetErrorMode.
            // Use SHGFI_USEFILEATTRIBUTES so the shell infers the icon from attributes only.
            DWORD fileAttrs = 0;
            if (w.size() >= 3 && w[1] == L':' && (w[2] == L'\\' || w[2] == L'/')) {
                wchar_t root[4] = { w[0], L':', L'\\', L'\0' };
                if (GetDriveTypeW(root) == DRIVE_REMOTE) {
                    flags |= SHGFI_USEFILEATTRIBUTES;
                    fileAttrs = FILE_ATTRIBUTE_DIRECTORY;
                }
            }

            const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
            const DWORD_PTR ok = SHGetFileInfoW(w.c_str(), fileAttrs, &sfi, sizeof(sfi), flags);
            SetErrorMode(oldMode);
            if (ok == 0) {
                return SwImage();
            }
            SwImage img = iconToImage(sfi.hIcon, sizePx);
            if (sfi.hIcon) {
                DestroyIcon(sfi.hIcon);
            }
            return img;
        }

        static SwImage iconToImage(HICON icon, int sizePx) {
            if (!icon || sizePx <= 0) {
                return SwImage();
            }

            BITMAPINFO bmi;
            std::memset(&bmi, 0, sizeof(bmi));
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = sizePx;
            bmi.bmiHeader.biHeight = -sizePx; // top-down
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void* pixels = nullptr;
            HDC screen = GetDC(nullptr);
            HBITMAP dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &pixels, nullptr, 0);
            HDC memdc = CreateCompatibleDC(screen);
            ReleaseDC(nullptr, screen);

            if (!dib || !memdc || !pixels) {
                if (memdc) DeleteDC(memdc);
                if (dib) DeleteObject(dib);
                return SwImage();
            }

            HGDIOBJ old = SelectObject(memdc, dib);
            std::memset(pixels, 0, static_cast<size_t>(sizePx) * static_cast<size_t>(sizePx) * 4u);

            DrawIconEx(memdc, 0, 0, icon, sizePx, sizePx, 0, nullptr, DI_NORMAL);

            SwImage img(sizePx, sizePx, SwImage::Format_ARGB32);
            if (!img.isNull()) {
                std::memcpy(img.bits(), pixels, static_cast<size_t>(sizePx) * static_cast<size_t>(sizePx) * 4u);
            }

            SelectObject(memdc, old);
            DeleteDC(memdc);
            DeleteObject(dib);
            return img;
        }
#endif

        static SwImage makeFallbackIcon(bool isDir, int sizePx) {
            SwImage img(sizePx, sizePx, SwImage::Format_ARGB32);
            if (img.isNull()) {
                return img;
            }
            img.fill(0x00000000u);

            if (isDir) {
                // Folder: tab on top-left + body
                const std::uint32_t fill   = 0xFF60A5FAu; // blue-400
                const std::uint32_t tab    = 0xFF93C5FDu; // blue-300
                const std::uint32_t border = 0xFF1D4ED8u; // blue-700
                const int pad   = std::max(1, sizePx / 10);
                const int tabW  = std::max(2, sizePx * 5 / 10);
                const int tabH  = std::max(2, sizePx / 5);
                const int bodyY = pad + tabH - 1;
                const int bodyH = sizePx - bodyY - pad;
                for (int y = pad; y < pad + tabH; ++y) {
                    std::uint32_t* row = img.scanLine(y);
                    if (!row) continue;
                    for (int x = pad; x < pad + tabW; ++x) {
                        const bool isBorder = (x == pad || x == pad + tabW - 1 || y == pad);
                        row[x] = isBorder ? border : tab;
                    }
                }
                for (int y = bodyY; y < bodyY + bodyH; ++y) {
                    std::uint32_t* row = img.scanLine(y);
                    if (!row) continue;
                    for (int x = pad; x < sizePx - pad; ++x) {
                        const bool isBorder = (x == pad || x == sizePx - pad - 1 || y == bodyY || y == bodyY + bodyH - 1);
                        row[x] = isBorder ? border : fill;
                    }
                }
            } else {
                // File: rectangle with folded top-right corner
                const std::uint32_t fill   = 0xFFF1F5F9u; // slate-100
                const std::uint32_t border = 0xFF64748Bu; // slate-500
                const std::uint32_t fold   = 0xFFCBD5E1u; // slate-300
                const int pad      = std::max(1, sizePx / 10);
                const int foldSize = std::max(2, sizePx / 4);
                for (int y = pad; y < sizePx - pad; ++y) {
                    std::uint32_t* row = img.scanLine(y);
                    if (!row) continue;
                    for (int x = pad; x < sizePx - pad; ++x) {
                        const bool inFold = (x >= sizePx - pad - foldSize) &&
                                            (y < pad + foldSize) &&
                                            ((x - (sizePx - pad - foldSize)) + (y - pad) < foldSize);
                        if (inFold) { row[x] = fold; continue; }
                        const bool isBorder = (x == pad || x == sizePx - pad - 1 || y == pad || y == sizePx - pad - 1);
                        row[x] = isBorder ? border : fill;
                    }
                }
            }
            return img;
        }

        std::unordered_map<std::string, SwImage> m_cache;
    };

    /**
     * @brief Custom-painted list view used by the dialog to display directory contents.
     *
     * @details
     * The widget owns row selection, hover tracking, wheel scrolling, and double-click activation.
     * It deliberately avoids a heavier general-purpose item view so the file dialog can keep tight
     * control over visuals, sizing, and interactions.
     */
    class FileListView final : public SwWidget {
        SW_OBJECT(FileListView, SwWidget)

    public:
        /**
         * @brief Constructs the list view with default row sizing and scroll-bar wiring.
         * @param parent Optional parent widget.
         */
        explicit FileListView(SwWidget* parent = nullptr)
            : SwWidget(parent) {
            initDefaults();
        }

        /**
         * @brief Replaces the visible row set and resets transient state.
         * @param entries New directory entries to display.
         * @param statusMessage Optional placeholder message shown when no rows are available.
         */
        void setEntries(const SwVector<BrowserEntry>& entries, const SwString& statusMessage = SwString()) {
            m_entries = entries;
            m_statusMessage = statusMessage;
            m_current = m_entries.isEmpty() ? -1 : 0;
            m_hover = -1;
            resetScroll();
            currentChanged(m_current);
            update();
        }

        /**
         * @brief Returns the currently selected row index.
         * @return Zero-based row index, or `-1` when nothing is selected.
         */
        int currentIndex() const { return m_current; }

        /**
         * @brief Returns the number of visible entries.
         * @return Entry count currently held by the list.
         */
        int entryCount() const { return m_entries.size(); }

        /**
         * @brief Returns the currently selected entry.
         * @return Pointer to the selected entry, or `nullptr` when the selection is invalid.
         */
        const BrowserEntry* currentEntry() const {
            if (m_current < 0 || m_current >= m_entries.size()) {
                return nullptr;
            }
            return &m_entries[static_cast<SwVector<BrowserEntry>::size_type>(m_current)];
        }

        /**
         * @brief Returns the entry stored at a given row index.
         * @param index Row index to inspect.
         * @return Pointer to the entry, or `nullptr` when the index is out of range.
         */
        const BrowserEntry* entryAt(int index) const {
            if (index < 0 || index >= m_entries.size()) {
                return nullptr;
            }
            return &m_entries[static_cast<SwVector<BrowserEntry>::size_type>(index)];
        }

        /**
         * @brief Changes the selected row.
         * @param index Target row index, clamped to the current entry range.
         */
        void setCurrentIndex(int index) {
            const int clamped = (index < 0) ? -1 : (index >= m_entries.size() ? (m_entries.size() - 1) : index);
            if (m_current == clamped) {
                return;
            }
            m_current = clamped;
            currentChanged(m_current);
            ensureCurrentVisible();
            update();
        }

        /**
         * @brief Returns the vertical scroll bar used for the list viewport.
         * @return Internal scroll bar widget.
         */
        SwScrollBar* verticalScrollBar() const { return m_vBar; }

        /**
         * @brief Returns the horizontal scroll bar used for the list viewport.
         * @return Internal scroll bar widget.
         */
        SwScrollBar* horizontalScrollBar() const { return m_hBar; }

        /**
         * @brief Sets the pixel size used when painting entry icons.
         * @param px Requested icon size in pixels.
         */
        void setIconSize(int px) {
            m_iconSize = std::max(12, px);
            update();
        }

        /**
         * @brief Returns the current icon size.
         * @return Icon size in pixels.
         */
        int iconSize() const { return m_iconSize; }

        /**
         * @brief Updates the header sort indicator shown by the list.
         * @param key Active sort key.
         * @param ascending `true` for ascending order, `false` for descending order.
         */
        void setSortState(SortKey key, bool ascending) {
            if (m_sortKey == key && m_sortAscending == ascending) {
                return;
            }
            m_sortKey = key;
            m_sortAscending = ascending;
            update();
        }

        DECLARE_SIGNAL(currentChanged, int); ///< Emitted when the current row selection changes.
        DECLARE_SIGNAL(activated, int);      ///< Emitted when the user activates a row, typically by double click.
        DECLARE_SIGNAL(sortRequested, int);  ///< Emitted when the user clicks one of the header columns.

    protected:
        /**
         * @brief Handles the resize Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
        void resizeEvent(ResizeEvent* event) override {
            SwWidget::resizeEvent(event);
            updateLayout();
            resetScroll();
        }

        /**
         * @brief Handles the paint Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
        void paintEvent(PaintEvent* event) override {
            if (!event || !event->painter() || !isVisibleInHierarchy()) {
                return;
            }

            SwPainter* painter = event->painter();
            const SwRect bounds = rect();

            painter->fillRoundedRect(bounds, 8, SwColor{255, 255, 255}, SwColor{229, 229, 229}, 1);

            const SwRect viewport = contentViewportRect(bounds);
            if (viewport.width <= 0 || viewport.height <= 0) {
                return;
            }

            const SwRect headerRect = headerViewportRect(bounds);
            paintHeader(painter, headerRect);

            const SwRect dataViewport = dataViewportRect(bounds);
            if (dataViewport.width > 0 && dataViewport.height > 0) {
                painter->pushClipRect(dataViewport);

                const int offsetY = m_vBar ? m_vBar->value() : 0;
                const int offsetX = horizontalOffset();
                const int first = (m_rowHeight > 0) ? (offsetY / m_rowHeight) : 0;
                int y = dataViewport.y - (offsetY % m_rowHeight);

                const SwColor textColor{32, 32, 32};
                const SwColor muted{128, 128, 128};
                const SwColor hoverFill{245, 248, 251};
                const SwColor selFill{217, 232, 247};
                const SwColor selBorder{122, 170, 218};

                const SwFont font = getFont().getPointSize() > 0 ? getFont() : SwFont(L"Segoe UI", 10, Medium);

                for (int i = first; i < m_entries.size() && y < dataViewport.y + dataViewport.height; ++i) {
                    const BrowserEntry& e = m_entries[static_cast<SwVector<BrowserEntry>::size_type>(i)];
                    SwRect row{dataViewport.x, y, dataViewport.width, m_rowHeight};
                    SwRect layoutRow{dataViewport.x - offsetX, y, std::max(m_contentWidth, dataViewport.width), m_rowHeight};
                    const ColumnLayout cols = columnLayout(layoutRow);

                    if (i == m_hover && i != m_current) {
                        painter->fillRect(row, hoverFill, hoverFill, 0);
                    }

                    if (i == m_current) {
                        SwRect hi{row.x + 2, row.y + 2, std::max(0, row.width - 4), std::max(0, row.height - 4)};
                        painter->fillRoundedRect(hi, 6, selFill, selBorder, 1);
                    }

                    const int iconY = row.y + (m_rowHeight - m_iconSize) / 2;
                    if (!e.icon.isNull()) {
                        painter->drawImage(SwRect{cols.iconX, iconY, m_iconSize, m_iconSize}, e.icon, nullptr);
                    }

                    painter->drawText(cols.nameRect,
                                      e.name,
                                      DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                      textColor,
                                      font);

                    painter->drawText(cols.typeRect,
                                      e.typeName,
                                      DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                      muted,
                                      SwFont(font.getFamily(), 9, Medium));

                    painter->drawText(cols.modifiedRect,
                                      modifiedText(e),
                                      DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                      muted,
                                      SwFont(font.getFamily(), 9, Medium));

                    if (!e.isDir && !e.isDrive) {
                        painter->drawText(cols.sizeRect,
                                          humanSize(e.size),
                                          DrawTextFormats(DrawTextFormat::Right | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                          muted,
                                          SwFont(font.getFamily(), 9, Medium));
                    }

                    y += m_rowHeight;
                }

                painter->popClipRect();
            }

            // Draw status message (e.g. "Disconnected") centered when the list is empty.
            if (m_entries.isEmpty() && !m_statusMessage.isEmpty()) {
                const SwRect center = dataViewportRect(bounds);
                painter->drawText(center,
                                  m_statusMessage,
                                  DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                  SwColor{160, 160, 160},
                                  SwFont(L"Segoe UI", 11, Normal));
            }

            if (m_vBar && m_vBar->getVisible()) {
                paintChild_(event, m_vBar);
            }
            if (m_hBar && m_hBar->getVisible()) {
                paintChild_(event, m_hBar);
            }
        }

        /**
         * @brief Handles the mouse Move Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
        void mouseMoveEvent(MouseEvent* event) override {
            if (!event) {
                return;
            }
            if (m_pressedChild) {
                MouseEvent childEvent = mapMouseEventToChild_(*event, this, m_pressedChild);
                static_cast<SwWidgetInterface*>(m_pressedChild)->mouseMoveEvent(&childEvent);
                if (childEvent.isAccepted()) {
                    event->accept();
                    return;
                }
            }
            if (!isPointInside(event->x(), event->y())) {
                if (m_hover != -1) {
                    m_hover = -1;
                    update();
                }
                SwWidget::mouseMoveEvent(event);
                return;
            }
            const int idx = rowAt(event->y());
            if (m_hover != idx) {
                m_hover = idx;
                update();
            }
            SwWidget::mouseMoveEvent(event);
        }

        /**
         * @brief Handles the mouse Press Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
        void mousePressEvent(MouseEvent* event) override {
            if (!event) {
                return;
            }
            m_pressedChild = nullptr;
            if (hasVisibleScrollBars_()) {
                if (SwWidget* child = getChildUnderCursor(event->x(), event->y())) {
                    MouseEvent childEvent = mapMouseEventToChild_(*event, this, child);
                    static_cast<SwWidgetInterface*>(child)->mousePressEvent(&childEvent);
                    if (childEvent.isAccepted()) {
                        event->accept();
                        m_pressedChild = child;
                        return;
                    }
                }
            }

            if (!isPointInside(event->x(), event->y())) {
                SwWidget::mousePressEvent(event);
                return;
            }

            const int sortKey = headerSectionAt(event->x(), event->y());
            if (sortKey >= 0) {
                sortRequested(sortKey);
                event->accept();
                return;
            }

            const int idx = rowAt(event->y());
            if (idx >= 0 && idx < m_entries.size()) {
                setCurrentIndex(idx);
                event->accept();
                return;
            }

            SwWidget::mousePressEvent(event);
        }

        /**
         * @brief Handles the mouse Double Click Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
        void mouseDoubleClickEvent(MouseEvent* event) override {
            if (!event) {
                return;
            }
            if (!isPointInside(event->x(), event->y())) {
                SwWidget::mouseDoubleClickEvent(event);
                return;
            }
            const int idx = rowAt(event->y());
            if (idx >= 0 && idx < m_entries.size()) {
                setCurrentIndex(idx);
                activated(idx);
                event->accept();
                return;
            }
            SwWidget::mouseDoubleClickEvent(event);
        }

        /**
         * @brief Handles the mouse Release Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
        void mouseReleaseEvent(MouseEvent* event) override {
            if (!event) {
                return;
            }
            if (m_pressedChild) {
                SwWidget* child = m_pressedChild;
                m_pressedChild = nullptr;
                MouseEvent childEvent = mapMouseEventToChild_(*event, this, child);
                static_cast<SwWidgetInterface*>(child)->mouseReleaseEvent(&childEvent);
                if (childEvent.isAccepted()) {
                    event->accept();
                    return;
                }
            }
            if (hasVisibleScrollBars_()) {
                if (SwWidget* child = getChildUnderCursor(event->x(), event->y())) {
                    MouseEvent childEvent = mapMouseEventToChild_(*event, this, child);
                    static_cast<SwWidgetInterface*>(child)->mouseReleaseEvent(&childEvent);
                    if (childEvent.isAccepted()) {
                        event->accept();
                        return;
                    }
                }
            }
            SwWidget::mouseReleaseEvent(event);
        }

        /**
         * @brief Handles the wheel Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
        void wheelEvent(WheelEvent* event) override {
            if (!event) {
                return;
            }
            if (!isPointInside(event->x(), event->y())) {
                SwWidget::wheelEvent(event);
                return;
            }

            int steps = event->delta() / 120;
            if (steps == 0) {
                steps = (event->delta() > 0) ? 1 : (event->delta() < 0 ? -1 : 0);
            }
            if (steps == 0) {
                event->accept();
                return;
            }

            const bool horizontalRequest = event->isShiftPressed();
            const int stepY = std::max(1, std::max(m_rowHeight, (m_vBar ? (m_vBar->pageStep() / 10) : 0)));
            const int stepX = std::max(1, std::max(24, (m_hBar ? (m_hBar->pageStep() / 10) : 0)));

            auto scrollBy = [&](SwScrollBar* bar, int stepPx) -> bool {
                if (!bar || !bar->getVisible() || stepPx <= 0) {
                    return false;
                }
                const int old = bar->value();
                bar->setValue(old - steps * stepPx);
                return bar->value() != old;
            };

            bool scrolled = false;
            if (horizontalRequest) {
                scrolled = scrollBy(m_hBar, stepX);
            } else {
                scrolled = scrollBy(m_vBar, stepY);
            }

            if (scrolled) {
                event->accept();
                return;
            }

            if (horizontalRequest) {
                if (m_hBar && m_hBar->getVisible()) {
                    event->accept();
                    return;
                }
            } else if (m_vBar && m_vBar->getVisible()) {
                event->accept();
                return;
            }

            // No relevant scrollbar visible â€” consume to prevent propagation to parent widgets.
            event->accept();
        }

    private:
        struct ColumnLayout {
            int iconX{0};
            SwRect nameCell;
            SwRect typeCell;
            SwRect modifiedCell;
            SwRect sizeCell;
            SwRect nameRect;
            SwRect typeRect;
            SwRect modifiedRect;
            SwRect sizeRect;
        };

        static SwString humanSize(size_t bytes) {
            const double b = static_cast<double>(bytes);
            if (b < 1024.0) {
                return SwString::number(static_cast<int>(bytes)) + " B";
            }
            const double kb = b / 1024.0;
            if (kb < 1024.0) {
                return SwString::number(static_cast<int>(kb + 0.5)) + " KB";
            }
            const double mb = kb / 1024.0;
            if (mb < 1024.0) {
                return SwString::number(static_cast<int>(mb + 0.5)) + " MB";
            }
            const double gb = mb / 1024.0;
            return SwString::number(static_cast<int>(gb + 0.5)) + " GB";
        }

        SwRect viewportRect(const SwRect& bounds) const {
            return SwRect{bounds.x + 1, bounds.y + 1, std::max(0, bounds.width - 2), std::max(0, bounds.height - 2)};
        }

        SwRect contentViewportRect(const SwRect& bounds) const {
            return viewportRect(bounds);
        }

        SwRect headerViewportRect(const SwRect& bounds) const {
            const SwRect viewport = contentViewportRect(bounds);
            return SwRect{viewport.x, viewport.y, viewport.width, std::min(m_headerHeight, viewport.height)};
        }

        SwRect bodyViewportRect(const SwRect& bounds) const {
            const SwRect viewport = contentViewportRect(bounds);
            const int headerH = std::min(m_headerHeight, viewport.height);
            return SwRect{viewport.x,
                          viewport.y + headerH,
                          viewport.width,
                          std::max(0, viewport.height - headerH)};
        }

        SwRect dataViewportRect(const SwRect& bounds) const {
            const SwRect viewport = bodyViewportRect(bounds);
            const bool showV = m_vBar && m_vBar->getVisible();
            const bool showH = m_hBar && m_hBar->getVisible();
            return SwRect{viewport.x,
                          viewport.y,
                          std::max(0, viewport.width - (showV ? m_scrollBarThickness : 0)),
                          std::max(0, viewport.height - (showH ? m_scrollBarThickness : 0))};
        }

        SwRect verticalScrollBarRect(const SwRect& bounds) const {
            if (!m_vBar || !m_vBar->getVisible()) {
                return SwRect{};
            }
            const SwRect viewport = bodyViewportRect(bounds);
            const bool showH = m_hBar && m_hBar->getVisible();
            return SwRect{viewport.x + std::max(0, viewport.width - m_scrollBarThickness),
                          viewport.y,
                          std::min(m_scrollBarThickness, viewport.width),
                          std::max(0, viewport.height - (showH ? m_scrollBarThickness : 0))};
        }

        SwRect horizontalScrollBarRect(const SwRect& bounds) const {
            if (!m_hBar || !m_hBar->getVisible()) {
                return SwRect{};
            }
            const SwRect viewport = bodyViewportRect(bounds);
            const bool showV = m_vBar && m_vBar->getVisible();
            return SwRect{viewport.x,
                          viewport.y + std::max(0, viewport.height - m_scrollBarThickness),
                          std::max(0, viewport.width - (showV ? m_scrollBarThickness : 0)),
                          std::min(m_scrollBarThickness, viewport.height)};
        }

        int horizontalOffset() const {
            return (m_hBar && m_hBar->getVisible()) ? m_hBar->value() : 0;
        }

        SwRect headerContentRect(const SwRect& headerRect) const {
            const int offsetX = horizontalOffset();
            return SwRect{
                headerRect.x - offsetX,
                headerRect.y,
                std::max(m_contentWidth, headerRect.width),
                headerRect.height
            };
        }

        int minimumContentWidth() const {
            const int leftPad = 12;
            const int nameMin = 280;
            const int typeW = 180;
            const int dateW = 190;
            const int sizeW = 132;
            const int rightPad = 8;
            return leftPad + m_iconSize + 10 + nameMin + typeW + dateW + sizeW + rightPad + 16;
        }

        ColumnLayout columnLayout(const SwRect& row) const {
            ColumnLayout cols;

            const int leftPad = 12;
            const int rightPad = 8;
            const int sizeW = 132;
            const int dateW = 190;
            const int typeW = 180;
            const int textX = row.x + leftPad + m_iconSize + 10;
            const int minNameW = 280;
            const int contentRight = row.x + std::max(row.width, leftPad + m_iconSize + 10 + minNameW + typeW + dateW + sizeW + rightPad);

            const int sizeX = std::max(row.x, contentRight - sizeW);
            const int dateX = std::max(row.x, sizeX - dateW);
            const int typeX = std::max(row.x, dateX - typeW);

            cols.iconX = row.x + leftPad;
            cols.nameCell = SwRect{row.x, row.y, std::max(0, typeX - row.x), row.height};
            cols.typeCell = SwRect{typeX, row.y, std::max(0, dateX - typeX), row.height};
            cols.modifiedCell = SwRect{dateX, row.y, std::max(0, sizeX - dateX), row.height};
            cols.sizeCell = SwRect{sizeX, row.y, std::max(0, row.x + row.width - sizeX), row.height};
            cols.nameRect = SwRect{textX,
                                   row.y,
                                   std::max(0, typeX - textX - 8),
                                   row.height};
            cols.typeRect = SwRect{typeX + 12,
                                   row.y,
                                   std::max(0, dateX - typeX - 18),
                                   row.height};
            cols.modifiedRect = SwRect{dateX + 12,
                                       row.y,
                                       std::max(0, sizeX - dateX - 18),
                                       row.height};
            cols.sizeRect = SwRect{sizeX + 10,
                                   row.y,
                                   std::max(0, row.x + row.width - sizeX - rightPad - 2),
                                   row.height};
            return cols;
        }

        SwString modifiedText(const BrowserEntry& entry) const {
            if (!entry.hasModifiedAt) {
                return {};
            }
            SwString text(entry.modifiedAt.toString());
            if (text.length() > 16) {
                text = text.left(16);
            }
            return text;
        }

        SwString headerText(const SwString& label, SortKey) const { return label; }

        SwString headerSortIndicator(SortKey key) const {
            if (m_sortKey != key) {
                return {};
            }
            return m_sortAscending ? "^" : "v";
        }

        void paintHeader(SwPainter* painter, const SwRect& headerRect) {
            if (!painter || headerRect.width <= 0 || headerRect.height <= 0) {
                return;
            }

            painter->pushClipRect(headerRect);

            const SwRect headerContent = headerContentRect(headerRect);
            const ColumnLayout cols = columnLayout(headerContent);
            const SwColor headerFill{246, 247, 249};
            const SwColor divider{223, 226, 230};
            const SwColor textColor{76, 76, 76};
            const SwColor activeText{32, 32, 32};
            const SwColor indicatorColor{90, 90, 90};
            const SwFont font(L"Segoe UI", 9, SemiBold);
            const int radius = 8;

            painter->fillRoundedRect(headerRect, radius, headerFill, headerFill, 0);
            painter->fillRect(SwRect{headerRect.x, headerRect.y + radius, headerRect.width, std::max(0, headerRect.height - radius)},
                              headerFill,
                              headerFill,
                              0);

            auto paintHeaderCell = [&](const SwRect& cellRect,
                                       const SwRect& textRect,
                                       const SwString& label,
                                       SortKey key,
                                       DrawTextFormats alignment,
                                       bool rightAligned) {
                if (cellRect.width <= 0 || cellRect.height <= 0) {
                    return;
                }
                const bool active = (m_sortKey == key);
                painter->drawText(textRect,
                                  headerText(label, key),
                                  alignment,
                                  active ? activeText : textColor,
                                  font);
                if (active) {
                    SwRect arrowRect = textRect;
                    if (rightAligned) {
                        arrowRect.width = std::min(12, arrowRect.width);
                        arrowRect.x = cellRect.x + 4;
                    } else {
                        arrowRect.width = std::min(14, std::max(0, cellRect.width - (textRect.x - cellRect.x) - 6));
                        arrowRect.x = cellRect.x + cellRect.width - arrowRect.width - 4;
                    }
                    painter->drawText(arrowRect,
                                      headerSortIndicator(key),
                                      DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                      indicatorColor,
                                      SwFont(L"Segoe UI", 8, SemiBold));
                }
            };

            paintHeaderCell(cols.nameCell,
                            cols.nameRect,
                            "Name",
                            SortByName,
                            DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                            false);
            paintHeaderCell(cols.typeCell,
                            cols.typeRect,
                            "Type",
                            SortByType,
                            DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                            false);
            paintHeaderCell(cols.modifiedCell,
                            cols.modifiedRect,
                            "Date modified",
                            SortByModified,
                            DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                            false);
            paintHeaderCell(cols.sizeCell,
                            cols.sizeRect,
                            "Size",
                            SortBySize,
                            DrawTextFormats(DrawTextFormat::Right | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                            true);

            painter->fillRect(SwRect{cols.typeCell.x, headerRect.y + 6, 1, std::max(0, headerRect.height - 12)},
                              divider,
                              divider,
                              0);
            painter->fillRect(SwRect{cols.modifiedCell.x, headerRect.y + 6, 1, std::max(0, headerRect.height - 12)},
                              divider,
                              divider,
                              0);
            painter->fillRect(SwRect{cols.sizeCell.x, headerRect.y + 6, 1, std::max(0, headerRect.height - 12)},
                              divider,
                              divider,
                              0);

            painter->fillRect(SwRect{headerRect.x, headerRect.y + headerRect.height - 1, headerRect.width, 1},
                              divider,
                              divider,
                              0);

            painter->popClipRect();
        }

        int headerSectionAt(int x, int y) const {
            const SwRect headerRect = headerViewportRect(rect());
            if (x < headerRect.x || x > headerRect.x + headerRect.width ||
                y < headerRect.y || y > headerRect.y + headerRect.height) {
                return -1;
            }

            const SwRect layoutRect = headerContentRect(headerRect);
            const ColumnLayout cols = columnLayout(layoutRect);
            if (x >= cols.sizeCell.x) {
                return SortBySize;
            }
            if (x >= cols.modifiedCell.x) {
                return SortByModified;
            }
            if (x >= cols.typeCell.x) {
                return SortByType;
            }
            return SortByName;
        }

        void initDefaults() {
            setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
            setCursor(CursorType::Arrow);
            setFocusPolicy(FocusPolicyEnum::Strong);
            setFont(SwFont(L"Segoe UI", 10, Medium));

            m_vBar = new SwScrollBar(SwScrollBar::Orientation::Vertical, this);
            m_vBar->hide();
            m_vBar->setSingleStep(m_rowHeight);
            m_hBar = new SwScrollBar(SwScrollBar::Orientation::Horizontal, this);
            m_hBar->hide();
            m_hBar->setSingleStep(32);

            SwObject::connect(m_vBar, &SwScrollBar::valueChanged, this, [this](int) { update(); });
            SwObject::connect(m_hBar, &SwScrollBar::valueChanged, this, [this](int) { update(); });
        }

        void updateLayout() {
            if (!m_vBar || !m_hBar) {
                return;
            }
            if (m_vBar->getVisible()) {
                const SwRect sb = verticalScrollBarRect(rect());
                m_vBar->move(sb.x, sb.y);
                m_vBar->resize(sb.width, sb.height);
            }
            if (m_hBar->getVisible()) {
                const SwRect sb = horizontalScrollBarRect(rect());
                m_hBar->move(sb.x, sb.y);
                m_hBar->resize(sb.width, sb.height);
            }
        }

        void resetScroll() {
            if (!m_vBar || !m_hBar) {
                return;
            }
            const SwRect bounds = rect();
            const SwRect bodyViewport = bodyViewportRect(bounds);
            m_contentWidth = std::max(bodyViewport.width, minimumContentWidth());
            const int contentH = std::max(0, m_entries.size() * m_rowHeight);
            bool needV = false;
            bool needH = false;

            for (int pass = 0; pass < 3; ++pass) {
                const int viewW = std::max(0, bodyViewport.width - (needV ? m_scrollBarThickness : 0));
                const int viewH = std::max(0, bodyViewport.height - (needH ? m_scrollBarThickness : 0));
                const bool nextNeedH = m_contentWidth > viewW;
                const bool nextNeedV = contentH > viewH;
                if (nextNeedH == needH && nextNeedV == needV) {
                    break;
                }
                needH = nextNeedH;
                needV = nextNeedV;
            }

            const int viewW = std::max(0, bodyViewport.width - (needV ? m_scrollBarThickness : 0));
            const int viewH = std::max(0, bodyViewport.height - (needH ? m_scrollBarThickness : 0));

            if (needV) {
                m_vBar->show();
                m_vBar->setRange(0, std::max(0, contentH - viewH));
                m_vBar->setPageStep(std::max(1, viewH));
            } else {
                m_vBar->hide();
                m_vBar->setRange(0, 0);
                m_vBar->setValue(0);
            }

            if (needH) {
                m_hBar->show();
                m_hBar->setRange(0, std::max(0, m_contentWidth - viewW));
                m_hBar->setPageStep(std::max(1, viewW));
            } else {
                m_hBar->hide();
                m_hBar->setRange(0, 0);
                m_hBar->setValue(0);
            }
            updateLayout();
        }

        int rowAt(int y) const {
            const SwRect bounds = rect();
            const SwRect viewport = dataViewportRect(bounds);
            if (y < viewport.y || y > viewport.y + viewport.height) {
                return -1;
            }
            const int offsetY = m_vBar ? m_vBar->value() : 0;
            const int localY = y - viewport.y + offsetY;
            if (m_rowHeight <= 0) {
                return -1;
            }
            const int idx = localY / m_rowHeight;
            return (idx >= 0 && idx < m_entries.size()) ? idx : -1;
        }

        void ensureCurrentVisible() {
            if (!m_vBar || !m_vBar->getVisible() || m_current < 0 || m_rowHeight <= 0) {
                return;
            }
            const SwRect bounds = rect();
            const SwRect viewport = dataViewportRect(bounds);
            const int viewH = viewport.height;
            const int top = m_vBar->value();
            const int rowY = m_current * m_rowHeight;
            if (rowY < top) {
                m_vBar->setValue(rowY);
            } else if (rowY + m_rowHeight > top + viewH) {
                m_vBar->setValue(std::max(0, rowY + m_rowHeight - viewH));
            }
        }

        bool hasVisibleScrollBars_() const {
            return (m_vBar && m_vBar->getVisible()) || (m_hBar && m_hBar->getVisible());
        }

        SwVector<BrowserEntry> m_entries;
        SwString m_statusMessage;
        int m_current{-1};
        int m_hover{-1};
        SwScrollBar* m_vBar{nullptr};
        SwScrollBar* m_hBar{nullptr};
        SwWidget* m_pressedChild{nullptr};
        int m_rowHeight{34};
        int m_iconSize{20};
        int m_contentWidth{0};
        int m_headerHeight{32};
        int m_scrollBarThickness{14};
        SortKey m_sortKey{SortByName};
        bool m_sortAscending{true};
    };

    // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

    void buildUi() {
        auto* content = contentWidget();
        auto* bar = buttonBarWidget();
        if (!content || !bar) {
            return;
        }

#if defined(_WIN32)
        m_back    = new SwToolButton("\xe2\x86\x90", content);   // â†
        m_forward = new SwToolButton("\xe2\x86\x92", content);   // â†’
        m_up      = new SwToolButton("\xe2\x86\x91", content);   // â†‘
        m_home    = new SwToolButton("\xe2\x8c\x82", content);   // âŒ‚
        m_refresh = new SwToolButton("\xe2\x86\xba", content);   // â†º
        m_go      = new SwToolButton("\xe2\x96\xb6", content);   // â–¶
#else
        m_back    = new SwToolButton("<",  content);
        m_forward = new SwToolButton(">",  content);
        m_up      = new SwToolButton("^",  content);
        m_home    = new SwToolButton("~",  content);
        m_refresh = new SwToolButton("R",  content);
        m_go      = new SwToolButton("Go", content);
#endif

        for (SwToolButton* btn : {m_back, m_forward, m_up, m_home, m_refresh, m_go}) {
            if (btn) {
                btn->setCheckable(false);
            }
        }

        m_dirEdit = new SwLineEdit(content);
        m_dirEdit->resize(520, 34);
        m_dirEdit->setPlaceholder("Path...");
        m_dirEdit->setStyleSheet("SwLineEdit { background-color: rgb(255,255,255); border-color: rgb(198,198,198); border-width: 1px; border-radius: 5px; padding: 4px 8px; color: rgb(32,32,32); }");

        m_view = new FileListView(content);

        m_sidebar = new SwTreeWidget(1, content);
        m_sidebar->setRootIsDecorated(false); // hide arrows for top-level section headers
        m_sidebar->setAlternatingRowColors(false);
        m_sidebar->setShowGrid(false);
        m_sidebar->setIndentation(4);
        m_sidebar->setContentLeftPad(28);
        m_sidebar->setIconSize(16);
        m_sidebar->setStyleSheet("SwWidget { background-color: rgb(240,240,240); border-radius: 8px; border-width: 1px; border-color: rgb(220,220,220); }");
        m_sidebar->setFont(SwFont(L"Segoe UI", 10, Medium));
        m_sidebar->setHeaderHidden(true);
        m_sidebar->setHorizontalScrollBarPolicy(SwScrollBarPolicy::ScrollBarAlwaysOff);
        buildSidebarItems();
        SwObject::connect(m_sidebar, &SwTreeWidget::itemClicked, this, [this](SwStandardItem* item) {
            if (!item) return;
            const SwString path = item->toolTip();
            if (path == "__section__" || path == "__separator__") return;
            navigateTo(path, true);
        });

        m_pathLabel = new SwLabel("File name", content);
        m_pathLabel->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(96, 96, 96); font-size: 13px; }");
        m_pathLabel->resize(120, 22);

        m_pathEdit = new SwLineEdit(content);
        m_pathEdit->resize(420, 34);
        m_pathEdit->setStyleSheet("SwLineEdit { background-color: rgb(255,255,255); border-color: rgb(198,198,198); border-width: 1px; border-radius: 5px; padding: 4px 8px; color: rgb(32,32,32); }");

        m_filterLabel = new SwLabel("Filter", content);
        m_filterLabel->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(96, 96, 96); font-size: 13px; }");
        m_filterLabel->resize(120, 22);

        m_filterCombo = new SwComboBox(content);
        m_filterCombo->resize(320, 34);
        m_filterCombo->setStyleSheet("SwComboBox { background-color: rgb(255,255,255); border-color: rgb(198,198,198); border-width: 1px; border-radius: 5px; }");

        m_ok = new SwPushButton("Open", bar);
        m_cancel = new SwPushButton("Cancel", bar);
        m_ok->resize(110, 34);
        m_cancel->resize(110, 34);
        m_ok->setStyleSheet(R"(
            SwPushButton { background-color: rgb(0,103,192); border-color: rgb(0,90,158); color: rgb(255,255,255); border-radius: 5px; border-width: 1px; font-size: 13px; }
            SwPushButton:hover { background-color: rgb(0,117,218); border-color: rgb(0,103,192); }
            SwPushButton:pressed { background-color: rgb(0,90,158); border-color: rgb(0,75,130); }
        )");
        m_cancel->setStyleSheet(R"(
            SwPushButton { background-color: rgb(255,255,255); border-color: rgb(213,213,213); color: rgb(32,32,32); border-radius: 5px; border-width: 1px; font-size: 13px; }
            SwPushButton:hover { background-color: rgb(243,243,243); border-color: rgb(198,198,198); }
            SwPushButton:pressed { background-color: rgb(229,229,229); border-color: rgb(180,180,180); }
        )");

        SwObject::connect(m_ok, &SwPushButton::clicked, this, [this]() {
            acceptSelection();
        });
        SwObject::connect(m_cancel, &SwPushButton::clicked, this, [this]() { reject(); });

        SwObject::connect(m_back, &SwToolButton::clicked, this, [this](bool) { goBack(); });
        SwObject::connect(m_forward, &SwToolButton::clicked, this, [this](bool) { goForward(); });
        SwObject::connect(m_up, &SwToolButton::clicked, this, [this](bool) { goUp(); });
        SwObject::connect(m_home, &SwToolButton::clicked, this, [this](bool) { goHome(); });
        SwObject::connect(m_refresh, &SwToolButton::clicked, this, [this](bool) { refreshEntries(); });
        SwObject::connect(m_go, &SwToolButton::clicked, this, [this](bool) { goToPathFromEdit(); });

        SwObject::connect(m_view, &FileListView::currentChanged, this, [this](int) { syncFileNameFromSelection(); });
        SwObject::connect(m_view, &FileListView::activated, this, [this](int idx) { activateEntry(idx); });
        m_view->setSortState(m_sortKey, m_sortAscending);
        SwObject::connect(m_view, &FileListView::sortRequested, this, [this](int key) {
            applySortRequest(static_cast<SortKey>(key));
        });

        if (m_filterCombo) {
            SwObject::connect(m_filterCombo, &SwComboBox::currentIndexChanged, this, [this](int idx) {
                m_activeFilterIndex = idx;
                rebuildActiveFilterPatterns();
                refreshEntries();
            });
        }

        rebuildFilterOptions();
        updateUiTexts();
        updateLayout();

        const SwString initialDir = m_directory.trimmed().isEmpty() ? swDirPlatform().currentPath() : m_directory;
        navigateTo(initialDir, false);
    }

    void buildSidebarItems() {
        if (!m_sidebar) return;
        m_sidebar->clear();

        const int iconSz = 16;

        // Helper: create a nav item with icon stored via setIcon, path via setToolTip
        auto makeNavItem = [&](const SwString& label, const SwString& path, const SwImage& icon) -> SwStandardItem* {
            auto* item = new SwStandardItem(label);
            item->setIcon(icon);
            item->setToolTip(path);  // path stored in toolTip for retrieval on click
            return item;
        };

        // â”€â”€ QUICK ACCESS section header â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        auto* qaHeader = new SwStandardItem("QUICK ACCESS");
        qaHeader->setToolTip("__section__");

#if defined(_WIN32)
        auto winFolder = [](int csidl) -> SwString {
            wchar_t buf[MAX_PATH] = {};
            if (SHGetSpecialFolderPathW(nullptr, buf, csidl, FALSE)) {
                return SwString::fromWCharArray(buf);
            }
            return {};
        };
        auto winKnownFolder = [](const KNOWNFOLDERID& rfid) -> SwString {
            PWSTR p = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(rfid, 0, nullptr, &p))) {
                SwString s = SwString::fromWCharArray(p);
                CoTaskMemFree(p);
                return s;
            }
            return {};
        };

        const SwString home      = winFolder(CSIDL_PROFILE);
        const SwString desktop   = winFolder(CSIDL_DESKTOPDIRECTORY);
        const SwString documents = winFolder(CSIDL_PERSONAL);
        const SwString pictures  = winFolder(CSIDL_MYPICTURES);
        const SwString music     = winFolder(CSIDL_MYMUSIC);
        const SwString videos    = winFolder(CSIDL_MYVIDEO);
        const SwString downloads = winKnownFolder(FOLDERID_Downloads);

        if (!home.isEmpty())      qaHeader->appendRow(makeNavItem("Home",      home,      m_iconCache.iconForHome(iconSz)));
        if (!desktop.isEmpty())   qaHeader->appendRow(makeNavItem("Desktop",   desktop,   m_iconCache.iconForActualPath(desktop,   iconSz)));
        if (!documents.isEmpty()) qaHeader->appendRow(makeNavItem("Documents", documents, m_iconCache.iconForActualPath(documents, iconSz)));
        if (!downloads.isEmpty()) qaHeader->appendRow(makeNavItem("Downloads", downloads, m_iconCache.iconForActualPath(downloads, iconSz)));
        if (!pictures.isEmpty())  qaHeader->appendRow(makeNavItem("Pictures",  pictures,  m_iconCache.iconForActualPath(pictures,  iconSz)));
        if (!music.isEmpty())     qaHeader->appendRow(makeNavItem("Music",     music,     m_iconCache.iconForActualPath(music,     iconSz)));
        if (!videos.isEmpty())    qaHeader->appendRow(makeNavItem("Videos",    videos,    m_iconCache.iconForActualPath(videos,    iconSz)));
#else
        const SwString home = swStandardLocationProvider().standardLocation(SwStandardLocationId::Home);
        if (!home.isEmpty()) {
            const char sep = home.contains("\\") ? '\\' : '/';
            qaHeader->appendRow(makeNavItem("Home",      home,                                                          m_iconCache.iconForHome(iconSz)));
            qaHeader->appendRow(makeNavItem("Desktop",   home + SwString(sep) + "Desktop",   m_iconCache.iconForActualPath(home + SwString(sep) + "Desktop",   iconSz)));
            qaHeader->appendRow(makeNavItem("Documents", home + SwString(sep) + "Documents", m_iconCache.iconForActualPath(home + SwString(sep) + "Documents", iconSz)));
            qaHeader->appendRow(makeNavItem("Downloads", home + SwString(sep) + "Downloads", m_iconCache.iconForActualPath(home + SwString(sep) + "Downloads", iconSz)));
            qaHeader->appendRow(makeNavItem("Pictures",  home + SwString(sep) + "Pictures",  m_iconCache.iconForActualPath(home + SwString(sep) + "Pictures",  iconSz)));
            qaHeader->appendRow(makeNavItem("Music",     home + SwString(sep) + "Music",     m_iconCache.iconForActualPath(home + SwString(sep) + "Music",     iconSz)));
            qaHeader->appendRow(makeNavItem("Videos",    home + SwString(sep) + "Videos",    m_iconCache.iconForActualPath(home + SwString(sep) + "Videos",    iconSz)));
        }
#endif
        m_sidebar->addTopLevelItem(qaHeader);

        // â”€â”€ Separator between QUICK ACCESS and COMPUTER â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        auto* sep2 = new SwStandardItem("");
        sep2->setToolTip("__separator__");
        m_sidebar->addTopLevelItem(sep2);

        // â”€â”€ COMPUTER section header â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        auto* compHeader = new SwStandardItem("COMPUTER");
        compHeader->setToolTip("__section__");

        // "This PC" â€” collapsible container for drives
        auto* thisPc = new SwStandardItem("This PC");
        thisPc->setIcon(m_iconCache.iconForComputer(iconSz));
        thisPc->setToolTip(""); // empty path = Computer view

#if defined(_WIN32)
        {
            typedef DWORD (WINAPI* PFN_WNetGetConnectionW)(LPCWSTR, LPWSTR, LPDWORD);
            HMODULE hMpr = LoadLibraryW(L"mpr.dll");
            PFN_WNetGetConnectionW pfnWNet = hMpr
                ? reinterpret_cast<PFN_WNetGetConnectionW>(GetProcAddress(hMpr, "WNetGetConnectionW"))
                : nullptr;

            wchar_t buf[512] = {};
            const DWORD n = GetLogicalDriveStringsW(511, buf);
            if (n > 0 && n < 512) {
                const wchar_t* p = buf;
                while (*p) {
                    const SwString drive = SwString::fromWCharArray(p);
                    const SwString driveLetter = drive.left(1);
                    SwString label = drive;

                    // GetVolumeInformationW + SEM_FAILCRITICALERRORS: fails fast for disconnected drives.
                    const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
                    wchar_t volName[MAX_PATH] = {};
                    const BOOL volOk = GetVolumeInformationW(p, volName, MAX_PATH,
                                                              nullptr, nullptr, nullptr, nullptr, 0);
                    SetErrorMode(oldMode);

                    if (volOk && volName[0]) {
                        // Connected â€” use volume name.
                        label = SwString::fromWCharArray(volName) + " (" + driveLetter + SwString(":)");
                    } else if (!volOk && GetDriveTypeW(p) == DRIVE_REMOTE && pfnWNet) {
                        // Disconnected network drive â€” get real share name via WNetGetConnectionW (reads registry, never blocks).
                        wchar_t dl[3] = { p[0], L':', L'\0' };
                        wchar_t remote[MAX_PATH] = {};
                        DWORD len = MAX_PATH;
                        if (pfnWNet(dl, remote, &len) == ERROR_SUCCESS && remote[0]) {
                            SwString unc = SwString::fromWCharArray(remote);
                            int lastSlash = static_cast<int>(unc.lastIndexOf(SwString("\\")));
                            SwString share = (lastSlash >= 0) ? unc.mid(lastSlash + 1) : unc;
                            if (!share.isEmpty())
                                label = share + " (" + driveLetter + SwString(":)");
                        }
                    }

                    thisPc->appendRow(makeNavItem(label, drive, m_iconCache.iconFor(true, true, drive, drive, iconSz)));
                    p += std::wcslen(p) + 1;
                }
            }
            if (hMpr) FreeLibrary(hMpr);
        }
#else
        thisPc->appendRow(makeNavItem("/", "/", m_iconCache.iconFor(true, false, "/", "/", iconSz)));
#endif
        compHeader->appendRow(thisPc);
        m_sidebar->addTopLevelItem(compHeader);

        // Expand section headers AFTER all items are added (modelReset clears m_expanded)
        m_sidebar->expand(qaHeader);
        m_sidebar->expand(compHeader);

        // Restore active path selection
        m_sidebar->setActivePath(m_directory);
    }

    void updateUiTexts() {
        if (m_ok) {
            m_ok->setText(okButtonText());
        }
        if (m_pathEdit) {
            m_pathEdit->setPlaceholder(m_fileMode == Directory ? "Select a directory..." : "Select or type a file name...");
        }
        if (m_pathLabel) {
            m_pathLabel->setText(m_fileMode == Directory ? "Directory" : "File name");
        }
        if (m_dirEdit) {
            m_dirEdit->setPlaceholder("Path...");
        }
        if (m_filterLabel) {
            m_filterLabel->setVisible(m_fileMode != Directory);
        }
        if (m_filterCombo) {
            m_filterCombo->setVisible(m_fileMode != Directory);
        }
        updateNavButtons();
    }

    SwString okButtonText() const {
        if (m_fileMode == Directory) {
            return "Select";
        }
        return m_acceptMode == AcceptSave ? "Save" : "Open";
    }

    void acceptSelection() {
        SwString raw = m_pathEdit ? m_pathEdit->getText().trimmed() : SwString();

        if (raw.isEmpty() && m_view) {
            const BrowserEntry* e = m_view->currentEntry();
            if (e) {
                if (m_fileMode == Directory && e->isDir) {
                    raw = e->name;
                } else if (m_fileMode != Directory && !e->isDir) {
                    raw = e->name;
                }
            }
        }

        if (raw.isEmpty()) {
            if (m_fileMode == Directory) {
                m_selectedFile = m_directory.trimmed();
                accept();
            }
            return;
        }

        m_selectedFile = normalizeSelection(raw);
        accept();
    }

    static SwString normalizeDirString(SwString dir) {
        SwString out = dir.trimmed();
        if (out.length() >= 2 && out.startsWith("\"") && out.endsWith("\"")) {
            out = out.mid(1, static_cast<int>(out.length()) - 2);
        }
        if (out.length() >= 2 && out.startsWith("'") && out.endsWith("'")) {
            out = out.mid(1, static_cast<int>(out.length()) - 2);
        }
        return out.trimmed();
    }

    static bool isWindowsDriveRoot(const SwString& path) {
        const SwString p = normalizeDirString(path);
        if (p.length() < 2 || p[1] != ':') {
            return false;
        }
        if (p.length() == 2) {
            return true;
        }
        if (p.length() == 3 && (p[2] == '\\' || p[2] == '/')) {
            return true;
        }
        return false;
    }

    static SwString parentDirectory(const SwString& path) {
        SwString p = normalizeDirString(path);
        while (!p.isEmpty() && (p.endsWith("/") || p.endsWith("\\"))) {
            p = p.left(static_cast<int>(p.length()) - 1);
        }
        if (p.isEmpty()) {
            return {};
        }
        if (isWindowsDriveRoot(p)) {
            return {};
        }

        const size_t s1 = p.lastIndexOf('/');
        const size_t s2 = p.lastIndexOf('\\');
        size_t sep = static_cast<size_t>(-1);
        if (s1 != static_cast<size_t>(-1) && s2 != static_cast<size_t>(-1)) {
            sep = std::max(s1, s2);
        } else if (s1 != static_cast<size_t>(-1)) {
            sep = s1;
        } else if (s2 != static_cast<size_t>(-1)) {
            sep = s2;
        }
        if (sep == static_cast<size_t>(-1)) {
            return {};
        }
        return p.left(static_cast<int>(sep) + 1);
    }

    static SwString joinPath(const SwString& dir, const SwString& name) {
        if (dir.trimmed().isEmpty()) {
            return name;
        }
        const char sep = dir.contains("\\") ? '\\' : '/';
        if (dir.endsWith("/") || dir.endsWith("\\")) {
            return dir + name;
        }
        return dir + SwString(sep) + name;
    }

    void navigateTo(const SwString& dir, bool recordHistory) {
        SwString target = normalizeDirString(dir);
        if (target.toLower() == "computer") {
            target.clear();
        }
        if (m_directory == target) {
            syncDirEdit();
            return;
        }

        if (recordHistory) {
            m_backHistory.push_back(m_directory);
            m_forwardHistory.clear();
        }

        m_directory = target;
        syncDirEdit();
        updateNavButtons();
        refreshEntries();
        if (m_sidebar) { m_sidebar->setActivePath(m_directory); }
    }

    void syncDirEdit() {
        if (!m_dirEdit) {
            return;
        }
        if (m_directory.trimmed().isEmpty()) {
            m_dirEdit->setText("Computer");
        } else {
            m_dirEdit->setText(m_directory);
        }
    }

    void updateNavButtons() {
        if (m_back) {
            m_back->setEnable(!m_backHistory.isEmpty());
        }
        if (m_forward) {
            m_forward->setEnable(!m_forwardHistory.isEmpty());
        }
        if (m_up) {
            m_up->setEnable(!m_directory.trimmed().isEmpty());
        }
    }

    void goBack() {
        if (m_backHistory.isEmpty()) {
            return;
        }
        const SwString prev = m_backHistory.back();
        m_backHistory.removeAt(m_backHistory.size() - 1);
        m_forwardHistory.push_back(m_directory);
        m_directory = prev;
        syncDirEdit();
        updateNavButtons();
        refreshEntries();
        if (m_sidebar) { m_sidebar->setActivePath(m_directory); }
    }

    void goForward() {
        if (m_forwardHistory.isEmpty()) {
            return;
        }
        const SwString next = m_forwardHistory.back();
        m_forwardHistory.removeAt(m_forwardHistory.size() - 1);
        m_backHistory.push_back(m_directory);
        m_directory = next;
        syncDirEdit();
        updateNavButtons();
        refreshEntries();
        if (m_sidebar) { m_sidebar->setActivePath(m_directory); }
    }

    void goUp() {
        if (m_directory.trimmed().isEmpty()) {
            return;
        }
        navigateTo(parentDirectory(m_directory), true);
    }

    void goHome() {
        const SwString home = swStandardLocationProvider().standardLocation(SwStandardLocationId::Home);
        if (!home.trimmed().isEmpty()) {
            navigateTo(home, true);
        } else {
            navigateTo(swDirPlatform().currentPath(), true);
        }
    }

    void goToPathFromEdit() {
        if (!m_dirEdit) {
            return;
        }
        SwString path = normalizeDirString(m_dirEdit->getText());
        if (path.toLower() == "computer") {
            path.clear();
        }
        if (path.trimmed().isEmpty()) {
            navigateTo({}, true);
            return;
        }

        if (swDirPlatform().exists(path) && swDirPlatform().isDirectory(path)) {
            navigateTo(path, true);
            return;
        }

        if (swFilePlatform().isFile(path)) {
            const SwString parent = parentDirectory(path);
            if (!parent.isEmpty()) {
                navigateTo(parent, true);
            }
            if (m_pathEdit) {
                const size_t s1 = path.lastIndexOf('/');
                const size_t s2 = path.lastIndexOf('\\');
                size_t sep = static_cast<size_t>(-1);
                if (s1 != static_cast<size_t>(-1) && s2 != static_cast<size_t>(-1)) {
                    sep = std::max(s1, s2);
                } else if (s1 != static_cast<size_t>(-1)) {
                    sep = s1;
                } else if (s2 != static_cast<size_t>(-1)) {
                    sep = s2;
                }
                if (sep != static_cast<size_t>(-1)) {
                    m_pathEdit->setText(path.substr(sep + 1));
                } else {
                    m_pathEdit->setText(path);
                }
            }
        }
    }

    void syncFileNameFromSelection() {
        if (!m_view || !m_pathEdit) {
            return;
        }
        const BrowserEntry* e = m_view->currentEntry();
        if (!e) {
            return;
        }
        if (m_fileMode == Directory) {
            if (e->isDir) {
                m_pathEdit->setText(e->name);
            }
            return;
        }
        if (!e->isDir) {
            m_pathEdit->setText(e->name);
        }
    }

    void activateEntry(int idx) {
        if (!m_view) {
            return;
        }
        const BrowserEntry* e = m_view->entryAt(idx);
        if (!e) {
            return;
        }
        if (e->isDir) {
            navigateTo(e->path, true);
            return;
        }
        if (m_fileMode != Directory) {
            if (m_pathEdit) {
                m_pathEdit->setText(e->name);
            }
            acceptSelection();
        }
    }

    void rebuildFilterOptions() {
        m_filterOptions.clear();
        m_activeFilterIndex = 0;
        m_activePatterns.clear();

        if (!m_filterCombo) {
            return;
        }

        m_filterCombo->clear();

        const SwString f = m_filter.trimmed();
        if (f.isEmpty()) {
            m_filterOptions.push_back(FilterOption{"All files (*)", {}});
            m_filterCombo->addItem("All files (*)");
            m_filterCombo->setCurrentIndex(0);
            return;
        }

        const SwList<SwString> parts = f.split(";;");
        for (const SwString& part : parts) {
            const SwString label = part.trimmed();
            if (label.isEmpty()) {
                continue;
            }

            SwStringList patterns;
            const int open = label.indexOf("(");
            const int close = label.indexOf(")");
            if (open >= 0 && close > open) {
                const SwString inside = label.mid(open + 1, close - open - 1).trimmed();
                if (!inside.isEmpty()) {
                    const SwList<SwString> toks = inside.split(' ');
                    for (const SwString& tokRaw : toks) {
                        const SwString tok = tokRaw.trimmed();
                        if (tok.isEmpty()) {
                            continue;
                        }
                        if (tok == "*" || tok == "*.*") {
                            patterns.clear();
                            break;
                        }
                        patterns.append(tok);
                    }
                }
            }

            m_filterOptions.push_back(FilterOption{label, patterns});
            m_filterCombo->addItem(label);
        }

        if (m_filterOptions.isEmpty()) {
            m_filterOptions.push_back(FilterOption{"All files (*)", {}});
            m_filterCombo->addItem("All files (*)");
        }

        m_filterCombo->setCurrentIndex(0);
        rebuildActiveFilterPatterns();
    }

    void rebuildActiveFilterPatterns() {
        m_activePatterns.clear();
        if (m_activeFilterIndex < 0 || m_activeFilterIndex >= m_filterOptions.size()) {
            return;
        }
        const FilterOption& opt = m_filterOptions[static_cast<SwVector<FilterOption>::size_type>(m_activeFilterIndex)];
        m_activePatterns = opt.patterns;
    }

    void applySortRequest(SortKey key) {
        if (m_sortKey == key) {
            m_sortAscending = !m_sortAscending;
        } else {
            m_sortKey = key;
            m_sortAscending = defaultSortAscending(key);
        }

        if (m_view) {
            m_view->setSortState(m_sortKey, m_sortAscending);
        }
        refreshEntries();
    }

    static bool defaultSortAscending(SortKey key) {
        switch (key) {
        case SortByModified:
            return false;
        case SortByName:
        case SortByType:
        case SortBySize:
        default:
            return true;
        }
    }

    static bool nameLess(const BrowserEntry& a, const BrowserEntry& b, bool ascending = true) {
        const SwString aName = a.name.toLower();
        const SwString bName = b.name.toLower();
        if (aName == bName) {
            const SwString aPath = a.path.toLower();
            const SwString bPath = b.path.toLower();
            return ascending ? (aPath < bPath) : (aPath > bPath);
        }
        return ascending ? (aName < bName) : (aName > bName);
    }

    static SwString extensionLower(const SwString& name) {
        const size_t dot = name.lastIndexOf('.');
        if (dot == static_cast<size_t>(-1)) {
            return {};
        }
        return name.substr(dot).toLower();
    }

    static SwString fallbackTypeName(const BrowserEntry& entry) {
        if (entry.isDrive) {
            return "Drive";
        }
        if (entry.isDir) {
            return "Folder";
        }
        const SwString ext = extensionLower(entry.name);
        if (ext.isEmpty()) {
            return "File";
        }
        return ext.substr(1).toUpper() + " File";
    }

    static SwString systemTypeName(const BrowserEntry& entry) {
#if defined(_WIN32)
        SHFILEINFOW sfi;
        std::memset(&sfi, 0, sizeof(sfi));

        UINT flags = SHGFI_TYPENAME;
        DWORD attrs = 0;
        std::wstring query;

        if (entry.isDrive && !entry.path.trimmed().isEmpty()) {
            query = entry.path.toStdWString();
            if (query.size() >= 3 && query[1] == L':' && (query[2] == L'\\' || query[2] == L'/')) {
                wchar_t root[4] = { query[0], L':', L'\\', L'\0' };
                if (GetDriveTypeW(root) == DRIVE_REMOTE) {
                    flags |= SHGFI_USEFILEATTRIBUTES;
                    attrs = FILE_ATTRIBUTE_DIRECTORY;
                }
            }
        } else if (entry.isDir) {
            flags |= SHGFI_USEFILEATTRIBUTES;
            attrs = FILE_ATTRIBUTE_DIRECTORY;
            query = L"folder";
        } else {
            const SwString ext = extensionLower(entry.name);
            if (!ext.isEmpty()) {
                flags |= SHGFI_USEFILEATTRIBUTES;
                attrs = FILE_ATTRIBUTE_NORMAL;
                query = ext.toStdWString();
            } else {
                query = entry.path.toStdWString();
            }
        }

        if (!query.empty()) {
            const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
            const DWORD_PTR ok = SHGetFileInfoW(query.c_str(), attrs, &sfi, sizeof(sfi), flags);
            SetErrorMode(oldMode);
            if (ok != 0 && sfi.szTypeName[0] != L'\0') {
                return SwString::fromWCharArray(sfi.szTypeName);
            }
        }
#endif
        return fallbackTypeName(entry);
    }

    void sortEntries(SwVector<BrowserEntry>& entries) const {
        std::stable_sort(entries.begin(), entries.end(), [this](const BrowserEntry& a, const BrowserEntry& b) {
            if (a.isDir != b.isDir) {
                return a.isDir && !b.isDir;
            }

            switch (m_sortKey) {
            case SortByType: {
                const SwString aType = a.typeName.toLower();
                const SwString bType = b.typeName.toLower();
                if (aType != bType) {
                    return m_sortAscending ? (aType < bType) : (aType > bType);
                }
                return nameLess(a, b, true);
            }
            case SortByModified:
                if (a.hasModifiedAt != b.hasModifiedAt) {
                    return a.hasModifiedAt && !b.hasModifiedAt;
                }
                if (a.hasModifiedAt && b.hasModifiedAt && a.modifiedAt != b.modifiedAt) {
                    return m_sortAscending ? (a.modifiedAt < b.modifiedAt) : (a.modifiedAt > b.modifiedAt);
                }
                return nameLess(a, b, true);

            case SortBySize:
                if (!a.isDir && !b.isDir && a.size != b.size) {
                    return m_sortAscending ? (a.size < b.size) : (a.size > b.size);
                }
                return nameLess(a, b, true);

            case SortByName:
            default:
                return nameLess(a, b, m_sortAscending);
            }
        });
    }

    void populateEntryMetadata(BrowserEntry& entry) const {
        entry.typeName = systemTypeName(entry);
        if (entry.path.trimmed().isEmpty()) {
            return;
        }

        SwDateTime creation{static_cast<std::time_t>(0)};
        SwDateTime access{static_cast<std::time_t>(0)};
        SwDateTime write{static_cast<std::time_t>(0)};
        if (swFilePlatform().getFileMetadata(entry.path, creation, access, write)) {
            entry.modifiedAt = write;
            entry.hasModifiedAt = true;
        }
    }

    void restoreSelectionByPath(const SwString& path) {
        if (!m_view || path.isEmpty()) {
            return;
        }
        for (int i = 0; i < m_view->entryCount(); ++i) {
            const BrowserEntry* entry = m_view->entryAt(i);
            if (entry && entry->path == path) {
                m_view->setCurrentIndex(i);
                return;
            }
        }
    }

    void refreshEntries() {
        if (!m_view) {
            return;
        }

        SwVector<BrowserEntry> entries;
        entries.reserve(256);
        const BrowserEntry* currentEntry = m_view->currentEntry();
        const SwString currentPath = currentEntry ? currentEntry->path : SwString();
        m_view->setSortState(m_sortKey, m_sortAscending);

        if (m_directory.trimmed().isEmpty()) {
            listComputerEntries(entries);
            sortEntries(entries);
            m_view->setEntries(entries);
            restoreSelectionByPath(currentPath);
            return;
        }

#if defined(_WIN32)
        bool isNetworkDrivePath = false;
        {
            std::string utf8 = m_directory.toStdString();
            wchar_t wPath[MAX_PATH] = {};
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wPath, MAX_PATH);

            // GetDriveTypeW never blocks â€” safe to call even on disconnected network drives.
            wchar_t rootPath[4] = { wPath[0], L':', L'\\', L'\0' };
            UINT driveType = GetDriveTypeW(rootPath);

            if (driveType == DRIVE_NO_ROOT_DIR || driveType == DRIVE_UNKNOWN) {
                m_view->setEntries(entries, SwString("Drive not available"));
                return;
            }

            if (driveType == DRIVE_REMOTE) {
                isNetworkDrivePath = true;
                // Do NOT call GetVolumeInformationW or GetFileAttributesW on network drives â€”
                // they can block or crash for disconnected shares.
                // listInternal is protected by SetErrorMode internally; the post-listing
                // check below will catch the empty-result case.
            } else {
                // Local drive: safe existence check, always fast.
                const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
                const DWORD attr = GetFileAttributesW(wPath);
                SetErrorMode(oldMode);
                if (attr == INVALID_FILE_ATTRIBUTES) {
                    m_view->setEntries(entries, SwString("Location not accessible"));
                    return;
                }
            }
        }
#endif

        EntryTypes dirFlags = EntryType::Directories;
        EntryTypes fileFlags = EntryType::Files;

        SwStringList dirs = swDirPlatform().entryList(m_directory, dirFlags);
        SwStringList files;
        if (m_fileMode != Directory) {
            if (!m_activePatterns.isEmpty()) {
                files = swDirPlatform().entryList(m_directory, m_activePatterns, fileFlags);
            } else {
                files = swDirPlatform().entryList(m_directory, fileFlags);
            }
        }

        auto lessCI = [](const SwString& a, const SwString& b) { return a.toLower() < b.toLower(); };
        std::sort(dirs.begin(), dirs.end(), lessCI);
        std::sort(files.begin(), files.end(), lessCI);

        for (const SwString& name : dirs) {
            BrowserEntry e;
            e.name = name;
            e.isDir = true;
            e.path = joinPath(m_directory, name);
            populateEntryMetadata(e);
            e.icon = m_iconCache.iconFor(true, false, e.name, e.path, m_iconSizePx);
            entries.push_back(e);
        }

        for (const SwString& name : files) {
            BrowserEntry e;
            e.name = name;
            e.isDir = false;
            e.path = joinPath(m_directory, name);
            e.size = swFileInfoPlatform().size(e.path.toStdString());
            populateEntryMetadata(e);
            e.icon = m_iconCache.iconFor(false, false, e.name, e.path, m_iconSizePx);
            entries.push_back(e);
        }

#if defined(_WIN32)
        // If we navigated into a network drive root and got nothing back, the drive
        // is inaccessible (server unreachable). WNetGetConnectionW returned SUCCESS
        // because Windows still has the mapping, but FindFirstFileW silently failed.
        if (isNetworkDrivePath && entries.isEmpty()) {
            m_view->setEntries(entries, SwString("Network drive not accessible"));
            return;
        }
#endif

        sortEntries(entries);
        m_view->setEntries(entries);
        restoreSelectionByPath(currentPath);
    }

    void listComputerEntries(SwVector<BrowserEntry>& out) {
#if defined(_WIN32)
        wchar_t buffer[512];
        const DWORD n = GetLogicalDriveStringsW(512, buffer);
        if (n == 0 || n >= 512) {
            return;
        }
        const wchar_t* p = buffer;
        while (*p) {
            SwString drive = SwString::fromWCharArray(p);
            BrowserEntry e;
            e.name = drive;
            e.path = drive;
            e.isDir = true;
            e.isDrive = true;
            populateEntryMetadata(e);
            e.icon = m_iconCache.iconFor(true, true, e.name, e.path, m_iconSizePx);
            out.push_back(e);
            p += std::wcslen(p) + 1;
        }
#else
        BrowserEntry e;
        e.name = "/";
        e.path = "/";
        e.isDir = true;
        populateEntryMetadata(e);
        e.icon = m_iconCache.iconFor(true, false, e.name, e.path, m_iconSizePx);
        out.push_back(e);
#endif
    }

    SwString normalizeSelection(SwString raw) const {
        SwString path = raw.trimmed();
        if (path.length() >= 2 && path.startsWith("\"") && path.endsWith("\"")) {
            path = path.mid(1, static_cast<int>(path.length()) - 2);
        }
        if (path.length() >= 2 && path.startsWith("'") && path.endsWith("'")) {
            path = path.mid(1, static_cast<int>(path.length()) - 2);
        }
        path = path.trimmed();

        if (m_directory.trimmed().isEmpty() || path.isEmpty() || isAbsolutePath(path)) {
            return path;
        }

        const SwString dir = m_directory.trimmed();
        const char sep = dir.contains("\\") ? '\\' : '/';
        if (dir.endsWith("/") || dir.endsWith("\\")) {
            return dir + path;
        }
        return dir + SwString(sep) + path;
    }

    static bool isAbsolutePath(const SwString& path) {
        if (path.startsWith("/") || path.startsWith("\\")) {
            return true;
        }
        if (path.length() >= 2 && path[1] == ':') {
            const char c0 = path[0];
            return (c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z');
        }
        return false;
    }

    void updateLayout() {
        auto* content = contentWidget();
        auto* bar = buttonBarWidget();
        if (!content || !bar || !m_ok || !m_cancel) {
            return;
        }

        const int cw = content->width();
        const int ch = content->height();
        const int x = 0;
        int y = 0;

        // â”€â”€ Sidebar â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        const int sideW  = 200;
        const int sideGap = 8;
        if (m_sidebar) {
            m_sidebar->move(x, y);
            m_sidebar->resize(sideW, ch);
        }
        // Main area starts after the sidebar
        const int mx = x + sideW + sideGap;
        const int mw = std::max(0, cw - sideW - sideGap);

        // â”€â”€ Nav bar â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        const int toolH = 34;
        const int gap   = 8;
        const int navH  = 42;

        int bx = mx;
        if (m_back)    { m_back->move(bx, y + 4);    m_back->resize(34, toolH);    bx += 34 + 4; }
        if (m_forward) { m_forward->move(bx, y + 4); m_forward->resize(34, toolH); bx += 34 + 4; }
        if (m_up)      { m_up->move(bx, y + 4);      m_up->resize(34, toolH);      bx += 34 + 4; }
        if (m_home)    { m_home->move(bx, y + 4);    m_home->resize(34, toolH);    bx += 34 + 8; }
        if (m_refresh) { m_refresh->move(bx, y + 4); m_refresh->resize(34, toolH); bx += 34 + 10; }

        const int goW  = 40;
        const int dirW = std::max(0, mw - (bx - mx) - goW - 6);
        if (m_dirEdit) { m_dirEdit->move(bx, y + 4); m_dirEdit->resize(dirW, toolH); }
        if (m_go)      { m_go->move(bx + dirW + 6, y + 4); m_go->resize(goW, toolH); }

        y += navH + gap;

        // â”€â”€ File list + bottom rows â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        const int rowH = 34;
        const bool showFilter = (m_fileMode != Directory);
        const int bottomH = gap + rowH + (showFilter ? (gap + rowH) : 0);
        const int viewH   = std::max(0, ch - (navH + gap + bottomH));

        if (m_view) { m_view->move(mx, y); m_view->resize(mw, viewH); }
        y += viewH + gap;

        const int labelW = 92;
        const int fieldW = std::max(0, mw - labelW - gap);

        if (m_pathLabel) { m_pathLabel->move(mx, y); m_pathLabel->resize(labelW, rowH); }
        if (m_pathEdit)  { m_pathEdit->move(mx + labelW + gap, y); m_pathEdit->resize(fieldW, rowH); }
        y += rowH;

        if (showFilter) {
            y += gap;
            if (m_filterLabel) { m_filterLabel->move(mx, y); m_filterLabel->resize(labelW, rowH); }
            if (m_filterCombo) { m_filterCombo->move(mx + labelW + gap, y); m_filterCombo->resize(fieldW, rowH); }
        }

        // â”€â”€ Button bar â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        const int bw = bar->width();
        const int by = 6;
        int buttonsX = bw - m_ok->width();
        m_ok->move(buttonsX, by);
        buttonsX -= m_spacing + m_cancel->width();
        m_cancel->move(buttonsX, by);
    }

    SwString m_directory;
    SwString m_filter;
    SwString m_selectedFile;
    FileMode m_fileMode{ExistingFile};
    AcceptMode m_acceptMode{AcceptOpen};

    SwToolButton* m_back{nullptr};
    SwToolButton* m_forward{nullptr};
    SwToolButton* m_up{nullptr};
    SwToolButton* m_home{nullptr};
    SwToolButton* m_refresh{nullptr};
    SwToolButton* m_go{nullptr};
    SwLineEdit* m_dirEdit{nullptr};
    FileListView* m_view{nullptr};

    SwLabel* m_pathLabel{nullptr};
    SwLineEdit* m_pathEdit{nullptr};
    SwLabel* m_filterLabel{nullptr};
    SwComboBox* m_filterCombo{nullptr};
    SwPushButton* m_ok{nullptr};
    SwPushButton* m_cancel{nullptr};
    int m_spacing{10};
    SwTreeWidget* m_sidebar{nullptr};

    SystemIconCache m_iconCache;
    int m_iconSizePx{20};

    SwVector<FilterOption> m_filterOptions;
    SwStringList m_activePatterns;
    int m_activeFilterIndex{0};
    SwVector<SwString> m_backHistory;
    SwVector<SwString> m_forwardHistory;
    SortKey m_sortKey{SortByName};
    bool m_sortAscending{true};
};

