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

// MainWindow.h
#pragma once
#include "SwGuiApplication.h"
#include "SwDialog.h"
#include "SwLabel.h"
#include "SwMenuBar.h"
#include "SwPushButton.h"
#include "SwStatusBar.h"
#include "SwTextEdit.h"
#include "SwToolBar.h"
#include "SwWidget.h"
#include "SwDragDrop.h"
#include "SwToolTip.h"
#include "SwShortcut.h"
#include "SwString.h"
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#if defined(__linux__)
#include <codecvt>
#endif
#include <memory>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#include "platform/win/SwWin32PlatformIntegration.h"
#include "platform/win/SwWin32Painter.h"
#include "platform/win/SwWindows.h"
#elif defined(__linux__)
#include "platform/x11/SwX11Painter.h"
#include "platform/x11/SwX11PlatformIntegration.h"
#endif

class SwMainWindow : public SwWidget {
public:
    SW_OBJECT(SwMainWindow, SwWidget)

    /**
     * @brief Constructs the SwMainWindow and registers it with SwGuiApplication.
     * @param title Title of the window.
     * @param width Width of the window.
     * @param height Height of the window.
     */
    SwMainWindow(const std::wstring& title = L"Main Window", int width = 800, int height = 600)
        : SwWidget(nullptr),
          lastMoveTime(std::chrono::steady_clock::now()),
          m_windowTitle(title) {
#if defined(_WIN32)
        // Define the window class name
        const wchar_t CLASS_NAME[] = L"SwMainWindowClass";

        // Initialize WNDCLASSW structure
        WNDCLASSW wc = {};
        wc.lpfnWndProc = SwWin32PlatformIntegration::WindowProc;  // Use Win32 platform WindowProc
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = CLASS_NAME;
        wc.style = CS_DBLCLKS;

        // Attempt to register the window class
        if (!RegisterClassW(&wc)) {
            if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                std::wcerr << L"Failed to register window class. Error: " << GetLastError() << std::endl;
                return;
            }
        }

        // Create the window
        hwnd = CreateWindowExW(0,
                               CLASS_NAME,
                               m_windowTitle.c_str(),
                               WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT,
                               CW_USEDEFAULT,
                               width,
                               height,
                               nullptr,
                               nullptr,
                               GetModuleHandle(nullptr),
                               this);

        if (hwnd == nullptr) {
            std::wcerr << L"Failed to create window. Error: " << GetLastError() << std::endl;
            return;
        }
        setNativeWindowHandle(SwWidgetPlatformAdapter::fromNativeHandle(hwnd));

        // Define the callbacks for this window
        SwWin32WindowCallbacks callbacks;
        callbacks.paintHandler = std::bind(&SwMainWindow::onPaint, this, std::placeholders::_1, std::placeholders::_2);
        callbacks.deleteHandler = [this]() { handleDeleteRequest(true); };
        callbacks.mousePressHandler = [this](int x,
                                            int y,
                                            SwMouseButton button,
                                            bool ctrlPressed,
                                            bool shiftPressed,
                                            bool altPressed) {
            onMousePress(x, y, button, ctrlPressed, shiftPressed, altPressed);
        };
        callbacks.mouseReleaseHandler = [this](int x,
                                              int y,
                                              SwMouseButton button,
                                              bool ctrlPressed,
                                              bool shiftPressed,
                                              bool altPressed) {
            onMouseRelease(x, y, button, ctrlPressed, shiftPressed, altPressed);
        };
        callbacks.mouseDoubleClickHandler = [this](int x,
                                                  int y,
                                                  SwMouseButton button,
                                                  bool ctrlPressed,
                                                  bool shiftPressed,
                                                  bool altPressed) {
            onMouseDoubleClick(x, y, button, ctrlPressed, shiftPressed, altPressed);
        };
        callbacks.mouseMoveHandler = [this](int x, int y, bool ctrlPressed, bool shiftPressed, bool altPressed) {
            onMouseMove(x, y, ctrlPressed, shiftPressed, altPressed);
        };
        callbacks.mouseWheelHandler = std::bind(&SwMainWindow::onMouseWheel, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6);
        callbacks.keyPressHandler = std::bind(&SwMainWindow::onKeyPress, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
        callbacks.resizeHandler = std::bind(&SwMainWindow::onResize, this, std::placeholders::_1, std::placeholders::_2);

        // Register this window with SwGuiApplication
        SwWin32PlatformIntegration::registerWindow(hwnd, callbacks);
#elif defined(__linux__)
        initializeX11Window(title, width, height);
#else
        (void)title;
        (void)width;
        (void)height;
#endif
        SwWidget::resize(width, height);
        setWindowTitle(m_windowTitle);
        ensureChrome();
    }

