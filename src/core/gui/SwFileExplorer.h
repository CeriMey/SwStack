#pragma once

/**
 * @file src/core/gui/SwFileExplorer.h
 * @ingroup core_gui
 * @brief Declares `SwFileExplorer`, a toolkit-native filesystem tree widget.
 *
 * The widget is intentionally lightweight:
 * - it renders a filesystem tree with `SwTreeWidget`,
 * - it uses native shell icons on Windows and deterministic fallback icons elsewhere,
 * - it supports file filters, path selection, and path activation,
 * - it can surface marked paths so callers can expose states such as "dirty".
 */

#include "SwDir.h"
#include "SwFileInfo.h"
#include "SwLabel.h"
#include "SwLayout.h"
#include "SwToolButton.h"
#include "SwTreeWidget.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#endif

namespace swFileExplorerDetail {

inline SwString normalizePath(const SwString& rawPath) {
    SwString normalized = SwDir::normalizePath(rawPath);
    normalized.replace("\\", "/");
    while (normalized.endsWith("/") && normalized.size() > 1) {
        if (normalized.size() == 3 && normalized[1] == ':') {
            break;
        }
        normalized.chop(1);
    }
    return normalized;
}

inline SwString joinPath(const SwString& directoryPath, const SwString& name) {
    if (directoryPath.isEmpty()) {
        return normalizePath(name);
    }

    SwString out = directoryPath;
    if (!out.endsWith("/")) {
        out += "/";
    }
    out += name;
    return normalizePath(out);
}

inline SwString fileName(const SwString& rawPath) {
    const SwFileInfo info(normalizePath(rawPath).toStdString());
    return SwString(info.fileName());
}

inline SwString parentPath(const SwString& rawPath) {
    const SwString path = normalizePath(rawPath);
    if (path.isEmpty()) {
        return SwString();
    }

    const size_t slash = path.lastIndexOf('/');
    if (slash == static_cast<size_t>(-1)) {
        return SwString();
    }
    if (slash == 0) {
        return SwString("/");
    }
    if (slash == 2 && path.size() >= 3 && path[1] == ':') {
        return path.substr(0, slash + 1);
    }
    return path.substr(0, slash);
}

inline bool isSameOrChildPath(const SwString& rawParentPath, const SwString& rawChildPath) {
    const SwString parentPath = normalizePath(rawParentPath);
    const SwString childPath = normalizePath(rawChildPath);
    if (parentPath.isEmpty() || childPath.isEmpty()) {
        return false;
    }
    if (parentPath == childPath) {
        return true;
    }

    SwString prefix = parentPath;
    if (!prefix.endsWith("/")) {
        prefix += "/";
    }
    return childPath.startsWith(prefix);
}

inline void sortEntries(SwStringList& entries) {
    std::sort(entries.begin(), entries.end(), [](const SwString& lhs, const SwString& rhs) {
        return lhs.compare(rhs, Sw::CaseInsensitive) < 0;
    });
}

class SystemIconCache {
public:
    SwImage iconForPath(const SwString& path, bool isDirectory, int sizePx) {
        const SwString normalizedPath = normalizePath(path);
        const SwString key = cacheKey_(normalizedPath, isDirectory, sizePx);
        const std::string stdKey = key.toStdString();
        std::unordered_map<std::string, SwImage>::const_iterator it = m_cache.find(stdKey);
        if (it != m_cache.end()) {
            return it->second;
        }

        SwImage image;
#if defined(_WIN32)
        if (isDirectory) {
            image = loadShellIconForPath_(normalizedPath, true, sizePx);
            if (image.isNull()) {
                image = loadShellIconForAttributes_("folder", FILE_ATTRIBUTE_DIRECTORY, sizePx);
            }
        } else {
            image = loadShellIconForPath_(normalizedPath, false, sizePx);
            if (image.isNull()) {
                const SwString extension = extensionLower_(normalizedPath);
                const SwString pseudo = extension.isEmpty() ? SwString("file") : SwString("file") + extension;
                image = loadShellIconForAttributes_(pseudo, FILE_ATTRIBUTE_NORMAL, sizePx);
            }
        }
#endif
        if (image.isNull()) {
            image = makeFallbackIcon_(isDirectory, sizePx);
        }

        m_cache[stdKey] = image;
        return image;
    }

