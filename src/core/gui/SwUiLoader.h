#pragma once

/**
 * @file src/core/gui/SwUiLoader.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwUiLoader in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the ui loader interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * This header mainly contributes module-level utilities, helper declarations, or namespaced types
 * that are consumed by the surrounding subsystem.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
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

#include "SwWidget.h"
#include "SwLayout.h"

#include "SwCheckBox.h"
#include "SwComboBox.h"
#include "SwDoubleSpinBox.h"
#include "SwFrame.h"
#include "SwGroupBox.h"
#include "SwLabel.h"
#include "SwLineEdit.h"
#include "SwPlainTextEdit.h"
#include "SwProgressBar.h"
#include "SwPushButton.h"
#include "SwRadioButton.h"
#include "SwScrollArea.h"
#include "SwSlider.h"
#include "SwSpinBox.h"
#include "SwSplitter.h"
#include "SwStackedWidget.h"
#include "SwSpacer.h"
#include "SwTabWidget.h"
#include "SwTableView.h"
#include "SwTableWidget.h"
#include "SwTextEdit.h"
#include "SwToolButton.h"
#include "SwToolBox.h"
#include "SwTreeView.h"
#include "SwTreeWidget.h"
#include "SwMainWindow.h"

#include "core/io/SwFile.h"
#include "SwXmlDocument.h"

namespace swui {

using XmlNode = SwXmlNode;

class UiFactory {
public:
    using WidgetCtor = SwWidget* (*)(SwWidget* parent);

    /**
     * @brief Returns the current instance.
     * @return The current instance.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static UiFactory& instance();
    /**
     * @brief Performs the `registerWidget` operation.
     * @param className Value passed to the method.
     * @param ctor Value passed to the method.
     */
    void registerWidget(const SwString& className, WidgetCtor ctor);
    /**
     * @brief Creates the requested widget.
     * @param className Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @return The resulting widget.
     */
    SwWidget* createWidget(const SwString& className, SwWidget* parent) const;

private:
    UiFactory();
    static SwString qtAliasToSw_(const SwString& qtClass);

    SwMap<SwString, WidgetCtor> m_widgetCtors;
};

class UiLoader {
public:
    struct LoadResult {
        SwWidget* root{nullptr};
        SwString error;
        bool ok{false};
    };

    /**
     * @brief Performs the `loadFromString` operation on the associated resource.
     * @param xml Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @return The resulting from String.
     */
    static LoadResult loadFromString(const SwString& xml, SwWidget* parent = nullptr);
    /**
     * @brief Performs the `loadFromFile` operation on the associated resource.
     * @param filePath Path of the target file.
     * @param parent Optional parent object that owns this instance.
     * @return The resulting from File.
     */
    static LoadResult loadFromFile(const SwString& filePath, SwWidget* parent = nullptr);

private:
    static int toInt_(const SwString& s, int def = 0);
    static double toDouble_(const SwString& s, double def = 0.0);
    static bool toBool_(SwString s, bool def = false);

    static SwString propertyNameToSw_(const SwString& name);
    static SwString childText_(const XmlNode& node, const char* childName);
    static SwString textValue_(const XmlNode& propNode);
    static SwString attributeValue_(const XmlNode& widgetNode, const char* attributeName);
    static SwMap<SwString, SwString> parseCustomWidgets_(const XmlNode& uiRoot);

    static void applyCommonProperty_(SwWidget* w, const SwString& rawName, const XmlNode& propNode);

    static bool attachChildToContainer_(SwWidget* parent, SwWidget* child, const XmlNode& childWidgetNode, SwString& outError);

    static SwAbstractLayout* createLayout_(const SwString& className, SwWidget* parent, SwString& outError);
    static SwSizePolicy::Policy spacerPolicyFromString_(SwString value);
    static SwSpacerItem* loadSpacerItem_(const XmlNode& spacerNode);
    static bool applyLayout_(SwWidget* parentWidget,
                            const XmlNode& widgetNode,
                            SwString& outError,
                            const SwMap<SwString, SwString>& customWidgetExtends);

    static SwWidget* loadWidget_(const XmlNode& widgetNode,
                                 SwWidget* parent,
                                 SwString& outError,
                                 const SwMap<SwString, SwString>& customWidgetExtends);

    static bool loadIntoExistingWidget_(SwWidget* target,
                                       const XmlNode& widgetNode,
                                       SwString& outError,
                                       const SwMap<SwString, SwString>& customWidgetExtends);

    static bool loadQtMainWindowCentralWidgetInto_(SwWidget* target,
                                                  const XmlNode& mainWindowNode,
                                                  SwString& outError,
                                                  const SwMap<SwString, SwString>& customWidgetExtends);

    static SwMainWindow* loadQtMainWindow_(const XmlNode& mainWindowNode,
                                          const SwMap<SwString, SwString>& customWidgetExtends,
                                          SwString& outError);
};

inline UiFactory& UiFactory::instance() {
    static UiFactory f;
    return f;
}

inline void UiFactory::registerWidget(const SwString& className, WidgetCtor ctor) {
    if (className.isEmpty() || !ctor) {
        return;
    }
    m_widgetCtors[className] = ctor;
}

