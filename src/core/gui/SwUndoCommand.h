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

#include "core/types/Sw.h"
#include "core/types/SwString.h"

class SwUndoCommand {
public:
    explicit SwUndoCommand(const SwString& text = SwString())
        : m_text(text) {}

    virtual ~SwUndoCommand() = default;

    virtual void undo() = 0;
    virtual void redo() = 0;

    // Return a stable id to enable mergeWith(). Default = no merge.
    virtual int id() const { return -1; }

    // Merge `other` into this command. Return true if merged (and the stack should delete `other`).
    virtual bool mergeWith(const SwUndoCommand* other) {
        SW_UNUSED(other)
        return false;
    }

    void setText(const SwString& text) { m_text = text; }
    SwString text() const { return m_text; }

private:
    SwString m_text;
};

