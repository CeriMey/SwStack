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

/***************************************************************************************************
 * SwFileDialog - Qt-like file dialog (≈ QFileDialog).
 *
 * Focus:
 * - Snapshot-friendly file dialog UI.
 * - Lightweight filesystem browser with basic navigation.
 * - System icons on Windows (cached).
 **************************************************************************************************/

#include "SwDialog.h"
#include "SwComboBox.h"
#include "SwLabel.h"
#include "SwLineEdit.h"
#include "SwPushButton.h"
#include "SwScrollBar.h"
#include "SwToolButton.h"

#include "SwPlatform.h"
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
#pragma comment(lib, "shell32.lib")
#endif

class SwFileDialog : public SwDialog {
    SW_OBJECT(SwFileDialog, SwDialog)

public:
    enum FileMode {
        AnyFile,
        ExistingFile,
        Directory,
        ExistingFiles
    };

    enum AcceptMode {
        AcceptOpen,
        AcceptSave
    };

    explicit SwFileDialog(SwWidget* parent = nullptr)
        : SwDialog(parent) {
        setWindowTitle("Open file");
        resize(720, 420);
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

    void setDirectory(const SwString& dir) {
        navigateTo(dir, false);
    }
    SwString directory() const { return m_directory; }

    void setNameFilter(const SwString& filter) {
        m_filter = filter;
        rebuildFilterOptions();
        refreshEntries();
    }
    SwString nameFilter() const { return m_filter; }

    void setFileMode(FileMode mode) {
        m_fileMode = mode;
        updateUiTexts();
        refreshEntries();
    }
    FileMode fileMode() const { return m_fileMode; }

    void setAcceptMode(AcceptMode mode) {
        m_acceptMode = mode;
        updateUiTexts();
    }
    AcceptMode acceptMode() const { return m_acceptMode; }

    void selectFile(const SwString& file) {
        m_selectedFile = file;
        if (m_pathEdit) {
            m_pathEdit->setText(file);
        }
    }

    SwString selectedFile() const { return m_selectedFile; }

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
    void resizeEvent(ResizeEvent* event) override {
        SwDialog::resizeEvent(event);
        updateLayout();
    }

private:
    struct BrowserEntry {
        SwString name;
        SwString path;
        bool isDir{false};
        bool isDrive{false};
        size_t size{0};
        SwImage icon;
    };

    struct FilterOption {
        SwString label;
        SwStringList patterns; // ex: "*.png", "*.jpg" (empty => all)
    };

    class SystemIconCache {
    public:
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

        void clear() { m_cache.clear(); }

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
            if (SHGetFileInfoW(w.c_str(), 0, &sfi, sizeof(sfi), flags) == 0) {
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

            const std::uint32_t bg = isDir ? 0xFFDBEAFEu : 0xFFE2E8F0u;
            const std::uint32_t border = isDir ? 0xFF3B82F6u : 0xFF64748Bu;
            const int pad = std::max(2, sizePx / 8);
            const int x0 = pad;
            const int y0 = pad;
            const int x1 = sizePx - pad - 1;
            const int y1 = sizePx - pad - 1;

            for (int y = y0; y <= y1; ++y) {
                std::uint32_t* row = img.scanLine(y);
                if (!row) {
                    continue;
                }
                for (int x = x0; x <= x1; ++x) {
                    const bool isBorder = (x == x0 || x == x1 || y == y0 || y == y1);
                    row[x] = isBorder ? border : bg;
                }
            }
            return img;
        }

        std::unordered_map<std::string, SwImage> m_cache;
    };

    class FileListView final : public SwWidget {
        SW_OBJECT(FileListView, SwWidget)

    public:
        explicit FileListView(SwWidget* parent = nullptr)
            : SwWidget(parent) {
            initDefaults();
        }

        void setEntries(const SwVector<BrowserEntry>& entries) {
            m_entries = entries;
            m_current = m_entries.isEmpty() ? -1 : 0;
            m_hover = -1;
            resetScroll();
            currentChanged(m_current);
            update();
        }

        int currentIndex() const { return m_current; }

        const BrowserEntry* currentEntry() const {
            if (m_current < 0 || m_current >= m_entries.size()) {
                return nullptr;
            }
            return &m_entries[static_cast<SwVector<BrowserEntry>::size_type>(m_current)];
        }

        const BrowserEntry* entryAt(int index) const {
            if (index < 0 || index >= m_entries.size()) {
                return nullptr;
            }
            return &m_entries[static_cast<SwVector<BrowserEntry>::size_type>(index)];
        }

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

        SwScrollBar* verticalScrollBar() const { return m_vBar; }

        void setIconSize(int px) {
            m_iconSize = std::max(12, px);
            update();
        }

        int iconSize() const { return m_iconSize; }

        DECLARE_SIGNAL(currentChanged, int);
        DECLARE_SIGNAL(activated, int);

    protected:
        void resizeEvent(ResizeEvent* event) override {
            SwWidget::resizeEvent(event);
            updateLayout();
            resetScroll();
        }

        void paintEvent(PaintEvent* event) override {
            if (!event || !event->painter() || !isVisibleInHierarchy()) {
                return;
            }

            SwPainter* painter = event->painter();
            const SwRect bounds = getRect();

            painter->fillRoundedRect(bounds, 12, SwColor{255, 255, 255}, SwColor{220, 224, 232}, 1);

            const SwRect viewport = contentViewportRect(bounds);
            if (viewport.width <= 0 || viewport.height <= 0) {
                return;
            }

            painter->pushClipRect(viewport);

            const int offsetY = m_vBar ? m_vBar->value() : 0;
            const int first = (m_rowHeight > 0) ? (offsetY / m_rowHeight) : 0;
            int y = viewport.y - (offsetY % m_rowHeight);

            const SwColor textColor{24, 28, 36};
            const SwColor muted{100, 116, 139};
            const SwColor altFill{248, 250, 252};
            const SwColor hoverFill{241, 245, 249};
            const SwColor selFill{219, 234, 254};
            const SwColor selBorder{59, 130, 246};

            const SwFont font = getFont().getPointSize() > 0 ? getFont() : SwFont(L"Segoe UI", 10, Medium);

            for (int i = first; i < m_entries.size() && y < viewport.y + viewport.height; ++i) {
                const BrowserEntry& e = m_entries[static_cast<SwVector<BrowserEntry>::size_type>(i)];
                SwRect row{viewport.x, y, viewport.width, m_rowHeight};

                if ((i % 2) == 1) {
                    painter->fillRect(row, altFill, altFill, 0);
                }

                if (i == m_hover && i != m_current) {
                    painter->fillRect(row, hoverFill, hoverFill, 0);
                }

                if (i == m_current) {
                    SwRect hi{row.x + 2, row.y + 2, std::max(0, row.width - 4), std::max(0, row.height - 4)};
                    painter->fillRoundedRect(hi, 8, selFill, selBorder, 1);
                }

                const int iconPad = 10;
                const int iconX = row.x + iconPad;
                const int iconY = row.y + (m_rowHeight - m_iconSize) / 2;
                if (!e.icon.isNull()) {
                    painter->drawImage(SwRect{iconX, iconY, m_iconSize, m_iconSize}, e.icon, nullptr);
                }

                const int textX = iconX + m_iconSize + 10;
                const int rightPad = 12;
                const int sizeW = 120;
                const int nameW = std::max(0, row.width - (textX - row.x) - rightPad - sizeW);
                SwRect nameRect{textX, row.y, nameW, m_rowHeight};
                SwRect sizeRect{row.x + row.width - rightPad - sizeW, row.y, sizeW, m_rowHeight};

                painter->drawText(nameRect,
                                  e.name,
                                  DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                  textColor,
                                  font);

                if (!e.isDir && !e.isDrive) {
                    painter->drawText(sizeRect,
                                      humanSize(e.size),
                                      DrawTextFormats(DrawTextFormat::Right | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                                      muted,
                                      SwFont(font.getFamily(), 9, Medium));
                }

                y += m_rowHeight;
            }

            painter->popClipRect();

            if (m_vBar && m_vBar->getVisible()) {
                static_cast<SwWidgetInterface*>(m_vBar)->paintEvent(event);
            }
        }

        void mouseMoveEvent(MouseEvent* event) override {
            if (!event) {
                return;
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

        void mousePressEvent(MouseEvent* event) override {
            if (!event) {
                return;
            }
            if (m_vBar && m_vBar->getVisible()) {
                if (SwWidget* child = getChildUnderCursor(event->x(), event->y())) {
                    static_cast<SwWidgetInterface*>(child)->mousePressEvent(event);
                    if (event->isAccepted()) {
                        return;
                    }
                }
            }

            if (!isPointInside(event->x(), event->y())) {
                SwWidget::mousePressEvent(event);
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

        void mouseReleaseEvent(MouseEvent* event) override {
            if (!event) {
                return;
            }
            if (m_vBar && m_vBar->getVisible()) {
                if (SwWidget* child = getChildUnderCursor(event->x(), event->y())) {
                    static_cast<SwWidgetInterface*>(child)->mouseReleaseEvent(event);
                    if (event->isAccepted()) {
                        return;
                    }
                }
            }
            SwWidget::mouseReleaseEvent(event);
        }

        void wheelEvent(WheelEvent* event) override {
            if (!event) {
                return;
            }
            if (!isPointInside(event->x(), event->y())) {
                SwWidget::wheelEvent(event);
                return;
            }
            if (!m_vBar) {
                SwWidget::wheelEvent(event);
                return;
            }

            int steps = event->delta() / 120;
            if (steps == 0) {
                steps = (event->delta() > 0) ? 1 : (event->delta() < 0 ? -1 : 0);
            }
            if (steps == 0) {
                SwWidget::wheelEvent(event);
                return;
            }

            const int stepPx = std::max(1, std::max(m_rowHeight, m_vBar->pageStep() / 10));
            const int old = m_vBar->value();
            m_vBar->setValue(old - steps * stepPx);
            if (m_vBar->value() != old) {
                event->accept();
                return;
            }

            SwWidget::wheelEvent(event);
        }

    private:
        static SwString humanSize(size_t bytes) {
            const double b = static_cast<double>(bytes);
            if (b < 1024.0) {
                return SwString("%1 B").arg(static_cast<int>(bytes));
            }
            const double kb = b / 1024.0;
            if (kb < 1024.0) {
                return SwString("%1 KB").arg(static_cast<int>(kb + 0.5));
            }
            const double mb = kb / 1024.0;
            if (mb < 1024.0) {
                return SwString("%1 MB").arg(static_cast<int>(mb + 0.5));
            }
            const double gb = mb / 1024.0;
            return SwString("%1 GB").arg(static_cast<int>(gb + 0.5));
        }

        SwRect viewportRect(const SwRect& bounds) const {
            const int pad = 6;
            return SwRect{bounds.x + pad, bounds.y + pad, std::max(0, bounds.width - pad * 2), std::max(0, bounds.height - pad * 2)};
        }

        SwRect contentViewportRect(const SwRect& bounds) const {
            SwRect viewport = viewportRect(bounds);
            const int thickness = m_scrollBarThickness;
            const bool showV = m_vBar && m_vBar->getVisible();
            viewport.width = std::max(0, viewport.width - (showV ? thickness : 0));
            return viewport;
        }

        void initDefaults() {
            setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
            setCursor(CursorType::Arrow);
            setFocusPolicy(FocusPolicyEnum::Strong);
            setFont(SwFont(L"Segoe UI", 10, Medium));

            m_vBar = new SwScrollBar(SwScrollBar::Orientation::Vertical, this);
            m_vBar->hide();
            m_vBar->setSingleStep(m_rowHeight);

            SwObject::connect(m_vBar, &SwScrollBar::valueChanged, this, [this](int) { update(); });
        }

        void updateLayout() {
            if (!m_vBar) {
                return;
            }
            const SwRect bounds = getRect();
            SwRect viewport = viewportRect(bounds);

            const int thickness = m_scrollBarThickness;
            if (m_vBar->getVisible()) {
                m_vBar->move(viewport.x + viewport.width - thickness, viewport.y);
                m_vBar->resize(thickness, viewport.height);
            }
        }

        void resetScroll() {
            if (!m_vBar) {
                return;
            }
            const SwRect bounds = getRect();
            const SwRect viewport = viewportRect(bounds);
            const int viewH = viewport.height;
            const int contentH = std::max(0, m_entries.size() * m_rowHeight);
            const bool needV = contentH > viewH;

            if (needV) {
                m_vBar->show();
                m_vBar->setRange(0, std::max(0, contentH - viewH));
                m_vBar->setPageStep(std::max(1, viewH));
            } else {
                m_vBar->hide();
                m_vBar->setRange(0, 0);
                m_vBar->setValue(0);
            }
            updateLayout();
        }

        int rowAt(int y) const {
            const SwRect bounds = getRect();
            const SwRect viewport = contentViewportRect(bounds);
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
            const SwRect bounds = getRect();
            const SwRect viewport = viewportRect(bounds);
            const int viewH = viewport.height;
            const int top = m_vBar->value();
            const int rowY = m_current * m_rowHeight;
            if (rowY < top) {
                m_vBar->setValue(rowY);
            } else if (rowY + m_rowHeight > top + viewH) {
                m_vBar->setValue(std::max(0, rowY + m_rowHeight - viewH));
            }
        }

        SwVector<BrowserEntry> m_entries;
        int m_current{-1};
        int m_hover{-1};
        SwScrollBar* m_vBar{nullptr};
        int m_rowHeight{34};
        int m_iconSize{20};
        int m_scrollBarThickness{14};
    };

    void buildUi() {
        auto* content = contentWidget();
        auto* bar = buttonBarWidget();
        if (!content || !bar) {
            return;
        }

        m_back = new SwToolButton("<", content);
        m_forward = new SwToolButton(">", content);
        m_up = new SwToolButton("Up", content);
        m_home = new SwToolButton("Home", content);
        m_refresh = new SwToolButton("Refresh", content);
        m_go = new SwToolButton("Go", content);

        for (SwToolButton* btn : {m_back, m_forward, m_up, m_home, m_refresh, m_go}) {
            if (btn) {
                btn->setCheckable(false);
            }
        }

        m_dirEdit = new SwLineEdit(content);
        m_dirEdit->resize(520, 36);
        m_dirEdit->setPlaceholder("Path...");

        m_view = new FileListView(content);

        m_pathLabel = new SwLabel("File name", content);
        m_pathLabel->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(71, 85, 105); font-size: 13px; }");
        m_pathLabel->resize(120, 22);

        m_pathEdit = new SwLineEdit(content);
        m_pathEdit->resize(420, 36);

        m_filterLabel = new SwLabel("Filter", content);
        m_filterLabel->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(71, 85, 105); font-size: 13px; }");
        m_filterLabel->resize(120, 22);

        m_filterCombo = new SwComboBox(content);
        m_filterCombo->resize(320, 34);

        m_ok = new SwPushButton("Open", bar);
        m_cancel = new SwPushButton("Cancel", bar);
        m_ok->resize(120, 40);
        m_cancel->resize(120, 40);

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

    void refreshEntries() {
        if (!m_view) {
            return;
        }

        SwVector<BrowserEntry> entries;
        entries.reserve(256);

        if (m_directory.trimmed().isEmpty()) {
            listComputerEntries(entries);
            m_view->setEntries(entries);
            return;
        }

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
            e.icon = m_iconCache.iconFor(true, false, e.name, e.path, m_iconSizePx);
            entries.push_back(e);
        }

        for (const SwString& name : files) {
            BrowserEntry e;
            e.name = name;
            e.isDir = false;
            e.path = joinPath(m_directory, name);
            e.size = swFileInfoPlatform().size(e.path.toStdString());
            e.icon = m_iconCache.iconFor(false, false, e.name, e.path, m_iconSizePx);
            entries.push_back(e);
        }

        m_view->setEntries(entries);
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
            e.icon = m_iconCache.iconFor(true, true, e.name, e.path, m_iconSizePx);
            out.push_back(e);
            p += std::wcslen(p) + 1;
        }
#else
        BrowserEntry e;
        e.name = "/";
        e.path = "/";
        e.isDir = true;
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

        const SwRect cr = content->getRect();
        const int x = cr.x;
        int y = cr.y;

        const int toolH = 34;
        const int gap = 10;
        const int navH = 42;

        int bx = x;
        if (m_back) {
            m_back->move(bx, y + 4);
            m_back->resize(36, toolH);
            bx += 36 + 6;
        }
        if (m_forward) {
            m_forward->move(bx, y + 4);
            m_forward->resize(36, toolH);
            bx += 36 + 6;
        }
        if (m_up) {
            m_up->move(bx, y + 4);
            m_up->resize(56, toolH);
            bx += 56 + 6;
        }
        if (m_home) {
            m_home->move(bx, y + 4);
            m_home->resize(66, toolH);
            bx += 66 + 6;
        }
        if (m_refresh) {
            m_refresh->move(bx, y + 4);
            m_refresh->resize(86, toolH);
            bx += 86 + 10;
        }

        const int goW = 54;
        const int dirW = std::max(0, cr.width - (bx - x) - goW - 6);
        if (m_dirEdit) {
            m_dirEdit->move(bx, y + 4);
            m_dirEdit->resize(dirW, toolH);
        }
        if (m_go) {
            m_go->move(bx + dirW + 6, y + 4);
            m_go->resize(goW, toolH);
        }

        y += navH + gap;

        const int rowH = 34;
        const bool showFilter = (m_fileMode != Directory);
        // Reserve space for the bottom rows *and* the gap after the list view.
        const int bottomH = gap + rowH + (showFilter ? (gap + rowH) : 0);
        const int viewH = std::max(0, cr.height - (navH + gap + bottomH));

        if (m_view) {
            m_view->move(x, y);
            m_view->resize(std::max(0, cr.width), viewH);
        }

        y += viewH + gap;

        const int labelW = 92;
        const int fieldW = std::max(0, cr.width - labelW - gap);

        if (m_pathLabel) {
            m_pathLabel->move(x, y);
            m_pathLabel->resize(labelW, rowH);
        }
        if (m_pathEdit) {
            m_pathEdit->move(x + labelW + gap, y);
            m_pathEdit->resize(fieldW, rowH);
        }
        y += rowH;

        if (showFilter) {
            y += gap;
            if (m_filterLabel) {
                m_filterLabel->move(x, y);
                m_filterLabel->resize(labelW, rowH);
            }
            if (m_filterCombo) {
                m_filterCombo->move(x + labelW + gap, y);
                m_filterCombo->resize(fieldW, rowH);
            }
        }

        const SwRect br = bar->getRect();
        const int by = br.y + 6;
        int buttonsX = br.x + br.width;
        buttonsX -= m_ok->width();
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

    SystemIconCache m_iconCache;
    int m_iconSizePx{20};

    SwVector<FilterOption> m_filterOptions;
    SwStringList m_activePatterns;
    int m_activeFilterIndex{0};
    SwVector<SwString> m_backHistory;
    SwVector<SwString> m_forwardHistory;
};