inline SwWidget* UiFactory::createWidget(const SwString& className, SwWidget* parent) const {
    auto it = m_widgetCtors.find(className);
    if (it != m_widgetCtors.end()) {
        return it->second(parent);
    }

    const SwString alias = qtAliasToSw_(className);
    if (alias != className) {
        it = m_widgetCtors.find(alias);
        if (it != m_widgetCtors.end()) {
            return it->second(parent);
        }
    }

    return nullptr;
}

inline UiFactory::UiFactory() {
    registerWidget("SwWidget", [](SwWidget* p) -> SwWidget* { return new SwWidget(p); });
    registerWidget("SwFrame", [](SwWidget* p) -> SwWidget* { return new SwFrame(p); });
    registerWidget("Line", [](SwWidget* p) -> SwWidget* { return new SwFrame(p); }); // Line separator used by the designer
    registerWidget("SwLabel", [](SwWidget* p) -> SwWidget* { return new SwLabel(p); });
    registerWidget("SwPushButton", [](SwWidget* p) -> SwWidget* { return new SwPushButton("PushButton", p); });
    registerWidget("SwLineEdit", [](SwWidget* p) -> SwWidget* { return new SwLineEdit(p); });
    registerWidget("SwCheckBox", [](SwWidget* p) -> SwWidget* { return new SwCheckBox(p); });
    registerWidget("SwRadioButton", [](SwWidget* p) -> SwWidget* { return new SwRadioButton(p); });
    registerWidget("SwComboBox", [](SwWidget* p) -> SwWidget* { return new SwComboBox(p); });
    registerWidget("SwProgressBar", [](SwWidget* p) -> SwWidget* { return new SwProgressBar(p); });
    registerWidget("SwPlainTextEdit", [](SwWidget* p) -> SwWidget* { return new SwPlainTextEdit(p); });
    registerWidget("SwTextEdit", [](SwWidget* p) -> SwWidget* { return new SwTextEdit(p); });
    registerWidget("SwTabWidget", [](SwWidget* p) -> SwWidget* { return new SwTabWidget(p); });
    registerWidget("SwSplitter",
                   [](SwWidget* p) -> SwWidget* { return new SwSplitter(SwSplitter::Orientation::Horizontal, p); });
    registerWidget("SwStackedWidget", [](SwWidget* p) -> SwWidget* { return new SwStackedWidget(p); });
    registerWidget("SwScrollArea", [](SwWidget* p) -> SwWidget* { return new SwScrollArea(p); });
    registerWidget("SwSpacer", [](SwWidget* p) -> SwWidget* { return new SwSpacer(p); });
    registerWidget("SwGroupBox", [](SwWidget* p) -> SwWidget* { return new SwGroupBox(p); });
    registerWidget("SwToolButton", [](SwWidget* p) -> SwWidget* { return new SwToolButton(p); });
    registerWidget("SwToolBox", [](SwWidget* p) -> SwWidget* { return new SwToolBox(p); });
    registerWidget("SwSpinBox", [](SwWidget* p) -> SwWidget* { return new SwSpinBox(p); });
    registerWidget("SwDoubleSpinBox", [](SwWidget* p) -> SwWidget* { return new SwDoubleSpinBox(p); });
    registerWidget("SwSlider",
                   [](SwWidget* p) -> SwWidget* { return new SwSlider(SwSlider::Orientation::Horizontal, p); });
    registerWidget("SwTableWidget", [](SwWidget* p) -> SwWidget* { return new SwTableWidget(0, 0, p); });
    registerWidget("SwTreeWidget", [](SwWidget* p) -> SwWidget* { return new SwTreeWidget(1, p); });
    registerWidget("SwTableView", [](SwWidget* p) -> SwWidget* { return new SwTableView(p); });
    registerWidget("SwTreeView", [](SwWidget* p) -> SwWidget* { return new SwTreeView(p); });
}

inline SwString UiFactory::qtAliasToSw_(const SwString& qtClass) {
    if (!qtClass.isEmpty() && qtClass[0] == 'Q') {
        return SwString("Sw") + qtClass.mid(1);
    }
    return qtClass;
}

inline int UiLoader::toInt_(const SwString& s, int def) {
    if (s.isEmpty()) {
        return def;
    }
    bool ok = false;
    const int v = s.trimmed().toInt(&ok);
    return ok ? v : def;
}

inline double UiLoader::toDouble_(const SwString& s, double def) {
    if (s.isEmpty()) {
        return def;
    }
    bool ok = false;
    const double v = s.trimmed().toDouble(&ok);
    return ok ? v : def;
}

inline bool UiLoader::toBool_(SwString s, bool def) {
    s = s.trimmed().toLower();
    if (s == "true" || s == "1") {
        return true;
    }
    if (s == "false" || s == "0") {
        return false;
    }
    return def;
}

inline SwString UiLoader::propertyNameToSw_(const SwString& name) {
    // Support lower camelCase names by mapping the ones we use.
    if (name == "objectName") return "ObjectName";
    if (name == "toolTip") return "ToolTips";
    if (name == "styleSheet") return "StyleSheet";
    if (name == "enabled") return "Enable";
    if (name == "visible") return "Visible";
    if (name == "text") return "Text";
    if (name == "title") return "Text";
    if (name == "placeholderText") return "Placeholder";
    if (name == "readOnly") return "ReadOnly";
    if (name == "geometry") return "geometry";

    // Generic: lower camelCase -> PascalCase.
    if (!name.isEmpty() && name[0] >= 'a' && name[0] <= 'z') {
        SwString out = name;
        out[0] = static_cast<char>(out[0] - ('a' - 'A'));
        return out;
    }

    // Already PascalCase (Sw-style).
    return name;
}