    /**
     * @brief Destructor. Deregisters the window from SwGuiApplication.
     */
    ~SwMainWindow() override {
        handleDeleteRequest(false);
#if defined(_WIN32)
        if (hwnd) {
            SwWin32PlatformIntegration::deregisterWindow(hwnd);
            DestroyWindow(hwnd);
            hwnd = nullptr;
            setNativeWindowHandle(SwWidgetPlatformHandle{});
        }
#elif defined(__linux__)
        m_platformWindow.reset();
        setNativeWindowHandle(SwWidgetPlatformHandle{});
#endif
    }

    /**
     * @brief Shows the window.
     *
     * Displays the window.
     */
    void show() override {
        #if defined(_WIN32)
        if (hwnd) {
            ShowWindow(hwnd, SW_SHOW);
            UpdateWindow(hwnd);
        }
        #elif defined(__linux__)
        if (m_platformWindow) {
            m_platformWindow->show();
        }
        #endif
        SwWidget::show();
    }

    /**
     * @brief Hides the window.
     *
     * Hides the window from the screen.
     */
    void hide() override {
        #if defined(_WIN32)
        if (hwnd) {
            ShowWindow(hwnd, SW_HIDE);
        }
        #elif defined(__linux__)
        if (m_platformWindow) {
            m_platformWindow->hide();
        }
        #endif
        SwWidget::hide();
    }

    /**
     * @brief Minimizes the window.
     *
     * Reduces the window to the taskbar.
     */
    void showMinimized() {
        #if defined(_WIN32)
        if (hwnd) {
            ShowWindow(hwnd, SW_MINIMIZE);
        }
        #endif
    }

    /**
     * @brief Maximizes the window.
     *
     * Expands the window to fill the screen.
     */
    void showMaximized() {
        #if defined(_WIN32)
        if (hwnd) {
            ShowWindow(hwnd, SW_MAXIMIZE);
        }
        #endif
    }

    /**
     * @brief Restores the window.
     *
     * Restores the window to its original size and position.
     */
    void showNormal() {
        #if defined(_WIN32)
        if (hwnd) {
            ShowWindow(hwnd, SW_RESTORE);
        }
        #endif
    }

    // Qt-like main window chrome helpers.
    SwMenuBar* menuBar() { ensureChrome(); return m_menuBar; }
    SwMenuBar* menuBar() const { return m_menuBar; }

    SwToolBar* toolBar() { ensureChrome(); ensureToolBar(); return m_toolBar; }
    SwToolBar* toolBar() const { return m_toolBar; }

    SwStatusBar* statusBar() { ensureChrome(); return m_statusBar; }
    SwStatusBar* statusBar() const { return m_statusBar; }

    SwMenu* helpMenu() { ensureChrome(); return m_helpMenu; }
    SwMenu* helpMenu() const { return m_helpMenu; }

    SwWidget* centralWidget() { ensureChrome(); return m_centralWidget; }
    SwWidget* centralWidget() const { return m_centralWidget; }

    // Backward compatible: layouts are applied to the central widget (Qt-like QMainWindow behavior).
    void setLayout(SwAbstractLayout* layout) { ensureChrome(); if (m_centralWidget) { m_centralWidget->setLayout(layout); } }
    SwAbstractLayout* layout() const { return m_centralWidget ? m_centralWidget->layout() : nullptr; }

    /**
     * @brief Defines the states for the window.
     */
    enum class WindowState {
        Minimized,
        Maximized,
        Normal
    };

    /**
     * @brief Sets the window to the specified state.
     *
     * Adjusts the window's display state based on the provided value.
     *
     * @param state The desired window state.
     */
    void setWindowState(WindowState state) {
#if defined(_WIN32)
        switch (state) {
        case WindowState::Minimized:
            showMinimized();
            break;
        case WindowState::Maximized:
            showMaximized();
            break;
        case WindowState::Normal:
        default:
            showNormal();
            break;
        }
#else
        (void)state;
#endif
    }

    void setWindowTitle(const std::wstring& title) {
        m_windowTitle = title;
        applyWindowTitle();
    }

