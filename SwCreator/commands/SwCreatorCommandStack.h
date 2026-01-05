#pragma once
/***************************************************************************************************
 * SwCreatorCommandStack - simple undo/redo stack (MVP).
 **************************************************************************************************/

#include <memory>
#include <vector>

class SwCreatorCommand;

class SwCreatorCommandStack {
public:
    void clear();

    // Push a command that has already been applied externally (e.g., canvas click-to-create).
    void pushApplied(std::unique_ptr<SwCreatorCommand> cmd);

    // Push a command and run redo() immediately.
    void pushAndRedo(std::unique_ptr<SwCreatorCommand> cmd);

    bool canUndo() const;
    bool canRedo() const;

    void undo();
    void redo();

    bool isExecuting() const { return m_executing; }

private:
    void truncateRedo_();

    std::vector<std::unique_ptr<SwCreatorCommand>> m_stack;
    size_t m_index{0}; // points to next command to redo
    bool m_executing{false};
};

