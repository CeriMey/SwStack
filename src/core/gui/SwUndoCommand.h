#pragma once

/**
 * @file
 * @ingroup core_gui
 * @brief Declares the base command object used by `SwUndoStack`.
 *
 * Each command encapsulates a reversible editing operation through `redo()` and
 * `undo()`. The optional `id()` and `mergeWith()` hooks let stacks coalesce related
 * edits such as typing bursts into a single logical undo step.
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



#include "core/types/Sw.h"
#include "core/types/SwString.h"

class SwUndoCommand {
public:
    /**
     * @brief Constructs a `SwUndoCommand` instance.
     * @param text Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwUndoCommand(const SwString& text = SwString())
        : m_text(text) {}

    /**
     * @brief Destroys the `SwUndoCommand` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwUndoCommand() = default;

    /**
     * @brief Returns the current undo.
     * @return The current undo.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void undo() = 0;
    /**
     * @brief Returns the current redo.
     * @return The current redo.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void redo() = 0;

    // Return a stable id to enable mergeWith(). Default = no merge.
    /**
     * @brief Returns the current id.
     * @return The current id.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual int id() const { return -1; }

    // Merge `other` into this command. Return true if merged (and the stack should delete `other`).
    /**
     * @brief Performs the `mergeWith` operation.
     * @param other Value passed to the method.
     * @return The requested merge With.
     */
    virtual bool mergeWith(const SwUndoCommand* other) {
        SW_UNUSED(other)
        return false;
    }

    /**
     * @brief Sets the text.
     * @param text Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setText(const SwString& text) { m_text = text; }
    /**
     * @brief Returns the current text.
     * @return The current text.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString text() const { return m_text; }

private:
    SwString m_text;
};