inline SwString UiLoader::childText_(const XmlNode& node, const char* childName) {
    const XmlNode* c = node.firstChild(childName);
    if (!c) {
        return {};
    }
    return c->text.trimmed();
}

inline SwString UiLoader::textValue_(const XmlNode& propNode) {
    // Designer XML stores typed values:
    // <property name="text"><string>Hi</string></property>
    // <property name="enabled"><bool>true</bool></property>
    // We'll accept the first child element text if present, otherwise property.text.
    if (!propNode.children.isEmpty()) {
        for (const auto& c : propNode.children) {
            if (c.name == "string" || c.name == "cstring" || c.name == "number" || c.name == "double" || c.name == "bool" ||
                c.name == "enum" || c.name == "set") {
                return c.text.trimmed();
            }
            if (c.name == "stringlist") {
                // Join <string> children with commas (compatible with SwAny conversion rules).
                SwString joined;
                bool first = true;
                for (const auto& s : c.children) {
                    if (s.name != "string") {
                        continue;
                    }
                    const SwString t = s.text.trimmed();
                    if (!first) {
                        joined.append(",");
                    }
                    first = false;
                    joined.append(t);
                }
                return joined;
            }
        }
    }

    return propNode.text.trimmed();
}

inline SwString UiLoader::attributeValue_(const XmlNode& widgetNode, const char* attributeName) {
    if (!attributeName) {
        return {};
    }
    for (const auto* a : widgetNode.childrenNamed("attribute")) {
        if (!a) {
            continue;
        }
        if (a->attr("name") == attributeName) {
            return textValue_(*a);
        }
    }
    return {};
}

inline SwMap<SwString, SwString> UiLoader::parseCustomWidgets_(const XmlNode& uiRoot) {
    SwMap<SwString, SwString> out;
    const XmlNode* customwidgets = uiRoot.firstChild("customwidgets");
    if (!customwidgets) {
        return out;
    }
    for (const auto* cw : customwidgets->childrenNamed("customwidget")) {
        if (!cw) {
            continue;
        }
        const SwString cls = childText_(*cw, "class").trimmed();
        const SwString ext = childText_(*cw, "extends").trimmed();
        if (!cls.isEmpty() && !ext.isEmpty()) {
            out[cls] = ext;
        }
    }
    return out;
}