    const std::wstring& windowTitle() const {
        return m_windowTitle;
    }

    void setWindowFlags(WindowFlags flags) {
#if defined(_WIN32)
        if (!hwnd) return;

        // Récupération des styles actuels
        LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
        LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);

        // Style de base : fenêtre classique
        style = WS_OVERLAPPEDWINDOW;

        // Gestion du style sans cadre (Frameless)
        if (flags.testFlag(WindowFlag::FramelessWindowHint)) {
            // Fenêtre sans bordure ni barre de titre
            style = WS_POPUP;
        } else {
            // Boutons de la barre de titre
            if (flags.testFlag(WindowFlag::NoMinimizeButton)) {
                style &= ~WS_MINIMIZEBOX;
            }
            if (flags.testFlag(WindowFlag::NoMaximizeButton)) {
                style &= ~WS_MAXIMIZEBOX;
            }
        }

        // Fenêtre outil (ToolWindowHint)
        if (flags.testFlag(WindowFlag::ToolWindowHint)) {
            exStyle |= WS_EX_TOOLWINDOW;
            exStyle &= ~WS_EX_APPWINDOW; // pour éviter l'affichage dans la barre des tâches
        }

        // Toujours au-dessus (StayOnTopHint)
        if (flags.testFlag(WindowFlag::StayOnTopHint)) {
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        } else {
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        }

        // Application du style
        SetWindowLongPtr(hwnd, GWL_STYLE, style);
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);

        // Gestion du bouton de fermeture (NoCloseButton)
        HMENU hMenu = GetSystemMenu(hwnd, FALSE);
        if (flags.testFlag(WindowFlag::NoCloseButton)) {
            if (hMenu) {
                RemoveMenu(hMenu, SC_CLOSE, MF_BYCOMMAND);
            }
        } else {
            // Réinsertion du bouton "Fermer" s'il avait été retiré
            if (hMenu && GetMenuState(hMenu, SC_CLOSE, MF_BYCOMMAND) == (UINT)-1) {
                AppendMenuW(hMenu, MF_STRING, SC_CLOSE, L"Close");
            }
        }

        // Forcer la prise en compte des changements
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
        InvalidateRect(hwnd, nullptr, TRUE);
#else
        (void)flags;
#endif
    }

    WindowFlags getWindowFlags() const {
#if defined(_WIN32)
        WindowFlags flags = WindowFlag::NoFlag;
        if (!hwnd) {
            return flags;
        }

        LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
        LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);

        // Déterminer si la fenêtre est frameless
        // On considère qu'elle est frameless si style == WS_POPUP et pas WS_OVERLAPPEDWINDOW
        // (Ajuster selon votre logique interne si nécessaire)
        if ((style & WS_POPUP) == WS_POPUP && (style & WS_OVERLAPPEDWINDOW) != WS_OVERLAPPEDWINDOW) {
            flags |= WindowFlag::FramelessWindowHint;
        } else {
            // Vérifier les boutons minimize/maximize
            if ((style & WS_MINIMIZEBOX) == 0) {
                flags |= WindowFlag::NoMinimizeButton;
            }
            if ((style & WS_MAXIMIZEBOX) == 0) {
                flags |= WindowFlag::NoMaximizeButton;
            }
        }

        // Vérifier ToolWindowHint
        // Si WS_EX_TOOLWINDOW est présent, la fenêtre est probablement un tool window
        if ((exStyle & WS_EX_TOOLWINDOW) == WS_EX_TOOLWINDOW) {
            flags |= WindowFlag::ToolWindowHint;
        }

        // Vérifier StayOnTop
        // Si WS_EX_TOPMOST est présent, alors la fenêtre est au-dessus
        if ((exStyle & WS_EX_TOPMOST) == WS_EX_TOPMOST) {
            flags |= WindowFlag::StayOnTopHint;
        }

        // Vérifier NoCloseButton
        // On regarde dans le menu système si le close est présent
        HMENU hMenu = GetSystemMenu(hwnd, FALSE);
        if (hMenu) {
            UINT state = GetMenuState(hMenu, SC_CLOSE, MF_BYCOMMAND);
            if (state == (UINT)-1) {
                // Le bouton close a été supprimé
                flags |= WindowFlag::NoCloseButton;
            }
        }

        return flags;
#else
        return WindowFlag::NoFlag;
