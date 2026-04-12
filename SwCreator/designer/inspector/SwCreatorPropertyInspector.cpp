#include "SwCreatorPropertyInspector.h"

#include "SwCheckBox.h"
#include "SwComboBox.h"
#include "SwFrame.h"
#include "SwLabel.h"
#include "SwLayout.h"
#include "SwLineEdit.h"
#include "SwListWidget.h"
#include "SwMenu.h"
#include "SwSpacer.h"
#include "SwToolButton.h"
#include "SwTreeWidget.h"

#include "designer/dialogs/SwCreatorStringListDialog.h"
#include "designer/dialogs/SwCreatorTextEditDialog.h"
#include "theme/SwCreatorTheme.h"

#include <algorithm>
#include <functional>
#include <vector>

namespace {
const SwString kSwCreatorPseudoItemsKey = "__SwCreator_Items";
const SwString kSwCreatorLayoutRowKey = "__SwCreator_LayoutRow";
const SwString kSwCreatorLayoutColumnKey = "__SwCreator_LayoutColumn";
const SwString kSwCreatorRowSpanKey = "__SwCreator_RowSpan";
const SwString kSwCreatorColumnSpanKey = "__SwCreator_ColumnSpan";

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

SwGridLayout* targetGridLayout_(SwWidget* target) {
    if (!target) {
        return nullptr;
    }
    auto* parentWidget = dynamic_cast<SwWidget*>(target->parent());
    return parentWidget ? dynamic_cast<SwGridLayout*>(parentWidget->layout()) : nullptr;
}

struct EnumEditorConfig {
    SwVector<SwString> choices;
    std::function<SwAny(const SwString&)> toValue;
};

bool sameChoiceText_(const SwString& a, const SwString& b) {
    return a.trimmed().toLower() == b.trimmed().toLower();
}

bool enumEditorConfigFor_(SwWidget* target,
                          const SwString& propName,
                          const SwAny& value,
                          EnumEditorConfig* out) {
    if (!out) {
        return false;
    }

    const auto makeStringConfig = [&](std::initializer_list<const char*> items) {
        out->choices.clear();
        for (const char* item : items) {
            out->choices.push_back(SwString(item));
        }
        out->toValue = [](const SwString& text) { return SwAny(text); };
        return true;
    };

    const std::string typeName = value.typeName();
    if (typeName == typeid(FocusPolicyEnum).name()) {
        out->choices = {SwString("Accept"), SwString("Strong"), SwString("NoFocus")};
        out->toValue = [](const SwString& text) {
            const SwString t = text.trimmed().toLower();
            if (t == "strong") return SwAny::fromValue(FocusPolicyEnum::Strong);
            if (t == "nofocus" || t == "no_focus" || t == "no-focus") return SwAny::fromValue(FocusPolicyEnum::NoFocus);
            return SwAny::fromValue(FocusPolicyEnum::Accept);
        };
        return true;
    }

    if (typeName == typeid(CursorType).name()) {
        out->choices = {
            SwString("Default"), SwString("Arrow"), SwString("Hand"), SwString("IBeam"),
            SwString("Cross"), SwString("Wait"), SwString("SizeAll"), SwString("SizeNS"),
            SwString("SizeWE"), SwString("SizeNWSE"), SwString("SizeNESW")
        };
        out->toValue = [](const SwString& text) {
            const SwString t = text.trimmed().toLower();
            if (t == "arrow") return SwAny::fromValue(CursorType::Arrow);
            if (t == "hand") return SwAny::fromValue(CursorType::Hand);
            if (t == "ibeam" || t == "i-beam") return SwAny::fromValue(CursorType::IBeam);
            if (t == "cross") return SwAny::fromValue(CursorType::Cross);
            if (t == "wait") return SwAny::fromValue(CursorType::Wait);
            if (t == "sizeall") return SwAny::fromValue(CursorType::SizeAll);
            if (t == "sizens") return SwAny::fromValue(CursorType::SizeNS);
            if (t == "sizewe") return SwAny::fromValue(CursorType::SizeWE);
            if (t == "sizenwse") return SwAny::fromValue(CursorType::SizeNWSE);
            if (t == "sizenesw") return SwAny::fromValue(CursorType::SizeNESW);
            return SwAny::fromValue(CursorType::Default);
        };
        return true;
    }

    if (typeName == typeid(EchoModeEnum).name()) {
        out->choices = {
            SwString("NormalEcho"),
            SwString("NoEcho"),
            SwString("PasswordEcho"),
            SwString("PasswordEchoOnEdit")
        };
        out->toValue = [](const SwString& text) {
            const SwString t = text.trimmed().toLower();
            if (t == "noecho") return SwAny::fromValue(EchoModeEnum::NoEcho);
            if (t == "passwordecho") return SwAny::fromValue(EchoModeEnum::PasswordEcho);
            if (t == "passwordechoonedit") return SwAny::fromValue(EchoModeEnum::PasswordEchoOnEdit);
            return SwAny::fromValue(EchoModeEnum::NormalEcho);
        };
        return true;
    }

    if (propName == "HorizontalPolicy" || propName == "VerticalPolicy") {
        return makeStringConfig({"Fixed", "Minimum", "Maximum", "Preferred", "MinimumExpanding", "Expanding", "Ignored"});
    }

    if (propName == "Orientation" && dynamic_cast<SwSpacer*>(target)) {
        return makeStringConfig({"Horizontal", "Vertical"});
    }

    return false;
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

SwSize SwCreatorPropertyInspector::minimumSizeHint() const {
    SwSize hint = SwWidget::minimumSizeHint();
    const int pad = 10;
    const int headerH = 36;
    const int buttonGap = 4;
    const int buttonSize = 28;

    const int titleWidth = m_title ? m_title->sizeHint().width : 0;
    const int headerMinWidth = pad + titleWidth + pad + buttonSize + buttonGap + buttonSize + pad;
    const SwSize treeMin = m_tree ? m_tree->minimumSizeHint() : SwSize{140, 96};

    hint.width = std::max(hint.width, std::max(headerMinWidth, treeMin.width));
    hint.height = std::max(hint.height, headerH + treeMin.height);
    return hint;
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
        "SwFrame { background-color: " + SwCreatorTheme::rgb(th.surface2)
        + "; border-width: 0px; border-bottom-width: 1px; border-color: "
        + SwCreatorTheme::rgb(th.borderLight) + "; border-radius: 0px; }"
    );

    m_title = new SwLabel(SwString("Properties"), m_header);
    m_title->setFont(th.uiLabel);
    m_title->setStyleSheet(
        "SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: " + SwCreatorTheme::rgb(th.textSecondary) + "; }"
    );

    m_add = new SwToolButton("+", m_header);
    m_add->resize(28, 28);
    m_add->setStyleSheet(
        "SwToolButton { background-color: rgba(0,0,0,0);"
        " background-color-hover: " + SwCreatorTheme::rgb(th.hoverBg) + ";"
        " background-color-pressed: " + SwCreatorTheme::rgb(th.pressedBg) + ";"
        " border-width: 0px; border-radius: 2px; padding: 0px;"
        " color: " + SwCreatorTheme::rgb(th.textSecondary) + "; }"
    );

    m_remove = new SwToolButton("-", m_header);
    m_remove->resize(28, 28);
    m_remove->setStyleSheet(
        "SwToolButton { background-color: rgba(0,0,0,0);"
        " background-color-hover: " + SwCreatorTheme::rgb(th.hoverBg) + ";"
        " background-color-pressed: " + SwCreatorTheme::rgb(th.pressedBg) + ";"
        " border-width: 0px; border-radius: 2px; padding: 0px;"
        " color: " + SwCreatorTheme::rgb(th.textSecondary) + "; }"
    );

    m_tree = new SwTreeWidget(2, this);
    m_tree->setHeaderLabels(SwList<SwString>{SwString("Property"), SwString("Value")});
    m_tree->setColumnsFitToWidth(true);
    m_tree->setColumnStretch(0, 1);
    m_tree->setColumnStretch(1, 2);
    {
        SwTreeView::TreeColors tc;
        tc.background       = th.surface2;
        tc.backgroundBorder = th.surface2;
        tc.altFill          = th.surface3;
        tc.selFill          = th.selectionBg;
        tc.selBorder        = th.accentPrimary;
        tc.hoverFill        = th.hoverBg;
        tc.text             = th.textPrimary;
        tc.toggleStroke     = th.textSecondary;
        tc.gridLine         = th.borderLight;
        tc.bgRadius         = 0;
        tc.selRadius        = 2;
        m_tree->setTreeColors(tc);
        m_tree->setTreeFont(th.uiBody);
        m_tree->setRowHeight(28);
    }
    if (m_tree->header()) {
        m_tree->header()->setStyleSheet(
            "SwHeaderView {"
            " background-color: " + SwCreatorTheme::rgb(th.surface2) + ";"
            " border-color: " + SwCreatorTheme::rgb(th.borderLight) + ";"
            " border-width: 0px; border-bottom-width: 1px;"
            " border-radius: 0px;"
            " padding: 0px 10px;"
            " color: " + SwCreatorTheme::rgb(th.textSecondary) + ";"
            " divider-color: " + SwCreatorTheme::rgb(th.borderLight) + ";"
            " indicator-color: " + SwCreatorTheme::rgb(th.textMuted) + ";"
            " font-size: 11px;"
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
    const int pad = 10;
    const int headerH = 36;
    const int buttonSize = 28;
    const int buttonGap = 4;

    if (m_header) {
        m_header->move(0, 0);
        m_header->resize(r.width, headerH);
    }

    if (m_header) {
        const SwRect hr = m_header->frameGeometry();
        const int totalButtonsWidth = buttonSize * 2 + buttonGap;
        const int buttonStartX = std::max(pad, hr.width - pad - totalButtonsWidth);
        const int titleWidth = std::max(0, buttonStartX - 2 * pad);

        if (m_title) {
            m_title->move(pad, (headerH - 28) / 2);
            m_title->resize(titleWidth, 28);
            m_title->setVisible(titleWidth >= 42);
        }
        if (m_add) {
            m_add->move(buttonStartX, (headerH - buttonSize) / 2);
        }
        if (m_remove) {
            m_remove->move(buttonStartX + buttonSize + buttonGap, (headerH - buttonSize) / 2);
        }
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

    if (SwGridLayout* grid = targetGridLayout_(m_target)) {
        if (const SwGridLayout::Cell* cell = grid->cellForWidget(m_target)) {
            auto addGridProp = [&](const SwString& key, const SwString& label, int v) {
                PropRow row;
                row.key = key;
                row.displayName = label;
                row.value = SwAny(v);
                row.owner = "SwGridLayout";
                groups[row.owner].push_back(row);
            };
            addGridProp(kSwCreatorLayoutRowKey, "Layout Row", cell->row);
            addGridProp(kSwCreatorLayoutColumnKey, "Layout Column", cell->column);
            addGridProp(kSwCreatorRowSpanKey, "Row Span", cell->rowSpan);
            addGridProp(kSwCreatorColumnSpanKey, "Column Span", cell->columnSpan);
        }
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
        if (propName == kSwCreatorLayoutRowKey ||
            propName == kSwCreatorLayoutColumnKey ||
            propName == kSwCreatorRowSpanKey ||
            propName == kSwCreatorColumnSpanKey) {
            bool ok = false;
            const int iv = v.toInt(&ok);
            if (!ok) {
                return;
            }
            SwGridLayout* grid = targetGridLayout_(m_target);
            const SwGridLayout::Cell* cell = grid ? grid->cellForWidget(m_target) : nullptr;
            if (!grid || !cell) {
                return;
            }

            int row = cell->row;
            int column = cell->column;
            int rowSpan = std::max(1, cell->rowSpan);
            int columnSpan = std::max(1, cell->columnSpan);

            if (propName == kSwCreatorLayoutRowKey) row = std::max(0, iv);
            if (propName == kSwCreatorLayoutColumnKey) column = std::max(0, iv);
            if (propName == kSwCreatorRowSpanKey) rowSpan = std::max(1, iv);
            if (propName == kSwCreatorColumnSpanKey) columnSpan = std::max(1, iv);

            if (!grid->setWidgetPosition(m_target, row, column, rowSpan, columnSpan)) {
                rebuild_();
                return;
            }

            documentModified();
            canvasNeedsUpdate();
            rebuild_();
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
            " border-color: " + SwCreatorTheme::rgb(th.borderLight) + ";"
            " border-width: 0px; border-bottom-width: 1px;"
            " border-radius: 0px;"
            " padding: 2px 6px;"
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

    EnumEditorConfig enumConfig;
    if (enumEditorConfigFor_(m_target, propName, value, &enumConfig) && !enumConfig.choices.isEmpty()) {
        auto* combo = new SwComboBox(m_tree);
        combo->setStyleSheet(
            "SwComboBox {"
            " background-color: " + SwCreatorTheme::rgb(th.surface3) + ";"
            " border-color: " + SwCreatorTheme::rgb(th.borderLight) + ";"
            " border-width: 0px; border-bottom-width: 1px;"
            " border-radius: 0px;"
            " padding: 0px 6px;"
            " color: " + SwCreatorTheme::rgb(th.textPrimary) + ";"
            " }"
        );

        const SwString currentValue = value.toString().trimmed();
        int currentIndex = -1;
        for (int i = 0; i < enumConfig.choices.size(); ++i) {
            combo->addItem(enumConfig.choices[i]);
            if (currentIndex < 0 && sameChoiceText_(enumConfig.choices[i], currentValue)) {
                currentIndex = i;
            }
        }
        if (currentIndex >= 0) {
            combo->setCurrentIndex(currentIndex);
        }

        SwObject::connect(combo, &SwComboBox::currentIndexChanged, this, [this, propName, combo, enumConfig](int index) {
            if (!m_target || !combo || index < 0 || index >= combo->count()) {
                return;
            }
            if (propName == kSwCreatorLayoutRowKey ||
                propName == kSwCreatorLayoutColumnKey ||
                propName == kSwCreatorRowSpanKey ||
                propName == kSwCreatorColumnSpanKey) {
                return;
            }

            const SwAny newValue = enumConfig.toValue ? enumConfig.toValue(combo->itemText(index))
                                                      : SwAny(combo->itemText(index));
            m_target->setProperty(propName, newValue);
            documentModified();
            canvasNeedsUpdate();
            if (propName == "ObjectName" || propName == "Text" || propName == "ToolTips") {
                hierarchyNeedsRebuild();
            }
        });

        m_tree->setIndexWidget(valueIndex, combo);
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
            " border-color: " + SwCreatorTheme::rgb(th.borderLight) + ";"
            " border-width: 0px; border-bottom-width: 1px;"
            " border-radius: 0px;"
            " padding: 2px 6px;"
            " color: " + SwCreatorTheme::rgb(th.textPrimary) + ";"
            " }"
        );

        auto* open = new SwToolButton("...", cell);
        open->resize(34, 26);
        open->setStyleSheet(
            "SwToolButton {"
            " background-color: " + SwCreatorTheme::rgb(th.surface3) + ";"
            " background-color-hover: " + SwCreatorTheme::rgb(th.hoverBg) + ";"
            " border-color: " + SwCreatorTheme::rgb(th.borderLight) + ";"
            " border-width: 1px;"
            " border-radius: 2px;"
            " padding: 0px;"
            " color: " + SwCreatorTheme::rgb(th.textSecondary) + ";"
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
            " border-color: " + SwCreatorTheme::rgb(th.borderLight) + ";"
            " border-width: 0px; border-bottom-width: 1px;"
            " border-radius: 0px;"
            " padding: 2px 6px;"
            " color: " + SwCreatorTheme::rgb(th.textPrimary) + ";"
            " }"
        );

        auto* open = new SwToolButton("...", cell);
        open->resize(34, 26);
        open->setStyleSheet(
            "SwToolButton {"
            " background-color: " + SwCreatorTheme::rgb(th.surface3) + ";"
            " background-color-hover: " + SwCreatorTheme::rgb(th.hoverBg) + ";"
            " border-color: " + SwCreatorTheme::rgb(th.borderLight) + ";"
            " border-width: 1px;"
            " border-radius: 2px;"
            " padding: 0px;"
            " color: " + SwCreatorTheme::rgb(th.textSecondary) + ";"
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
        " border-color: " + SwCreatorTheme::rgb(th.borderLight) + ";"
        " border-width: 0px; border-bottom-width: 1px;"
        " border-radius: 0px;"
        " padding: 2px 6px;"
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
