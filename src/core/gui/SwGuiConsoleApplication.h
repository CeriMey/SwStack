
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
#include "SwDebug.h"

#include <functional>
#include <iostream>
#include <limits>
#include <utility>
static constexpr const char* kSwLogCategory_SwGuiConsoleApplication = "sw.core.gui.swguiconsoleapplication";


/**
 * @brief Runs a navigable console menu composed of nodes and callback actions.
 */
class SwGuiConsoleApplication {
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
            swCDebug(kSwLogCategory_SwGuiConsoleApplication) << title.toStdString() << "\n\n";
        }
        if (body) {
            body();
        }
        if (waitForEnter) {
            swCDebug(kSwLogCategory_SwGuiConsoleApplication) << "\nPress Enter to return...";
            std::string dummy;
            std::getline(std::cin, dummy);
        }
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
            if (!std::getline(std::cin, input)) {
                break;
            }
            SwString trimmed = SwString(input).trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }
            if (trimmed == "q" || trimmed == "Q") {
                m_running = false;
                break;
            }
            if ((trimmed == "b" || trimmed == "..") && current->parent) {
                current = current->parent;
                continue;
            }

            bool ok = false;
            int index = trimmed.toInt(&ok);
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
        clearScreen();
        swCDebug(kSwLogCategory_SwGuiConsoleApplication) << (m_title.isEmpty() ? "SwGuiConsole" : m_title.toStdString()) << "\n";
        swCDebug(kSwLogCategory_SwGuiConsoleApplication) << "Path: " << currentPath(current).toStdString() << "\n\n";

        if (current->children.isEmpty()) {
            swCDebug(kSwLogCategory_SwGuiConsoleApplication) << "(No entries)\n";
        } else {
            for (int i = 0; i < current->children.size(); ++i) {
                Node* child = current->children[i];
                swCDebug(kSwLogCategory_SwGuiConsoleApplication) << "[" << i << "] " << child->label.toStdString()
                          << (child->children.isEmpty() ? "" : " >");
            }
        }

        swCDebug(kSwLogCategory_SwGuiConsoleApplication) << "\n";
        if (!m_footer.isEmpty()) {
            swCDebug(kSwLogCategory_SwGuiConsoleApplication) << m_footer.toStdString() << "\n";
        } else {
            swCDebug(kSwLogCategory_SwGuiConsoleApplication) << "[b] Back  [q] Quit\n";
        }
        if (!m_status.isEmpty()) {
            swCDebug(kSwLogCategory_SwGuiConsoleApplication) << "Status: " << m_status.toStdString() << "\n";
        }
        swCDebug(kSwLogCategory_SwGuiConsoleApplication) << "> ";
        m_status.clear();
    }

    void clearScreen() const
    {
        swCDebug(kSwLogCategory_SwGuiConsoleApplication) << "\033[2J\033[H";
    }

    Node* m_root;
    SwVector<Node*> m_storage;
    SwString m_title;
    SwString m_footer;
    SwString m_status;
    bool m_running;
};