#endif
    }

    // Public event dispatch helpers (useful for popups / overlays).
    void dispatchMousePress(int x, int y) { onMousePress(x, y, SwMouseButton::Left); }
    void dispatchMouseRelease(int x, int y) { onMouseRelease(x, y, SwMouseButton::Left); }

private:
#if defined(_WIN32)
    HWND hwnd{nullptr};
#elif defined(__linux__)
    std::unique_ptr<SwPlatformWindow> m_platformWindow;
    SwX11PlatformIntegration* m_x11Integration{nullptr};
#endif
    std::wstring m_windowTitle;
    struct {
        int x{0};
        int y{0};
    } lastMousePosition;
    std::chrono::steady_clock::time_point lastMoveTime; ///< Last recorded time of mouse movement.
    bool m_deleteHandled{false};

    bool m_chromeInitialized{false};
    SwVerticalLayout* m_chromeLayout{nullptr};
    SwWidget* m_centralWidget{nullptr};
    SwMenuBar* m_menuBar{nullptr};
    SwToolBar* m_toolBar{nullptr};
    SwStatusBar* m_statusBar{nullptr};
    SwMenu* m_helpMenu{nullptr};
    SwDialog* m_aboutDialog{nullptr};

    static SwString defaultAboutHtml() {
        return SwString(
            "<span style='font-weight: bold; color: #0F172A;'>SwCore GUI</span><br/>"
            "SwCore is developed by <b>Eymeric O&#39;Neill</b>.<br/>"
            "A powerful, Qt-inspired C++ toolkit to ship polished desktop tools fast:<br/>"
            "- Build UIs quickly with widgets, layouts, and styling<br/>"
            "- Ship dialogs, menus, toolbars, and more<br/>"
            "- Create custom editors with a graphics canvas<br/>"
            "Between Qt and ROS: build nodes that communicate via RPC<br/>"
            "and inter-process signals/slots, then plug them into a clean desktop UI.<br/>"
            "If SwCore helped you build something cool, I&#39;d love to see it.<br/>"
            "A donation is the best way to say thanks; spreading the word is already plenty.<br/>"
            "License: GPLv3<br/>"
            "Contact: eymeric.oneill@gmail.com | +33 6 52 83 83 31");
    }

    void ensureAboutDialog() {
        if (m_aboutDialog) {
            return;
        }

        m_aboutDialog = new SwDialog(this);
        m_aboutDialog->setMinimumSize(640, 320);
        m_aboutDialog->resize(640, 320);
        m_aboutDialog->setWindowTitle("About SwCore");

        SwWidget* content = m_aboutDialog->contentWidget();
        auto* contentLayout = new SwVerticalLayout(content);
        contentLayout->setMargin(0);
        contentLayout->setSpacing(8);
        content->setLayout(contentLayout);

        auto* text = new SwTextEdit(content);
        text->setReadOnly(true);
        text->setFocusPolicy(FocusPolicyEnum::NoFocus);
        text->setCursor(CursorType::Arrow);
        text->setHtml(defaultAboutHtml());
        text->setStyleSheet(R"(
            SwTextEdit {
                background-color: rgb(241, 245, 249);
                border-color: rgb(241, 245, 249);
                border-width: 0px;
                border-radius: 0px;
                padding: 0px;
                color: rgb(51, 65, 85);
                font-size: 13px;
            }
            SwPlainTextEdit {
                background-color: rgb(241, 245, 249);
                border-color: rgb(241, 245, 249);
                border-width: 0px;
                border-radius: 0px;
                padding: 0px;
                color: rgb(51, 65, 85);
                font-size: 13px;
            }
        )");
        contentLayout->addWidget(text, 1, 0);

        SwWidget* buttonBar = m_aboutDialog->buttonBarWidget();
        auto* buttonLayout = new SwHorizontalLayout(buttonBar);
        buttonLayout->setMargin(0);
        buttonLayout->setSpacing(8);
        buttonBar->setLayout(buttonLayout);

        auto* spacer = new SwWidget(buttonBar);
        spacer->setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        buttonLayout->addWidget(spacer, 1, 0);

        auto* ok = new SwPushButton("OK", buttonBar);
        ok->resize(120, 36);
        SwObject::connect(ok, &SwPushButton::clicked, m_aboutDialog, [this]() {
            if (m_aboutDialog) {
                m_aboutDialog->accept();
            }
        });
        buttonLayout->addWidget(ok, 0, ok->width());
    }

    void showAboutDialog() {
        ensureChrome();
        ensureAboutDialog();
        if (m_aboutDialog) {
            m_aboutDialog->open();
        }
    }

    void ensureChrome() {
        if (m_chromeInitialized) {
            return;
        }
        m_chromeInitialized = true;

        m_menuBar = new SwMenuBar(this);
        m_statusBar = new SwStatusBar(this);

        m_helpMenu = m_menuBar->addMenuRight("?");
        if (m_helpMenu) {
            m_helpMenu->addAction("About...", [this]() { showAboutDialog(); });
        }

        m_centralWidget = new SwWidget(this);
        m_centralWidget->setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");

        auto* chromeLayout = new SwVerticalLayout(this);
        m_chromeLayout = chromeLayout;
        chromeLayout->setMargin(0);
        chromeLayout->setSpacing(0);
        SwWidget::setLayout(chromeLayout);

        if (m_menuBar) {
            chromeLayout->addWidget(m_menuBar, 0, m_menuBar->height());
        }
        if (m_centralWidget) {
            chromeLayout->addWidget(m_centralWidget, 1, 0);
        }
        if (m_statusBar) {
            chromeLayout->addWidget(m_statusBar, 0, m_statusBar->height());
        }
    }

    void ensureToolBar() {
        ensureChrome();
        if (m_toolBar) {
            return;
        }

        m_toolBar = new SwToolBar(this);
        if (m_chromeLayout) {
            const size_t index = m_menuBar ? 1 : 0;
            m_chromeLayout->insertWidget(index, m_toolBar, 0, m_toolBar->height());
        }
    }

    // Event Handlers

    /**
     * @brief Handles paint events.
     * @param hdc Handle to device context.
     * @param rect Rectangle to paint.
     */