inline void UiLoader::applyCommonProperty_(SwWidget* w, const SwString& rawName, const XmlNode& propNode) {
    if (!w || rawName.isEmpty()) {
        return;
    }

    if (rawName == "geometry") {
        const XmlNode* rect = propNode.firstChild("rect");
        if (!rect) {
            return;
        }
        const int x = toInt_(childText_(*rect, "x"));
        const int y = toInt_(childText_(*rect, "y"));
        const int width = toInt_(childText_(*rect, "width"));
        const int height = toInt_(childText_(*rect, "height"));
        w->move(x, y);
        w->resize(width, height);
        return;
    }

    if (rawName == "minimumSize") {
        const XmlNode* size = propNode.firstChild("size");
        if (!size) {
            return;
        }
        const int wpx = toInt_(childText_(*size, "width"));
        const int hpx = toInt_(childText_(*size, "height"));
        w->setMinimumSize(wpx, hpx);
        return;
    }

    if (rawName == "maximumSize") {
        const XmlNode* size = propNode.firstChild("size");
        if (!size) {
            return;
        }
        const int wpx = toInt_(childText_(*size, "width"));
        const int hpx = toInt_(childText_(*size, "height"));
        w->setMaximumSize(wpx, hpx);
        return;
    }

    const SwString propertyName = propertyNameToSw_(rawName);
    const SwString valueText = textValue_(propNode);

    // Splitter orientation
    if (rawName == "orientation" || rawName == "Orientation") {
        const bool isVert = valueText.contains("Vertical") || valueText.toLower() == "vertical";
        if (auto* splitter = dynamic_cast<SwSplitter*>(w)) {
            splitter->setOrientation(isVert ? SwSplitter::Orientation::Vertical
                                            : SwSplitter::Orientation::Horizontal);
            return;
        }
        if (auto* slider = dynamic_cast<SwSlider*>(w)) {
            slider->setOrientation(isVert ? SwSlider::Orientation::Vertical
                                          : SwSlider::Orientation::Horizontal);
            return;
        }
        if (auto* pb = dynamic_cast<SwProgressBar*>(w)) {
            pb->setOrientation(isVert ? SwProgressBar::Orientation::Vertical
                                      : SwProgressBar::Orientation::Horizontal);
            return;
        }
        if (auto* spacer = dynamic_cast<SwSpacer*>(w)) {
            spacer->setDirection(isVert ? SwSpacer::Direction::Vertical
                                        : SwSpacer::Direction::Horizontal);
            return;
        }
        if (w->propertyExist(propertyName)) {
            w->setProperty(propertyName, SwAny(valueText.trimmed()));
            return;
        }
    }

    // .ui checkable/checked support (subset).
    if (rawName == "checkable") {
        const bool v = toBool_(valueText, false);
        if (w->propertyExist(propertyName)) {
            w->setProperty(propertyName, SwAny(v));
            return;
        }
        if (auto* gb = dynamic_cast<SwGroupBox*>(w)) {
            gb->setCheckable(v);
            return;
        }
        if (auto* pb = dynamic_cast<SwPushButton*>(w)) {
            pb->setCheckable(v);
            return;
        }
        if (auto* tb = dynamic_cast<SwToolButton*>(w)) {
            tb->setCheckable(v);
            return;
        }
        w->setProperty(propertyName, SwAny(v));
        return;
    }

    if (rawName == "checked") {
        const bool v = toBool_(valueText, false);
        if (w->propertyExist(propertyName)) {
            w->setProperty(propertyName, SwAny(v));
            return;
        }
        if (auto* cb = dynamic_cast<SwCheckBox*>(w)) {
            cb->setChecked(v);
            return;
        }
        if (auto* rb = dynamic_cast<SwRadioButton*>(w)) {
            rb->setChecked(v);
            return;
        }
        if (auto* gb = dynamic_cast<SwGroupBox*>(w)) {
            gb->setChecked(v);
            return;
        }
        if (auto* pb = dynamic_cast<SwPushButton*>(w)) {
            pb->setCheckable(true);
            pb->setChecked(v);
            return;
        }
        if (auto* tb = dynamic_cast<SwToolButton*>(w)) {
            tb->setCheckable(true);
            tb->setChecked(v);
            return;
        }
        w->setProperty(propertyName, SwAny(v));
        return;
    }

    // Numeric properties (subset).
    if (rawName == "value") {
        const int iv = toInt_(valueText, 0);
        if (auto* pb = dynamic_cast<SwProgressBar*>(w)) {
            pb->setValue(iv);
            return;
        }
        if (auto* slider = dynamic_cast<SwSlider*>(w)) {
            slider->setValue(iv);
            return;
        }
        if (auto* spin = dynamic_cast<SwSpinBox*>(w)) {
            spin->setValue(iv);
            return;
        }
        if (auto* dspin = dynamic_cast<SwDoubleSpinBox*>(w)) {
            dspin->setValue(toDouble_(valueText, dspin->value()));
            return;
        }
    }

    if (rawName == "minimum") {
        const int iv = toInt_(valueText, 0);
        if (auto* pb = dynamic_cast<SwProgressBar*>(w)) {
            pb->setMinimum(iv);
            return;
        }
        if (auto* slider = dynamic_cast<SwSlider*>(w)) {
            slider->setMinimum(iv);
            return;
        }
        if (auto* spin = dynamic_cast<SwSpinBox*>(w)) {
            spin->setMinimum(iv);
            return;
        }
        if (auto* dspin = dynamic_cast<SwDoubleSpinBox*>(w)) {
            dspin->setMinimum(toDouble_(valueText, dspin->minimum()));
            return;
        }
    }

    if (rawName == "maximum") {
        const int iv = toInt_(valueText, 0);
        if (auto* pb = dynamic_cast<SwProgressBar*>(w)) {
            pb->setMaximum(iv);
            return;
        }
        if (auto* slider = dynamic_cast<SwSlider*>(w)) {
            slider->setMaximum(iv);
            return;
        }
        if (auto* spin = dynamic_cast<SwSpinBox*>(w)) {
            spin->setMaximum(iv);
            return;
        }
        if (auto* dspin = dynamic_cast<SwDoubleSpinBox*>(w)) {
            dspin->setMaximum(toDouble_(valueText, dspin->maximum()));
            return;
        }
    }

    if (rawName == "orientation") {
        const SwString o = valueText.trimmed().toLower();
        const bool vertical = (o.indexOf("vertical") >= 0);

        if (auto* splitter = dynamic_cast<SwSplitter*>(w)) {
            splitter->setOrientation(vertical ? SwSplitter::Orientation::Vertical : SwSplitter::Orientation::Horizontal);
            return;
        }
        if (auto* slider = dynamic_cast<SwSlider*>(w)) {
            slider->setOrientation(vertical ? SwSlider::Orientation::Vertical : SwSlider::Orientation::Horizontal);
            return;
        }
        if (auto* pb = dynamic_cast<SwProgressBar*>(w)) {
            pb->setOrientation(vertical ? SwProgressBar::Orientation::Vertical : SwProgressBar::Orientation::Horizontal);
            return;
        }
    }

    if (rawName == "handleWidth") {
        if (auto* splitter = dynamic_cast<SwSplitter*>(w)) {
            splitter->setHandleWidth(toInt_(valueText, splitter->handleWidth()));
            return;
        }
    }

    if (rawName == "widgetResizable") {
        if (auto* scroll = dynamic_cast<SwScrollArea*>(w)) {
            scroll->setWidgetResizable(toBool_(valueText, scroll->widgetResizable()));
            return;
        }
    }

    // Preserve XML type when possible (helps dynamic properties).
    if (!propNode.children.isEmpty()) {
        for (const auto& c : propNode.children) {
            if (c.name == "bool") {
                w->setProperty(propertyName, SwAny(toBool_(c.text, false)));
                return;
            }
            if (c.name == "number") {
                w->setProperty(propertyName, SwAny(toInt_(c.text, 0)));
                return;
            }
            if (c.name == "double") {
                w->setProperty(propertyName, SwAny(toDouble_(c.text, 0.0)));
                return;
            }
            if (c.name == "enum" || c.name == "set") {
                w->setProperty(propertyName, SwAny(c.text.trimmed()));
                return;
            }
            if (c.name == "string" || c.name == "cstring") {
                w->setProperty(propertyName, SwAny(c.text.trimmed()));
                return;
            }
        }
    }

    // Fallback: treat as string and rely on SwAny conversions.
    w->setProperty(propertyName, SwAny(valueText));
}