    void clear() {
        m_cache.clear();
    }

private:
    static SwString cacheKey_(const SwString& path, bool isDirectory, int sizePx) {
        if (isDirectory) {
            return SwString("dir:") + path + ":" + SwString::number(sizePx);
        }
        return SwString("file:") + extensionLower_(path) + ":" + SwString::number(sizePx);
    }

    static SwString extensionLower_(const SwString& path) {
        const SwString name = fileName(path);
        const size_t dot = name.lastIndexOf('.');
        if (dot == static_cast<size_t>(-1)) {
            return {};
        }
        return name.substr(dot).toLower();
    }

#if defined(_WIN32)
    static SwImage loadShellIconForAttributes_(const SwString& pseudoPath, DWORD attrs, int sizePx) {
        SHFILEINFOW shellInfo;
        std::memset(&shellInfo, 0, sizeof(shellInfo));

        UINT flags = SHGFI_ICON | SHGFI_USEFILEATTRIBUTES;
        flags |= (sizePx <= 16) ? SHGFI_SMALLICON : SHGFI_LARGEICON;

        const std::wstring widePath = pseudoPath.toStdWString();
        if (SHGetFileInfoW(widePath.c_str(), attrs, &shellInfo, sizeof(shellInfo), flags) == 0) {
            return SwImage();
        }

        SwImage image = iconToImage_(shellInfo.hIcon, sizePx);
        if (shellInfo.hIcon) {
            DestroyIcon(shellInfo.hIcon);
        }
        return image;
    }

    static SwImage loadShellIconForPath_(const SwString& path, bool isDirectory, int sizePx) {
        SHFILEINFOW shellInfo;
        std::memset(&shellInfo, 0, sizeof(shellInfo));

        UINT flags = SHGFI_ICON;
        flags |= (sizePx <= 16) ? SHGFI_SMALLICON : SHGFI_LARGEICON;

        DWORD fileAttrs = 0;
        if (isDirectory) {
            fileAttrs = FILE_ATTRIBUTE_DIRECTORY;
        }

        const std::wstring widePath = path.toStdWString();
        const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
        const DWORD_PTR ok = SHGetFileInfoW(widePath.c_str(), fileAttrs, &shellInfo, sizeof(shellInfo), flags);
        SetErrorMode(oldMode);
        if (ok == 0) {
            return SwImage();
        }

        SwImage image = iconToImage_(shellInfo.hIcon, sizePx);
        if (shellInfo.hIcon) {
            DestroyIcon(shellInfo.hIcon);
        }
        return image;
    }

    static SwImage iconToImage_(HICON icon, int sizePx) {
        if (!icon || sizePx <= 0) {
            return SwImage();
        }

        BITMAPINFO bitmapInfo;
        std::memset(&bitmapInfo, 0, sizeof(bitmapInfo));
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = sizePx;
        bitmapInfo.bmiHeader.biHeight = -sizePx;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        void* pixels = nullptr;
        HDC screen = GetDC(nullptr);
        HBITMAP dib = CreateDIBSection(screen, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
        HDC memoryDc = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);

        if (!dib || !memoryDc || !pixels) {
            if (memoryDc) {
                DeleteDC(memoryDc);
            }
            if (dib) {
                DeleteObject(dib);
            }
            return SwImage();
        }

        HGDIOBJ oldObject = SelectObject(memoryDc, dib);
        std::memset(pixels, 0, static_cast<size_t>(sizePx) * static_cast<size_t>(sizePx) * 4u);
        DrawIconEx(memoryDc, 0, 0, icon, sizePx, sizePx, 0, nullptr, DI_NORMAL);

        SwImage image(sizePx, sizePx, SwImage::Format_ARGB32);
        if (!image.isNull()) {
            std::memcpy(image.bits(), pixels, static_cast<size_t>(sizePx) * static_cast<size_t>(sizePx) * 4u);
        }

        SelectObject(memoryDc, oldObject);
        DeleteDC(memoryDc);
        DeleteObject(dib);
        return image;
    }
#endif

