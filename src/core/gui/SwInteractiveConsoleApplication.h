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
 * @file src/core/gui/SwInteractiveConsoleApplication.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwInteractiveConsoleApplication in the CoreSw
 * GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the interactive console application interface. The
 * declarations exposed here define the stable surface that adjacent code can rely on while the
 * implementation remains free to evolve behind the header.
 *
 * The main declarations in this header are NonBlockingReader and SwInteractiveConsoleApplication.
 *
 * Application-oriented declarations here define the top-level lifecycle surface for startup,
 * shutdown, event processing, and integration with the rest of the framework.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */


#include <SwCommandLineParser.h>
#include <SwCommandLineOption.h>
#include <SwJsonDocument.h>
#include <SwString.h>
#include <SwList.h>
#include <SwMap.h>
#include <SwTimer.h>
#include <functional>
#include <sstream>
#include <string>
#include <iostream>
static constexpr const char* kSwLogCategory_SwInteractiveConsoleApplication = "sw.core.gui.swinteractiveconsoleapplication";


#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "platform/win/SwWindows.h"
#else
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/select.h>
#endif


class NonBlockingReader {
public:
    /**
     * @brief Constructs a `NonBlockingReader` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    NonBlockingReader() {
#if defined(_WIN32)
        hStdin = GetStdHandle(STD_INPUT_HANDLE);
        if (hStdin == INVALID_HANDLE_VALUE) {
            swCError(kSwLogCategory_SwInteractiveConsoleApplication) << "Failed to get STD_INPUT_HANDLE";
        }
        DWORD mode;
        GetConsoleMode(hStdin, &mode);
        // Désactiver LINE_INPUT, activer la lecture immédiate
        // Désactiver ECHO_INPUT pour gérer l'écho manuellement
        // ENABLE_PROCESSED_INPUT permet toujours l'interprétation de Ctrl-C etc.
        SetConsoleMode(hStdin, (mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT)) | ENABLE_PROCESSED_INPUT);
#else
        hStdin = STDIN_FILENO;
        tcgetattr(hStdin, &origTermios);
        termios raw = origTermios;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(hStdin, TCSANOW, &raw);
        int flags = fcntl(hStdin, F_GETFL, 0);
        fcntl(hStdin, F_SETFL, flags | O_NONBLOCK);
#endif
    }

    /**
     * @brief Destroys the `NonBlockingReader` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~NonBlockingReader() {
#if !defined(_WIN32)
        tcsetattr(hStdin, TCSANOW, &origTermios);
#endif
    }

    /**
     * @brief Returns the current line Non Blocking.
     * @return The current line Non Blocking.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::string readLineNonBlocking() {
#if defined(_WIN32)
        DWORD numEvents = 0;
        if (!GetNumberOfConsoleInputEvents(hStdin, &numEvents)) {
            return std::string();
        }

        if (numEvents == 0) {
            // Aucun événement
            return std::string();
        }

        INPUT_RECORD ir[128];
        DWORD eventsPeeked = 0;

        if (!PeekConsoleInput(hStdin, ir, 128, &eventsPeeked) || eventsPeeked == 0) {
            return std::string();
        }

        DWORD eventsRead = 0;
        if (!ReadConsoleInput(hStdin, ir, eventsPeeked, &eventsRead)) {
            return std::string();
        }

        for (DWORD i = 0; i < eventsRead; ++i) {
            if (ir[i].EventType == KEY_EVENT && ir[i].Event.KeyEvent.bKeyDown) {
                KEY_EVENT_RECORD ker = ir[i].Event.KeyEvent;
                WCHAR ch = ker.uChar.UnicodeChar;

                // Gérer les touches spéciales (Entrée, Backspace)
                if (ch == '\r') {
                    // Entrée: retourner la ligne actuelle
                    swCDebug(kSwLogCategory_SwInteractiveConsoleApplication) << "\n";
                    std::string line = m_buffer;
                    m_buffer.clear();
                    return line;
                } else if (ker.wVirtualKeyCode == VK_BACK) {
                    // Backspace
                    if (!m_buffer.empty()) {
                        m_buffer.pop_back();
                        // Effacer le dernier caractère à l'écran
                        swCDebug(kSwLogCategory_SwInteractiveConsoleApplication) << "\b \b";
                    }
                } else if (ch >= 0x20 && ch != 0x7F) {
                    // Caractère imprimable
                    m_buffer.push_back((char)ch);
                    swCDebug(kSwLogCategory_SwInteractiveConsoleApplication) << (char)ch; // Echo manuel du caractère
                }
                // Sinon, ignorer les autres touches (flèches, Ctrl, etc.)
            }
        }

        return std::string();
#else
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(hStdin, &readfds);
        timeval tv {0, 0};
        int ready = select(hStdin + 1, &readfds, nullptr, nullptr, &tv);
        if (ready <= 0) {
            return std::string();
        }

        char buffer[64];
        ssize_t bytes = ::read(hStdin, buffer, sizeof(buffer));
        if (bytes <= 0) {
            return std::string();
        }

        for (ssize_t i = 0; i < bytes; ++i) {
            char ch = buffer[i];
            if (ch == '\r' || ch == '\n') {
                if (!m_buffer.empty()) {
                    swCDebug(kSwLogCategory_SwInteractiveConsoleApplication) << "";
                    std::string line = m_buffer;
                    m_buffer.clear();
                    return line;
                }
            } else if (ch == 0x7f || ch == '\b') {
                if (!m_buffer.empty()) {
                    m_buffer.pop_back();
                    swCDebug(kSwLogCategory_SwInteractiveConsoleApplication) << "\b \b";
                }
            } else if (ch >= 0x20) {
                m_buffer.push_back(ch);
                swCDebug(kSwLogCategory_SwInteractiveConsoleApplication) << ch;
            }
        }

        return std::string();
#endif
    }

private:
#if defined(_WIN32)
    HANDLE hStdin;
#else
    int hStdin;
    termios origTermios;
#endif
    std::string m_buffer;
};



/**
 * Interactive console application to navigate through a JSON document.
 *
 * - Object nodes are considered sub-menus.
 * - Leaf nodes (values) are displayed with their value and an indicator [R] or [R/W].
 *   - [R] if no action is registered for that value.
 *   - [R/W] if an action (command) is registered via registerCommand().
 *
 * Supported commands:
 *  - help : Show help
 *  - pwd : Show current path
 *  - dir : List sub-elements of the current path
 *  - cd <name> : Navigate into a sub-node
 *  - cd.. : Go up one level
 *
 * Example of how to modify a leaf value:
 *
 *    interactiveApp.registerCommand("settings/display/brightness", [&](const SwString &value) {
 *        swCDebug(kSwLogCategory_SwInteractiveConsoleApplication) << "Current brightness: " << value.toStdString() << "\n";
 *        swCDebug(kSwLogCategory_SwInteractiveConsoleApplication) << "Enter a new value (q to quit without changing): ";
 *        std::string newVal = waitForNewValue("settings/display/brightness", "q");
 *        if (!newVal.empty()) {
 *            swCDebug(kSwLogCategory_SwInteractiveConsoleApplication) << "New brightness: " << newVal << "\n";
 *        } else {
 *            swCDebug(kSwLogCategory_SwInteractiveConsoleApplication) << "Value unchanged.\n";
 *        }
 *    });
 *
 * Additionally, when single line mode is enabled with setSingleLineMode(true),
 * the user input is always taken from the first line. After each command, the screen
 * is cleared and the new info is displayed below the prompt.
 */
class SwInteractiveConsoleApplication: public SwObject {
public:

    /**
     * @brief Constructs the interactive console application.
     *
     * Initializes the JSON document @p config, ensuring the root node is an object, then starts an internal timer 
     * to poll standard input. This constructor also registers the built-in commands and displays the initial prompt.
     *
     * @param config Reference to the JSON document to be explored and possibly modified.
     */
    SwInteractiveConsoleApplication(SwJsonDocument &config)
        : m_config(config)
        , m_timer()
        , m_currentPath("")
        , m_singleLineMode(false)
    {
        // Check if root is a valid object
        SwJsonValue &rootNode = m_config.find("", true);
        if (!rootNode.isObject()) {
            rootNode = SwJsonObject();
        }


        m_timer.setInterval(100);
        connect(&m_timer, &SwTimer::timeout, this, &SwInteractiveConsoleApplication::pollInput);
        m_timer.start();



        registerNativeCommands();
        printPrompt();
    }

    /**
     * @brief Registers a custom command (callback) for a specified JSON path.
     *
     * This method allows you to associate a callback (lambda, function, etc.) with a particular value node in the JSON document.
     * When the user inputs that JSON path in the console, the registered action is triggered. This feature enables 
     * read/write operations on leaf nodes as well as complex business logic.
     *
     * @param path The full JSON path to the value (e.g., "settings/display/brightness").
     * @param action A function that takes the current node value as an argument. Within this function, you can 
     *               display the current value, prompt the user for a new value (using @c waitForNewValue()), 
     *               or perform other modifications.
     */
    void registerCommand(const SwString &path, std::function<void(const SwString &value)> action) {
        m_commands[path] = action;
    }

