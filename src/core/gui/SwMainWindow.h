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

/**
 * @file src/core/gui/SwMainWindow.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwMainWindow in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the main window interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwMainWindow.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */

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
        wc.hbrBackground = CreateSolidBrush(RGB(249, 249, 249));

        // Attempt to register the window class
        if (!RegisterClassW(&wc)) {
            if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                std::wcerr << L"Failed to register window class. Error: " << GetLastError() << std::endl;
                return;
            }
        }

        // Create the window
        const std::wstring nativeTitle = toNativeWindowsTitle_(m_windowTitle);
        hwnd = CreateWindowExW(0,
                               CLASS_NAME,
                               nativeTitle.c_str(),
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
        callbacks.keyPressHandler = std::bind(&SwMainWindow::onKeyPress, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6);
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
    }

    /**
     * @brief Constructs a `SwMainWindow` instance.
     * @param title Title text applied by the operation.
     * @param width Width value.
     * @param height Height value.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwMainWindow(const SwString& title, int width = 800, int height = 600)
        : SwMainWindow(title.toStdWString(), width, height) {}

    /**
     * @brief Constructs a `SwMainWindow` instance.
     * @param title Title text applied by the operation.
     * @param width Width value.
     * @param height Height value.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwMainWindow(const wchar_t* title, int width = 800, int height = 600)
        : SwMainWindow(title ? std::wstring(title) : std::wstring(), width, height) {}

    /**
     * @brief Constructs a `SwMainWindow` instance.
     * @param title Title text applied by the operation.
     * @param width Width value.
     * @param height Height value.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwMainWindow(const char* title, int width = 800, int height = 600)
        : SwMainWindow(decodeWindowTitleFromChar_(title), width, height) {}

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

    // Main window chrome helpers.
    /**
     * @brief Performs the `menuBar` operation.
     * @return The requested menu Bar.
     */
    SwMenuBar* menuBar() { ensureChrome(); return m_menuBar; }
    /**
     * @brief Returns the current menu Bar.
     * @return The current menu Bar.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwMenuBar* menuBar() const { return m_menuBar; }

    /**
     * @brief Performs the `toolBar` operation.
     * @return The requested tool Bar.
     */
    SwToolBar* toolBar() { ensureChrome(); ensureToolBar(); return m_toolBar; }
    /**
     * @brief Returns the current tool Bar.
     * @return The current tool Bar.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwToolBar* toolBar() const { return m_toolBar; }

    /**
     * @brief Performs the `statusBar` operation.
     * @return The requested status Bar.
     */
    SwStatusBar* statusBar() { ensureChrome(); return m_statusBar; }
    /**
     * @brief Returns the current status Bar.
     * @return The current status Bar.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwStatusBar* statusBar() const { return m_statusBar; }

    /**
     * @brief Performs the `helpMenu` operation.
     * @return The requested help Menu.
     */
    SwMenu* helpMenu() { ensureChrome(); return m_helpMenu; }
    /**
     * @brief Returns the current help Menu.
     * @return The current help Menu.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwMenu* helpMenu() const { return m_helpMenu; }

    /**
     * @brief Performs the `centralWidget` operation.
     * @return The requested central Widget.
     */
    SwWidget* centralWidget() { ensureChrome(); return m_centralWidget; }
    /**
     * @brief Returns the current central Widget.
     * @return The current central Widget.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwWidget* centralWidget() const { return m_centralWidget; }

    // Backward compatible: layouts are applied to the central widget.
    /**
     * @brief Sets the layout.
     * @param layout Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setLayout(SwAbstractLayout* layout) { ensureChrome(); if (m_centralWidget) { m_centralWidget->setLayout(layout); } }
    /**
     * @brief Performs the `layout` operation.
     * @return The requested layout.
     */
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

    /**
     * @brief Sets the window Title.
     * @param title Title text applied by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setWindowTitle(const std::wstring& title) {
        m_windowTitle = title;
        applyWindowTitle();
    }

    /**
     * @brief Sets the window Title.
     * @param title Title text applied by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setWindowTitle(const SwString& title) {
        setWindowTitle(title.toStdWString());
    }

    /**
     * @brief Sets the window Title.
     * @param title Title text applied by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setWindowTitle(const wchar_t* title) {
        setWindowTitle(title ? std::wstring(title) : std::wstring());
    }

    /**
     * @brief Sets the window Title.
     * @param title Title text applied by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setWindowTitle(const char* title) {
        setWindowTitle(decodeWindowTitleFromChar_(title));
    }

    /**
     * @brief Returns the current window Title.
     * @return The current window Title.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const std::wstring& windowTitle() const {
        return m_windowTitle;
    }

    /**
     * @brief Sets the window Flags.
     * @param flags Flags that refine the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setWindowFlags(WindowFlags flags) {
#if defined(_WIN32)
        if (!hwnd) return;

        // RÃ©cupÃ©ration des styles actuels
        LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
        LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);

        // Style de base : fenÃªtre classique
        style = WS_OVERLAPPEDWINDOW;

        // Gestion du style sans cadre (Frameless)
        if (flags.testFlag(WindowFlag::FramelessWindowHint)) {
            // FenÃªtre sans bordure ni barre de titre
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

        // FenÃªtre outil (ToolWindowHint)
        if (flags.testFlag(WindowFlag::ToolWindowHint)) {
            exStyle |= WS_EX_TOOLWINDOW;
            exStyle &= ~WS_EX_APPWINDOW; // pour Ã©viter l'affichage dans la barre des tÃ¢ches
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
            // RÃ©insertion du bouton "Fermer" s'il avait Ã©tÃ© retirÃ©
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

    /**
     * @brief Returns the current window Flags.
     * @return The current window Flags.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    WindowFlags getWindowFlags() const {
#if defined(_WIN32)
        WindowFlags flags = WindowFlag::NoFlag;
        if (!hwnd) {
            return flags;
        }

        LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
        LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);

        // DÃ©terminer si la fenÃªtre est frameless
        // On considÃ¨re qu'elle est frameless si style == WS_POPUP et pas WS_OVERLAPPEDWINDOW
        // (Ajuster selon votre logique interne si nÃ©cessaire)
        if ((style & WS_POPUP) == WS_POPUP && (style & WS_OVERLAPPEDWINDOW) != WS_OVERLAPPEDWINDOW) {
            flags |= WindowFlag::FramelessWindowHint;
        } else {
            // VÃ©rifier les boutons minimize/maximize
            if ((style & WS_MINIMIZEBOX) == 0) {
                flags |= WindowFlag::NoMinimizeButton;
            }
            if ((style & WS_MAXIMIZEBOX) == 0) {
                flags |= WindowFlag::NoMaximizeButton;
            }
        }

        // VÃ©rifier ToolWindowHint
        // Si WS_EX_TOOLWINDOW est prÃ©sent, la fenÃªtre est probablement un tool window
        if ((exStyle & WS_EX_TOOLWINDOW) == WS_EX_TOOLWINDOW) {
            flags |= WindowFlag::ToolWindowHint;
        }

        // VÃ©rifier StayOnTop
        // Si WS_EX_TOPMOST est prÃ©sent, alors la fenÃªtre est au-dessus
        if ((exStyle & WS_EX_TOPMOST) == WS_EX_TOPMOST) {
            flags |= WindowFlag::StayOnTopHint;
        }

        // VÃ©rifier NoCloseButton
        // On regarde dans le menu systÃ¨me si le close est prÃ©sent
        HMENU hMenu = GetSystemMenu(hwnd, FALSE);
        if (hMenu) {
            UINT state = GetMenuState(hMenu, SC_CLOSE, MF_BYCOMMAND);
            if (state == (UINT)-1) {
                // Le bouton close a Ã©tÃ© supprimÃ©
                flags |= WindowFlag::NoCloseButton;
            }
        }

        return flags;
#else
        return WindowFlag::NoFlag;
#endif
    }

    // Public event dispatch helpers (useful for popups / overlays).
    /**
     * @brief Performs the `dispatchMousePress` operation.
     * @param x Horizontal coordinate.
     * @param Left Value passed to the method.
     */
    void dispatchMousePress(int x, int y) { onMousePress(x, y, SwMouseButton::Left); }
    /**
     * @brief Performs the `dispatchMouseRelease` operation.
     * @param x Horizontal coordinate.
     * @param Left Value passed to the method.
     */
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
            "<span style='color:#9CA3AF;'>version 1.0  -  Apache License 2.0  -  Ariya Consulting</span><br/>"
            "<br/>"
            "<b>SwStack</b> is a Qt-inspired C++ framework for building polished<br/>"
            "cross-platform desktop applications on Windows and Linux,<br/>"
            "without any commercial licensing constraints.<br/>"
            "<br/>"
            "<b>What it includes</b><br/>"
            "- Widgets, layouts, dialogs, menus, toolbars, status bars<br/>"
            "- Typed signals and slots, property system, stylesheet engine<br/>"
            "- IPC and RPC: live inter-process signals across processes<br/>"
            "- SwNode: distributed computing with desktop integration<br/>"
            "- Node-based graphics canvas for custom visual editors<br/>"
            "<br/>"
            "<span style='color:#9CA3AF;'>Developed by Eymeric O'Neill<br/>"
            "eymeric.oneill@gmail.com</span>"
        );
    }

    void ensureAboutDialog() {
        if (m_aboutDialog) {
            return;
        }

        m_aboutDialog = new SwDialog(this);
        m_aboutDialog->setMinimumSize(460, 380);
        m_aboutDialog->resize(460, 380);
        m_aboutDialog->setWindowTitle("About SwStack");

        // â”€â”€ Body fills the entire content area â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        SwWidget* content = m_aboutDialog->contentWidget();
        auto* contentLayout = new SwVerticalLayout(content);
        contentLayout->setMargin(0);
        contentLayout->setSpacing(0);
        content->setLayout(contentLayout);

        auto* body = new SwTextEdit(content);
        body->setReadOnly(true);
        body->setFocusPolicy(FocusPolicyEnum::NoFocus);
        body->setCursor(CursorType::Arrow);
        body->setHtml(defaultAboutHtml());
        body->setFont(SwFont(L"Segoe UI", 10, Normal));
        body->setStyleSheet(R"(
            SwTextEdit {
                background-color: rgb(255,255,255);
                border-width: 0px;
                border-radius: 0px;
                padding: 2px 4px;
                color: rgb(31,41,55);
            }
            SwPlainTextEdit {
                background-color: rgb(255,255,255);
                border-width: 0px;
                border-radius: 0px;
                padding: 2px 4px;
                color: rgb(31,41,55);
            }
        )");
        contentLayout->addWidget(body, 1, 0);

        // â”€â”€ Button bar: licence label left, Close button right â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        SwWidget* buttonBar = m_aboutDialog->buttonBarWidget();
        auto* buttonLayout = new SwHorizontalLayout(buttonBar);
        buttonLayout->setMargin(0);
        buttonLayout->setSpacing(0);
        buttonBar->setLayout(buttonLayout);

        auto* licLabel = new SwLabel("Apache License 2.0", buttonBar);
        licLabel->setFont(SwFont(L"Segoe UI", 9, Normal));
        licLabel->setStyleSheet(R"(
            SwLabel {
                background-color: rgba(0,0,0,0);
                border-width: 0px;
                color: rgb(156,163,175);
            }
        )");
        buttonLayout->addWidget(licLabel, 0, 140);

        auto* spacer = new SwWidget(buttonBar);
        spacer->setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        buttonLayout->addWidget(spacer, 1, 0);

        auto* ok = new SwPushButton("Close", buttonBar);
        ok->resize(90, 32);
        ok->setStyleSheet(R"(
            SwPushButton {
                background-color: rgb(55,65,81);
                color: rgb(255,255,255);
                border-radius: 6px;
                border-width: 0px;
            }
        )");
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
            const std::wstring nativeTitle = toNativeWindowsTitle_(m_windowTitle);
            SetWindowTextW(hwnd, nativeTitle.c_str());
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
            WheelEvent localEvent = mapWheelEventToChild_(wheelEvent, this, current);
            current->wheelEvent(&localEvent);
            if (localEvent.isAccepted()) {
                wheelEvent.accept();
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
    void onKeyPress(int keyCode, bool ctrlPressed, bool shiftPressed, bool altPressed, wchar_t textChar = L'\0', bool textProvided = false) {
        SwToolTip::handleKeyPress();
        KeyEvent keyEvent(keyCode, ctrlPressed, shiftPressed, altPressed, textChar, textProvided);
        if (SwShortcut::dispatch(this, &keyEvent)) {
            return;
        }
        SwWidget::keyPressEvent(&keyEvent);
    }

#if defined(_WIN32)
    static std::wstring decodeWindowTitleFromChar_(const char* title) {
        if (!title) {
            return std::wstring();
        }
        // Defensive compatibility: handle accidental UTF-16LE pointer passed as char*.
        if (title[0] != '\0' && title[1] == '\0') {
            const wchar_t* wide = reinterpret_cast<const wchar_t*>(title);
            return wide ? std::wstring(wide) : std::wstring();
        }
        return SwString::fromUtf8(title).toStdWString();
    }

    static std::wstring toNativeWindowsTitle_(const std::wstring& title) {
        if (title.empty()) {
            return std::wstring();
        }
        if (title.find(L'\0') == std::wstring::npos) {
            return title;
        }
        // Defensive: strip embedded NULs to avoid Win32 truncating at first character.
        std::wstring sanitized;
        sanitized.reserve(title.size());
        for (wchar_t ch : title) {
            if (ch != L'\0') {
                sanitized.push_back(ch);
            }
        }
        return sanitized;
    }
#endif

#if !defined(_WIN32)
    static std::wstring decodeWindowTitleFromChar_(const char* title) {
        return title ? SwString::fromUtf8(title).toStdWString() : std::wstring();
    }
#endif

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

        SwRect clientRect = SwWidgetPlatformAdapter::clientRect(nativeWindowHandle());
        if (clientRect.width <= 0) {
            clientRect.width = width();
        }
        if (clientRect.height <= 0) {
            clientRect.height = height();
        }

        if (clientRect.width <= 0) {
            clientRect.width = 1;
        }
        if (clientRect.height <= 0) {
            clientRect.height = 1;
        }

        if (width() != clientRect.width || height() != clientRect.height) {
            SwWidget::resize(clientRect.width, clientRect.height);
        }

        SwRect rect = this->rect();
        rect.width = clientRect.width;
        rect.height = clientRect.height;

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
        onKeyPress(event.keyCode, event.ctrl, event.shift, event.alt, event.text, event.textProvided);
    }
#endif
};