    static SwImage makeFallbackIcon_(bool isDirectory, int sizePx) {
        SwImage image(sizePx, sizePx, SwImage::Format_ARGB32);
        if (image.isNull()) {
            return image;
        }
        image.fill(0x00000000u);

        if (isDirectory) {
            const std::uint32_t fill = 0xFF60A5FAu;
            const std::uint32_t tab = 0xFF93C5FDu;
            const std::uint32_t border = 0xFF1D4ED8u;
            const int pad = std::max(1, sizePx / 10);
            const int tabWidth = std::max(2, sizePx * 5 / 10);
            const int tabHeight = std::max(2, sizePx / 5);
            const int bodyY = pad + tabHeight - 1;
            const int bodyH = sizePx - bodyY - pad;

            for (int y = pad; y < pad + tabHeight; ++y) {
                std::uint32_t* row = image.scanLine(y);
                if (!row) {
                    continue;
                }
                for (int x = pad; x < pad + tabWidth; ++x) {
                    const bool isBorder = (x == pad || x == pad + tabWidth - 1 || y == pad);
                    row[x] = isBorder ? border : tab;
                }
            }

            for (int y = bodyY; y < bodyY + bodyH; ++y) {
                std::uint32_t* row = image.scanLine(y);
                if (!row) {
                    continue;
                }
                for (int x = pad; x < sizePx - pad; ++x) {
                    const bool isBorder = (x == pad || x == sizePx - pad - 1 || y == bodyY || y == bodyY + bodyH - 1);
                    row[x] = isBorder ? border : fill;
                }
            }
            return image;
        }

        const std::uint32_t fill = 0xFFF1F5F9u;
        const std::uint32_t border = 0xFF64748Bu;
        const std::uint32_t fold = 0xFFCBD5E1u;
        const int pad = std::max(1, sizePx / 10);
        const int foldSize = std::max(2, sizePx / 4);

        for (int y = pad; y < sizePx - pad; ++y) {
            std::uint32_t* row = image.scanLine(y);
            if (!row) {
                continue;
            }
            for (int x = pad; x < sizePx - pad; ++x) {
                const bool inFold = (x >= sizePx - pad - foldSize) &&
                                    (y < pad + foldSize) &&
                                    ((x - (sizePx - pad - foldSize)) + (y - pad) < foldSize);
                if (inFold) {
                    row[x] = fold;
                    continue;
                }

                const bool isBorder = (x == pad || x == sizePx - pad - 1 || y == pad || y == sizePx - pad - 1);
                row[x] = isBorder ? border : fill;
            }
        }

        return image;
    }

    std::unordered_map<std::string, SwImage> m_cache;
};

} // namespace swFileExplorerDetail

