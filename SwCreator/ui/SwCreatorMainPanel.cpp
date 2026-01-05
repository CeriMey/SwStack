#include "SwCreatorMainPanel.h"

#include "SwCreatorFormCanvas.h"
#include "SwCreatorWorkspace.h"
#include "commands/SwCreatorCreateWidgetCommand.h"
#include "commands/SwCreatorDeleteWidgetCommand.h"
#include "serialization/SwCreatorSwuiSerializer.h"
#include "ui/inspector/SwCreatorPropertyInspector.h"
#include "ui/palette/SwCreatorWidgetPalette.h"

#include "SwDragDrop.h"
#include "SwFrame.h"
#include "SwGuiApplication.h"
#include "SwMenu.h"
#include "SwSplitter.h"
#include "SwTreeWidget.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

SwCreatorMainPanel::SwCreatorMainPanel(SwWidget* parent)
    : SwWidget(parent) {
    setStyleSheet("SwCreatorMainPanel { background-color: rgb(226, 232, 240); border-width: 0px; }");

    m_splitter = new SwSplitter(SwSplitter::Orientation::Horizontal, this);
    m_splitter->setHandleWidth(8);

    m_palettePanel = new SwFrame(this);
    m_palettePanel->setFrameShape(SwFrame::Shape::StyledPanel);
    m_palettePanel->setStyleSheet(R"(
            SwFrame { background-color: rgb(255, 255, 255); border-color: rgb(226, 232, 240); border-radius: 14px; border-width: 1px; }
        )");

    m_palette = new SwCreatorWidgetPalette(m_palettePanel);

    m_workspace = new SwCreatorWorkspace(this);

    m_inspectorPanel = new SwFrame(this);
    m_inspectorPanel->setFrameShape(SwFrame::Shape::StyledPanel);
    m_inspectorPanel->setStyleSheet(R"(
            SwFrame { background-color: rgb(255, 255, 255); border-color: rgb(226, 232, 240); border-radius: 14px; border-width: 1px; }
        )");

    m_inspectorSplitter = new SwSplitter(SwSplitter::Orientation::Vertical, m_inspectorPanel);
    m_inspectorSplitter->setHandleWidth(8);

    m_hierarchyTree = new SwTreeWidget(2, m_inspectorSplitter);
    m_hierarchyTree->setHeaderLabels(SwList<SwString>{"Object", "Class"});
    m_hierarchyTree->setColumnsFitToWidth(true);
    m_hierarchyTree->setColumnStretch(0, 2);
    m_hierarchyTree->setColumnStretch(1, 1);
    if (m_hierarchyTree->header()) {
        m_hierarchyTree->header()->setStyleSheet(R"(
            SwHeaderView {
                background-color: rgb(255, 255, 255);
                border-color: rgb(226, 232, 240);
                border-width: 0px;
                border-top-left-radius: 12px;
                border-top-right-radius: 12px;
                border-bottom-left-radius: 0px;
                border-bottom-right-radius: 0px;
                padding: 0px 10px;
                color: rgb(15, 23, 42);
                divider-color: rgb(226, 232, 240);
                indicator-color: rgb(100, 116, 139);
            }
        )");
    }

    m_propertyInspector = new SwCreatorPropertyInspector(m_inspectorSplitter);

    m_inspectorSplitter->addWidget(m_hierarchyTree);
    m_inspectorSplitter->addWidget(m_propertyInspector);

    m_splitter->addWidget(m_palettePanel);
    m_splitter->addWidget(m_workspace);
    m_splitter->addWidget(m_inspectorPanel);

    m_splitter->setSizes(SwVector<int>{260, 820, 360});
    m_inspectorSplitter->setSizes(SwVector<int>{300, 360});

    wireEvents_();
    rebuildHierarchy_();
    setSelected_(nullptr);

    m_dropRegistration = SwCreatorSystemDragDrop::registerDropTarget(nativeWindowHandle(), this);
}

