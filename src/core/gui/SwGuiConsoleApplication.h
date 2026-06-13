
/**
 * @file
 * @ingroup core_gui
 * @brief Declares a console-driven menu shell built on the stack type system.
 *
 * `SwGuiConsoleApplication` organizes actions in a tree of paths and renders a simple
 * interactive textual navigator on standard input and output. It is useful for tooling or
 * demos that want structured navigation without bringing up a native windowing backend.
 */

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

#include <cctype>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "platform/win/SwWindows.h"
#else
#include <termios.h>
#include <unistd.h>
#endif


/**
 * @brief Runs a navigable console menu composed of nodes and callback actions.
 */
class SwGuiConsoleApplication {
    class ConsoleEchoGuard;

public:
    /**
     * @brief Constructs a `SwGuiConsoleApplication` instance.
     * @param true Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwGuiConsoleApplication()
        : m_root(new Node())
        , m_running(true)
    {
        m_root->name = "root";
        m_root->label = "root";
        m_root->parent = nullptr;
        m_storage.push_back(m_root);
    }

    /**
     * @brief Destroys the `SwGuiConsoleApplication` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwGuiConsoleApplication()
    {
        for (Node* node : m_storage) {
            delete node;
        }
    }

    /**
     * @brief Sets the title.
     * @param title Title text applied by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTitle(const SwString& title) { m_title = title; }
    /**
     * @brief Sets the footer.
     * @param footer Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFooter(const SwString& footer) { m_footer = footer; }
    /**
     * @brief Sets the status Message.
     * @param message Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setStatusMessage(const SwString& message) { m_status = message; }

    /**
     * @brief Sets a callback invoked before each menu render.
     *
     * The hook is useful for callers that need to refresh dynamic status text before
     * the menu is drawn.
     */
    void setBeforeRenderCallback(std::function<void()> callback) { m_beforeRender = std::move(callback); }

    /**
     * @brief Adds the specified entry.
     * @param path Path used by the operation.
     * @param label Value passed to the method.
     * @param action Value passed to the method.
     */
    void addEntry(const SwString& path,
                  const SwString& label,
                  std::function<void()> action = std::function<void()>())
    {
        Node* node = ensureNode(path, label);
        if (action) {
            node->action = std::move(action);
        }
    }

    /**
     * @brief Performs the `showModal` operation.
     * @param title Title text applied by the operation.
     * @param body Value passed to the method.
     * @param waitForEnter Value passed to the method.
     */
    void showModal(const SwString& title,
                   const std::function<void()>& body,
                   bool waitForEnter = true)
    {
        clearScreen();
        if (!title.isEmpty()) {
            std::cout << title.toStdString() << "\n\n";
        }
        if (body) {
            body();
        }
        if (waitForEnter) {
            promptLine("\nPress Enter to return...", true);
        }
    }

    /**
     * @brief Prompts for one console line.
     * @param prompt Text displayed before reading.
     * @param echo Whether typed characters should be echoed by the terminal.
     * @return Trimmed user input.
     */
    SwString promptLine(const SwString& prompt, bool echo = true)
    {
        std::cout << prompt.toStdString() << std::flush;
        std::string input;
        if (!readLine(input, echo)) {
            return SwString();
        }
        return normalizedInput_(input);
    }

