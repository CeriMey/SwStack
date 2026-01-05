#include "SwCreatorCommandStack.h"

#include "SwCreatorCommand.h"

void SwCreatorCommandStack::clear() {
    m_stack.clear();
    m_index = 0;
}

void SwCreatorCommandStack::truncateRedo_() {
    if (m_index >= m_stack.size()) {
        return;
    }
    m_stack.erase(m_stack.begin() + static_cast<long long>(m_index), m_stack.end());
}

void SwCreatorCommandStack::pushApplied(std::unique_ptr<SwCreatorCommand> cmd) {
    if (!cmd) {
        return;
    }
    truncateRedo_();
    m_stack.push_back(std::move(cmd));
    m_index = m_stack.size();
}

void SwCreatorCommandStack::pushAndRedo(std::unique_ptr<SwCreatorCommand> cmd) {
    if (!cmd) {
        return;
    }
    truncateRedo_();
    m_executing = true;
    cmd->redo();
    m_executing = false;
    m_stack.push_back(std::move(cmd));
    m_index = m_stack.size();
}

bool SwCreatorCommandStack::canUndo() const {
    return m_index > 0 && m_index <= m_stack.size();
}

bool SwCreatorCommandStack::canRedo() const {
    return m_index < m_stack.size();
}

void SwCreatorCommandStack::undo() {
    if (!canUndo()) {
        return;
    }
    m_executing = true;
    --m_index;
    m_stack[m_index]->undo();
    m_executing = false;
}

void SwCreatorCommandStack::redo() {
    if (!canRedo()) {
        return;
    }
    m_executing = true;
    m_stack[m_index]->redo();
    ++m_index;
    m_executing = false;
}