SwCreatorFormCanvas* SwCreatorMainPanel::canvas() const {
    return m_workspace ? m_workspace->canvas() : nullptr;
}

bool SwCreatorMainPanel::canAcceptDrop(const SwCreatorSystemDragDrop::Payload& payload, int clientX, int clientY) {
    auto* c = canvas();
    if (!c || payload.className.isEmpty()) {
        return false;
    }

    const SwRect cr = c->getRect();
    return clientX >= cr.x && clientX <= (cr.x + cr.width) && clientY >= cr.y && clientY <= (cr.y + cr.height);
}

void SwCreatorMainPanel::onDragOver(const SwCreatorSystemDragDrop::Payload&, int clientX, int clientY) {
    if (auto* c = canvas()) {
        c->updateDropPreview(clientX, clientY, nullptr);
    }
}

void SwCreatorMainPanel::onDragLeave() {
    if (auto* c = canvas()) {
        c->clearDropPreview();
    }
}

void SwCreatorMainPanel::onDrop(const SwCreatorSystemDragDrop::Payload& payload, int clientX, int clientY) {
    auto* c = canvas();
    if (!c) {
        return;
    }

    c->clearDropPreview();

    if (payload.isLayout) {
        (void)c->createLayoutContainerAt(payload.className, clientX, clientY);
        return;
    }

    (void)c->createWidgetAt(payload.className, clientX, clientY);
}

void SwCreatorMainPanel::resizeEvent(ResizeEvent* event) {
    SwWidget::resizeEvent(event);
    updateLayout_();
}

void SwCreatorMainPanel::keyPressEvent(KeyEvent* event) {
    SwWidget::keyPressEvent(event);
    if (!event || event->isAccepted()) {
        return;
    }

    if (event->isCtrlPressed()) {
        if (SwWidgetPlatformAdapter::matchesShortcutKey(event->key(), 'Z')) {
            if (event->isShiftPressed()) {
                m_commands.redo();
            } else {
                m_commands.undo();
            }
            event->accept();
            return;
        }
        if (SwWidgetPlatformAdapter::matchesShortcutKey(event->key(), 'Y')) {
            m_commands.redo();
            event->accept();
            return;
        }
        if (SwWidgetPlatformAdapter::matchesShortcutKey(event->key(), 'C')) {
            copySelected_();
            event->accept();
            return;
        }
        if (SwWidgetPlatformAdapter::matchesShortcutKey(event->key(), 'X')) {
            cutSelected_();
            event->accept();
            return;
        }
        if (SwWidgetPlatformAdapter::matchesShortcutKey(event->key(), 'V')) {
            paste_();
            event->accept();
            return;
        }
    }

    if (SwWidgetPlatformAdapter::isDeleteKey(event->key())) {
        deleteSelected_();
        event->accept();
        return;
    }
}

void SwCreatorMainPanel::updateLayout_() {
    const SwRect r = getRect();
    if (m_splitter) {
        m_splitter->move(r.x, r.y);
        m_splitter->resize(r.width, r.height);
    }
    if (m_palette && m_palettePanel) {
        const SwRect pr = m_palettePanel->getRect();
        m_palette->move(pr.x + 10, pr.y + 10);
        m_palette->resize(std::max(0, pr.width - 20), std::max(0, pr.height - 20));
    }
    if (m_inspectorSplitter && m_inspectorPanel) {
        const SwRect ir = m_inspectorPanel->getRect();
        m_inspectorSplitter->move(ir.x + 10, ir.y + 10);
        m_inspectorSplitter->resize(std::max(0, ir.width - 20), std::max(0, ir.height - 20));
    }
}

