#pragma once

#include "SwCreatorCommand.h"

#include "SwString.h"

class SwCreatorFormCanvas;
class SwWidget;

class SwCreatorDeleteWidgetCommand : public SwCreatorCommand {
public:
    SwCreatorDeleteWidgetCommand(SwCreatorFormCanvas* canvas, SwWidget* widget);

    void undo() override;
    void redo() override;

    SwWidget* widget() const { return m_widget; }

private:
    SwCreatorFormCanvas* m_canvas{nullptr};
    SwString m_xml;
    SwWidget* m_widget{nullptr};
};

