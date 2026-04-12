#include "SwCreatorSwuiSerializer.h"

#include "SwUiLoader.h"
#include "SwLayout.h"
#include "SwScrollArea.h"
#include "SwSpacer.h"
#include "SwStackedWidget.h"
#include "SwTabWidget.h"
#include "SwToolBox.h"
#include "SwWidget.h"
#include "core/types/SwXmlDocument.h"
#include "designer/SwCreatorFormCanvas.h"

#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
const SwString kSwCreatorDesignMarker = "__SwCreator_DesignWidget";

std::string xmlEscape(std::string s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
        case '&': out.append("&amp;"); break;
        case '<': out.append("&lt;"); break;
        case '>': out.append("&gt;"); break;
        case '"': out.append("&quot;"); break;
        case '\'': out.append("&apos;"); break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

std::string tagForValue(const SwAny& v) {
    switch (v.metaType()) {
    case SwMetaType::Bool:
        return "bool";
    case SwMetaType::Int:
    case SwMetaType::UInt:
        return "number";
    default:
        return "string";
    }
}

bool isExcludedInternalWidget_(const SwWidget* widget) {
    if (!widget) {
        return false;
    }

    // SwScrollArea has internal children (viewport, scrollbars). Only the content widget should be serialized.
    if (auto* parentScroll = dynamic_cast<const SwScrollArea*>(widget->parent())) {
        return parentScroll->widget() != widget;
    }

    return false;
}

bool isDesignMarkerSet_(SwWidget* widget) {
    if (!widget) {
        return false;
    }
    if (!widget->isDynamicProperty(kSwCreatorDesignMarker)) {
        return false;
    }
    const SwAny v = widget->property(kSwCreatorDesignMarker);
    if (v.metaType() == SwMetaType::Bool) {
        return v.toString().toLower() == "true";
    }
    if (v.metaType() == SwMetaType::Int || v.metaType() == SwMetaType::UInt) {
        return v.toString() != "0";
    }
    const SwString s = v.toString().toLower();
    return s == "true" || s == "1" || s == "yes";
}

static bool startsWithSwCreatorInternal_(const SwString& propName) {
    return propName.startsWith("__SwCreator_");
}

void emitIndent_(std::ostringstream& oss, int spaces) {
    for (int i = 0; i < spaces; ++i) {
        oss << ' ';
    }
}

void emitGeometry_(std::ostringstream& oss, const SwRect& r, int indent) {
    emitIndent_(oss, indent);
    oss << "<property name=\"geometry\"><rect>";
    oss << "<x>" << r.x << "</x>";
    oss << "<y>" << r.y << "</y>";
    oss << "<width>" << r.width << "</width>";
    oss << "<height>" << r.height << "</height>";
    oss << "</rect></property>\n";
}

void emitProperty_(std::ostringstream& oss,
                   const SwString& name,
                   const SwAny& value,
                   int indent) {
    const std::string propName = xmlEscape(name.toStdString());
    const std::string tag = tagForValue(value);
    const std::string val = xmlEscape(value.toString().toStdString());
    emitIndent_(oss, indent);
    oss << "<property name=\"" << propName << "\">";
    oss << "<" << tag << ">" << val << "</" << tag << ">";
    oss << "</property>\n";
}

void emitAttribute_(std::ostringstream& oss,
                    const SwString& name,
                    const SwString& value,
                    int indent) {
    emitIndent_(oss, indent);
    oss << "<attribute name=\"" << xmlEscape(name.toStdString()) << "\">";
    oss << "<string>" << xmlEscape(value.toStdString()) << "</string>";
    oss << "</attribute>\n";
}

bool isSpacerWidget_(const SwWidget* widget) {
    if (!widget) {
        return false;
    }
    if (dynamic_cast<const SwSpacer*>(widget)) {
        return true;
    }
    if (!widget->isDynamicProperty("__SwCreator_IsSpacer")) {
        return false;
    }
    const SwAny value = const_cast<SwWidget*>(widget)->property("__SwCreator_IsSpacer");
    const SwString text = value.toString().trimmed().toLower();
    return text == "true" || text == "1" || text == "yes";
}

SwSizePolicy::Policy spacerPolicyFromString_(SwString value) {
    value = value.trimmed();
    const size_t sep = value.lastIndexOf(':');
    if (sep != static_cast<size_t>(-1)) {
        value = value.mid(static_cast<int>(sep + 1));
    }
    return SwSpacer::policyFromString(value);
}

struct SpacerInfo {
    SwString name;
    SwString orientation{"Qt::Horizontal"};
    SwSizePolicy::Policy horizontalPolicy{SwSizePolicy::Minimum};
    SwSizePolicy::Policy verticalPolicy{SwSizePolicy::Minimum};
    int width{40};
    int height{20};
};

SpacerInfo spacerInfoFromWidget_(const SwWidget* widget) {
    SpacerInfo info;
    if (!widget) {
        return info;
    }
    info.name = widget->getObjectName();
    info.width = std::max(0, widget->width());
    info.height = std::max(0, widget->height());

    if (const auto* spacer = dynamic_cast<const SwSpacer*>(widget)) {
        info.orientation = spacer->direction() == SwSpacer::Direction::Vertical ? SwString("Qt::Vertical") : SwString("Qt::Horizontal");
        const SwSizePolicy policy = spacer->sizePolicy();
        info.horizontalPolicy = policy.horizontalPolicy();
        info.verticalPolicy = policy.verticalPolicy();
        const SwSize hint = spacer->sizeHint();
        info.width = std::max(0, hint.width);
        info.height = std::max(0, hint.height);
        return info;
    }

    const SwAny orientation = const_cast<SwWidget*>(widget)->property("Orientation");
    if (!orientation.toString().isEmpty()) {
        info.orientation = orientation.toString().trimmed().toLower() == "vertical" ? SwString("Qt::Vertical") : SwString("Qt::Horizontal");
    }

    const SwAny horizontalPolicy = const_cast<SwWidget*>(widget)->property("HorizontalPolicy");
    const SwAny verticalPolicy = const_cast<SwWidget*>(widget)->property("VerticalPolicy");
    info.horizontalPolicy = spacerPolicyFromString_(horizontalPolicy.toString());
    info.verticalPolicy = spacerPolicyFromString_(verticalPolicy.toString());

    const SwAny width = const_cast<SwWidget*>(widget)->property("SizeHintWidth");
    const SwAny height = const_cast<SwWidget*>(widget)->property("SizeHintHeight");
    if (!width.toString().isEmpty()) {
        info.width = std::max(0, width.toString().toInt());
    }
    if (!height.toString().isEmpty()) {
        info.height = std::max(0, height.toString().toInt());
    }
    return info;
}

SpacerInfo spacerInfoFromNode_(const SwXmlNode& spacerNode) {
    SpacerInfo info;
    info.name = spacerNode.attr("name");

    bool explicitHorizontalPolicy = false;
    bool explicitVerticalPolicy = false;
    bool hasSizeType = false;
    SwSizePolicy::Policy sizeTypePolicy = SwSizePolicy::Minimum;

    for (const auto* prop : spacerNode.childrenNamed("property")) {
        if (!prop) {
            continue;
        }
        const SwString name = prop->attr("name");
        if (name == "orientation") {
            const SwXmlNode* valueNode = prop->firstChild("enum");
            if (valueNode) {
                info.orientation = valueNode->text.trimmed();
            }
        } else if (name == "sizeHint") {
            const SwXmlNode* size = prop->firstChild("size");
            if (!size) {
                continue;
            }
            const SwXmlNode* width = size->firstChild("width");
            const SwXmlNode* height = size->firstChild("height");
            if (width) {
                info.width = std::max(0, width->text.toInt());
            }
            if (height) {
                info.height = std::max(0, height->text.toInt());
            }
        } else if (name == "sizeType") {
            const SwXmlNode* valueNode = prop->firstChild("enum");
            if (valueNode) {
                hasSizeType = true;
                sizeTypePolicy = spacerPolicyFromString_(valueNode->text.trimmed());
            }
        } else if (name == "horizontalSizeType") {
            const SwXmlNode* valueNode = prop->firstChild("enum");
            if (valueNode) {
                explicitHorizontalPolicy = true;
                info.horizontalPolicy = spacerPolicyFromString_(valueNode->text.trimmed());
            }
        } else if (name == "verticalSizeType") {
            const SwXmlNode* valueNode = prop->firstChild("enum");
            if (valueNode) {
                explicitVerticalPolicy = true;
                info.verticalPolicy = spacerPolicyFromString_(valueNode->text.trimmed());
            }
        }
    }

    const bool vertical = info.orientation.trimmed().toLower().contains("vertical");
    if (hasSizeType) {
        if (vertical) {
            if (!explicitVerticalPolicy) {
                info.verticalPolicy = sizeTypePolicy;
            }
            if (!explicitHorizontalPolicy) {
                info.horizontalPolicy = SwSizePolicy::Minimum;
            }
        } else {
            if (!explicitHorizontalPolicy) {
                info.horizontalPolicy = sizeTypePolicy;
            }
            if (!explicitVerticalPolicy) {
                info.verticalPolicy = SwSizePolicy::Minimum;
            }
        }
    }
    return info;
}

void emitSpacerNode_(std::ostringstream& oss, const SpacerInfo& info, int indent) {
    emitIndent_(oss, indent);
    oss << "<spacer";
    if (!info.name.isEmpty()) {
        oss << " name=\"" << xmlEscape(info.name.toStdString()) << "\"";
    }
    oss << ">\n";

    emitIndent_(oss, indent + 2);
    oss << "<property name=\"orientation\"><enum>" << xmlEscape(info.orientation.toStdString()) << "</enum></property>\n";

    const bool vertical = info.orientation.trimmed().toLower().contains("vertical");
    const SwSizePolicy::Policy primaryPolicy = vertical ? info.verticalPolicy : info.horizontalPolicy;
    emitIndent_(oss, indent + 2);
    oss << "<property name=\"sizeType\"><enum>QSizePolicy::"
        << xmlEscape(SwSpacer::policyToString(primaryPolicy).toStdString())
        << "</enum></property>\n";

    emitIndent_(oss, indent + 2);
    oss << "<property name=\"horizontalSizeType\"><enum>QSizePolicy::"
        << xmlEscape(SwSpacer::policyToString(info.horizontalPolicy).toStdString())
        << "</enum></property>\n";

    emitIndent_(oss, indent + 2);
    oss << "<property name=\"verticalSizeType\"><enum>QSizePolicy::"
        << xmlEscape(SwSpacer::policyToString(info.verticalPolicy).toStdString())
        << "</enum></property>\n";

    emitIndent_(oss, indent + 2);
    oss << "<property name=\"sizeHint\"><size><width>" << info.width << "</width><height>" << info.height
        << "</height></size></property>\n";

    emitIndent_(oss, indent);
    oss << "</spacer>\n";
}

void emitDesignerSpacerWidget_(std::ostringstream& oss, const SpacerInfo& info, int indent) {
    emitIndent_(oss, indent);
    oss << "<widget class=\"SwSpacer\"";
    if (!info.name.isEmpty()) {
        oss << " name=\"" << xmlEscape(info.name.toStdString()) << "\"";
    }
    oss << ">\n";

    emitGeometry_(oss, SwRect{0, 0, info.width, info.height}, indent + 2);
    emitProperty_(oss, kSwCreatorDesignMarker, SwAny(true), indent + 2);
    emitProperty_(oss,
                  "Orientation",
                  SwAny(info.orientation.trimmed().toLower().contains("vertical") ? SwString("Vertical") : SwString("Horizontal")),
                  indent + 2);
    emitProperty_(oss, "HorizontalPolicy", SwAny(SwSpacer::policyToString(info.horizontalPolicy)), indent + 2);
    emitProperty_(oss, "VerticalPolicy", SwAny(SwSpacer::policyToString(info.verticalPolicy)), indent + 2);
    emitProperty_(oss, "SizeHintWidth", SwAny(info.width), indent + 2);
    emitProperty_(oss, "SizeHintHeight", SwAny(info.height), indent + 2);

    emitIndent_(oss, indent);
    oss << "</widget>\n";
}

void emitXmlNodeRecursive_(std::ostringstream& oss, const SwXmlNode& node, int indent) {
    if (node.name == "spacer") {
        emitDesignerSpacerWidget_(oss, spacerInfoFromNode_(node), indent);
        return;
    }

    emitIndent_(oss, indent);
    oss << "<" << node.name.toStdString();
    for (const auto& attr : node.attributes) {
        oss << " " << attr.first.toStdString() << "=\"" << xmlEscape(attr.second.toStdString()) << "\"";
    }

    const bool hasChildren = !node.children.isEmpty();
    const SwString text = node.text;
    if (!hasChildren && text.isEmpty()) {
        oss << "/>\n";
        return;
    }

    oss << ">";
    if (!hasChildren) {
        oss << xmlEscape(text.toStdString()) << "</" << node.name.toStdString() << ">\n";
        return;
    }

    oss << "\n";
    for (const SwXmlNode& child : node.children) {
        emitXmlNodeRecursive_(oss, child, indent + 2);
    }
    emitIndent_(oss, indent);
    oss << "</" << node.name.toStdString() << ">\n";
}

SwString materializeCanvasSpacers_(const SwString& xml, SwString* outError) {
    const SwXmlDocument::ParseResult parsed = SwXmlDocument::parse(xml);
    if (!parsed.ok) {
        if (outError) {
            *outError = parsed.error.isEmpty() ? SwString("Invalid XML") : parsed.error;
        }
        return SwString();
    }

    std::ostringstream oss;
    emitXmlNodeRecursive_(oss, parsed.root, 0);
    return SwString(oss.str());
}
} // namespace