void SwCreatorMainPanel::wireEvents_() {
    if (m_splitter) {
        SwObject::connect(m_splitter, &SwSplitter::splitterMoved, this, [this](int, int) { updateLayout_(); });
    }
    if (m_inspectorSplitter) {
        SwObject::connect(m_inspectorSplitter, &SwSplitter::splitterMoved, this, [this](int, int) { updateLayout_(); });
    }

    if (m_palette) {
        SwObject::connect(m_palette,
                          &SwCreatorWidgetPalette::entryActivated,
                          this,
                          [this](const SwCreatorPaletteEntry& e) {
                              if (auto* c = canvas()) {
                                  if (e.isLayout) {
                                      c->setCreateClass(SwString("layout:%1").arg(e.className));
                                  } else {
                                      c->setCreateClass(e.className);
                                  }
                              }
                          });
        SwObject::connect(m_palette,
                          &SwCreatorWidgetPalette::entryDropped,
                          this,
                          [this](const SwCreatorPaletteEntry& e, int x, int y) {
                              auto* c = canvas();
                              if (!c) {
                                  return;
                              }
                               c->clearDropPreview();

                               if (e.isLayout) {
                                   const SwRect cr = c->getRect();
                                   if (x < cr.x || x > (cr.x + cr.width) || y < cr.y || y > (cr.y + cr.height)) {
                                       return;
                                   }
                                   (void)c->createLayoutContainerAt(e.className, x, y);
                                   return;
                               }

                               const SwRect cr = c->getRect();
                               if (x < cr.x || x > (cr.x + cr.width) || y < cr.y || y > (cr.y + cr.height)) {
                                   return;
                              }

                               (void)c->createWidgetAt(e.className, x, y);
                           });
        SwObject::connect(m_palette,
                          &SwCreatorWidgetPalette::entryDragMoved,
                          this,
                          [this](const SwCreatorPaletteEntry& e, int x, int y) {
                              auto* c = canvas();
                              if (!c) {
                                  return;
                              }

                              SwCreatorSystemDragDrop::Payload payload;
                              payload.className = e.className;
                              payload.isLayout = e.isLayout;

                              const bool canDrop = canAcceptDrop(payload, x, y);
                              if (canDrop) {
                                  c->updateDropPreview(x, y, nullptr);
                              } else {
                                  c->clearDropPreview();
                              }
                              SwDragDrop::instance().setDropAllowed(canDrop);
                          });
    }

    if (auto* c = canvas()) {
        SwObject::connect(c, &SwCreatorFormCanvas::widgetAdded, this, [this, c](SwWidget* w) {
            rebuildHierarchy_();
            documentModified();
            if (!w || m_commands.isExecuting()) {
                return;
            }
            m_commands.pushApplied(std::unique_ptr<SwCreatorCommand>(new SwCreatorCreateWidgetCommand(c, w)));
        });
        SwObject::connect(c, &SwCreatorFormCanvas::widgetRemoved, this, [this](SwWidget*) {
            rebuildHierarchy_();
            documentModified();
        });
        SwObject::connect(c, &SwCreatorFormCanvas::selectionChanged, this, [this](SwWidget* w) { setSelected_(w); });
        SwObject::connect(c, &SwCreatorFormCanvas::designWidgetsChanged, this, [this]() {
            rebuildHierarchy_();
            documentModified();
        });

        SwObject::connect(c, &SwCreatorFormCanvas::requestUndo, this, [this]() { m_commands.undo(); });
        SwObject::connect(c, &SwCreatorFormCanvas::requestRedo, this, [this]() { m_commands.redo(); });
        SwObject::connect(c, &SwCreatorFormCanvas::requestCut, this, [this]() { cutSelected_(); });
        SwObject::connect(c, &SwCreatorFormCanvas::requestCopy, this, [this]() { copySelected_(); });
        SwObject::connect(c, &SwCreatorFormCanvas::requestPaste, this, [this]() { paste_(); });
        SwObject::connect(c, &SwCreatorFormCanvas::requestDelete, this, [this]() { deleteSelected_(); });
    }

    if (m_propertyInspector) {
        SwObject::connect(m_propertyInspector, &SwCreatorPropertyInspector::hierarchyNeedsRebuild, this, [this]() { rebuildHierarchy_(); });
        SwObject::connect(m_propertyInspector, &SwCreatorPropertyInspector::canvasNeedsUpdate, this, [this]() {
            if (auto* c = canvas()) {
                c->update();
            }
        });
        SwObject::connect(m_propertyInspector, &SwCreatorPropertyInspector::documentModified, this, [this]() { documentModified(); });
    }

    if (m_hierarchyTree && m_hierarchyTree->selectionModel()) {
        SwObject::connect(m_hierarchyTree->selectionModel(),
                          &SwItemSelectionModel::currentChanged,
                          this,
                          [this](const SwModelIndex& current, const SwModelIndex&) {
                              auto* item = current.isValid() ? static_cast<SwStandardItem*>(current.internalPointer()) : nullptr;
                              auto it = item ? m_hierarchyItemToWidget.find(item) : m_hierarchyItemToWidget.end();
                              SwWidget* w = (it == m_hierarchyItemToWidget.end()) ? nullptr : it->second;
                              if (!w) {
                                  return;
                              }
                              w->setFocus(true);
                          });
    }

    if (m_hierarchyTree) {
        m_hierarchyTree->setDragEnabled(true);
        m_hierarchyTree->setAcceptDrops(true);
        m_hierarchyTree->setDropIndicatorShown(true);

        SwObject::connect(m_hierarchyTree,
                          &SwTreeWidget::contextMenuRequested,
                          this,
                          [this](const SwModelIndex& idx, int x, int y) {
                              auto* item = idx.isValid() ? static_cast<SwStandardItem*>(idx.internalPointer()) : nullptr;
                              auto it = item ? m_hierarchyItemToWidget.find(item) : m_hierarchyItemToWidget.end();
                              SwWidget* w = (it == m_hierarchyItemToWidget.end()) ? nullptr : it->second;
                              if (w) {
                                  w->setFocus(true);
                              }
                              showHierarchyContextMenu_(x, y);
                          });
        SwObject::connect(m_hierarchyTree,
                          &SwTreeWidget::dragDropped,
                          this,
                          [this](const SwModelIndex& dragged, const SwModelIndex& droppedOn) { onHierarchyDragDropped_(dragged, droppedOn); });
    }
}