#if defined(_WIN32)
    void onPaint(HDC hdc, const RECT& rect) {
        if (!hwnd) {
            return;
        }

        RECT clientRect{};
        GetClientRect(hwnd, &clientRect);
        int clientWidth = clientRect.right - clientRect.left;
        int clientHeight = clientRect.bottom - clientRect.top;
        if (clientWidth <= 0 || clientHeight <= 0) {
            return;
        }

        HDC memDC = CreateCompatibleDC(hdc);
        if (!memDC) {
            return;
        }
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientWidth, clientHeight);
        if (!memBitmap) {
            DeleteDC(memDC);
            return;
        }

        HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memDC, memBitmap));

        SwWin32Painter painter(memDC);
        SwRect paintRect;
        paintRect.x = 0;
        paintRect.y = 0;
        paintRect.width = clientWidth;
        paintRect.height = clientHeight;
        PaintEvent paintEvent(&painter, paintRect);
        this->paintEvent(&paintEvent);
        SwDragDrop::instance().paintOverlay(&painter);

        int copyWidth = rect.right - rect.left;
        int copyHeight = rect.bottom - rect.top;
        BitBlt(hdc, rect.left, rect.top, copyWidth, copyHeight, memDC, rect.left, rect.top, SRCCOPY);

        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
    }
