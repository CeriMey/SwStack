#include "SwCreatorPropertyInspector.h"

#include "SwCheckBox.h"
#include "SwComboBox.h"
#include "SwFrame.h"
#include "SwLabel.h"
#include "SwLineEdit.h"
#include "SwListWidget.h"
#include "SwMenu.h"
#include "SwToolButton.h"
#include "SwTreeWidget.h"

#include "designer/dialogs/SwCreatorStringListDialog.h"
#include "designer/dialogs/SwCreatorTextEditDialog.h"
#include "theme/SwCreatorTheme.h"

#include <algorithm>
#include <vector>

namespace {
const SwString kSwCreatorPseudoItemsKey = "__SwCreator_Items";

SwString styleSheetPreviewText(const SwString& full) {
    SwString s = full.simplified().trimmed();
    constexpr int kMaxLen = 80;
    if (static_cast<int>(s.length()) > kMaxLen) {
        s = s.left(kMaxLen - 3) + "...";
    }
    return s;
}

SwString itemsPreviewText(const SwVector<SwString>& items) {
    if (items.isEmpty()) {
        return SwString();
    }
    SwString out;
    const int maxItems = 3;
    const int n = items.size();
    for (int i = 0; i < n && i < maxItems; ++i) {
        if (i > 0) {
            out.append(", ");
        }
        out.append(items[i].simplified().trimmed());
    }
    if (n > maxItems) {
        out.append(SwString(", +%1").arg(SwString::number(n - maxItems)));
    }
    return out;
}

SwVector<SwString> readItemsFromTarget(SwWidget* target) {
    SwVector<SwString> out;
    if (!target) {
        return out;
    }

    if (auto* cb = dynamic_cast<SwComboBox*>(target)) {
        const int n = cb->count();
        out.reserve(static_cast<SwVector<SwString>::size_type>(std::max(0, n)));
        for (int i = 0; i < n; ++i) {
            out.push_back(cb->itemText(i));
        }
        return out;
    }

    if (auto* lw = dynamic_cast<SwListWidget*>(target)) {
        const int n = lw->count();
        out.reserve(static_cast<SwVector<SwString>::size_type>(std::max(0, n)));
        for (int i = 0; i < n; ++i) {
            SwStandardItem* it = lw->item(i);
            out.push_back(it ? it->text() : SwString());
        }
        return out;
    }

    return out;
}

void applyItemsToTarget(SwWidget* target, const SwVector<SwString>& items) {
    if (!target) {
        return;
    }

    if (auto* cb = dynamic_cast<SwComboBox*>(target)) {
        cb->clear();
        for (int i = 0; i < items.size(); ++i) {
            cb->addItem(items[i]);
        }
        cb->update();
        return;
    }

    if (auto* lw = dynamic_cast<SwListWidget*>(target)) {
        lw->clear();
        for (int i = 0; i < items.size(); ++i) {
            lw->addItem(items[i]);
        }
        lw->update();
        return;
    }
}
} // namespace

SwCreatorPropertyInspector::SwCreatorPropertyInspector(SwWidget* parent)
    : SwWidget(parent) {
    const auto& th = SwCreatorTheme::current();
    setStyleSheet("SwCreatorPropertyInspector { background-color: " + SwCreatorTheme::rgb(th.surface1) + "; border-width: 0px; }");
    buildUi_();
}

void SwCreatorPropertyInspector::setTarget(SwWidget* widget) {
    if (m_target == widget) {
        closeEditorDialogs_();
        rebuild_();
        return;
    }
    m_target = widget;
    m_currentProperty.clear();
    m_currentDynamic = false;
    closeEditorDialogs_();
    rebuild_();
}

SwWidget* SwCreatorPropertyInspector::target() const {
    return m_target;
}

void SwCreatorPropertyInspector::resizeEvent(ResizeEvent* event) {
    SwWidget::resizeEvent(event);
    updateLayout_();
}