    /**
     * @brief Returns the current exec.
     * @return The current exec.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int exec()
    {
        Node* current = m_root;
        while (m_running) {
            render(current);

            std::string input;
            if (!readLine(input, true)) {
                break;
            }
            SwString trimmed = normalizedInput_(input);
            if (trimmed.isEmpty()) {
                continue;
            }
            if (trimmed == "q" || trimmed == "Q") {
                m_running = false;
                break;
            }

            Node* cdTarget = nullptr;
            if (resolveCdCommand_(trimmed, current, cdTarget)) {
                current = cdTarget;
                continue;
            }
            if ((trimmed == "b" || trimmed == "..") && current->parent) {
                current = current->parent;
                continue;
            }

            int index = -1;
            const bool ok = parseMenuIndex_(trimmed, index);
            if (!ok) {
                m_status = SwString("Invalid choice: ") + trimmed;
                continue;
            }

            if (index < 0 || index >= current->children.size()) {
                m_status = SwString("Choice out of range.");
                continue;
            }

            Node* selected = current->children[index];
            if (!selected->children.isEmpty()) {
                current = selected;
                continue;
            }
            if (selected->action) {
                selected->action();
            } else {
                m_status = SwString("Entry has no action.");
            }
        }

        clearScreen();
        return 0;
    }

private:
    class ConsoleEchoGuard {
    public:
        explicit ConsoleEchoGuard(bool echo)
            : m_enabled(!echo)
        {
            if (!m_enabled) {
                return;
            }
#if defined(_WIN32)
            m_input = GetStdHandle(STD_INPUT_HANDLE);
            if (m_input == INVALID_HANDLE_VALUE || !GetConsoleMode(m_input, &m_originalMode)) {
                m_enabled = false;
                return;
            }
            SetConsoleMode(m_input, m_originalMode & ~ENABLE_ECHO_INPUT);
#else
            if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &m_originalTermios) != 0) {
                m_enabled = false;
                return;
            }
            termios noEcho = m_originalTermios;
            noEcho.c_lflag &= ~ECHO;
            tcsetattr(STDIN_FILENO, TCSANOW, &noEcho);
#endif
        }

        ~ConsoleEchoGuard()
        {
            if (!m_enabled) {
                return;
            }
#if defined(_WIN32)
            SetConsoleMode(m_input, m_originalMode);
#else
            tcsetattr(STDIN_FILENO, TCSANOW, &m_originalTermios);
#endif
        }

    private:
        bool m_enabled{false};
#if defined(_WIN32)
        HANDLE m_input{INVALID_HANDLE_VALUE};
        DWORD m_originalMode{0};
#else
        termios m_originalTermios{};
#endif
    };

    static bool readLine(std::string& input, bool echo)
    {
        input.clear();
        {
            ConsoleEchoGuard echoGuard(echo);
            if (std::getline(std::cin, input)) {
                if (!echo) {
                    std::cout << "\n";
                }
                return true;
            }
            std::cin.clear();
        }

#if defined(_WIN32)
        return readLineFromWindowsConsole_(input, echo);
#else
        return false;
#endif
    }

#if defined(_WIN32)
    static bool readLineFromWindowsConsole_(std::string& input, bool echo)
    {
        input.clear();

        HANDLE console = GetStdHandle(STD_INPUT_HANDLE);
        bool closeConsole = false;
        DWORD originalMode = 0;
        if (console == INVALID_HANDLE_VALUE || !GetConsoleMode(console, &originalMode)) {
            console = CreateFileA("CONIN$",
                                  GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr,
                                  OPEN_EXISTING,
                                  0,
                                  nullptr);
            closeConsole = true;
            if (console == INVALID_HANDLE_VALUE || !GetConsoleMode(console, &originalMode)) {
                if (closeConsole && console != INVALID_HANDLE_VALUE) {
                    CloseHandle(console);
                }
                return false;
            }
        }

        const DWORD readMode = echo ? originalMode : (originalMode & ~ENABLE_ECHO_INPUT);
        SetConsoleMode(console, readMode | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);

        char buffer[256] = {};
        bool ok = false;
        for (;;) {
            DWORD read = 0;
            if (!ReadConsoleA(console, buffer, static_cast<DWORD>(sizeof(buffer) - 1), &read, nullptr)) {
                break;
            }
            if (read == 0) {
                break;
            }
            ok = true;
            buffer[read] = '\0';
            input.append(buffer, buffer + read);
            if (input.find('\n') != std::string::npos || input.find('\r') != std::string::npos) {
                break;
            }
        }

        SetConsoleMode(console, originalMode);
        if (closeConsole) {
            CloseHandle(console);
        }
        if (ok && !echo) {
            std::cout << "\n";
        }
        return ok;
    }
#endif

    struct Node {
        SwString name;
        SwString label;
        Node* parent{nullptr};
        SwVector<Node*> children;
        /**
         * @brief Returns the current function<void.
         * @return The current function<void.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        std::function<void()> action;
    };

    Node* ensureNode(const SwString& path, const SwString& label)
    {
        Node* node = m_root;
        SwVector<SwString> parts = splitPath(path);
        for (size_t i = 0; i < parts.size(); ++i) {
            const SwString& part = parts[i];
            Node* child = nullptr;
            for (Node* candidate : node->children) {
                if (candidate->name == part) {
                    child = candidate;
                    break;
                }
            }
            if (!child) {
                child = new Node();
                child->name = part;
                child->label = part;
                child->parent = node;
                node->children.push_back(child);
                m_storage.push_back(child);
            }
            node = child;
        }
        if (!label.isEmpty()) {
            node->label = label;
        }
        return node;
    }

    static SwVector<SwString> splitPath(const SwString& path)
    {
        SwVector<SwString> parts;
        SwStringList tokens = path.split('/');
        for (const SwString& token : tokens) {
            SwString trimmed = token.trimmed();
            if (!trimmed.isEmpty()) {
                parts.push_back(trimmed);
            }
        }
        return parts;
    }

    static SwString normalizedInput_(const std::string& input)
    {
        std::string value = input;
        if (value.size() >= 3 &&
            static_cast<unsigned char>(value[0]) == 0xef &&
            static_cast<unsigned char>(value[1]) == 0xbb &&
            static_cast<unsigned char>(value[2]) == 0xbf) {
            value.erase(0, 3);
        }
        return SwString(value).trimmed();
    }

    static bool parseMenuIndex_(const SwString& value, int& index)
    {
        const std::string text = value.toStdString();
        if (text.empty()) {
            return false;
        }
        for (size_t i = 0; i < text.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
                return false;
            }
        }
        char* end = nullptr;
        const long parsed = std::strtol(text.c_str(), &end, 10);
        if (!end || *end != '\0' || parsed < 0 || parsed > std::numeric_limits<int>::max()) {
            return false;
        }
        index = static_cast<int>(parsed);
        return true;
    }

    bool resolveCdCommand_(const SwString& value, Node* current, Node*& target) const
    {
        target = nullptr;
        std::string text = value.toStdString();
        for (size_t i = 0; i < text.size(); ++i) {
            text[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
        }
        if (text.size() < 2 || text.substr(0, 2) != "cd") {
            return false;
        }
        if (text.size() > 2 && !std::isspace(static_cast<unsigned char>(text[2]))) {
            return false;
        }

        std::string arg = text.size() > 2 ? text.substr(2) : std::string();
        trimAscii_(arg);
        if (arg.empty() || arg == "/" || arg == "\\") {
            target = m_root;
            return true;
        }
        if (arg == "..") {
            target = current->parent ? current->parent : current;
            return true;
        }
        return false;
    }

    static void trimAscii_(std::string& value)
    {
        size_t first = 0;
        while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
            ++first;
        }
        size_t last = value.size();
        while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
            --last;
        }
        value = value.substr(first, last - first);
    }

    SwString currentPath(Node* node) const
    {
        SwVector<SwString> tokens;
        while (node && node != m_root) {
            tokens.push_back(node->label);
            node = node->parent;
        }
        SwString path;
        for (int i = tokens.size() - 1; i >= 0; --i) {
            path += "/";
            path += tokens[i];
        }
        if (path.isEmpty()) {
            path = "/";
        }
        return path;
    }

    void render(Node* current)
    {
        if (m_beforeRender) {
            m_beforeRender();
        }
        clearScreen();
        std::cout << (m_title.isEmpty() ? "SwGuiConsole" : m_title.toStdString()) << "\n";
        std::cout << "Path: " << currentPath(current).toStdString() << "\n\n";

        if (current->children.isEmpty()) {
            std::cout << "(No entries)\n";
        } else {
            for (int i = 0; i < current->children.size(); ++i) {
                Node* child = current->children[i];
                std::cout << "[" << i << "] " << child->label.toStdString()
                          << (child->children.isEmpty() ? "" : " >") << "\n";
            }
        }

        std::cout << "\n";
        if (!m_footer.isEmpty()) {
            std::cout << m_footer.toStdString() << "\n";
        } else {
            std::cout << "[b] Back  [q] Quit\n";
        }
        if (!m_status.isEmpty()) {
            std::cout << "Status: " << m_status.toStdString() << "\n";
        }
        std::cout << "> " << std::flush;
        m_status.clear();
    }

    void clearScreen() const
    {
#if defined(_WIN32)
        HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        if (output == INVALID_HANDLE_VALUE) {
            std::cout << "\033[3J\033[2J\033[H" << std::flush;
            return;
        }

        CONSOLE_SCREEN_BUFFER_INFO info;
        if (!GetConsoleScreenBufferInfo(output, &info)) {
            std::cout << "\033[3J\033[2J\033[H" << std::flush;
            return;
        }

        const DWORD cells = static_cast<DWORD>(info.dwSize.X) * static_cast<DWORD>(info.dwSize.Y);
        const COORD home = {0, 0};
        DWORD written = 0;
        FillConsoleOutputCharacterA(output, ' ', cells, home, &written);
        FillConsoleOutputAttribute(output, info.wAttributes, cells, home, &written);
        SetConsoleCursorPosition(output, home);
#else
        std::cout << "\033[2J\033[H" << std::flush;
#endif
    }

    Node* m_root;
    SwVector<Node*> m_storage;
    SwString m_title;
    SwString m_footer;
    SwString m_status;
    std::function<void()> m_beforeRender;
    bool m_running;
};
