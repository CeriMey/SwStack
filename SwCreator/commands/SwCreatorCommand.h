#pragma once

class SwCreatorCommand {
public:
    virtual ~SwCreatorCommand() = default;
    virtual void undo() = 0;
    virtual void redo() = 0;
};