void SwCreatorPropertyInspector::buildUi_() {
    const auto& th = SwCreatorTheme::current();

    m_header = new SwFrame(this);
    m_header->setFrameShape(SwFrame::Shape::StyledPanel);
    m_header->setStyleSheet(
        "SwFrame { background-color: " + SwCreatorTheme::rgb(th.surface1) + "; border-width: 0px; }"
    );

    m_title = new SwLabel(SwString("Properties"), m_header);
    m_title->setStyleSheet(
        "SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: " + SwCreatorTheme::rgb(th.textPrimary) + "; font-size: 13px; }"
    );

    m_add = new SwToolButton("+", m_header);
    m_add->resize(34, 34);
    m_add->setStyleSheet(
        "SwToolButton { background-color: " + SwCreatorTheme::rgb(th.surface3) + "; border-color: " + SwCreatorTheme::rgb(th.border) + "; border-width: 1px; border-radius: 10px; padding: 0px; }"
    );

    m_remove = new SwToolButton("-", m_header);
    m_remove->resize(34, 34);
    m_remove->setStyleSheet(
        "SwToolButton { background-color: " + SwCreatorTheme::rgb(th.surface3) + "; border-color: " + SwCreatorTheme::rgb(th.border) + "; border-width: 1px; border-radius: 10px; padding: 0px; }"
    );

    m_tree = new SwTreeWidget(2, this);
    m_tree->setHeaderLabels(SwList<SwString>{SwString("Property"), SwString("Value")});
    m_tree->setColumnsFitToWidth(true);
    m_tree->setColumnStretch(0, 1);
    m_tree->setColumnStretch(1, 2);
    if (m_tree->header()) {
        m_tree->header()->setStyleSheet(
            "SwHeaderView {"
            " background-color: " + SwCreatorTheme::rgb(th.surface1) + ";"
            " border-color: " + SwCreatorTheme::rgb(th.border) + ";"
            " border-width: 0px;"
            " border-top-left-radius: 12px;"
            " border-top-right-radius: 12px;"
            " border-bottom-left-radius: 0px;"
            " border-bottom-right-radius: 0px;"
            " padding: 0px 10px;"
            " color: " + SwCreatorTheme::rgb(th.textPrimary) + ";"
            " divider-color: " + SwCreatorTheme::rgb(th.border) + ";"
            " indicator-color: " + SwCreatorTheme::rgb(th.textSecondary) + ";"
            " }"
        );
    }

    m_addMenu = new SwMenu(this);
    m_addMenu->addAction("Add String", [this]() { addDynamic_(AddType::String); });
    m_addMenu->addAction("Add Bool", [this]() { addDynamic_(AddType::Bool); });
    m_addMenu->addAction("Add Int", [this]() { addDynamic_(AddType::Int); });

    SwObject::connect(m_add, &SwToolButton::clicked, this, [this](bool) { onAddClicked_(); });
    SwObject::connect(m_remove, &SwToolButton::clicked, this, [this](bool) { onRemoveClicked_(); });

    if (m_tree && m_tree->selectionModel()) {
        SwObject::connect(m_tree->selectionModel(),
                          &SwItemSelectionModel::currentChanged,
                          this,
                          [this](const SwModelIndex& current, const SwModelIndex&) { onSelectionChanged_(current); });
    }

    m_remove->setEnable(false);

    updateLayout_();
}

void SwCreatorPropertyInspector::updateLayout_() {
    const SwRect r = rect();
    const int pad = 8;
    const int headerH = 40;

    if (m_header) {
        m_header->move(0, 0);
        m_header->resize(r.width, headerH);
    }

    if (m_title && m_header) {
        const SwRect hr = m_header->frameGeometry();
        m_title->move(pad, (headerH - 28) / 2);
        m_title->resize(std::max(0, hr.width - 3 * pad - 70), 28);
    }
    if (m_add && m_header) {
        const SwRect hr = m_header->frameGeometry();
        m_add->move(hr.width - pad - 34 * 2 - 6, (headerH - 34) / 2);
    }
    if (m_remove && m_header) {
        const SwRect hr = m_header->frameGeometry();
        m_remove->move(hr.width - pad - 34, (headerH - 34) / 2);
    }

    if (m_tree) {
        m_tree->move(0, headerH);
        m_tree->resize(r.width, std::max(0, r.height - headerH));
    }
}