    /**
     * @brief Adds a comment to a given JSON path.
     *
     * Registered comments can be displayed when showing the help (via @c help) or upon entering a node (during navigation).
     * This provides contextual information about certain paths.
     *
     * @param path The JSON path to annotate.
     * @param comment The comment string associated with the given path.
     */
    void addComment(const SwString &path, const SwString &comment) {
        m_comments[path] = comment;
    }

    /**
     * @brief Waits for the user to input a new value for a specified node.
     *
     * Prompts the user to enter a new value. If the entered value differs from the escape command @p esc, 
     * it is written to the JSON document (via @c setValue()). If the user enters the escape command, 
     * no changes are applied.
     *
     * @param path The JSON path to modify. If empty, the function simply returns the user's input without updating the document.
     * @param esc The escape command (e.g., "q") that cancels the operation. If empty, all user input is considered valid.
     * @return The new user-entered value, or an empty string if the operation was canceled or an error occurred.
     */
    std::string waitForNewValue(const SwString &path = "", const SwString &esc = "") {
        if(!esc.isEmpty()) {
            swCDebug(kSwLogCategory_SwInteractiveConsoleApplication) << "(" << esc.toStdString() << " to cancel): ";
        }
        std::string line;
        if (!std::getline(std::cin, line)) {
            return std::string();
        }
        if (!esc.isEmpty() && line == esc.toStdString()) {
            // No change
            return std::string();
        }

        if(!path.isEmpty()) {
            // Update the document if not the escape command
            if (setValue(path, SwString(line))) {
                swCDebug(kSwLogCategory_SwInteractiveConsoleApplication) << "Value updated for " << path.toStdString() << " : " << line << "\n";
            } else {
                swCWarning(kSwLogCategory_SwInteractiveConsoleApplication) << "Unable to update value for " << path.toStdString() << "\n";
            }
        }

        return line;
    }

    /**
     * @brief Updates the value of a JSON node.
     *
     * Attempts to write a new value to a given node. If the path is invalid, or the node is an object (not a leaf),
     * no update is performed.
     *
     * @param path The full JSON path to the node to be updated.
     * @param newValue The new value to set.
     * @return @c true if the value was successfully updated, @c false otherwise.
     */
    bool setValue(const SwString &path, const SwString &newValue) {
        SwJsonValue &node = m_config.find(path, true);
        if (!node.isValid() || node.isObject()) {
            // Invalid node or it's an object -> no update
            return false;
        }
        node = SwJsonValue(newValue);
        return true;
    }

    /**
     * @brief Enables or disables single-line mode.
     *
     * In single-line mode, the application clears the screen after each input and repositions the cursor 
     * so that the prompt always appears on the first line and command results follow below it.
     *
     * @param enabled @c true to enable single-line mode, @c false to disable it.
     */
    void setSingleLineMode(bool enabled) {
        m_singleLineMode = enabled;
        if (m_singleLineMode) {
            clearScreen();
            printPrompt();
        }
    }

private:

    /**
     * @brief Internal method, invoked regularly by the timer, to read and process user input.
     *
     * This function polls standard input (stdin) and, if a line is available, processes it via @c processLine(). 
     * If single-line mode is active, the screen is cleared and the prompt is repositioned before and after processing.
     */
    void pollInput() {
        std::string line = m_nonBlockingReader.readLineNonBlocking();
        if (line != "") {
              if (m_singleLineMode) {
                // In single line mode:
                // 1. Clear screen
                // 2. Move to line 2 and print results
                // 3. Move back to line 1 and print prompt
                clearScreen();
                // Move cursor to line 2, column 1
                swCDebug(kSwLogCategory_SwInteractiveConsoleApplication) << "\033[2;1H";
                std::string result = processLine(SwString(line));

                
                if (!result.empty()) {
                    swCDebug(kSwLogCategory_SwInteractiveConsoleApplication) << result;
                }

                // Move back to line 1, column 1 and print prompt
                swCDebug(kSwLogCategory_SwInteractiveConsoleApplication) << "\033[1;1H> ";

            } else {
                std::string result = processLine(SwString(line));
                // Normal mode: just print a new prompt
                if (!result.empty()) {
                    swCDebug(kSwLogCategory_SwInteractiveConsoleApplication) << result;
                }
                swCDebug(kSwLogCategory_SwInteractiveConsoleApplication) << "> ";
            }
        }
    }

