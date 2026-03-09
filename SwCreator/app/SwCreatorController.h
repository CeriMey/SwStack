#pragma once
#include "SwObject.h"

class SwMainWindow;
class SwCreatorMainPanel;
class SwCreatorShell;
class SwCreatorDocument;

class SwCreatorController : public SwObject {
    SW_OBJECT(SwCreatorController, SwObject)
public:
    SwCreatorController(SwMainWindow* window, SwCreatorShell* shell, SwCreatorDocument* doc, SwObject* parent = nullptr);
    void setup();   // wire signals, menus, shortcuts
private:
    SwMainWindow* m_window{nullptr};
    SwCreatorShell* m_shell{nullptr};
    SwCreatorMainPanel* m_panel{nullptr};
    SwCreatorDocument* m_doc{nullptr};
};