#endif

    void applyWindowTitle() {
#if defined(_WIN32)
        if (hwnd) {
            SetWindowTextW(hwnd, m_windowTitle.c_str());
        }
#elif defined(__linux__)
        if (m_platformWindow) {
            try {
                std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
                m_platformWindow->setTitle(conv.to_bytes(m_windowTitle));
            } catch (...) {
                std::string fallback(m_windowTitle.begin(), m_windowTitle.end());
                m_platformWindow->setTitle(fallback);
            }
        }
#endif
    }

    void handleDeleteRequest(bool requestAppExit) {
        if (m_deleteHandled) {
            return;
        }
        m_deleteHandled = true;
        onDelete();
        if (requestAppExit) {
            if (auto* guiApp = SwGuiApplication::instance(false)) {
                guiApp->exit(0);
            }
        }
    }

    /**
     * @brief Handles window deletion.
     */
    void onDelete() {
        std::wcout << L"MainWindow is being deleted." << std::endl;
    }

    /**
     * @brief Handles resize events.
     *
     * Adjusts the size of the widget based on the given width and height dimensions.
     *
     * @param width The new width of the widget.
     * @param height The new height of the widget.
     */
    void onResize(int width, int height) {
        SwWidget::resize(width, height);
    }


    /**
     * @brief Handles mouse press events.
     * @param x X-coordinate of the mouse.
     * @param y Y-coordinate of the mouse.
     */
    void onMousePress(int x,
                      int y,
                      SwMouseButton button = SwMouseButton::Left,
                      bool ctrlPressed = false,
                      bool shiftPressed = false,
                      bool altPressed = false) {
        SwToolTip::handleMousePress();
        MouseEvent mouseEvent(EventType::MousePressEvent, x, y, button, ctrlPressed, shiftPressed, altPressed);
        SwWidget::mousePressEvent(&mouseEvent);
    }

    /**
     * @brief Handles mouse release events.
     * @param x X-coordinate of the mouse.
     * @param y Y-coordinate of the mouse.
     */
    void onMouseRelease(int x,
                        int y,
                        SwMouseButton button = SwMouseButton::Left,
                        bool ctrlPressed = false,
                        bool shiftPressed = false,
                        bool altPressed = false) {
        MouseEvent mouseEvent(EventType::MouseReleaseEvent, x, y, button, ctrlPressed, shiftPressed, altPressed);
        SwWidget::mouseReleaseEvent(&mouseEvent);
    }

    /**
     * @brief Handles mouse double-click events.
     * @param x X-coordinate of the mouse.
     * @param y Y-coordinate of the mouse.
     */
    void onMouseDoubleClick(int x,
                            int y,
                            SwMouseButton button = SwMouseButton::Left,
                            bool ctrlPressed = false,
                            bool shiftPressed = false,
                            bool altPressed = false) {
        MouseEvent mouseEvent(EventType::MouseDoubleClickEvent, x, y, button, ctrlPressed, shiftPressed, altPressed);
        SwWidget::mouseDoubleClickEvent(&mouseEvent);
    }

    /**
     * @brief Handles mouse move events.
     * @param x X-coordinate of the mouse.
     * @param y Y-coordinate of the mouse.
     */
    void onMouseMove(int x, int y, bool ctrlPressed = false, bool shiftPressed = false, bool altPressed = false) {
        // Calculate delta and speed
        MouseEvent mouseEvent(EventType::MouseMoveEvent, x, y, SwMouseButton::NoButton, ctrlPressed, shiftPressed, altPressed);
        if (lastMoveTime.time_since_epoch().count() != 0) { // Check if lastMoveTime is initialized
            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMoveTime).count();
            int deltaX = x - lastMousePosition.x;
            int deltaY = y - lastMousePosition.y;
            double speedX = duration > 0 ? (static_cast<double>(deltaX) / duration) * 1000 : 0;
            double speedY = duration > 0 ? (static_cast<double>(deltaY) / duration) * 1000 : 0;


            mouseEvent.setDeltaX(deltaX);
            mouseEvent.setDeltaY(deltaY);
            mouseEvent.setSpeedX(speedX);
            mouseEvent.setSpeedY(speedY);

            // Update lastMoveTime
            lastMoveTime = now;
        } else {
            // Initialize lastMoveTime
            lastMoveTime = std::chrono::steady_clock::now();
        }

        // Update lastMousePosition
        lastMousePosition.x = x;
        lastMousePosition.y = y;

        // Optionally, call the base class mouseMoveEvent if necessary
        SwWidgetPlatformAdapter::setCursor(CursorType::Arrow);
        SwWidget::mouseMoveEvent(&mouseEvent);
        SwToolTip::handleMouseMove(this, x, y);
    }

    void onMouseWheel(int x, int y, int delta, bool ctrlPressed, bool shiftPressed, bool altPressed) {
        WheelEvent wheelEvent(x, y, delta, ctrlPressed, shiftPressed, altPressed);

        SwWidget* target = getChildUnderCursor(x, y);
        SwWidget* current = target ? target : this;
        while (current) {
            current->wheelEvent(&wheelEvent);
            if (wheelEvent.isAccepted()) {
                return;
            }
            current = dynamic_cast<SwWidget*>(current->parent());
        }
    }

    /**
     * @brief Handles key press events.
     * @param keyCode Virtual key code.
     * @param ctrlPressed Whether Ctrl is pressed.
     * @param shiftPressed Whether Shift is pressed.
     * @param altPressed Whether Alt is pressed.
     */
    void onKeyPress(int keyCode, bool ctrlPressed, bool shiftPressed, bool altPressed) {
        SwToolTip::handleKeyPress();
        KeyEvent keyEvent(keyCode, ctrlPressed, shiftPressed, altPressed);
        if (SwShortcut::dispatch(this, &keyEvent)) {
            return;
        }
        SwWidget::keyPressEvent(&keyEvent);
    }