SwString SwCreatorSwuiSerializer::serializeWidget(const SwWidget* widget) {
    if (!widget) {
        return SwString();
    }

    const SwString cls = widget->className();
    const SwString name = widget->getObjectName();
    const SwRect r = widget->frameGeometry();

    std::ostringstream oss;
    oss << "<swui>\n";
    oss << "  <widget class=\"" << xmlEscape(cls.toStdString()) << "\"";
    if (!name.isEmpty()) {
        oss << " name=\"" << xmlEscape(name.toStdString()) << "\"";
    }
    oss << ">\n";

    oss << "    <property name=\"geometry\"><rect>";
    oss << "<x>" << r.x << "</x>";
    oss << "<y>" << r.y << "</y>";
    oss << "<width>" << r.width << "</width>";
    oss << "<height>" << r.height << "</height>";
    oss << "</rect></property>\n";

    const auto props = widget->propertyNames();
    for (const SwString& prop : props) {
        if (prop.isEmpty()) {
            continue;
        }
        if (prop == "ObjectName" || prop == "Focus" || prop == "Hover") {
            continue;
        }

        SwAny v = const_cast<SwWidget*>(widget)->property(prop);
        SwString s = v.toString();
        if (s.isEmpty() && v.metaType() == SwMetaType::UnknownType) {
            // Skip non-string-serializable types for now.
            continue;
        }

        const std::string tag = tagForValue(v);
        oss << "    <property name=\"" << xmlEscape(prop.toStdString()) << "\">";
        oss << "<" << tag << ">" << xmlEscape(s.toStdString()) << "</" << tag << ">";
        oss << "</property>\n";
    }

    oss << "  </widget>\n";
    oss << "</swui>\n";
    return SwString(oss.str());
}