void SwCreatorPropertyInspector::rebuild_() {
    m_styleSheetPreview = nullptr;
    m_styleSheetEditButton = nullptr;
    m_itemsPreview = nullptr;
    m_itemsEditButton = nullptr;
    closeEditorDialogs_();

    m_itemToProperty.clear();
    m_itemToDynamic.clear();
    m_propertyToIndex.clear();

    if (!m_tree || !m_tree->model()) {
        return;
    }

    m_tree->clearIndexWidgets();
    auto* model = m_tree->model();
    model->clear();

    if (!m_target) {
        m_remove->setEnable(false);
        model->modelReset();
        return;
    }

    struct PropRow {
        SwString key;
        SwString displayName;
        SwAny value;
        SwString owner;
        bool isDynamic{false};
        bool readOnly{false};
        bool isGeometry{false};
    };

    std::map<SwString, std::vector<PropRow>> groups;

    // Pseudo/meta properties.
    {
        PropRow cls;
        cls.key = "Class";
        cls.displayName = "Class";
        cls.value = SwAny(m_target->className());
        cls.owner = "SwObject";
        cls.readOnly = true;
        groups[cls.owner].push_back(cls);
    }

    // Geometry pseudo properties (SwWidget).
    {
        const SwRect gr = m_target->frameGeometry();
        auto addGeom = [&](const SwString& name, int v) {
            PropRow row;
            row.key = name;
            row.displayName = name;
            row.value = SwAny(v);
            row.owner = "SwWidget";
            row.isGeometry = true;
            groups[row.owner].push_back(row);
        };
        addGeom("x", gr.x);
        addGeom("y", gr.y);
        addGeom("width", gr.width);
        addGeom("height", gr.height);
    }

    // Real properties from SwObject registry.
    for (const SwString& prop : m_target->propertyNames()) {
        if (prop == "Focus" || prop == "Hover" || prop == "DisplayText") {
            continue;
        }
        if (prop.startsWith("__SwCreator_")) {
            continue;
        }
        PropRow row;
        row.key = prop;
        row.displayName = prop;
        row.value = m_target->property(prop);
        row.owner = m_target->propertyOwnerClass(prop);
        row.isDynamic = m_target->isDynamicProperty(prop);
        groups[row.owner].push_back(row);
    }

    // Pseudo list properties (SwComboBox / SwListWidget).
    if (dynamic_cast<SwComboBox*>(m_target) || dynamic_cast<SwListWidget*>(m_target)) {
        PropRow row;
        row.key = kSwCreatorPseudoItemsKey;
        row.displayName = "Items";
        row.value = SwAny(itemsPreviewText(readItemsFromTarget(m_target)));
        row.owner = m_target->className();
        groups[row.owner].push_back(row);
    }

    // Group order: base -> derived, then Dynamic.
    SwList<SwString> order;
    {
        auto hierarchy = m_target->classHierarchy(); // derived -> base
        for (auto it = hierarchy.rbegin(); it != hierarchy.rend(); ++it) {
            order.push_back(*it);
        }
        order.push_back("Dynamic");
    }

    // Append any unknown groups (stable by key).
    for (const auto& it : groups) {
        if (std::find(order.begin(), order.end(), it.first) == order.end()) {
            order.push_back(it.first);
        }
    }

    auto* root = model->invisibleRootItem();

    int groupRow = 0;
    for (const SwString& group : order) {
        auto itg = groups.find(group);
        if (itg == groups.end() || itg->second.empty()) {
            continue;
        }

        auto* groupItem = new SwStandardItem(group);
        auto* groupSpacer = new SwStandardItem();
        groupItem->setEditable(false);
        groupSpacer->setEditable(false);

        SwList<SwStandardItem*> gRow;
        gRow.append(groupItem);
        gRow.append(groupSpacer);
        root->appendRow(gRow);

        const SwModelIndex groupIndex = model->index(groupRow, 0, SwModelIndex());

        int childRow = 0;
        for (const PropRow& row : itg->second) {
            SwString display = row.value.toString();
            if (display.isEmpty() && row.value.metaType() == SwMetaType::UnknownType && !row.value.typeName().empty()) {
                display = SwString(row.value.typeName());
            }

            auto* nameItem = new SwStandardItem(row.displayName);
            auto* valueItem = new SwStandardItem(display);
            nameItem->setEditable(false);
            valueItem->setEditable(false);

            SwList<SwStandardItem*> pRow;
            pRow.append(nameItem);
            pRow.append(valueItem);
            groupItem->appendRow(pRow);

            const SwModelIndex nameIndex = model->index(childRow, 0, groupIndex);
            const SwModelIndex valueIndex = model->index(childRow, 1, groupIndex);

            m_itemToProperty[nameItem] = row.key;
            m_itemToProperty[valueItem] = row.key;
            m_itemToDynamic[nameItem] = row.isDynamic;
            m_itemToDynamic[valueItem] = row.isDynamic;
            m_propertyToIndex[row.key] = nameIndex;

            setEditorsForRow_(nameIndex, valueIndex, row.key, row.isDynamic, row.readOnly, row.value);

            ++childRow;
        }

        ++groupRow;
    }

    model->modelReset();
    if (m_tree) {
        for (int r = 0; r < groupRow; ++r) {
            const SwModelIndex idx = model->index(r, 0, SwModelIndex());
            if (idx.isValid()) {
                static_cast<SwTreeView*>(m_tree)->expand(idx);
            }
        }
        m_tree->resizeColumnsToContents();
    }
    update();
}