void SwCreatorMainPanel::rebuildHierarchy_() {
    m_hierarchyItemToWidget.clear();
    m_widgetToHierarchyIndex.clear();

    if (!m_hierarchyTree || !m_hierarchyTree->model()) {
        return;
    }
    auto* model = m_hierarchyTree->model();
    model->clear();

    auto* root = model->invisibleRootItem();
    auto* formObj = new SwStandardItem("Form");
    auto* formCls = new SwStandardItem(canvas() ? canvas()->className() : SwString("Form"));
    SwList<SwStandardItem*> formRow;
    formRow.append(formObj);
    formRow.append(formCls);
    root->appendRow(formRow);

    const SwModelIndex formIndex = model->index(0, 0, SwModelIndex());
    m_hierarchyItemToWidget[formObj] = canvas();
    m_hierarchyItemToWidget[formCls] = canvas();
    if (canvas()) {
        m_widgetToHierarchyIndex[canvas()] = formIndex;
    }

    const auto* c = canvas();
    if (c) {
        const std::vector<SwWidget*>& widgets = c->designWidgets();
        std::unordered_set<SwWidget*> designSet;
        designSet.reserve(widgets.size());
        for (SwWidget* w : widgets) {
            if (w) {
                designSet.insert(w);
            }
        }

        std::unordered_map<SwWidget*, std::vector<SwWidget*>> children;
        children.reserve(widgets.size());

        auto parentDesignWidgetOf = [&](SwWidget* w) -> SwWidget* {
            if (!w) {
                return nullptr;
            }
            for (SwObject* p = w->parent(); p; p = p->parent()) {
                if (p == c) {
                    return const_cast<SwCreatorFormCanvas*>(c);
                }
                auto* pw = dynamic_cast<SwWidget*>(p);
                if (pw && designSet.find(pw) != designSet.end()) {
                    return pw;
                }
            }
            return const_cast<SwCreatorFormCanvas*>(c);
        };

        for (SwWidget* w : widgets) {
            if (!w) {
                continue;
            }
            SwWidget* p = parentDesignWidgetOf(w);
            children[p].push_back(w);
        }

        std::function<void(SwWidget*, SwStandardItem*, const SwModelIndex&)> addChildren;
        addChildren = [&](SwWidget* parentWidget, SwStandardItem* parentItem, const SwModelIndex& parentIndex) {
            auto it = children.find(parentWidget);
            if (it == children.end()) {
                return;
            }
            for (SwWidget* w : it->second) {
                if (!w || !parentItem) {
                    continue;
                }

                const SwString name = w->getObjectName();
                auto* objItem = new SwStandardItem(name.isEmpty() ? SwString("<unnamed>") : name);
                auto* clsItem = new SwStandardItem(w->className());
                SwList<SwStandardItem*> row;
                row.append(objItem);
                row.append(clsItem);
                parentItem->appendRow(row);

                m_hierarchyItemToWidget[objItem] = w;
                m_hierarchyItemToWidget[clsItem] = w;

                const SwModelIndex idx = model->index(objItem->row(), 0, parentIndex);
                m_widgetToHierarchyIndex[w] = idx;

                addChildren(w, objItem, idx);
            }
        };

        addChildren(const_cast<SwCreatorFormCanvas*>(c), formObj, formIndex);
    }

    if (m_selected) {
        auto it = m_widgetToHierarchyIndex.find(m_selected);
        if (it != m_widgetToHierarchyIndex.end() && m_hierarchyTree->selectionModel()) {
            m_hierarchyTree->selectionModel()->setCurrentIndex(it->second);
        }
    }

    model->modelReset();
    if (m_hierarchyTree) {
        const SwModelIndex formIndex2 = model->index(0, 0, SwModelIndex());
        if (formIndex2.isValid()) {
            // Default to expanded so nested widgets are immediately visible.
            std::function<void(const SwModelIndex&)> expandAll;
            expandAll = [&](const SwModelIndex& idx) {
                if (!idx.isValid()) {
                    return;
                }
                m_hierarchyTree->expand(idx);
                const int rows = model->rowCount(idx);
                for (int r = 0; r < rows; ++r) {
                    expandAll(model->index(r, 0, idx));
                }
            };
            expandAll(formIndex2);
        }
        m_hierarchyTree->resizeColumnsToContents();
    }
    update();
}