    /**
     * @brief Processes a single line of user input.
     *
     * Analyzes the input line and performs the corresponding action:
     * - Executes built-in commands (help, pwd, dir, cd, cd..)
     * - Navigates to a specified JSON node
     * - Displays or modifies a JSON value
     * - Executes a custom registered command if available
     *
     * @param line The line of user input.
     * @return A string representing the output to display in the console (result, error message, etc.).
     */
    std::string processLine(const SwString &line) {
        SwString trimmed = line.trimmed();
        std::ostringstream output;

        if (trimmed.isEmpty()) {
            return "";
        }

        if (trimmed == "help") {
            output << printHelp();
        } else if (trimmed == "pwd") {
            output << "Current path: " << (m_currentPath.isEmpty() ? "/" : m_currentPath.toStdString()) << "\n";
        } else if (trimmed == "dir") {
            output << listCurrentNode();
        } else if (trimmed.replace(" ", "") == "cd..") {
            output << navigateUp();
        } else if (trimmed.startsWith("cd ")) {
            SwString target = trimmed.mid(3).trimmed();
            output << navigateTo(target);
        } else {
            // Build full path from current path
            SwString fullPath = m_currentPath.isEmpty() ? trimmed : m_currentPath + "/" + trimmed;
            SwJsonValue &node = m_config.find(fullPath, false);
            if (!node.isValid()) {
                output << "Unknown path or command: " << fullPath.toStdString() << "\n";
            } else {
                // If the node is an object, we "enter" it
                if (node.isObject()) {
                    output << "\nYou are now in node: " << fullPath.toStdString() << "\n";
                    if (m_comments.contains(fullPath)) {
                        output << m_comments[fullPath].toStdString() << "\n";
                    }
                    m_currentPath = fullPath;
                    output << listCurrentNode();
                } else {
                    // It's a leaf value
                    SwString value = node.toString();
                    bool hasCommand = m_commands.contains(fullPath);

                    if (hasCommand) {
                        // Execute the associated command (may print directly)
                        m_commands[fullPath](value);
                    } else {
                        // Just show the value
                        output << "Value: " << value.toStdString() << (hasCommand ? " [R/W]\n" : " [R]\n");
                    }
                }
            }
        }

        return output.str();
    }

    /**
     * @brief Registers built-in commands (help, pwd, dir, cd, etc.) and associates comments with them.
     *
     * Called from the constructor to provide contextual information in the help menu and navigation.
     */
    void registerNativeCommands() {
        addComment("pwd", "Show the current path.");
        addComment("dir", "List the sub-elements of the current path.");
        addComment("cd..", "Go up one level.");
        addComment("cd <path>", "Navigate to a specific path.");
    }

    /**
     * @brief Lists the sub-elements of the current JSON node.
     *
     * Iterates over the current JSON node and lists its keys. For each key:
     * - If the node is an object, it is considered a sub-menu.
     * - If it is a leaf value, it displays the value and shows [R/W] if a command is associated 
     *   with that node, or [R] if it is read-only.
     *
     * @return A string describing the sub-elements of the current node.
     */
    std::string listCurrentNode() {
        std::ostringstream output;
        SwJsonValue &node = m_config.find(m_currentPath, false);
        if (!node.isValid() || !node.isObject()) {
            output << "The current path is not a valid node.\n";
            return output.str();
        }

        output << "Sub-options available:\n";
        for (auto &key : node.toObject().keys()) {
            SwString childPath = m_currentPath.isEmpty() ? key : (m_currentPath + "/" + key);
            SwJsonValue &child = m_config.find(childPath, false);

            if (child.isObject()) {
                // Sub-menu
                output << " - " << key.toStdString() << " -> sub-menu\n";
            } else {
                // Leaf
                SwString value = child.toString();
                bool hasCommand = m_commands.contains(childPath);
                output << " - " << key.toStdString() << ": "
                       << value.toStdString()
                       << (hasCommand ? " [R/W]" : " [R]") << "\n";
            }
        }
        return output.str();
    }