class SwFileExplorer : public SwWidget {
    SW_OBJECT(SwFileExplorer, SwWidget)

public:
    explicit SwFileExplorer(SwWidget* parent = nullptr,
                            bool showWorkspaceButton = true,
                            bool showRootPath = true)
        : SwWidget(parent) {
        setStyleSheet(R"(
            SwFileExplorer {
                background-color: rgb(248, 250, 252);
                border-width: 0px;
            }
        )");

        SwVerticalLayout* layout = new SwVerticalLayout(this);
        layout->setMargin(10);
        layout->setSpacing(8);
        setLayout(layout);

        SwWidget* headerRow = new SwWidget(this);
        headerRow->setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        SwHorizontalLayout* headerLayout = new SwHorizontalLayout(headerRow);
        headerLayout->setMargin(0);
        headerLayout->setSpacing(8);
        headerRow->setLayout(headerLayout);
        layout->addWidget(headerRow, 0, 40);

        m_titleLabel = new SwLabel("File Explorer", headerRow);
        m_titleLabel->setStyleSheet(R"(
            SwLabel {
                background-color: rgba(0,0,0,0);
                border-width: 0px;
                color: rgb(15, 23, 42);
                font-size: 15px;
            }
        )");
        headerLayout->addWidget(m_titleLabel, 0, 28);
        headerLayout->addStretch(1);

        if (showWorkspaceButton) {
            m_workspaceButton = new SwToolButton("Workspace", headerRow);
            m_workspaceButton->resize(110, 32);
            m_workspaceButton->setStyleSheet(R"(
                SwToolButton {
                    background-color: rgb(255, 255, 255);
                    border-color: rgb(203, 213, 225);
                    border-width: 1px;
                    border-radius: 10px;
                    color: rgb(30, 41, 59);
                    font-size: 12px;
                    padding: 6px 10px;
                }
                SwToolButton:hover {
                    background-color: rgb(241, 245, 249);
                    border-color: rgb(148, 163, 184);
                }
            )");
            headerLayout->addWidget(m_workspaceButton, 0, 110);
        }

        if (showRootPath) {
            m_rootLabel = new SwLabel("", this);
            m_rootLabel->setStyleSheet(R"(
                SwLabel {
                    background-color: rgba(0,0,0,0);
                    border-width: 0px;
                    color: rgb(100, 116, 139);
                    font-size: 11px;
                }
            )");
            layout->addWidget(m_rootLabel, 0, 32);
        }

        m_tree = new FileTreeWidget(this);
        m_tree->setHeaderHidden(true);
        m_tree->setColumnsFitToWidth(true);
        m_tree->setAlternatingRowColors(false);
        m_tree->setShowGrid(false);
        m_tree->setRowHeight(26);
        m_tree->setIconSize(16);
        m_tree->setStyleSheet(R"(
            SwTreeWidget {
                background-color: rgb(255, 255, 255);
                border-color: rgb(226, 232, 240);
                border-width: 1px;
                border-radius: 12px;
                color: rgb(30, 41, 59);
            }
        )");
        layout->addWidget(m_tree, 1, 0);

        SwObject::connect(m_tree, &SwTreeWidget::currentItemChanged, this,
                          [this](SwStandardItem* current, SwStandardItem*) { onCurrentItemChanged_(current); });
        SwObject::connect(m_tree, &SwTreeWidget::itemClicked, this,
                          [this](SwStandardItem* item) { onItemClicked_(item); });
        SwObject::connect(m_tree, &FileTreeWidget::itemDoubleActivated, this,
                          [this](SwStandardItem* item) { activateItem_(item); });
        SwObject::connect(m_tree, &SwTreeView::contextMenuRequested, this,
                          [this](const SwModelIndex& index, int x, int y) {
                              if (!index.isValid()) {
                                  return;
                              }
                              SwStandardItem* item = static_cast<SwStandardItem*>(index.internalPointer());
                              if (!item) {
                                  return;
                              }

                              const SwString path = swFileExplorerDetail::normalizePath(item->toolTip());
                              if (!path.isEmpty()) {
                                  emit pathContextMenuRequested(path, x, y);
                              }
                          });
        if (m_workspaceButton) {
            SwObject::connect(m_workspaceButton, &SwToolButton::clicked, this, [this](bool) {
                emit workspaceBrowseRequested();
            });
        }
    }

    void setTitle(const SwString& title) {
        m_title = title;
        if (m_titleLabel) {
            m_titleLabel->setText(m_title.isEmpty() ? SwString("File Explorer") : m_title);
        }
    }

    SwString title() const {
        return m_title;
    }

    void setRootPath(const SwString& rawRootPath) {
        const SwString normalizedRootPath = swFileExplorerDetail::normalizePath(rawRootPath);
        if (m_rootPath == normalizedRootPath) {
            reload();
            return;
        }

        m_rootPath = normalizedRootPath;
        reload();
    }

    SwString rootPath() const {
        return m_rootPath;
    }

    void setCurrentPath(const SwString& rawPath) {
        m_currentPath = swFileExplorerDetail::normalizePath(rawPath);
        syncSelection_(false);
    }

    SwString currentPath() const {
        return m_currentPath;
    }

    void setMarkedPaths(const SwMap<SwString, bool>& markedPaths) {
        m_markedPaths = markedPaths;
        refreshLabels_();
    }