inline bool UiLoader::attachChildToContainer_(SwWidget* parent, SwWidget* child, const XmlNode& childWidgetNode, SwString& outError) {
    if (!parent || !child) {
        return true;
    }

    if (auto* tab = dynamic_cast<SwTabWidget*>(parent)) {
        SwString label = attributeValue_(childWidgetNode, "title");
        if (label.isEmpty()) label = attributeValue_(childWidgetNode, "label");
        if (label.isEmpty()) label = childWidgetNode.attr("name");
        if (label.isEmpty()) label = child->getObjectName();
        if (label.isEmpty()) label = child->className();
        tab->addTab(child, label);
        return true;
    }

    if (auto* toolbox = dynamic_cast<SwToolBox*>(parent)) {
        SwString label = attributeValue_(childWidgetNode, "label");
        if (label.isEmpty()) label = attributeValue_(childWidgetNode, "title");
        if (label.isEmpty()) label = childWidgetNode.attr("name");
        if (label.isEmpty()) label = child->getObjectName();
        if (label.isEmpty()) label = child->className();
        toolbox->addItem(child, label);
        return true;
    }

    if (auto* stack = dynamic_cast<SwStackedWidget*>(parent)) {
        stack->addWidget(child);
        return true;
    }

    if (auto* splitter = dynamic_cast<SwSplitter*>(parent)) {
        splitter->addWidget(child);
        return true;
    }

    if (auto* scroll = dynamic_cast<SwScrollArea*>(parent)) {
        if (scroll->widget() && scroll->widget() != child) {
            outError = "SwScrollArea can only have one content widget";
            return false;
        }
        scroll->setWidget(child);
        return true;
    }

    return true;
}

inline SwAbstractLayout* UiLoader::createLayout_(const SwString& className, SwWidget* parent, SwString& outError) {
    if (!parent) {
        outError = "Layout without parent widget";
        return nullptr;
    }

    // Aliases accepted by the loader.
    SwString cls = className;
    if (cls == "QVBoxLayout") cls = "SwVerticalLayout";
    if (cls == "QHBoxLayout") cls = "SwHorizontalLayout";
    if (cls == "QGridLayout") cls = "SwGridLayout";
    if (cls == "QFormLayout") cls = "SwFormLayout";

    if (cls == "SwVerticalLayout") return new SwVerticalLayout(parent);
    if (cls == "SwHorizontalLayout") return new SwHorizontalLayout(parent);
    if (cls == "SwGridLayout") return new SwGridLayout(parent);
    if (cls == "SwFormLayout") return new SwFormLayout(parent);

    outError = SwString("Unsupported layout class: ") + className;
    return nullptr;
}

inline SwSizePolicy::Policy UiLoader::spacerPolicyFromString_(SwString value) {
    value = value.trimmed();
    const size_t sep = value.lastIndexOf(':');
    if (sep != static_cast<size_t>(-1)) {
        value = value.mid(static_cast<int>(sep + 1));
    }
    value = value.trimmed();
    if (value == "Fixed") return SwSizePolicy::Fixed;
    if (value == "Minimum") return SwSizePolicy::Minimum;
    if (value == "Maximum") return SwSizePolicy::Maximum;
    if (value == "Preferred") return SwSizePolicy::Preferred;
    if (value == "MinimumExpanding") return SwSizePolicy::MinimumExpanding;
    if (value == "Expanding") return SwSizePolicy::Expanding;
    if (value == "Ignored") return SwSizePolicy::Ignored;
    return SwSizePolicy::Minimum;
}

inline SwSpacerItem* UiLoader::loadSpacerItem_(const XmlNode& spacerNode) {
    int width = 40;
    int height = 20;
    SwString orientation = "Qt::Horizontal";
    SwSizePolicy::Policy horizontalPolicy = SwSizePolicy::Minimum;
    SwSizePolicy::Policy verticalPolicy = SwSizePolicy::Minimum;
    bool explicitHorizontalPolicy = false;
    bool explicitVerticalPolicy = false;
    bool hasSizeType = false;
    SwSizePolicy::Policy sizeTypePolicy = SwSizePolicy::Minimum;

    for (const auto* prop : spacerNode.childrenNamed("property")) {
        if (!prop) {
            continue;
        }

        const SwString rawName = prop->attr("name");
        if (rawName == "sizeHint") {
            const XmlNode* size = prop->firstChild("size");
            if (!size) {
                continue;
            }
            width = std::max(0, toInt_(childText_(*size, "width"), width));
            height = std::max(0, toInt_(childText_(*size, "height"), height));
        } else if (rawName == "orientation") {
            orientation = textValue_(*prop).trimmed();
        } else if (rawName == "sizeType") {
            hasSizeType = true;
            sizeTypePolicy = spacerPolicyFromString_(textValue_(*prop));
        } else if (rawName == "horizontalSizeType") {
            explicitHorizontalPolicy = true;
            horizontalPolicy = spacerPolicyFromString_(textValue_(*prop));
        } else if (rawName == "verticalSizeType") {
            explicitVerticalPolicy = true;
            verticalPolicy = spacerPolicyFromString_(textValue_(*prop));
        }
    }

    const SwString orientationLower = orientation.trimmed().toLower();
    const bool vertical = orientationLower.contains("vertical");
    if (hasSizeType) {
        if (vertical) {
            if (!explicitVerticalPolicy) {
                verticalPolicy = sizeTypePolicy;
            }
            if (!explicitHorizontalPolicy) {
                horizontalPolicy = SwSizePolicy::Minimum;
            }
        } else {
            if (!explicitHorizontalPolicy) {
                horizontalPolicy = sizeTypePolicy;
            }
            if (!explicitVerticalPolicy) {
                verticalPolicy = SwSizePolicy::Minimum;
            }
        }
    }

    return new SwSpacerItem(width, height, horizontalPolicy, verticalPolicy);
}