#if defined(__linux__)
    static std::string toUtf8(const std::wstring& value) {
        if (value.empty()) {
            return {};
        }
        SwString converted = SwString::fromWCharArray(value.c_str());
        return converted.toStdString();
    }

    void initializeX11Window(const std::wstring& title, int width, int height) {
        SwGuiApplication* app = SwGuiApplication::instance(false);
        if (!app) {
            throw std::runtime_error("SwGuiApplication instance must exist before creating SwMainWindow.");
        }

        m_x11Integration = dynamic_cast<SwX11PlatformIntegration*>(app->platformIntegration());
        if (!m_x11Integration) {
            throw std::runtime_error("X11 platform integration is not available.");
        }

        SwWindowCallbacks callbacks;
        callbacks.paintRequestHandler = [this]() { handlePaintRequest(); };
        callbacks.deleteHandler = [this]() { handleDeleteRequest(true); };
        callbacks.resizeHandler = [this](const SwPlatformSize& size) { onResize(size.width, size.height); };
        callbacks.mousePressHandler = [this](const SwMouseEvent& event) {
            handleMouseEvent(EventType::MousePressEvent, event);
        };
        callbacks.mouseDoubleClickHandler = [this](const SwMouseEvent& event) {
            handleMouseEvent(EventType::MouseDoubleClickEvent, event);
        };
        callbacks.mouseReleaseHandler = [this](const SwMouseEvent& event) {
            handleMouseEvent(EventType::MouseReleaseEvent, event);
        };
        callbacks.mouseMoveHandler = [this](const SwMouseEvent& event) { handleMouseMove(event); };
        callbacks.mouseWheelHandler = [this](const SwMouseEvent& event) { handleWheelEvent(event); };
        callbacks.keyPressHandler = [this](const SwKeyEvent& event) { handleKeyEvent(event); };

        std::string utf8Title = toUtf8(title);
        m_platformWindow = m_x11Integration->createWindow(utf8Title.empty() ? "Main Window" : utf8Title,
                                                          width,
                                                          height,
                                                          callbacks);

        if (auto* x11Window = dynamic_cast<SwX11PlatformWindow*>(m_platformWindow.get())) {
            setNativeWindowHandle(SwWidgetPlatformAdapter::fromNativeHandle(
                reinterpret_cast<void*>(static_cast<std::uintptr_t>(x11Window->handle())),
                m_x11Integration->display()));
        }

        if (m_platformWindow) {
            m_platformWindow->show();
        }
    }

    void handlePaintRequest() {
        if (!m_x11Integration || !m_platformWindow) {
            return;
        }
        auto* x11Window = dynamic_cast<SwX11PlatformWindow*>(m_platformWindow.get());
        if (!x11Window) {
            return;
        }

        SwRect rect = getRect();
        if (rect.width <= 0) {
            rect.width = 1;
        }
        if (rect.height <= 0) {
            rect.height = 1;
        }

        SwX11Painter painter(m_x11Integration->display(), x11Window->handle(), rect.width, rect.height);
        PaintEvent paintEvent(&painter, rect);
        this->paintEvent(&paintEvent);
        SwDragDrop::instance().paintOverlay(&painter);
        painter.finalize();
    }

    void handleMouseEvent(EventType type, const SwMouseEvent& event) {
        switch (type) {
        case EventType::MousePressEvent:
            onMousePress(event.position.x, event.position.y, event.button, event.ctrl, event.shift, event.alt);
            break;
        case EventType::MouseReleaseEvent:
            onMouseRelease(event.position.x, event.position.y, event.button, event.ctrl, event.shift, event.alt);
            break;
        case EventType::MouseDoubleClickEvent:
            onMouseDoubleClick(event.position.x, event.position.y, event.button, event.ctrl, event.shift, event.alt);
            break;
        default:
            break;
        }
    }

    void handleMouseMove(const SwMouseEvent& event) {
        onMouseMove(event.position.x, event.position.y, event.ctrl, event.shift, event.alt);
    }

    void handleWheelEvent(const SwMouseEvent& event) {
        onMouseWheel(event.position.x, event.position.y, event.wheelDelta, event.ctrl, event.shift, event.alt);
    }

    void handleKeyEvent(const SwKeyEvent& event) {
        onKeyPress(event.keyCode, event.ctrl, event.shift, event.alt);
    }
#endif
};