    SwMap<SwString, bool> markedPaths() const {
        return m_markedPaths;
    }

    void setNameFilters(const SwStringList& filters) {
        m_nameFilters = filters;
        reload();
    }

    SwStringList nameFilters() const {
        return m_nameFilters;
    }

    void setDoubleClickActivationEnabled(bool enabled) {
        m_doubleClickActivationEnabled = enabled;
    }

    bool doubleClickActivationEnabled() const {
        return m_doubleClickActivationEnabled;
    }

    SwSize minimumSizeHint() const override {
        SwSize hint = SwWidget::minimumSizeHint();
        hint.width = std::max(240, std::min(hint.width, 360));
        hint.height = std::max(hint.height, 180);
        return hint;
    }

    void reload() {
        m_itemsByPath.clear();
        m_populatedDirectories.clear();
        m_iconCache.clear();
        if (m_tree) {
            m_tree->clear();
        }

        if (m_rootPath.isEmpty()) {
            if (m_rootLabel) {
                m_rootLabel->setText("No root path");
            }
            return;
        }

        const SwFileInfo rootInfo(m_rootPath.toStdString());
        if (!rootInfo.exists() || !rootInfo.isDir()) {
            if (m_rootLabel) {
                m_rootLabel->setText(SwString("Invalid root: ") + m_rootPath);
            }
            return;
        }

        if (m_rootLabel) {
            m_rootLabel->setText(m_rootPath);
        }

        SwStandardItem* rootItem = makeDirectoryItem_(m_rootPath);
        m_itemsByPath.insert(m_rootPath, rootItem);
        if (m_tree) {
            m_tree->addTopLevelItem(rootItem);
        }

        populateDirectoryChildren_(rootItem, m_rootPath);
        refreshLabels_();
        expandToItem_(rootItem);
        syncSelection_(false);
    }

    SwTreeWidget* treeWidget() const {
        return m_tree;
    }

signals:
    DECLARE_SIGNAL(pathActivated, const SwString&);
    DECLARE_SIGNAL(currentPathChanged, const SwString&);
    DECLARE_SIGNAL(pathContextMenuRequested, const SwString&, int, int);
    DECLARE_SIGNAL_VOID(workspaceBrowseRequested);

private:
    class FileTreeWidget final : public SwTreeWidget {
        SW_OBJECT(FileTreeWidget, SwTreeWidget)

    public:
        explicit FileTreeWidget(SwWidget* parent = nullptr)
            : SwTreeWidget(1, parent) {}

    signals:
        DECLARE_SIGNAL(itemDoubleActivated, SwStandardItem*);

    protected:
        void mouseDoubleClickEvent(MouseEvent* event) override {
            SwTreeWidget::mouseDoubleClickEvent(event);
            if (!event || event->button() != SwMouseButton::Left) {
                return;
            }
            if (SwStandardItem* item = currentItem()) {
                emit itemDoubleActivated(item);
            }
            event->accept();
        }
    };

    static SwString lazyPlaceholderToken_() {
        return "__sw_file_explorer_lazy_placeholder__";
    }

    bool directoryMayHaveVisibleChildren_(const SwString& directoryPath) const {
        SwDir directory(directoryPath);
        SwStringList directories = directory.entryList(EntryType::Directories);
        if (!directories.isEmpty()) {
            return true;
        }

        SwStringList files = m_nameFilters.isEmpty()
                                 ? directory.entryList(EntryType::Files)
                                 : directory.entryList(m_nameFilters, EntryType::Files);
        return !files.isEmpty();
    }

    void primeDirectoryItem_(SwStandardItem* item, const SwString& directoryPath) {
        if (!item) {
            return;
        }
        if (directoryMayHaveVisibleChildren_(directoryPath)) {
            SwStandardItem* placeholder = new SwStandardItem("");
            placeholder->setToolTip(lazyPlaceholderToken_());
            item->appendRow(placeholder);
        }
    }

