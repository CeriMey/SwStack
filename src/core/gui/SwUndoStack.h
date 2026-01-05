#pragma once
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
    explicit SwUndoStack(SwObject* parent = nullptr)
        : SwObject(parent) {}

    ~SwUndoStack() override { clear(); }

    void clear() {
        for (SwUndoCommand* cmd : m_stack) {
            delete cmd;
        }
        m_stack.clear();
        m_index = 0;
    }

    int count() const { return static_cast<int>(m_stack.size()); }
    int index() const { return static_cast<int>(m_index); }

    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index < m_stack.size(); }

    bool isExecuting() const { return m_executing; }

    void push(SwUndoCommand* cmd) {
        if (!cmd) {
            return;
        }
        if (m_executing) {
            delete cmd;
            return;
        }

        truncateRedo_();

        // Qt-like semantics: execute immediately, then attempt merge with previous.
        m_executing = true;
        cmd->redo();
        m_executing = false;

        if (tryMergeWithPrevious_(cmd)) {
            return;
        }

        m_stack.push_back(cmd);
        ++m_index;
    }

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