SwWidget* SwCreatorSwuiSerializer::deserializeWidget(const SwString& xml, SwWidget* parent, SwString* outError) {
    if (xml.isEmpty()) {
        if (outError) {
            *outError = SwString("Empty xml");
        }
        return nullptr;
    }

    swui::UiLoader::LoadResult res = swui::UiLoader::loadFromString(xml, parent);
    if (!res.ok || !res.root) {
        if (outError) {
            *outError = res.error;
        }
        return nullptr;
    }

    return res.root;
}

SwString SwCreatorSwuiSerializer::serializeCanvas(const SwCreatorFormCanvas* canvas) {
    if (!canvas) {
        return SwString();
    }

    const std::vector<SwWidget*>& designWidgets = canvas->designWidgets();
    std::unordered_set<const SwWidget*> designSet;
    designSet.reserve(designWidgets.size());
    for (SwWidget* w : designWidgets) {
        if (w) {
            designSet.insert(w);
        }
    }

    // Doc nodes = design widgets + required container/page widgets between them and the canvas.
    std::unordered_set<const SwWidget*> docSet = designSet;

    auto includeDocNode = [&](const SwWidget* w) {
        if (!w || w == canvas) {
            return;
        }
        if (isExcludedInternalWidget_(w)) {
            return;
        }
        docSet.insert(w);
    };

    // Include non-design ancestors (ex: tab pages, scroll contents) so structure can be reconstructed.
    for (SwWidget* w : designWidgets) {
        if (!w) {
            continue;
        }
        for (SwObject* p = w->parent(); p; p = p->parent()) {
            if (p == canvas) {
                break;
            }
            auto* pw = dynamic_cast<SwWidget*>(p);
            if (!pw) {
                continue;
            }
            if (designSet.find(pw) != designSet.end()) {
                break;
            }
            includeDocNode(pw);
        }
    }

    // Include "container content widgets" even if they are empty.
    for (SwWidget* w : designWidgets) {
        if (!w) {
            continue;
        }

        if (auto* scroll = dynamic_cast<const SwScrollArea*>(w)) {
            includeDocNode(scroll->widget());
        } else if (auto* tab = dynamic_cast<const SwTabWidget*>(w)) {
            for (SwObject* childObj : tab->children()) {
                auto* page = dynamic_cast<SwWidget*>(childObj);
                if (!page) {
                    continue;
                }
                includeDocNode(page);
            }
        } else if (auto* stack = dynamic_cast<const SwStackedWidget*>(w)) {
            for (SwObject* childObj : stack->children()) {
                auto* page = dynamic_cast<SwWidget*>(childObj);
                if (!page || page->parent() != stack) {
                    continue;
                }
                includeDocNode(page);
            }
        } else if (auto* toolbox = dynamic_cast<const SwToolBox*>(w)) {
            for (SwObject* childObj : toolbox->children()) {
                auto* page = dynamic_cast<SwWidget*>(childObj);
                if (!page || page->parent() != toolbox) {
                    continue;
                }
                // ToolBox headers are internal SwToolButtons and do not come from the designer palette.
                if (dynamic_cast<SwToolButton*>(page)) {
                    continue;
                }
                includeDocNode(page);
            }
        }
    }

    auto logicalParentOf = [&](const SwWidget* w) -> const SwWidget* {
        if (!w) {
            return nullptr;
        }
        for (SwObject* p = w->parent(); p; p = p->parent()) {
            if (p == canvas || !p) {
                return nullptr;
            }
            auto* pw = dynamic_cast<SwWidget*>(p);
            if (!pw) {
                continue;
            }
            if (isExcludedInternalWidget_(pw)) {
                continue;
            }
            if (docSet.find(pw) != docSet.end()) {
                return pw;
            }
        }
        return nullptr;
    };

    // Build the doc tree in a deterministic order based on widget hierarchy traversal.
    std::unordered_set<const SwWidget*> added;
    std::unordered_map<const SwWidget*, std::vector<const SwWidget*>> children;

    std::function<void(const SwWidget*)> visit = [&](const SwWidget* root) {
        if (!root) {
            return;
        }
        for (SwObject* objChild : root->children()) {
            auto* child = dynamic_cast<const SwWidget*>(objChild);
            if (!child) {
                continue;
            }

            if (docSet.find(child) != docSet.end()) {
                const SwWidget* lp = logicalParentOf(child);
                if (added.insert(child).second) {
                    children[lp].push_back(child);
                }
            }

            visit(child);
        }
    };
    visit(canvas);

    auto defaultPageLabel = [](int index) -> SwString {
        return SwString("Page %1").arg(SwString::number(index + 1));
    };

    auto layoutClassOf = [](const SwAbstractLayout* layout) -> const char* {
        if (!layout) {
            return nullptr;
        }
        if (dynamic_cast<const SwVerticalLayout*>(layout)) {
            return "SwVerticalLayout";
        }
        if (dynamic_cast<const SwHorizontalLayout*>(layout)) {
            return "SwHorizontalLayout";
        }
        if (dynamic_cast<const SwGridLayout*>(layout)) {
            return "SwGridLayout";
        }
        if (dynamic_cast<const SwFormLayout*>(layout)) {
            return "SwFormLayout";
        }
        return nullptr;
    };

    std::ostringstream oss;
    oss << "<swui>\n";

    // Synthetic root is a temporary local container inside the canvas.
    const SwRect canvasRect = canvas->rect();
    oss << "  <widget class=\"SwWidget\" name=\"Form\">\n";
    emitGeometry_(oss, canvasRect, 4);
    emitProperty_(oss,
                  "StyleSheet",
                  SwAny(SwString("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }")),
                  4);

    std::function<void(const SwWidget*, const SwWidget*, int, int)> emitNode;
    emitNode = [&](const SwWidget* w, const SwWidget* parent, int indexInParent, int indent) {
        if (!w) {
            return;
        }

        if (isSpacerWidget_(w) && parent && parent->layout()) {
            emitSpacerNode_(oss, spacerInfoFromWidget_(w), indent);
            return;
        }

        const SwString cls = w->className();
        const SwString name = w->getObjectName();

        emitIndent_(oss, indent);
        oss << "<widget class=\"" << xmlEscape(cls.toStdString()) << "\"";
        if (!name.isEmpty()) {
            oss << " name=\"" << xmlEscape(name.toStdString()) << "\"";
        }
        oss << ">\n";

        // Container page labels.
        if (parent) {
            if (dynamic_cast<const SwTabWidget*>(parent)) {
                SwString label = !name.isEmpty() ? name : defaultPageLabel(indexInParent);
                emitAttribute_(oss, "title", label, indent + 2);
            } else if (dynamic_cast<const SwToolBox*>(parent)) {
                SwString label = !name.isEmpty() ? name : defaultPageLabel(indexInParent);
                emitAttribute_(oss, "label", label, indent + 2);
            }
        }

        emitGeometry_(oss, w->frameGeometry(), indent + 2);

        // Persist designer marker only for design widgets (used on load to re-register).
        if (designSet.find(w) != designSet.end()) {
            emitProperty_(oss, kSwCreatorDesignMarker, SwAny(true), indent + 2);
        }

        // Helpful state for common containers ("currentIndex").
        if (auto* tab = dynamic_cast<const SwTabWidget*>(w)) {
            emitProperty_(oss, "currentIndex", SwAny(tab->currentIndex()), indent + 2);
        } else if (auto* stack = dynamic_cast<const SwStackedWidget*>(w)) {
            emitProperty_(oss, "currentIndex", SwAny(stack->currentIndex()), indent + 2);
        } else if (auto* toolbox = dynamic_cast<const SwToolBox*>(w)) {
            emitProperty_(oss, "currentIndex", SwAny(toolbox->currentIndex()), indent + 2);
        }

        // Other serializable properties.
        const auto props = w->propertyNames();
        for (const SwString& prop : props) {
            if (prop.isEmpty()) {
                continue;
            }
            if (prop == "ObjectName" || prop == "Focus" || prop == "Hover") {
                continue;
            }
            if (prop == kSwCreatorDesignMarker || startsWithSwCreatorInternal_(prop)) {
                continue;
            }

            SwAny v = const_cast<SwWidget*>(w)->property(prop);
            SwString s = v.toString();
            if (s.isEmpty() && v.metaType() == SwMetaType::UnknownType) {
                continue;
            }

            emitProperty_(oss, prop, v, indent + 2);
        }

        const SwAbstractLayout* layout = w->layout();
        const char* layoutClass = layoutClassOf(layout);

        const std::vector<const SwWidget*>* kids = nullptr;
        auto it = children.find(w);
        if (it != children.end()) {
            kids = &it->second;
        }

        if (layoutClass) {
            emitIndent_(oss, indent + 2);
            oss << "<layout class=\"" << layoutClass << "\">\n";
            emitProperty_(oss, "spacing", SwAny(layout->spacing()), indent + 4);
            emitProperty_(oss, "margin", SwAny(layout->margin()), indent + 4);

            if (const auto* grid = dynamic_cast<const SwGridLayout*>(layout)) {
                std::vector<const SwGridLayout::Cell*> orderedCells;
                orderedCells.reserve(grid->cells().size());
                for (const auto& cell : grid->cells()) {
                    if (!cell.item || !cell.item->widget()) {
                        continue;
                    }
                    orderedCells.push_back(&cell);
                }
                std::sort(orderedCells.begin(), orderedCells.end(), [](const SwGridLayout::Cell* a, const SwGridLayout::Cell* b) {
                    if (a->row != b->row) {
                        return a->row < b->row;
                    }
                    if (a->column != b->column) {
                        return a->column < b->column;
                    }
                    if (a->rowSpan != b->rowSpan) {
                        return a->rowSpan < b->rowSpan;
                    }
                    return a->columnSpan < b->columnSpan;
                });

                for (size_t i = 0; i < orderedCells.size(); ++i) {
                    const SwGridLayout::Cell& cell = *orderedCells[i];
                    emitIndent_(oss, indent + 4);
                    oss << "<item row=\"" << cell.row << "\" column=\"" << cell.column
                        << "\" rowspan=\"" << cell.rowSpan << "\" colspan=\"" << cell.columnSpan << "\">\n";
                    emitNode(dynamic_cast<const SwWidget*>(cell.item->widget()), w, static_cast<int>(i), indent + 6);
                    emitIndent_(oss, indent + 4);
                    oss << "</item>\n";
                }
            } else {
                const int count = kids ? static_cast<int>(kids->size()) : 0;
                for (int i = 0; i < count; ++i) {
                    emitIndent_(oss, indent + 4);
                    oss << "<item>\n";
                    emitNode((*kids)[static_cast<size_t>(i)], w, i, indent + 6);

                    emitIndent_(oss, indent + 4);
                    oss << "</item>\n";
                }
            }

            emitIndent_(oss, indent + 2);
            oss << "</layout>\n";
        } else if (kids) {
            for (int i = 0; i < static_cast<int>(kids->size()); ++i) {
                emitNode((*kids)[static_cast<size_t>(i)], w, i, indent + 2);
            }
        }

        emitIndent_(oss, indent);
        oss << "</widget>\n";
    };

    auto itTop = children.find(nullptr);
    if (itTop != children.end()) {
        const auto& tops = itTop->second;
        for (int i = 0; i < static_cast<int>(tops.size()); ++i) {
            emitNode(tops[static_cast<size_t>(i)], nullptr, i, 4);
        }
    }

    oss << "  </widget>\n";
    oss << "</swui>\n";
    return SwString(oss.str());
}

