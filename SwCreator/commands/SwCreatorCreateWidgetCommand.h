#pragma once

#include "SwCreatorCommand.h"

#include "SwString.h"

class SwCreatorFormCanvas;
class SwWidget;

class SwCreatorCreateWidgetCommand : public SwCreatorCommand {
public:
    SwCreatorCreateWidgetCommand(SwCreatorFormCanvas* canvas, SwWidget* existingWidget);
    SwCreatorCreateWidgetCommand(SwCreatorFormCanvas* canvas, const SwString& swuiXml, int dx = 20, int dy = 20);

    void undo() override;
    void redo() override;

    SwWidget* widget() const { return m_widget; }

private:
    SwCreatorFormCanvas* m_canvas{nullptr};
    SwString m_xml;
    SwWidget* m_widget{nullptr};
    int m_dx{0};
    int m_dy{0};
};