void SwCreatorPropertyInspector::ensureTextEditDialog_() {
    if (m_textEditDialog) {
        return;
    }
    m_textEditDialog = new SwCreatorTextEditDialog(this);
    m_textEditDialog->setCloseOnOutsideClick(true);
}

void SwCreatorPropertyInspector::ensureStringListDialog_() {
    if (m_stringListDialog) {
        return;
    }
    m_stringListDialog = new SwCreatorStringListDialog(this);
    m_stringListDialog->setCloseOnOutsideClick(true);
}

void SwCreatorPropertyInspector::closeEditorDialogs_() {
    if (m_textEditDialog) {
        if (m_textEditDialog->isDockedOpen()) {
            m_textEditDialog->closeDocked();
        } else {
            m_textEditDialog->reject();
        }
    }
    if (m_stringListDialog) {
        m_stringListDialog->closeDocked();
    }
}

void SwCreatorPropertyInspector::openStyleSheetEditor_(const SwRect& anchorRect) {
    if (!m_target) {
        return;
    }
    (void)anchorRect;

    ensureTextEditDialog_();
    if (!m_textEditDialog) {
        return;
    }

    m_textEditDialog->setWindowTitle("Edit StyleSheet");
    m_textEditDialog->setUseNativeWindow(true);
    m_textEditDialog->setModal(true);
    m_textEditDialog->setPlaceholderText("SwWidget { background-color: ...; border-radius: ...; }");
    m_textEditDialog->setText(m_target->property("StyleSheet").toString());
    m_textEditDialog->setOnApply([this](const SwString& text) {
        if (!m_target) {
            return;
        }
        m_target->setProperty("StyleSheet", SwAny(text));
        documentModified();
        canvasNeedsUpdate();
        if (m_styleSheetPreview) {
            m_styleSheetPreview->setText(styleSheetPreviewText(text));
        }
    });

    m_textEditDialog->open();
}

void SwCreatorPropertyInspector::openItemsEditor_(const SwRect& anchorRect) {
    if (!m_target) {
        return;
    }

    ensureStringListDialog_();
    if (!m_stringListDialog) {
        return;
    }

    m_stringListDialog->setWindowTitle("Edit items");
    m_stringListDialog->setItems(readItemsFromTarget(m_target));
    m_stringListDialog->setOnApply([this](const SwVector<SwString>& items) {
        if (!m_target) {
            return;
        }
        applyItemsToTarget(m_target, items);
        documentModified();
        canvasNeedsUpdate();
        if (m_itemsPreview) {
            m_itemsPreview->setText(itemsPreviewText(items));
        }
    });

    m_stringListDialog->openDocked(this, anchorRect, SwCreatorDockDialog::DockSide::Auto, 12);
}

void SwCreatorPropertyInspector::onAddClicked_() {
    if (!m_target || !m_addMenu || !m_add) {
        return;
    }
    const SwRect r = m_add->frameGeometry();
    m_addMenu->popup(r.x, r.y + r.height + 2);
}

SwString SwCreatorPropertyInspector::uniqueDynamicName_(const SwString& base) const {
    if (!m_target) {
        return base;
    }
    SwString name = base;
    int i = 1;
    while (m_target->propertyExist(name)) {
        name = SwString("%1%2").arg(base).arg(SwString::number(i));
        ++i;
    }
    return name;
}

void SwCreatorPropertyInspector::addDynamic_(AddType type) {
    if (!m_target) {
        return;
    }

    const SwString name = uniqueDynamicName_("dynamicProperty");
    if (type == AddType::Bool) {
        m_target->setDynamicProperty(name, SwAny(false));
    } else if (type == AddType::Int) {
        m_target->setDynamicProperty(name, SwAny(0));
    } else {
        m_target->setDynamicProperty(name, SwAny(SwString()));
    }

    documentModified();
    rebuild_();

    auto it = m_propertyToIndex.find(name);
    if (it != m_propertyToIndex.end() && m_tree && m_tree->selectionModel()) {
        m_tree->selectionModel()->setCurrentIndex(it->second);
    }
}