bool SwCreatorSwuiSerializer::deserializeCanvas(const SwString& xml, SwCreatorFormCanvas* canvas, SwString* outError) {
    if (!canvas) {
        if (outError) {
            *outError = SwString("Missing canvas");
        }
        return false;
    }

    if (xml.isEmpty()) {
        if (outError) {
            *outError = SwString("Empty xml");
        }
        return false;
    }

    SwString preparedXml = materializeCanvasSpacers_(xml, outError);
    if (preparedXml.isEmpty()) {
        return false;
    }

    swui::UiLoader::LoadResult res = swui::UiLoader::loadFromString(preparedXml, canvas);
    if (!res.ok || !res.root) {
        if (outError) {
            *outError = res.error;
        }
        return false;
    }

    SwWidget* root = res.root;
    // The synthetic root lives inside the canvas, so its coordinates must stay local.
    root->move(0, 0);
    canvas->setFormSize(root->width(), root->height());

    // Flatten synthetic root into the canvas.
    std::vector<SwWidget*> topLevel;
    topLevel.reserve(root->children().size());
    for (SwObject* obj : root->children()) {
        auto* w = dynamic_cast<SwWidget*>(obj);
        if (w && w->parent() == root) {
            topLevel.push_back(w);
        }
    }
    for (SwWidget* w : topLevel) {
        // newParentEvent resets w's absolute position to canvas origin without propagating
        // to w's descendants. We must restore all positions top-down after re-parenting.
        std::vector<std::pair<SwWidget*, SwRect>> descendants;
        std::function<void(SwWidget*)> gatherDesc = [&](SwWidget* widget) {
            for (SwObject* obj : widget->children()) {
                if (auto* child = dynamic_cast<SwWidget*>(obj)) {
                    descendants.push_back({child, child->frameGeometry()});
                    gatherDesc(child);
                }
            }
        };
        gatherDesc(w);

        const SwRect wr = w->geometry();
        w->setParent(canvas);
        w->move(wr.x, wr.y);

        // Restore descendants top-down: each parent is correct before its children are processed.
        for (auto& entry : descendants) {
            SwWidget* desc = entry.first;
            const SwRect& savedRect = entry.second;
            desc->move(savedRect.x, savedRect.y);
        }
    }
    delete root;

    // Re-register design widgets using the marker property.
    std::vector<SwWidget*> toRegister;
    std::function<void(SwWidget*)> collect = [&](SwWidget* parent) {
        if (!parent) {
            return;
        }
        for (SwObject* obj : parent->children()) {
            auto* child = dynamic_cast<SwWidget*>(obj);
            if (!child) {
                continue;
            }
            if (isDesignMarkerSet_(child)) {
                toRegister.push_back(child);
            }
            collect(child);
        }
    };
    collect(canvas);

    for (SwWidget* w : toRegister) {
        canvas->registerDesignWidgetNoLayout(w);
    }

    return true;
}