inline bool UiLoader::applyLayout_(SwWidget* parentWidget,
                                  const XmlNode& widgetNode,
                                  SwString& outError,
                                  const SwMap<SwString, SwString>& customWidgetExtends) {
    if (!parentWidget) {
        return false;
    }

    const XmlNode* layoutNode = widgetNode.firstChild("layout");
    if (!layoutNode) {
        return true;
    }

    const SwString layoutClass = layoutNode->attr("class");
    if (layoutClass.isEmpty()) {
        outError = "Layout missing class attribute";
        return false;
    }

    SwAbstractLayout* layout = createLayout_(layoutClass, parentWidget, outError);
    if (!layout) {
        return false;
    }

    // Apply common layout properties (spacing/margin).
    for (const auto* prop : layoutNode->childrenNamed("property")) {
        const SwString rawName = prop ? prop->attr("name") : SwString();
        if (rawName == "spacing") {
            layout->setSpacing(toInt_(textValue_(*prop), layout->spacing()));
        } else if (rawName == "margin") {
            layout->setMargin(toInt_(textValue_(*prop), layout->margin()));
        } else if (rawName == "leftMargin" || rawName == "topMargin" || rawName == "rightMargin" || rawName == "bottomMargin") {
            // The source format has per-side margins; SwLayout has a single margin for now -> pick the first one we see.
            layout->setMargin(toInt_(textValue_(*prop), layout->margin()));
        }
    }

    // Parse items.
    const auto items = layoutNode->childrenNamed("item");
    for (const auto* item : items) {
        if (!item) {
            continue;
        }
        const XmlNode* childWidgetNode = item->firstChild("widget");
        const XmlNode* childSpacerNode = item->firstChild("spacer");

        if (childWidgetNode) {
            SwWidget* child = loadWidget_(*childWidgetNode, parentWidget, outError, customWidgetExtends);
            if (!child) {
                delete layout;
                return false;
            }

            if (auto* grid = dynamic_cast<SwGridLayout*>(layout)) {
                const int row = toInt_(item->attr("row", "0"), 0);
                const int col = toInt_(item->attr("column", "0"), 0);
                const int rowSpan = toInt_(item->attr("rowspan", "1"), 1);
                const int colSpan = toInt_(item->attr("colspan", "1"), 1);
                grid->addWidget(child, row, col, rowSpan, colSpan);
            } else if (auto* boxV = dynamic_cast<SwVerticalLayout*>(layout)) {
                boxV->addWidget(child);
            } else if (auto* boxH = dynamic_cast<SwHorizontalLayout*>(layout)) {
                boxH->addWidget(child);
            } else if (auto* form = dynamic_cast<SwFormLayout*>(layout)) {
                const bool hasRowAttr = item->attributes.find("row") != item->attributes.end();
                const bool hasColAttr = item->attributes.find("column") != item->attributes.end();
                if (hasRowAttr || hasColAttr) {
                    const int row = toInt_(item->attr("row", "0"), 0);
                    const int col = toInt_(item->attr("column", "0"), 0);
                    form->setCell(row, col, child);
                } else {
                    form->addWidget(child);
                }
            }
            continue;
        }

        if (childSpacerNode) {
            SwSpacerItem* spacer = loadSpacerItem_(*childSpacerNode);
            if (!spacer) {
                delete layout;
                outError = "Failed to create spacer item";
                return false;
            }

            if (auto* grid = dynamic_cast<SwGridLayout*>(layout)) {
                const int row = toInt_(item->attr("row", "0"), 0);
                const int col = toInt_(item->attr("column", "0"), 0);
                const int rowSpan = toInt_(item->attr("rowspan", "1"), 1);
                const int colSpan = toInt_(item->attr("colspan", "1"), 1);
                grid->addItem(spacer, row, col, rowSpan, colSpan);
            } else if (auto* boxV = dynamic_cast<SwVerticalLayout*>(layout)) {
                boxV->addSpacerItem(spacer);
            } else if (auto* boxH = dynamic_cast<SwHorizontalLayout*>(layout)) {
                boxH->addSpacerItem(spacer);
            } else if (auto* form = dynamic_cast<SwFormLayout*>(layout)) {
                const bool hasRowAttr = item->attributes.find("row") != item->attributes.end();
                const bool hasColAttr = item->attributes.find("column") != item->attributes.end();
                if (hasRowAttr || hasColAttr) {
                    const int row = toInt_(item->attr("row", "0"), 0);
                    const int col = toInt_(item->attr("column", "0"), 0);
                    form->setItem(row, col, spacer);
                } else {
                    form->addItem(spacer);
                }
            } else {
                delete spacer;
            }
        }
    }

    parentWidget->setLayout(layout);
    return true;
}