    void populateDirectoryChildren_(SwStandardItem* parentItem, const SwString& directoryPath) {
        if (!parentItem || m_populatedDirectories.contains(directoryPath)) {
            return;
        }

        parentItem->removeAllChildren();

        SwDir directory(directoryPath);
        SwStringList directories = directory.entryList(EntryType::Directories);
        SwStringList files = m_nameFilters.isEmpty()
                                 ? directory.entryList(EntryType::Files)
                                 : directory.entryList(m_nameFilters, EntryType::Files);

        swFileExplorerDetail::sortEntries(directories);
        swFileExplorerDetail::sortEntries(files);

        for (size_t i = 0; i < directories.size(); ++i) {
            const SwString childPath = swFileExplorerDetail::joinPath(directoryPath, directories[i]);
            SwStandardItem* childItem = makeDirectoryItem_(childPath);
            primeDirectoryItem_(childItem, childPath);
            parentItem->appendRow(childItem);
            m_itemsByPath.insert(childPath, childItem);
        }

        for (size_t i = 0; i < files.size(); ++i) {
            const SwString childPath = swFileExplorerDetail::joinPath(directoryPath, files[i]);
            SwStandardItem* childItem = makeFileItem_(childPath);
            parentItem->appendRow(childItem);
            m_itemsByPath.insert(childPath, childItem);
        }

        m_populatedDirectories.insert(directoryPath, true);
    }

    void ensureDirectoryPopulated_(SwStandardItem* item) {
        if (!item) {
            return;
        }

        const SwString path = swFileExplorerDetail::normalizePath(item->toolTip());
        if (path.isEmpty()) {
            return;
        }

        const SwFileInfo info(path.toStdString());
        if (!info.exists() || !info.isDir()) {
            return;
        }

        populateDirectoryChildren_(item, path);
    }

    SwStandardItem* ensureItemForPath_(const SwString& rawPath) {
        const SwString path = swFileExplorerDetail::normalizePath(rawPath);
        if (path.isEmpty()) {
            return nullptr;
        }

        SwStandardItem* existing = m_itemsByPath.value(path, nullptr);
        if (existing) {
            return existing;
        }

        if (!swFileExplorerDetail::isSameOrChildPath(m_rootPath, path) || path == m_rootPath) {
            return m_itemsByPath.value(path, nullptr);
        }

        const SwString parentPath = swFileExplorerDetail::parentPath(path);
        if (parentPath.isEmpty() || parentPath == path) {
            return nullptr;
        }

        SwStandardItem* parentItem = ensureItemForPath_(parentPath);
        if (!parentItem) {
            return nullptr;
        }

        ensureDirectoryPopulated_(parentItem);
        return m_itemsByPath.value(path, nullptr);
    }

    SwStandardItem* makeDirectoryItem_(const SwString& path) {
        SwStandardItem* item = new SwStandardItem(labelForPath_(path));
        item->setToolTip(path);
        item->setIcon(m_iconCache.iconForPath(path, true, 16));
        return item;
    }

    SwStandardItem* makeFileItem_(const SwString& path) {
        SwStandardItem* item = new SwStandardItem(labelForPath_(path));
        item->setToolTip(path);
        item->setIcon(m_iconCache.iconForPath(path, false, 16));
        return item;
    }

    void refreshLabels_() {
        for (SwMap<SwString, SwStandardItem*>::iterator it = m_itemsByPath.begin(); it != m_itemsByPath.end(); ++it) {
            SwStandardItem* item = it.value();
            if (!item) {
                continue;
            }
            item->setText(labelForPath_(it.key()));
        }
        if (m_tree) {
            m_tree->update();
        }
    }

    void syncSelection_(bool emitSignal) {
        if (!m_tree) {
            return;
        }

        m_syncingSelection = true;
        if (m_currentPath.isEmpty()) {
            if (m_tree->selectionModel()) {
                m_tree->selectionModel()->clearSelection();
            }
        } else {
            SwStandardItem* item = ensureItemForPath_(m_currentPath);
            if (item) {
                expandToItem_(item);
            }
            m_tree->setActivePath(m_currentPath);
        }
        m_syncingSelection = false;

        if (emitSignal) {
            emit currentPathChanged(m_currentPath);
        }
        m_tree->update();
    }