    /**
     * @brief Navigates up one level in the JSON hierarchy.
     *
     * If already at the root, it prints a message indicating that you cannot go higher.
     *
     * @return A string indicating the current path after navigation.
     */
    std::string navigateUp() {
        std::ostringstream output;
        if (m_currentPath.isEmpty()) {
            output << "You are already at the root.\n";
            return output.str();
        }

        const size_t lastSlash = m_currentPath.lastIndexOf("/");
        if (lastSlash == static_cast<size_t>(-1)) {
            m_currentPath = "";
        } else {
            m_currentPath = m_currentPath.left(static_cast<int>(lastSlash));
        }

        output << "Current path: " << (m_currentPath.isEmpty() ? "/" : m_currentPath.toStdString()) << "\n";
        return output.str();
    }

    /**
     * @brief Navigates to a child node via @c cd <path>.
     *
     * Moves directly to a specified child node. If the node does not exist or is not an object, 
     * an error message is returned.
     *
     * @param target The name or path of the sub-node to navigate to.
     * @return A string indicating the current path after navigation.
     */
    std::string navigateTo(const SwString &target) {
        std::ostringstream output;
        SwString cleanTarget = target;
        if (cleanTarget.startsWith("/")) {
            cleanTarget = cleanTarget.mid(1);
        }

        SwString newPath = m_currentPath.isEmpty() ? cleanTarget : (m_currentPath + "/" + cleanTarget);
        SwJsonValue &node = m_config.find(newPath, false);
        if (!node.isValid() || !node.isObject()) {
            output << "Invalid or non-existent path: " << newPath.toStdString() << "\n";
            return output.str();
        }

        m_currentPath = newPath;
        output << "Current path: " << (m_currentPath.isEmpty() ? "/" : m_currentPath.toStdString()) << "\n";
        output << listCurrentNode();
        return output.str();
    }

    /**
     * @brief Displays the help information for the application.
     *
     * Builds the help output recursively by traversing the JSON hierarchy. It shows comments attached to paths 
     * as well as details about commands and nodes. Both built-in and custom-annotated nodes are presented.
     *
     * @return A string containing the complete help text.
     */
    std::string printHelp() {
        std::ostringstream output;
        output << "\nApplication Help:\n";
        printHelpRecursive(m_config.toJsonValue(), "", output);
        return output.str();
    }

     /**
     * @brief A helper function for recursive help display.
     *
     * Recursively walks through the JSON tree starting from @p node and builds a full @p path.
     * It displays associated comments, and if the node is an object, it calls itself recursively for its children.
     *
     * @param node The current JSON node.
     * @param path The current JSON path.
     * @param output The output stream used to build the help text.
     */
    void printHelpRecursive(const SwJsonValue &node, const SwString &path, std::ostringstream &output) {
        if (node.isObject()) {
            // Print node comment if exists
            if (m_comments.contains(path)) {
                output << "\n" << (path.isEmpty() ? "/" : path.toStdString()) << ": "
                       << m_comments[path].toStdString() << "\n";
            }
            for (auto &key : node.toObject().keys()) {
                SwString childPath = path.isEmpty() ? key : (path + "/" + key);
                printHelpRecursive(node.toObject().data()[key.toStdString()], childPath, output);
            }
        } else {
            // Leaf node
            output << (path.isEmpty() ? "/" : path.toStdString());
            if (m_comments.contains(path)) {
                output << " - " << m_comments[path].toStdString();
            }
            output << "\n";
        }
    }

    /**
     * @brief Prints the prompt in the console.
     *
     * By default, the prompt is simply "> ".
     */
    void printPrompt() {
        swCDebug(kSwLogCategory_SwInteractiveConsoleApplication) << "> ";
    }

    /**
     * @brief Clears the console screen.
     *
     * Uses the appropriate system command depending on the platform. On Windows, it executes @c cls, 
     * and on other systems @c clear.
     */
    void clearScreen() {
#if defined(_WIN32) || defined(_WIN64)
        system("cls");
#else
        system("clear");
#endif
    }

    SwJsonDocument &m_config;                                     ///< Reference to the JSON document being manipulated.
    SwMap<SwString, std::function<void(const SwString &)>> m_commands; ///< Map of custom commands associated with JSON paths.
    SwMap<SwString, SwString> m_comments;                        ///< Map of comments associated with specific paths.
    SwTimer m_timer;                                             ///< Internal timer for polling user input.
    SwString m_currentPath;                                      ///< Current path in the JSON hierarchy.
    bool m_singleLineMode;                                       ///< Indicates whether single-line mode is active.
    NonBlockingReader m_nonBlockingReader;
};