inline SwWidget* UiLoader::loadWidget_(const XmlNode& widgetNode,
                                      SwWidget* parent,
                                      SwString& outError,
                                      const SwMap<SwString, SwString>& customWidgetExtends) {
    const SwString className = widgetNode.attr("class");
    if (className.isEmpty()) {
        outError = "Widget missing class attribute";
        return nullptr;
    }

    SwWidget* w = UiFactory::instance().createWidget(className, parent);
    if (!w) {
        SwString resolved = className;
        for (int depth = 0; depth < 8 && !w; ++depth) {
            auto it = customWidgetExtends.find(resolved);
            if (it == customWidgetExtends.end()) {
                break;
            }
            resolved = it->second;
            w = UiFactory::instance().createWidget(resolved, parent);
        }
    }
    if (!w) {
        outError = SwString("Unknown widget class: ") + className;
        return nullptr;
    }

    const SwString objName = widgetNode.attr("name");
    if (!objName.isEmpty()) {
        w->setObjectName(objName);
    }

    int deferredCurrentIndex = -1;

    // Apply direct <property> children.
    for (const auto* prop : widgetNode.childrenNamed("property")) {
        if (!prop) {
            continue;
        }
        const SwString rawName = prop->attr("name");
        if (rawName.isEmpty()) {
            continue;
        }

        if (rawName == "currentIndex") {
            if (dynamic_cast<SwTabWidget*>(w) || dynamic_cast<SwStackedWidget*>(w) || dynamic_cast<SwToolBox*>(w)) {
                deferredCurrentIndex = toInt_(textValue_(*prop), -1);
                continue;
            }
        }

        applyCommonProperty_(w, rawName, *prop);
    }

    // Layout blocks take precedence; if present, children are created through <layout><item>...
    if (!applyLayout_(w, widgetNode, outError, customWidgetExtends)) {
        if (!parent) {
            delete w;
        }
        return nullptr;
    }

    // Create direct child widgets (absolute positioning) if no layout is present.
    if (!widgetNode.firstChild("layout")) {
        for (const auto* childWidgetNode : widgetNode.childrenNamed("widget")) {
            if (!childWidgetNode) {
                continue;
            }
            SwWidget* child = loadWidget_(*childWidgetNode, w, outError, customWidgetExtends);
            if (!child) {
                if (!parent) {
                    delete w;
                }
                return nullptr;
            }
            if (!attachChildToContainer_(w, child, *childWidgetNode, outError)) {
                if (!parent) {
                    delete w;
                }
                return nullptr;
            }
        }
    }

    if (deferredCurrentIndex >= 0) {
        if (auto* tab = dynamic_cast<SwTabWidget*>(w)) {
            tab->setCurrentIndex(deferredCurrentIndex);
        } else if (auto* stack = dynamic_cast<SwStackedWidget*>(w)) {
            stack->setCurrentIndex(deferredCurrentIndex);
        } else if (auto* toolbox = dynamic_cast<SwToolBox*>(w)) {
            toolbox->setCurrentIndex(deferredCurrentIndex);
        }
    }

    return w;
}

inline bool UiLoader::loadIntoExistingWidget_(SwWidget* target,
                                             const XmlNode& widgetNode,
                                             SwString& outError,
                                             const SwMap<SwString, SwString>& customWidgetExtends) {
    if (!target) {
        outError = "Null target widget";
        return false;
    }

    const SwString objName = widgetNode.attr("name");
    if (!objName.isEmpty()) {
        target->setObjectName(objName);
    }

    int deferredCurrentIndex = -1;

    for (const auto* prop : widgetNode.childrenNamed("property")) {
        if (!prop) {
            continue;
        }
        const SwString rawName = prop->attr("name");
        if (rawName.isEmpty()) {
            continue;
        }

        if (rawName == "currentIndex") {
            if (dynamic_cast<SwTabWidget*>(target) || dynamic_cast<SwStackedWidget*>(target) || dynamic_cast<SwToolBox*>(target)) {
                deferredCurrentIndex = toInt_(textValue_(*prop), -1);
                continue;
            }
        }

        // Avoid moving/resizing containers we don't own (centralWidget, embedded widgets...).
        if (rawName == "geometry") {
            continue;
        }

        applyCommonProperty_(target, rawName, *prop);
    }

    if (!applyLayout_(target, widgetNode, outError, customWidgetExtends)) {
        return false;
    }

    if (!widgetNode.firstChild("layout")) {
        for (const auto* childWidgetNode : widgetNode.childrenNamed("widget")) {
            if (!childWidgetNode) {
                continue;
            }
            SwWidget* child = loadWidget_(*childWidgetNode, target, outError, customWidgetExtends);
            if (!child) {
                return false;
            }
            if (!attachChildToContainer_(target, child, *childWidgetNode, outError)) {
                return false;
            }
        }
    }

    if (deferredCurrentIndex >= 0) {
        if (auto* tab = dynamic_cast<SwTabWidget*>(target)) {
            tab->setCurrentIndex(deferredCurrentIndex);
        } else if (auto* stack = dynamic_cast<SwStackedWidget*>(target)) {
            stack->setCurrentIndex(deferredCurrentIndex);
        } else if (auto* toolbox = dynamic_cast<SwToolBox*>(target)) {
            toolbox->setCurrentIndex(deferredCurrentIndex);
        }
    }

    return true;
}