void SwCreatorMainPanel::setSelected_(SwWidget* w) {
    if (m_selected == w) {
        rebuildProperties_();
        return;
    }
    m_selected = w;
    if (auto* c = canvas()) {
        c->setSelectedWidget(w);
    }
    if (m_hierarchyTree && m_hierarchyTree->selectionModel()) {
        if (!w) {
            m_hierarchyTree->selectionModel()->clear();
        } else {
            auto it = m_widgetToHierarchyIndex.find(w);
            if (it != m_widgetToHierarchyIndex.end()) {
                m_hierarchyTree->selectionModel()->setCurrentIndex(it->second);
            }
        }
    }
    rebuildProperties_();
}

void SwCreatorMainPanel::rebuildProperties_() {
    if (m_propertyInspector) {
        m_propertyInspector->setTarget(m_selected);
    }
}

SwString SwCreatorMainPanel::clipboardText_() const {
    SwGuiApplication* app = SwGuiApplication::instance(false);
    auto* platform = app ? app->platformIntegration() : nullptr;
    return platform ? platform->clipboardText() : SwString();
}

void SwCreatorMainPanel::setClipboardText_(const SwString& text) const {
    SwGuiApplication* app = SwGuiApplication::instance(false);
    auto* platform = app ? app->platformIntegration() : nullptr;
    if (platform) {
        platform->setClipboardText(text);
    }
}