    void expandToItem_(SwStandardItem* item) {
        if (!m_tree || !item) {
            return;
        }

        SwStandardItem* current = item;
        while (current) {
            ensureDirectoryPopulated_(current);
            m_tree->expand(current);
            current = current->parent();
        }
    }

    void onCurrentItemChanged_(SwStandardItem* current) {
        if (!current) {
            return;
        }

        const SwString path = swFileExplorerDetail::normalizePath(current->toolTip());
        if (path.isEmpty() || path == m_currentPath) {
            return;
        }

        ensureDirectoryPopulated_(current);
        m_currentPath = path;
        if (!m_syncingSelection) {
            emit currentPathChanged(m_currentPath);
        }
    }

    void onItemClicked_(SwStandardItem* item) {
        if (!item || m_doubleClickActivationEnabled) {
            return;
        }

        const SwString path = swFileExplorerDetail::normalizePath(item->toolTip());
        if (path.isEmpty()) {
            return;
        }

        const SwFileInfo info(path.toStdString());
        if (info.exists() && info.isFile()) {
            activateItem_(item);
        }
    }

    void activateItem_(SwStandardItem* item) {
        if (!item) {
            return;
        }

        const SwString path = swFileExplorerDetail::normalizePath(item->toolTip());
        if (path.isEmpty()) {
            return;
        }

        const SwFileInfo info(path.toStdString());
        if (!info.exists()) {
            return;
        }

        if (info.isDir()) {
            ensureDirectoryPopulated_(item);
            const SwModelIndex index = indexForItem_(item);
            if (index.isValid()) {
                if (m_tree->isExpanded(index)) {
                    m_tree->SwTreeView::collapse(index);
                } else {
                    m_tree->SwTreeView::expand(index);
                }
            }
            return;
        }

        m_currentPath = path;
        syncSelection_(false);
        emit pathActivated(path);
    }

    SwModelIndex indexForItem_(SwStandardItem* item) const {
        if (!m_tree || !m_tree->model() || !item) {
            return SwModelIndex();
        }

        SwStandardItem* parent = item->parent();
        if (!parent) {
            parent = m_tree->model()->invisibleRootItem();
        }

        const int row = item->row();
        const int column = item->column();
        if (row < 0 || column < 0) {
            return SwModelIndex();
        }

        const SwModelIndex parentIndex = (parent == m_tree->model()->invisibleRootItem())
                                             ? SwModelIndex()
                                             : indexForItem_(parent);
        return m_tree->model()->index(row, column, parentIndex);
    }

    SwString labelForPath_(const SwString& path) const {
        SwString label = swFileExplorerDetail::fileName(path);
        if (label.isEmpty()) {
            label = path;
        }

        if (pathHasMarkedChildren_(path)) {
            label += " *";
        }
        return label;
    }

    bool pathHasMarkedChildren_(const SwString& rawPath) const {
        const SwString path = swFileExplorerDetail::normalizePath(rawPath);
        if (m_markedPaths.contains(path) && m_markedPaths.value(path, false)) {
            return true;
        }

        const SwFileInfo info(path.toStdString());
        if (!info.exists() || !info.isDir()) {
            return false;
        }

        for (SwMap<SwString, bool>::const_iterator it = m_markedPaths.begin(); it != m_markedPaths.end(); ++it) {
            if (!it.value()) {
                continue;
            }
            if (swFileExplorerDetail::isSameOrChildPath(path, it.key())) {
                return true;
            }
        }

        return false;
    }

    SwLabel* m_titleLabel{nullptr};
    SwLabel* m_rootLabel{nullptr};
    SwToolButton* m_workspaceButton{nullptr};
    FileTreeWidget* m_tree{nullptr};

    SwString m_title;
    SwString m_rootPath;
    SwString m_currentPath;
    SwStringList m_nameFilters;
    SwMap<SwString, bool> m_markedPaths;
    SwMap<SwString, SwStandardItem*> m_itemsByPath;
    SwMap<SwString, bool> m_populatedDirectories;
    swFileExplorerDetail::SystemIconCache m_iconCache;
    bool m_syncingSelection{false};
    bool m_doubleClickActivationEnabled{false};
};