inline bool UiLoader::loadQtMainWindowCentralWidgetInto_(SwWidget* target,
                                                        const XmlNode& mainWindowNode,
                                                        SwString& outError,
                                                        const SwMap<SwString, SwString>& customWidgetExtends) {
    if (!target) {
        outError = "Null target widget";
        return false;
    }

    for (const auto* childWidgetNode : mainWindowNode.childrenNamed("widget")) {
        if (!childWidgetNode) {
            continue;
        }
        if (childWidgetNode->attr("name") == "centralWidget") {
            return loadIntoExistingWidget_(target, *childWidgetNode, outError, customWidgetExtends);
        }
    }

    outError = "QMainWindow has no centralWidget";
    return false;
}

inline SwMainWindow* UiLoader::loadQtMainWindow_(const XmlNode& mainWindowNode,
                                                const SwMap<SwString, SwString>& customWidgetExtends,
                                                SwString& outError) {
    int x = 0;
    int y = 0;
    int w = 800;
    int h = 600;
    SwString title = "Main Window";

    for (const auto* prop : mainWindowNode.childrenNamed("property")) {
        if (!prop) {
            continue;
        }
        const SwString rawName = prop->attr("name");
        if (rawName == "geometry") {
            const XmlNode* rect = prop->firstChild("rect");
            if (rect) {
                x = toInt_(childText_(*rect, "x"), x);
                y = toInt_(childText_(*rect, "y"), y);
                w = toInt_(childText_(*rect, "width"), w);
                h = toInt_(childText_(*rect, "height"), h);
            }
        } else if (rawName == "windowTitle") {
            const SwString t = textValue_(*prop).trimmed();
            if (!t.isEmpty()) {
                title = t;
            }
        }
    }

    SwMainWindow* win = new SwMainWindow(title.toStdWString(), w, h);

    const SwString objName = mainWindowNode.attr("name");
    if (!objName.isEmpty()) {
        win->setObjectName(objName);
    }

    for (const auto* prop : mainWindowNode.childrenNamed("property")) {
        if (!prop) {
            continue;
        }
        const SwString rawName = prop->attr("name");
        if (rawName.isEmpty()) {
            continue;
        }
        if (rawName == "windowTitle") {
            continue;
        }
        if (rawName == "geometry") {
            win->move(x, y);
            win->resize(w, h);
            continue;
        }
        applyCommonProperty_(win, rawName, *prop);
    }

    if (!loadQtMainWindowCentralWidgetInto_(win->centralWidget(), mainWindowNode, outError, customWidgetExtends)) {
        delete win;
        return nullptr;
    }

    return win;
}

inline UiLoader::LoadResult UiLoader::loadFromString(const SwString& xml, SwWidget* parent) {
    LoadResult out;

    const auto parsed = SwXmlDocument::parse(xml);
    if (!parsed.ok) {
        out.error = parsed.error.isEmpty() ? "XML parse error" : parsed.error;
        return out;
    }

    // Accept either <ui> or <swui> as root.
    const XmlNode* uiRoot = &parsed.root;
    if (uiRoot->name != "ui" && uiRoot->name != "swui") {
        out.error = "Root element must be <ui> or <swui>";
        return out;
    }

    // Expected shape: <ui><widget .../></ui>
    const XmlNode* widgetNode = uiRoot->firstChild("widget");
    if (!widgetNode) {
        out.error = "No <widget> found in document";
        return out;
    }

    const auto customWidgetExtends = parseCustomWidgets_(*uiRoot);

    // Special-case: main window form.
    const SwString rootClass = widgetNode->attr("class");
    if (rootClass == "QMainWindow" || rootClass == "SwMainWindow") {
        if (!parent) {
            SwMainWindow* win = loadQtMainWindow_(*widgetNode, customWidgetExtends, out.error);
            if (!win) {
                if (out.error.isEmpty()) {
                    out.error = "Failed to create QMainWindow/SwMainWindow root";
                }
                return out;
            }
            out.root = win;
            out.ok = true;
            return out;
        }

        // If a parent is provided, embed the main window central widget content into a plain SwWidget.
        SwWidget* embedded = new SwWidget(parent);
        if (!loadQtMainWindowCentralWidgetInto_(embedded, *widgetNode, out.error, customWidgetExtends)) {
            delete embedded;
            if (out.error.isEmpty()) {
                out.error = "Failed to load QMainWindow centralWidget into embedded widget";
            }
            return out;
        }
        out.root = embedded;
        out.ok = true;
        return out;
    }

    SwWidget* createdRoot = loadWidget_(*widgetNode, parent, out.error, customWidgetExtends);
    if (!createdRoot) {
        if (out.error.isEmpty()) {
            out.error = "Failed to create root widget";
        }
        return out;
    }

    out.root = createdRoot;
    out.ok = true;
    return out;
}

inline UiLoader::LoadResult UiLoader::loadFromFile(const SwString& filePath, SwWidget* parent) {
    LoadResult out;
    SwFile f(filePath);
    if (!f.open(SwFile::Read)) {
        out.error = "Failed to open file";
        return out;
    }
    const SwString content = f.readAll();
    f.close();
    return loadFromString(content, parent);
}

} // namespace swui