void SwCreatorPropertyInspector::onRemoveClicked_() {
    if (!m_target || !m_currentDynamic || m_currentProperty.isEmpty()) {
        return;
    }
    if (!m_target->removeDynamicProperty(m_currentProperty)) {
        return;
    }
    m_currentProperty.clear();
    m_currentDynamic = false;
    documentModified();
    rebuild_();
}

void SwCreatorPropertyInspector::onSelectionChanged_(const SwModelIndex& current) {
    m_currentProperty.clear();
    m_currentDynamic = false;

    auto* item = current.isValid() ? static_cast<SwStandardItem*>(current.internalPointer()) : nullptr;
    if (item) {
        auto it = m_itemToProperty.find(item);
        if (it != m_itemToProperty.end()) {
            m_currentProperty = it->second;
            m_currentDynamic = m_itemToDynamic[item];
        }
    }

    if (m_remove) {
        m_remove->setEnable(m_currentDynamic && !m_currentProperty.isEmpty());
    }
}

void SwCreatorPropertyInspector::setEditorsForRow_(const SwModelIndex& nameIndex,
                                                  const SwModelIndex& valueIndex,
                                                  const SwString& propName,
                                                  bool isDynamic,
                                                  bool isReadOnly,
                                                  const SwAny& value) {
    if (!m_tree || !m_target) {
        return;
    }

    const auto& th = SwCreatorTheme::current();

    const auto commitString = [this, propName](const SwString& v) {
        if (!m_target) {
            return;
        }
        if (propName == "x" || propName == "y" || propName == "width" || propName == "height") {
            bool ok = false;
            int iv = v.toInt(&ok);
            if (!ok) {
                return;
            }
            SwRect r = m_target->frameGeometry();
            if (propName == "x") r.x = iv;
            if (propName == "y") r.y = iv;
            if (propName == "width") r.width = std::max(1, iv);
            if (propName == "height") r.height = std::max(1, iv);
            m_target->move(r.x, r.y);
            m_target->resize(r.width, r.height);
            documentModified();
            canvasNeedsUpdate();
            return;
        }

        m_target->setProperty(propName, SwAny(v));
        documentModified();
        canvasNeedsUpdate();
        if (propName == "ObjectName" || propName == "Text" || propName == "ToolTips") {
            hierarchyNeedsRebuild();
        }
    };

    if (isDynamic) {
        auto* nameEdit = new SwLineEdit(m_tree);
        nameEdit->setText(propName);
        nameEdit->setStyleSheet(
            "SwLineEdit {"
            " background-color: " + SwCreatorTheme::rgb(th.surface3) + ";"
            " border-color: " + SwCreatorTheme::rgb(th.border) + ";"
            " border-width: 1px;"
            " border-radius: 10px;"
            " padding: 4px 8px;"
            " color: " + SwCreatorTheme::rgb(th.textPrimary) + ";"
            " }"
        );
        SwObject::connect(nameEdit, &SwLineEdit::FocusChanged, this, [this, propName, nameEdit](bool focus) {
            if (focus || !m_target) {
                return;
            }
            const SwString newName = nameEdit->getText().trimmed();
            if (newName.isEmpty() || newName == propName) {
                return;
            }
            if (m_target->propertyExist(newName)) {
                nameEdit->setText(propName);
                return;
            }

            SwAny v = m_target->property(propName);
            if (!m_target->removeDynamicProperty(propName)) {
                return;
            }
            m_target->setDynamicProperty(newName, v);
            documentModified();
            hierarchyNeedsRebuild();
            rebuild_();
        });

        m_tree->setIndexWidget(nameIndex, nameEdit);
    }

    if (isReadOnly) {
        return;
    }

    if (value.metaType() == SwMetaType::Bool) {
        auto* cb = new SwCheckBox(m_tree);
        cb->setText("");
        cb->setChecked(value.toString().toLower().trimmed() == "true");
        SwObject::connect(cb, &SwCheckBox::toggled, this, [this, propName](bool v) {
            if (!m_target) {
                return;
            }
            m_target->setProperty(propName, SwAny(v));
            documentModified();
            canvasNeedsUpdate();
        });
        m_tree->setIndexWidget(valueIndex, cb);
        return;
    }

    if (propName == "StyleSheet") {
        auto* cell = new SwWidget(m_tree);
        cell->setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");

        auto* preview = new SwLineEdit(cell);
        preview->setReadOnly(true);
        preview->setText(styleSheetPreviewText(value.toString()));
        preview->setStyleSheet(
            "SwLineEdit {"
            " background-color: " + SwCreatorTheme::rgb(th.surface3) + ";"
            " border-color: " + SwCreatorTheme::rgb(th.border) + ";"
            " border-width: 1px;"
            " border-radius: 10px;"
            " padding: 4px 8px;"
            " color: " + SwCreatorTheme::rgb(th.textPrimary) + ";"
            " }"
        );

        auto* open = new SwToolButton("...", cell);
        open->resize(34, 26);
        open->setStyleSheet(
            "SwToolButton {"
            " background-color: " + SwCreatorTheme::rgb(th.surface3) + ";"
            " border-color: " + SwCreatorTheme::rgb(th.border) + ";"
            " border-width: 1px;"
            " border-radius: 10px;"
            " padding: 0px;"
            " }"
        );

        const auto updateLayout = [cell, preview, open]() {
            if (!cell || !preview || !open) {
                return;
            }
            const SwRect r = cell->frameGeometry();
            const int gap = 8;
            const int buttonW = 34;

            preview->move(0, 0);
            preview->resize(std::max(0, r.width - buttonW - gap), r.height);

            open->move(r.width - buttonW, 0);
            open->resize(buttonW, r.height);
        };

        SwObject::connect(cell, &SwWidget::resized, this, [updateLayout](int, int) { updateLayout(); });
        updateLayout();

        SwObject::connect(open, &SwToolButton::clicked, this, [this, open](bool) {
            if (!open) {
                return;
            }
            openStyleSheetEditor_(open->frameGeometry());
        });

        m_styleSheetPreview = preview;
        m_styleSheetEditButton = open;

        m_tree->setIndexWidget(valueIndex, cell);
        return;
    }

    if (propName == kSwCreatorPseudoItemsKey) {
        auto* cell = new SwWidget(m_tree);
        cell->setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");

        auto* preview = new SwLineEdit(cell);
        preview->setReadOnly(true);
        preview->setText(value.toString());
        preview->setStyleSheet(
            "SwLineEdit {"
            " background-color: " + SwCreatorTheme::rgb(th.surface3) + ";"
            " border-color: " + SwCreatorTheme::rgb(th.border) + ";"
            " border-width: 1px;"
            " border-radius: 10px;"
            " padding: 4px 8px;"
            " color: " + SwCreatorTheme::rgb(th.textPrimary) + ";"
            " }"
        );

        auto* open = new SwToolButton("...", cell);
        open->resize(34, 26);
        open->setStyleSheet(
            "SwToolButton {"
            " background-color: " + SwCreatorTheme::rgb(th.surface3) + ";"
            " border-color: " + SwCreatorTheme::rgb(th.border) + ";"
            " border-width: 1px;"
            " border-radius: 10px;"
            " padding: 0px;"
            " }"
        );

        const auto updateLayout = [cell, preview, open]() {
            if (!cell || !preview || !open) {
                return;
            }
            const SwRect r = cell->frameGeometry();
            const int gap = 8;
            const int buttonW = 34;

            preview->move(0, 0);
            preview->resize(std::max(0, r.width - buttonW - gap), r.height);

            open->move(r.width - buttonW, 0);
            open->resize(buttonW, r.height);
        };

        SwObject::connect(cell, &SwWidget::resized, this, [updateLayout](int, int) { updateLayout(); });
        updateLayout();

        SwObject::connect(open, &SwToolButton::clicked, this, [this, open](bool) {
            if (!open) {
                return;
            }
            openItemsEditor_(open->frameGeometry());
        });

        m_itemsPreview = preview;
        m_itemsEditButton = open;

        m_tree->setIndexWidget(valueIndex, cell);
        return;
    }

    auto* edit = new SwLineEdit(m_tree);
    edit->setText(value.toString());
    edit->setStyleSheet(
        "SwLineEdit {"
        " background-color: " + SwCreatorTheme::rgb(th.surface3) + ";"
        " border-color: " + SwCreatorTheme::rgb(th.border) + ";"
        " border-width: 1px;"
        " border-radius: 10px;"
        " padding: 4px 8px;"
        " color: " + SwCreatorTheme::rgb(th.textPrimary) + ";"
        " }"
    );
    SwObject::connect(edit, &SwLineEdit::FocusChanged, this, [commitString, edit](bool focus) {
        if (focus) {
            return;
        }
        commitString(edit->getText());
    });
    m_tree->setIndexWidget(valueIndex, edit);
}
