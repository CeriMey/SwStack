#pragma once

/**
 * @file src/core/gui/SwUndoStack.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwUndoStack in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the undo stack interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwUndoStack.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
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

#include "core/object/SwObject.h"
#include "core/types/Sw.h"
#include "core/types/SwString.h"

#include "SwUndoCommand.h"

#include <vector>

class SwUndoStack : public SwObject {
    SW_OBJECT(SwUndoStack, SwObject)

public:
    /**
     * @brief Constructs a `SwUndoStack` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwUndoStack(SwObject* parent = nullptr)
        : SwObject(parent) {}

    /**
     * @brief Destroys the `SwUndoStack` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwUndoStack() override { clear(); }

    /**
     * @brief Clears the current object state.
     */
    void clear() {
        for (SwUndoCommand* cmd : m_stack) {
            delete cmd;
        }
        m_stack.clear();
        m_index = 0;
    }

    /**
     * @brief Performs the `count` operation.
     * @return The current count value.
     */
    int count() const { return static_cast<int>(m_stack.size()); }
    /**
     * @brief Performs the `index` operation.
     * @param m_index Value passed to the method.
     * @return The requested index.
     */
    int index() const { return static_cast<int>(m_index); }

    /**
     * @brief Returns whether the object reports undo.
     * @return `true` when the object reports undo; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool canUndo() const { return m_index > 0; }
    /**
     * @brief Returns whether the object reports redo.
     * @return `true` when the object reports redo; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool canRedo() const { return m_index < m_stack.size(); }

    /**
     * @brief Returns whether the object reports executing.
     * @return `true` when the object reports executing; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isExecuting() const { return m_executing; }

    /**
     * @brief Performs the `push` operation.
     * @param cmd Value passed to the method.
     */
    void push(SwUndoCommand* cmd) {
        if (!cmd) {
            return;
        }
        if (m_executing) {
            delete cmd;
            return;
        }

        truncateRedo_();

        // Execute immediately, then attempt merge with the previous command.
        m_executing = true;
        cmd->redo();
        m_executing = false;

        if (tryMergeWithPrevious_(cmd)) {
            return;
        }

        m_stack.push_back(cmd);
        ++m_index;
    }

    /**
     * @brief Performs the `undo` operation.
     */
    void undo() {
        if (!canUndo() || m_executing) {
            return;
        }
        m_executing = true;
        SwUndoCommand* cmd = m_stack[m_index - 1];
        if (cmd) {
            cmd->undo();
        }
        m_executing = false;
        --m_index;
    }

    /**
     * @brief Performs the `redo` operation.
     */
    void redo() {
        if (!canRedo() || m_executing) {
            return;
        }
        m_executing = true;
        SwUndoCommand* cmd = m_stack[m_index];
        if (cmd) {
            cmd->redo();
        }
        m_executing = false;
        ++m_index;
    }

private:
    void truncateRedo_() {
        while (m_stack.size() > m_index) {
            delete m_stack.back();
            m_stack.pop_back();
        }
    }

    bool tryMergeWithPrevious_(SwUndoCommand* cmd) {
        if (!cmd || m_index == 0 || m_index > m_stack.size()) {
            return false;
        }

        const int cmdId = cmd->id();
        if (cmdId < 0) {
            return false;
        }

        SwUndoCommand* prev = m_stack[m_index - 1];
        if (!prev || prev->id() != cmdId) {
            return false;
        }

        if (prev->mergeWith(cmd)) {
            delete cmd;
            return true;
        }
        return false;
    }

    std::vector<SwUndoCommand*> m_stack;
    size_t m_index{0}; // points to next command to redo
    bool m_executing{false};
};