void SwCreatorMainPanel::copySelected_() {
    if (!m_selected) {
        return;
    }
    const SwString xml = SwCreatorSwuiSerializer::serializeWidget(m_selected);
    if (xml.isEmpty()) {
        return;
    }
    setClipboardText_(xml);
    m_pasteSerial = 0;
}

void SwCreatorMainPanel::cutSelected_() {
    if (!m_selected) {
        return;
    }
    copySelected_();
    deleteSelected_();
}

void SwCreatorMainPanel::paste_() {
    auto* c = canvas();
    if (!c) {
        return;
    }

    const SwString xml = clipboardText_();
    if (xml.isEmpty()) {
        return;
    }

    const std::string s = xml.toStdString();
    if (s.find("<swui") == std::string::npos || s.find("<widget") == std::string::npos) {
        return;
    }

    const int next = std::min(12, m_pasteSerial + 1);
    const int dx = 20 * next;
    const int dy = 20 * next;
    m_pasteSerial = (next >= 12) ? 0 : next;

    m_commands.pushAndRedo(std::unique_ptr<SwCreatorCommand>(new SwCreatorCreateWidgetCommand(c, xml, dx, dy)));
}

void SwCreatorMainPanel::deleteSelected_() {
    auto* c = canvas();
    if (!c || !m_selected) {
        return;
    }
    m_commands.pushAndRedo(std::unique_ptr<SwCreatorCommand>(new SwCreatorDeleteWidgetCommand(c, m_selected)));
}

void SwCreatorMainPanel::showHierarchyContextMenu_(int globalX, int globalY) {
    if (m_hierarchyContextMenu) {
        m_hierarchyContextMenu->hide();
        delete m_hierarchyContextMenu;
        m_hierarchyContextMenu = nullptr;
    }

    m_hierarchyContextMenu = new SwMenu(this);

    const bool hasSelection = (m_selected != nullptr);
    const bool canDelete = hasSelection && (m_selected != canvas());

    auto* cut = m_hierarchyContextMenu->addAction("Cut", [this]() { cutSelected_(); });
    auto* copy = m_hierarchyContextMenu->addAction("Copy", [this]() { copySelected_(); });
    auto* paste = m_hierarchyContextMenu->addAction("Paste", [this]() { paste_(); });
    auto* del = m_hierarchyContextMenu->addAction("Delete", [this]() { deleteSelected_(); });

    cut->setEnabled(canDelete);
    copy->setEnabled(hasSelection);
    del->setEnabled(canDelete);
    paste->setEnabled(true);

    m_hierarchyContextMenu->popup(globalX, globalY);
}

void SwCreatorMainPanel::onHierarchyDragDropped_(const SwModelIndex& dragged, const SwModelIndex& droppedOn) {
    auto* c = canvas();
    if (!c) {
        return;
    }

    if (!dragged.isValid()) {
        return;
    }
    if (!droppedOn.isValid()) {
        return;
    }

    auto* draggedItem = static_cast<SwStandardItem*>(dragged.internalPointer());
    auto itDragged = draggedItem ? m_hierarchyItemToWidget.find(draggedItem) : m_hierarchyItemToWidget.end();
    SwWidget* draggedWidget = (itDragged == m_hierarchyItemToWidget.end()) ? nullptr : itDragged->second;
    if (!draggedWidget || draggedWidget == c) {
        return;
    }

    SwWidget* dropWidget = nullptr;
    auto* dropItem = static_cast<SwStandardItem*>(droppedOn.internalPointer());
    auto itDrop = dropItem ? m_hierarchyItemToWidget.find(dropItem) : m_hierarchyItemToWidget.end();
    dropWidget = (itDrop == m_hierarchyItemToWidget.end()) ? nullptr : itDrop->second;
    if (!dropWidget) {
        return;
    }

    if (dropWidget == draggedWidget) {
        return;
    }

    if (dropWidget == c) {
        dropWidget = nullptr;
    }

    (void)c->reparentDesignWidget(draggedWidget, dropWidget);
}
