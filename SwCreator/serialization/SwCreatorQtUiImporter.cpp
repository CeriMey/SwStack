#include "SwCreatorQtUiImporter.h"
#include "SwCreatorSwuiSerializer.h"

#include "core/types/SwXmlDocument.h"
#include "designer/SwCreatorFormCanvas.h"

#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------------------
// Internal helpers (anonymous namespace)
// ---------------------------------------------------------------------------
namespace {

// ---- XML output helpers ---------------------------------------------------

std::string xmlEscapeImp(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
        case '&':  out.append("&amp;");  break;
        case '<':  out.append("&lt;");   break;
        case '>':  out.append("&gt;");   break;
        case '"':  out.append("&quot;"); break;
        case '\'': out.append("&apos;"); break;
        default:   out.push_back(c);     break;
        }
    }
    return out;
}

void writeIndent(std::ostringstream& oss, int n) {
    for (int i = 0; i < n; ++i) oss << ' ';
}

// ---- Class name mapping ---------------------------------------------------

static const char* mapQtClassToSw(const SwString& qtClass) {
    const std::string cls = qtClass.toStdString();
    if (cls == "QMainWindow")        return "SwWidget";
    if (cls == "QWidget")            return "SwWidget";
    if (cls == "QDialog")            return "SwWidget";
    if (cls == "QFrame")             return "SwFrame";
    if (cls == "QLabel")             return "SwLabel";
    if (cls == "QPushButton")        return "SwPushButton";
    if (cls == "QCommandLinkButton") return "SwPushButton";
    if (cls == "QToolButton")        return "SwToolButton";
    if (cls == "QCheckBox")          return "SwCheckBox";
    if (cls == "QRadioButton")       return "SwRadioButton";
    if (cls == "QComboBox")          return "SwComboBox";
    if (cls == "QLineEdit")          return "SwLineEdit";
    if (cls == "QTextEdit")          return "SwTextEdit";
    if (cls == "QPlainTextEdit")     return "SwPlainTextEdit";
    if (cls == "QSpinBox")           return "SwSpinBox";
    if (cls == "QDoubleSpinBox")     return "SwDoubleSpinBox";
    if (cls == "QSlider")            return "SwSlider";
    if (cls == "QProgressBar")       return "SwProgressBar";
    if (cls == "QGroupBox")          return "SwGroupBox";
    if (cls == "QScrollArea")        return "SwScrollArea";
    if (cls == "QTabWidget")         return "SwTabWidget";
    if (cls == "QStackedWidget")     return "SwStackedWidget";
    if (cls == "QSplitter")          return "SwSplitter";
    if (cls == "QTreeWidget")        return "SwTreeWidget";
    if (cls == "QTableWidget")       return "SwTableWidget";
    if (cls == "QTreeView")          return "SwTreeView";
    if (cls == "QTableView")         return "SwTableView";
    if (cls == "QListWidget")        return "SwWidget";
    if (cls == "QListView")          return "SwWidget";
    if (cls == "QToolBox")           return "SwWidget";
    return "SwWidget"; // unknown → fallback
}

// ---- Property name mapping / filtering -----------------------------------

static std::string mapQtPropertyName(const std::string& qtName) {
    if (qtName == "geometry")         return "geometry";
    if (qtName == "text")             return "text";
    if (qtName == "placeholderText")  return "placeholderText";
    if (qtName == "checked")          return "checked";
    if (qtName == "enabled")          return "enabled";
    if (qtName == "visible")          return "visible";
    if (qtName == "styleSheet")       return "styleSheet";
    if (qtName == "currentIndex")     return "currentIndex";
    if (qtName == "windowTitle")      return "text";
    if (qtName == "title")            return "text";
    // all other Qt properties → skip
    return "";
}

// ---- Geometry struct ------------------------------------------------------

struct Rect { int x{0}, y{0}, w{400}, h{300}; };

Rect parseRectNode(const SwXmlNode& rectNode) {
    Rect r;
    const SwXmlNode* xn = rectNode.firstChild("x");
    const SwXmlNode* yn = rectNode.firstChild("y");
    const SwXmlNode* wn = rectNode.firstChild("width");
    const SwXmlNode* hn = rectNode.firstChild("height");
    if (xn) r.x = xn->text.toInt();
    if (yn) r.y = yn->text.toInt();
    if (wn) r.w = wn->text.toInt();
    if (hn) r.h = hn->text.toInt();
    return r;
}

bool readWidgetGeometry(const SwXmlNode& widgetNode, Rect& out) {
    const SwVector<const SwXmlNode*> props = widgetNode.childrenNamed("property");
    for (const SwXmlNode* p : props) {
        if (p->attr("name") == "geometry") {
            const SwXmlNode* rectNode = p->firstChild("rect");
            if (rectNode) { out = parseRectNode(*rectNode); return true; }
        }
    }
    return false;
}

bool isNodeVisible_(const SwXmlNode& node) {
    const SwVector<const SwXmlNode*> props = node.childrenNamed("property");
    for (const SwXmlNode* p : props) {
        if (p->attr("name") != "visible") {
            continue;
        }

        if (const SwXmlNode* boolNode = p->firstChild("bool")) {
            const SwString value = boolNode->text.trimmed().toLower();
            return value != "false" && value != "0";
        }
        if (const SwXmlNode* numberNode = p->firstChild("number")) {
            return numberNode->text.toInt() != 0;
        }
        if (!p->children.isEmpty()) {
            const SwString value = p->children[0].text.trimmed().toLower();
            return value != "false" && value != "0";
        }
        return true;
    }
    return true;
}

// ---- Property emission ----------------------------------------------------

void emitProperty(std::ostringstream& oss, const std::string& swName,
                  const SwXmlNode& propNode, int indLvl) {
    if (propNode.children.isEmpty()) return;
    const SwXmlNode& valNode = propNode.children[0];
    const std::string valTag = valNode.name.toStdString();

    if (valTag == "rect") {
        Rect r = parseRectNode(valNode);
        writeIndent(oss, indLvl);
        oss << "<property name=\"" << xmlEscapeImp(swName) << "\">"
            << "<rect>"
            << "<x>" << r.x << "</x><y>" << r.y << "</y>"
            << "<width>" << r.w << "</width><height>" << r.h << "</height>"
            << "</rect></property>\n";
        return;
    }

    std::string emitTag = "string";
    if (valTag == "bool")        emitTag = "bool";
    else if (valTag == "number") emitTag = "number";

    writeIndent(oss, indLvl);
    oss << "<property name=\"" << xmlEscapeImp(swName) << "\">"
        << "<" << emitTag << ">" << xmlEscapeImp(valNode.text.toStdString()) << "</" << emitTag << ">"
        << "</property>\n";
}

void emitGeometry(std::ostringstream& oss, const Rect& r, int indLvl) {
    writeIndent(oss, indLvl);
    oss << "<property name=\"geometry\"><rect>"
        << "<x>" << r.x << "</x><y>" << r.y << "</y>"
        << "<width>" << r.w << "</width><height>" << r.h << "</height>"
        << "</rect></property>\n";
}

// ---- Widget size constraints ----------------------------------------------

struct SizeConstraints {
    int minW = 0,       minH = 0;
    int maxW = 16777215, maxH = 16777215;
    bool hasMinW = false, hasMinH = false;
    bool hasMaxW = false, hasMaxH = false;
};

static SizeConstraints readSizeConstraints(const SwXmlNode& widgetNode) {
    SizeConstraints sc;
    const SwVector<const SwXmlNode*> props = widgetNode.childrenNamed("property");
    for (const SwXmlNode* p : props) {
        const std::string name = p->attr("name").toStdString();
        if (name == "minimumSize" || name == "maximumSize") {
            const SwXmlNode* sz = p->firstChild("size");
            if (!sz) continue;
            const SwXmlNode* wn = sz->firstChild("width");
            const SwXmlNode* hn = sz->firstChild("height");
            const int wv = wn ? wn->text.toInt() : 0;
            const int hv = hn ? hn->text.toInt() : 0;
            if (name == "minimumSize") {
                if (wv > 0) { sc.minW = wv; sc.hasMinW = true; }
                if (hv > 0) { sc.minH = hv; sc.hasMinH = true; }
            } else {
                if (wv > 0 && wv < 16777215) { sc.maxW = wv; sc.hasMaxW = true; }
                if (hv > 0 && hv < 16777215) { sc.maxH = hv; sc.hasMaxH = true; }
            }
        }
    }
    return sc;
}

// ---- Class-based preferred dimensions ------------------------------------
// Returns 0 = "flexible / unknown"
static int classPreferredH(const std::string& cls) {
    if (cls == "QLabel")             return 22;
    if (cls == "QLineEdit")          return 28;
    if (cls == "QPushButton")        return 32;
    if (cls == "QToolButton")        return 28;
    if (cls == "QCheckBox")          return 22;
    if (cls == "QRadioButton")       return 22;
    if (cls == "QComboBox")          return 28;
    if (cls == "QProgressBar")       return 20;
    if (cls == "QSlider")            return 28;
    if (cls == "QSpinBox")           return 28;
    if (cls == "QDoubleSpinBox")     return 28;
    if (cls == "QCommandLinkButton") return 50;
    return 0;
}
static int classPreferredW(const std::string& cls) {
    if (cls == "QLabel")             return 60;
    if (cls == "QLineEdit")          return 80;
    if (cls == "QPushButton")        return 80;
    if (cls == "QToolButton")        return 40;
    if (cls == "QCheckBox")          return 80;
    if (cls == "QRadioButton")       return 80;
    if (cls == "QComboBox")          return 100;
    if (cls == "QProgressBar")       return 80;
    if (cls == "QSpinBox")           return 70;
    if (cls == "QDoubleSpinBox")     return 80;
    if (cls == "QCommandLinkButton") return 120;
    return 0;
}

static SwString readWidgetText_(const SwXmlNode& widgetNode) {
    const SwVector<const SwXmlNode*> props = widgetNode.childrenNamed("property");
    for (const SwXmlNode* p : props) {
        const SwString name = p->attr("name");
        if (name != "text" && name != "title" && name != "windowTitle") {
            continue;
        }
        if (p->children.isEmpty()) {
            continue;
        }
        return p->children[0].text.trimmed();
    }
    return {};
}

static int estimatedTextWidth_(const SwXmlNode& widgetNode) {
    const std::string cls = widgetNode.attr("class", "").toStdString();
    if (cls != "QLabel" &&
        cls != "QPushButton" &&
        cls != "QToolButton" &&
        cls != "QCheckBox" &&
        cls != "QRadioButton" &&
        cls != "QCommandLinkButton") {
        return 0;
    }

    const SwString text = readWidgetText_(widgetNode);
    if (text.isEmpty()) {
        return 0;
    }

    const int textWidth = static_cast<int>(text.toStdString().size()) * 7;
    int padding = 12;
    if (cls == "QPushButton" || cls == "QToolButton") {
        padding = 24;
    } else if (cls == "QCheckBox" || cls == "QRadioButton") {
        padding = 28;
    } else if (cls == "QCommandLinkButton") {
        padding = 40;
    }
    return textWidth + padding;
}

// ---- Layout props ---------------------------------------------------------

struct LayoutProps {
    int leftMargin   = 9;
    int topMargin    = 9;
    int rightMargin  = 9;
    int bottomMargin = 9;
    int horizontalSpacing = 6;
    int verticalSpacing   = 6;
};

LayoutProps readLayoutProps(const SwXmlNode& layoutNode) {
    LayoutProps p;
    const SwVector<const SwXmlNode*> props = layoutNode.childrenNamed("property");
    for (const SwXmlNode* prop : props) {
        const std::string name = prop->attr("name").toStdString();
        const SwXmlNode* num = prop->firstChild("number");
        if (!num) continue;
        const int val = num->text.toInt();
        if      (name == "leftMargin")   p.leftMargin   = val;
        else if (name == "topMargin")    p.topMargin    = val;
        else if (name == "rightMargin")  p.rightMargin  = val;
        else if (name == "bottomMargin") p.bottomMargin = val;
        else if (name == "spacing")      {
            p.horizontalSpacing = val;
            p.verticalSpacing = val;
        }
        else if (name == "horizontalSpacing") p.horizontalSpacing = val;
        else if (name == "verticalSpacing")   p.verticalSpacing = val;
        else if (name == "margin")       {
            p.leftMargin = p.topMargin = p.rightMargin = p.bottomMargin = val;
        }
    }
    return p;
}

// ---- Layout child info ----------------------------------------------------

struct LayoutChild {
    enum Type { WidgetNode, LayoutNode, SpacerNode } type = WidgetNode;
    const SwXmlNode* node = nullptr;
    int row=0, col=0, rowspan=1, colspan=1;
    int sizeHintW=20, sizeHintH=20;
    SwString orientation{"Qt::Horizontal"};
    SwString sizeType{"QSizePolicy::Minimum"};
    bool hidden=false;
};

struct PreferredSize {
    int w{0};
    int h{0};
};

static PreferredSize estimateLayoutPreferredSize(const SwXmlNode& layoutNode);
static PreferredSize estimateWidgetPreferredSize(const SwXmlNode& widgetNode);
static PreferredSize estimateChildPreferredSize(const LayoutChild& lc);

static bool layoutChildParticipates_(const LayoutChild& lc) {
    return !lc.hidden;
}

static bool widgetUsesMaximumSizeLayoutConstraint_(const SwXmlNode& widgetNode) {
    for (const SwXmlNode& child : widgetNode.children) {
        if (child.name != "layout") {
            continue;
        }
        const SwVector<const SwXmlNode*> props = child.childrenNamed("property");
        for (const SwXmlNode* p : props) {
            if (p->attr("name") != "sizeConstraint") {
                continue;
            }
            const SwXmlNode* enumNode = p->firstChild("enum");
            if (enumNode && enumNode->text.contains("SetMaximumSize")) {
                return true;
            }
        }
    }
    return false;
}

static LayoutChild readSpacerInfo(const SwXmlNode& spacerNode) {
    LayoutChild lc;
    lc.type = LayoutChild::SpacerNode;
    lc.node = &spacerNode;
    lc.sizeHintW = 20;
    lc.sizeHintH = 20;
    bool hasSizeType = false;
    const SwVector<const SwXmlNode*> props = spacerNode.childrenNamed("property");
    for (const SwXmlNode* p : props) {
        const SwString name = p->attr("name");
        if (name == "sizeHint") {
            const SwXmlNode* sz = p->firstChild("size");
            if (sz) {
                const SwXmlNode* w = sz->firstChild("width");
                const SwXmlNode* h = sz->firstChild("height");
                if (w) lc.sizeHintW = std::max(0, w->text.toInt());
                if (h) lc.sizeHintH = std::max(0, h->text.toInt());
            }
        } else if (name == "orientation") {
            const SwXmlNode* e = p->firstChild("enum");
            if (e && !e->text.isEmpty()) {
                lc.orientation = e->text.trimmed();
            }
        } else if (name == "sizeType") {
            const SwXmlNode* e = p->firstChild("enum");
            if (e && !e->text.isEmpty()) {
                hasSizeType = true;
                lc.sizeType = e->text.trimmed();
            }
        }
    }
    if (!hasSizeType) {
        lc.sizeType = SwString("QSizePolicy::Expanding");
    }
    return lc;
}

// Collect <item> children from a <layout> node into flat list.
// Preferred fixed height for a LayoutChild in a VBox (0 = flexible)
static int preferredH(const LayoutChild& lc) {
    return estimateChildPreferredSize(lc).h;
}

// Preferred fixed width for a LayoutChild in a HBox (0 = flexible)
static int preferredW(const LayoutChild& lc) {
    return estimateChildPreferredSize(lc).w;
}

static SwString normalizedSpacerPolicy_(SwString value) {
    value = value.trimmed();
    const size_t sep = value.lastIndexOf(':');
    if (sep != static_cast<size_t>(-1)) {
        value = value.mid(static_cast<int>(sep + 1));
    }
    return value.trimmed();
}

static bool spacerCanExpand_(const LayoutChild& lc) {
    if (lc.type != LayoutChild::SpacerNode) {
        return false;
    }
    const SwString policy = normalizedSpacerPolicy_(lc.sizeType);
    return policy == "Expanding" || policy == "MinimumExpanding" || policy == "Ignored";
}

static bool widgetIsFlexibleHorizontally_(const LayoutChild& lc) {
    if (lc.type != LayoutChild::WidgetNode || !lc.node) {
        return false;
    }

    const SizeConstraints sc = readSizeConstraints(*lc.node);
    if (sc.hasMinW && sc.hasMaxW && sc.minW == sc.maxW) {
        return false;
    }
    if (sc.hasMaxW && sc.maxW > 0 && sc.maxW < 400) {
        return false;
    }

    const std::string cls = lc.node->attr("class", "").toStdString();
    return cls == "QLineEdit" ||
           cls == "QTextEdit" ||
           cls == "QPlainTextEdit" ||
           cls == "QTreeWidget" ||
           cls == "QListWidget" ||
           cls == "QTableWidget" ||
           cls == "QTreeView" ||
           cls == "QListView" ||
           cls == "QTableView";
}

static void shrinkSegmentsToFit_(std::vector<int>& sizes,
                                 const std::vector<int>& minSizes,
                                 const std::vector<int>& activeIndices,
                                 int availableTotal) {
    if (availableTotal <= 0 || activeIndices.empty()) {
        return;
    }

    int total = 0;
    for (int idx : activeIndices) {
        total += std::max(0, sizes[static_cast<size_t>(idx)]);
    }
    if (total <= availableTotal) {
        return;
    }

    int remaining = total - availableTotal;
    while (remaining > 0) {
        int totalSlack = 0;
        for (int idx : activeIndices) {
            totalSlack += std::max(0, sizes[static_cast<size_t>(idx)] - minSizes[static_cast<size_t>(idx)]);
        }
        if (totalSlack <= 0) {
            break;
        }

        int reduced = 0;
        for (int idx : activeIndices) {
            const int slack = std::max(0, sizes[static_cast<size_t>(idx)] - minSizes[static_cast<size_t>(idx)]);
            if (slack <= 0) {
                continue;
            }

            int shrink = std::max(1, (remaining * slack) / totalSlack);
            shrink = std::min(shrink, slack);
            shrink = std::min(shrink, remaining - reduced);
            if (shrink <= 0) {
                continue;
            }

            sizes[static_cast<size_t>(idx)] -= shrink;
            reduced += shrink;
            if (reduced >= remaining) {
                break;
            }
        }

        if (reduced <= 0) {
            break;
        }
        remaining -= reduced;
    }
}

static int resolvedHBoxChildHeight(const LayoutChild& lc, int availableHeight) {
    if ((lc.type != LayoutChild::WidgetNode && lc.type != LayoutChild::LayoutNode) || !lc.node) {
        return std::max(1, availableHeight);
    }

    const SizeConstraints sc = lc.type == LayoutChild::WidgetNode ? readSizeConstraints(*lc.node) : SizeConstraints{};
    int height = availableHeight;
    const int preferred = estimateChildPreferredSize(lc).h;

    if (sc.hasMinH && sc.hasMaxH && sc.minH == sc.maxH) {
        height = sc.minH;
    } else if (sc.hasMaxH && sc.maxH < height) {
        height = sc.maxH;
    } else if (preferred > 0) {
        height = std::min(height, preferred);
    }

    if (sc.hasMinH) {
        height = std::max(height, sc.minH);
    }
    if (sc.hasMaxH) {
        height = std::min(height, sc.maxH);
    }

    return std::max(1, height);
}

static int resolvedVBoxChildWidth(const LayoutChild& lc, int availableWidth) {
    if ((lc.type != LayoutChild::WidgetNode && lc.type != LayoutChild::LayoutNode) || !lc.node) {
        return std::max(1, availableWidth);
    }

    const SizeConstraints sc = lc.type == LayoutChild::WidgetNode ? readSizeConstraints(*lc.node) : SizeConstraints{};
    int width = availableWidth;
    const int preferred = estimateChildPreferredSize(lc).w;

    if (sc.hasMinW && sc.hasMaxW && sc.minW == sc.maxW) {
        width = sc.minW;
    } else if (sc.hasMaxW && sc.maxW < width) {
        width = sc.maxW;
    } else if (preferred > 0 && sc.hasMaxW) {
        width = std::min(width, std::max(preferred, sc.maxW));
    }

    if (sc.hasMinW) {
        width = std::max(width, sc.minW);
    }
    if (sc.hasMaxW) {
        width = std::min(width, sc.maxW);
    }

    return std::max(1, width);
}

void collectFromLayout(const SwXmlNode& layoutNode, std::vector<LayoutChild>& out) {
    for (const SwXmlNode& item : layoutNode.children) {
        if (item.name != "item") continue;

        const int row     = item.attr("row",     "0").toInt();
        const int col     = item.attr("column",  "0").toInt();
        const int rowspan = item.attr("rowspan", "1").toInt();
        const int colspan = item.attr("colspan", "1").toInt();

        for (const SwXmlNode& child : item.children) {
            LayoutChild lc;
            lc.row = row; lc.col = col;
            lc.rowspan = rowspan; lc.colspan = colspan;

            if (child.name == "widget") {
                lc.type = LayoutChild::WidgetNode;
                lc.node = &child;
                lc.hidden = !isNodeVisible_(child);
                out.push_back(lc);
                break;
            } else if (child.name == "layout") {
                lc.type = LayoutChild::LayoutNode;
                lc.node = &child;
                out.push_back(lc);
                break;
            } else if (child.name == "spacer") {
                lc = readSpacerInfo(child);
                lc.row = row; lc.col = col;
                lc.rowspan = rowspan; lc.colspan = colspan;
                out.push_back(lc);
                break;
            }
        }
    }
}

static PreferredSize estimateWidgetPreferredSize(const SwXmlNode& widgetNode) {
    if (!isNodeVisible_(widgetNode)) {
        return {};
    }

    PreferredSize size;

    for (const SwXmlNode& child : widgetNode.children) {
        if (child.name == "layout") {
            size = estimateLayoutPreferredSize(child);
            break;
        }
    }

    const std::string cls = widgetNode.attr("class", "").toStdString();
    if (size.w <= 0) size.w = classPreferredW(cls);
    if (size.h <= 0) size.h = classPreferredH(cls);
    size.w = std::max(size.w, estimatedTextWidth_(widgetNode));

    Rect geom;
    if (readWidgetGeometry(widgetNode, geom)) {
        if (size.w <= 0) size.w = geom.w;
        if (size.h <= 0) size.h = geom.h;
    }

    const SizeConstraints sc = readSizeConstraints(widgetNode);
    if (sc.hasMinW && sc.hasMaxW && sc.minW == sc.maxW) {
        size.w = sc.minW;
    } else {
        if (size.w <= 0 && sc.hasMaxW && sc.maxW < 400) size.w = sc.maxW;
        if (sc.hasMinW) size.w = std::max(size.w, sc.minW);
        if (size.w > 0 && sc.hasMaxW && sc.maxW < 16777215) size.w = std::min(size.w, sc.maxW);
    }

    if (sc.hasMinH && sc.hasMaxH && sc.minH == sc.maxH) {
        size.h = sc.minH;
    } else {
        if (size.h <= 0 && sc.hasMaxH && sc.maxH < 300) size.h = sc.maxH;
        if (sc.hasMinH) size.h = std::max(size.h, sc.minH);
        if (size.h > 0 && sc.hasMaxH && sc.maxH < 16777215) size.h = std::min(size.h, sc.maxH);
    }

    return size;
}

static PreferredSize estimateLayoutPreferredSize(const SwXmlNode& layoutNode) {
    std::vector<LayoutChild> items;
    collectFromLayout(layoutNode, items);
    if (items.empty()) {
        return {};
    }

    const LayoutProps lp = readLayoutProps(layoutNode);
    const std::string lclass = layoutNode.attr("class").toStdString();

    if (lclass == "QVBoxLayout" || lclass == "QFormLayout") {
        int contentW = 0;
        int contentH = 0;
        int visibleCount = 0;
        for (const LayoutChild& lc : items) {
            if (!layoutChildParticipates_(lc)) {
                continue;
            }
            const PreferredSize child = estimateChildPreferredSize(lc);
            contentW = std::max(contentW, child.w);
            contentH += child.h;
            ++visibleCount;
        }
        contentH += lp.verticalSpacing * std::max(0, visibleCount - 1);
        return {contentW + lp.leftMargin + lp.rightMargin, contentH + lp.topMargin + lp.bottomMargin};
    }

    if (lclass == "QHBoxLayout") {
        int contentW = 0;
        int contentH = 0;
        int visibleCount = 0;
        for (const LayoutChild& lc : items) {
            if (!layoutChildParticipates_(lc)) {
                continue;
            }
            const PreferredSize child = estimateChildPreferredSize(lc);
            contentW += child.w;
            contentH = std::max(contentH, child.h);
            ++visibleCount;
        }
        contentW += lp.horizontalSpacing * std::max(0, visibleCount - 1);
        return {contentW + lp.leftMargin + lp.rightMargin, contentH + lp.topMargin + lp.bottomMargin};
    }

    if (lclass == "QGridLayout") {
        int maxRow = 0;
        int maxCol = 0;
        for (const LayoutChild& lc : items) {
            if (!layoutChildParticipates_(lc)) {
                continue;
            }
            maxRow = std::max(maxRow, lc.row + lc.rowspan - 1);
            maxCol = std::max(maxCol, lc.col + lc.colspan - 1);
        }

        const int numRows = maxRow + 1;
        const int numCols = maxCol + 1;
        std::vector<int> rowH(static_cast<size_t>(numRows), 0);
        std::vector<int> colW(static_cast<size_t>(numCols), 0);
        std::vector<bool> rowHasItem(static_cast<size_t>(numRows), false);
        std::vector<bool> colHasItem(static_cast<size_t>(numCols), false);

        for (const LayoutChild& lc : items) {
            if (!layoutChildParticipates_(lc)) {
                continue;
            }
            const PreferredSize child = estimateChildPreferredSize(lc);
            const int sharedH = lc.rowspan > 0 ? (std::max(0, child.h) + lc.rowspan - 1) / lc.rowspan : std::max(0, child.h);
            const int sharedW = lc.colspan > 0 ? (std::max(0, child.w) + lc.colspan - 1) / lc.colspan : std::max(0, child.w);

            for (int r = lc.row; r < lc.row + lc.rowspan; ++r) {
                rowHasItem[static_cast<size_t>(r)] = true;
                rowH[static_cast<size_t>(r)] = std::max(rowH[static_cast<size_t>(r)], sharedH);
            }
            for (int c = lc.col; c < lc.col + lc.colspan; ++c) {
                colHasItem[static_cast<size_t>(c)] = true;
                colW[static_cast<size_t>(c)] = std::max(colW[static_cast<size_t>(c)], sharedW);
            }
        }

        int totalH = 0;
        int totalW = 0;
        int activeRows = 0;
        int activeCols = 0;
        for (int r = 0; r < numRows; ++r) {
            if (!rowHasItem[static_cast<size_t>(r)]) continue;
            totalH += rowH[static_cast<size_t>(r)];
            ++activeRows;
        }
        for (int c = 0; c < numCols; ++c) {
            if (!colHasItem[static_cast<size_t>(c)]) continue;
            totalW += colW[static_cast<size_t>(c)];
            ++activeCols;
        }

        totalH += lp.verticalSpacing * std::max(0, activeRows - 1);
        totalW += lp.horizontalSpacing * std::max(0, activeCols - 1);
        return {totalW + lp.leftMargin + lp.rightMargin, totalH + lp.topMargin + lp.bottomMargin};
    }

    return {};
}

static PreferredSize estimateChildPreferredSize(const LayoutChild& lc) {
    if (lc.type == LayoutChild::SpacerNode) {
        return {lc.sizeHintW, lc.sizeHintH};
    }
    if (!lc.node) {
        return {};
    }
    if (lc.type == LayoutChild::LayoutNode) {
        return estimateLayoutPreferredSize(*lc.node);
    }
    return estimateWidgetPreferredSize(*lc.node);
}

// ---- Rect computation for layout items ------------------------------------
// parentRect is in the widget's local coordinate space (origin can be non-zero for nested layouts).

void computeChildRects(const SwXmlNode& layoutNode,
                       const std::vector<LayoutChild>& items,
                       const Rect& parentRect,
                       std::vector<Rect>& outRects) {
    outRects.resize(items.size());
    if (items.empty()) return;

    const LayoutProps lp = readLayoutProps(layoutNode);
    const std::string lclass = layoutNode.attr("class").toStdString();
    const int n = static_cast<int>(items.size());

    const int innerX = parentRect.x + lp.leftMargin;
    const int innerY = parentRect.y + lp.topMargin;
    const int innerW = std::max(1, parentRect.w - lp.leftMargin - lp.rightMargin);
    const int innerH = std::max(1, parentRect.h - lp.topMargin - lp.bottomMargin);

    std::vector<int> activeIndices;
    activeIndices.reserve(items.size());
    for (int i = 0; i < n; ++i) {
        if (layoutChildParticipates_(items[static_cast<size_t>(i)])) {
            activeIndices.push_back(i);
        }
    }
    if (activeIndices.empty()) {
        return;
    }

    if (lclass == "QVBoxLayout" || lclass == "QFormLayout") {
        // Assign preferred (fixed) heights; flexible items share the remainder.
        std::vector<int> heights(static_cast<size_t>(n), 0);
        int fixedSum = 0; int spacerSum = 0; int flexCnt = 0;
        for (int idx : activeIndices) {
            const LayoutChild& lc = items[static_cast<size_t>(idx)];
            if (lc.type == LayoutChild::SpacerNode) {
                if (spacerCanExpand_(lc)) {
                    heights[static_cast<size_t>(idx)] = 0;
                    ++flexCnt;
                } else {
                    heights[static_cast<size_t>(idx)] = lc.sizeHintH;
                    spacerSum += lc.sizeHintH;
                }
            } else {
                const int ph = preferredH(lc);
                heights[static_cast<size_t>(idx)] = ph; // 0 = flexible
                if (ph > 0) fixedSum += ph; else flexCnt++;
            }
        }
        const int spacingTotal = lp.verticalSpacing * std::max(0, static_cast<int>(activeIndices.size()) - 1);
        const int avail = std::max(0, innerH - fixedSum - spacerSum - spacingTotal);
        const int flexH = flexCnt > 0 ? (avail / flexCnt) : 0;
        int remainder = flexCnt > 0 ? (avail % flexCnt) : 0;
        for (int idx : activeIndices) {
            const LayoutChild& lc = items[static_cast<size_t>(idx)];
            if (heights[static_cast<size_t>(idx)] != 0) {
                continue;
            }
            if (lc.type == LayoutChild::SpacerNode && !spacerCanExpand_(lc)) {
                continue;
            }
            heights[static_cast<size_t>(idx)] = flexH;
            if (remainder > 0) {
                ++heights[static_cast<size_t>(idx)];
                --remainder;
            }
        }

        int curY = innerY;
        for (size_t activePos = 0; activePos < activeIndices.size(); ++activePos) {
            const int idx = activeIndices[activePos];
            const LayoutChild& lc = items[static_cast<size_t>(idx)];
            const int remainingHeight = std::max(0, innerY + innerH - curY);
            const int height = std::max(1, std::min(heights[static_cast<size_t>(idx)], remainingHeight > 0 ? remainingHeight : heights[static_cast<size_t>(idx)]));
            const int width = resolvedVBoxChildWidth(lc, innerW);
            outRects[static_cast<size_t>(idx)] = {innerX, curY, width, height};
            curY += height;
            if (activePos + 1 < activeIndices.size()) {
                curY += lp.verticalSpacing;
            }
        }

    } else if (lclass == "QHBoxLayout") {
        std::vector<int> widths(static_cast<size_t>(n), 0);
        int fixedSum = 0; int spacerSum = 0; int flexCnt = 0;
        for (int idx : activeIndices) {
            const LayoutChild& lc = items[static_cast<size_t>(idx)];
            if (lc.type == LayoutChild::SpacerNode) {
                if (spacerCanExpand_(lc)) {
                    widths[static_cast<size_t>(idx)] = 0;
                    ++flexCnt;
                } else {
                    widths[static_cast<size_t>(idx)] = lc.sizeHintW;
                    spacerSum += lc.sizeHintW;
                }
            } else {
                const int pw = preferredW(lc);
                if (widgetIsFlexibleHorizontally_(lc)) {
                    widths[static_cast<size_t>(idx)] = 0;
                    ++flexCnt;
                } else {
                    widths[static_cast<size_t>(idx)] = pw;
                    if (pw > 0) fixedSum += pw; else flexCnt++;
                }
            }
        }
        const int spacingTotal = lp.horizontalSpacing * std::max(0, static_cast<int>(activeIndices.size()) - 1);
        const int avail = std::max(0, innerW - fixedSum - spacerSum - spacingTotal);
        const int flexW = flexCnt > 0 ? (avail / flexCnt) : 0;
        int remainder = flexCnt > 0 ? (avail % flexCnt) : 0;
        for (int idx : activeIndices) {
            const LayoutChild& lc = items[static_cast<size_t>(idx)];
            if (widths[static_cast<size_t>(idx)] != 0) {
                continue;
            }
            if (lc.type == LayoutChild::SpacerNode && !spacerCanExpand_(lc)) {
                continue;
            }
            widths[static_cast<size_t>(idx)] = flexW;
            if (remainder > 0) {
                ++widths[static_cast<size_t>(idx)];
                --remainder;
            }
        }

        std::vector<int> minWidths(static_cast<size_t>(n), 1);
        for (int idx : activeIndices) {
            const LayoutChild& lc = items[static_cast<size_t>(idx)];
            if (lc.type == LayoutChild::SpacerNode) {
                minWidths[static_cast<size_t>(idx)] = spacerCanExpand_(lc) ? 1 : std::max(1, lc.sizeHintW);
            } else if (lc.type == LayoutChild::WidgetNode && lc.node) {
                const SizeConstraints sc = readSizeConstraints(*lc.node);
                if (sc.hasMinW && sc.hasMaxW && sc.minW == sc.maxW) {
                    minWidths[static_cast<size_t>(idx)] = std::max(1, sc.minW);
                } else if (sc.hasMinW) {
                    minWidths[static_cast<size_t>(idx)] = std::max(1, sc.minW);
                }
            }
        }
        shrinkSegmentsToFit_(widths, minWidths, activeIndices, std::max(1, innerW - spacingTotal));

        int curX = innerX;
        for (size_t activePos = 0; activePos < activeIndices.size(); ++activePos) {
            const int idx = activeIndices[activePos];
            const LayoutChild& lc = items[static_cast<size_t>(idx)];
            const int remainingWidth = std::max(0, innerX + innerW - curX);
            const int width = std::max(1, std::min(widths[static_cast<size_t>(idx)], remainingWidth > 0 ? remainingWidth : widths[static_cast<size_t>(idx)]));
            const int height = resolvedHBoxChildHeight(lc, innerH);
            const int y = innerY + std::max(0, (innerH - height) / 2);
            outRects[static_cast<size_t>(idx)] = {curX, y, width, height};
            curX += width;
            if (activePos + 1 < activeIndices.size()) {
                curX += lp.horizontalSpacing;
            }
        }

    } else if (lclass == "QGridLayout") {
        // Find grid dimensions
        int maxRow = 0, maxCol = 0;
        for (const LayoutChild& lc : items) {
            if (!layoutChildParticipates_(lc)) {
                continue;
            }
            maxRow = std::max(maxRow, lc.row + lc.rowspan - 1);
            maxCol = std::max(maxCol, lc.col + lc.colspan - 1);
        }
        const int numRows = maxRow + 1;
        const int numCols = maxCol + 1;

        // Mark which rows/cols actually have items (empty ones get height=0)
        std::vector<bool> rowHasItem(static_cast<size_t>(numRows), false);
        std::vector<bool> colHasItem(static_cast<size_t>(numCols), false);
        std::vector<bool> rowHasNonSpacer(static_cast<size_t>(numRows), false);
        std::vector<bool> colHasNonSpacer(static_cast<size_t>(numCols), false);
        for (const LayoutChild& lc : items) {
            if (!layoutChildParticipates_(lc)) {
                continue;
            }
            for (int r = lc.row; r < lc.row + lc.rowspan; ++r) {
                rowHasItem[static_cast<size_t>(r)] = true;
                if (lc.type != LayoutChild::SpacerNode) {
                    rowHasNonSpacer[static_cast<size_t>(r)] = true;
                }
            }
            for (int c = lc.col; c < lc.col + lc.colspan; ++c) {
                colHasItem[static_cast<size_t>(c)] = true;
                if (lc.type != LayoutChild::SpacerNode) {
                    colHasNonSpacer[static_cast<size_t>(c)] = true;
                }
            }
        }

        // -1 = empty row/col (no items, contributes 0 space)
        // 0  = flexible (has items but no fixed size)
        // >0 = fixed size
        std::vector<int> rowH(static_cast<size_t>(numRows), -1);
        std::vector<int> colW(static_cast<size_t>(numCols), -1);
        std::vector<int> rowMinH(static_cast<size_t>(numRows), 0);
        std::vector<int> colMinW(static_cast<size_t>(numCols), 0);
        for (int r = 0; r < numRows; ++r)
            if (rowHasItem[static_cast<size_t>(r)]) {
                rowH[static_cast<size_t>(r)] = 0;
                rowMinH[static_cast<size_t>(r)] = 1;
            }
        for (int c = 0; c < numCols; ++c)
            if (colHasItem[static_cast<size_t>(c)]) {
                colW[static_cast<size_t>(c)] = 0;
                colMinW[static_cast<size_t>(c)] = 1;
            }

        // Determine fixed heights/widths from item content
        for (const LayoutChild& lc : items) {
            if (!layoutChildParticipates_(lc)) {
                continue;
            }
            if (lc.rowspan == 1) {
                int fixedH = 0;
                if (lc.type == LayoutChild::SpacerNode) {
                    const bool verticalSpacer = lc.orientation.toLower().contains("vertical");
                    if (verticalSpacer) {
                        if (!spacerCanExpand_(lc)) {
                            fixedH = lc.sizeHintH;
                            rowMinH[static_cast<size_t>(lc.row)] = std::max(rowMinH[static_cast<size_t>(lc.row)], std::max(1, lc.sizeHintH));
                        }
                    } else {
                        fixedH = lc.sizeHintH;
                        rowMinH[static_cast<size_t>(lc.row)] = std::max(rowMinH[static_cast<size_t>(lc.row)], std::max(1, lc.sizeHintH));
                    }
                } else if (lc.type == LayoutChild::WidgetNode && lc.node) {
                    const SizeConstraints sc = readSizeConstraints(*lc.node);
                    if (sc.hasMaxH && sc.maxH < 300)
                        fixedH = sc.maxH;
                    else if (sc.hasMinH && sc.hasMaxH && sc.minH == sc.maxH)
                        fixedH = sc.minH;
                    else if (widgetUsesMaximumSizeLayoutConstraint_(*lc.node))
                        fixedH = estimateChildPreferredSize(lc).h;
                    else
                        fixedH = classPreferredH(lc.node->attr("class","").toStdString());
                }
                // LayoutNode in grid cell → stays flexible (0)
                if (fixedH > 0)
                    rowH[static_cast<size_t>(lc.row)] = std::max(rowH[static_cast<size_t>(lc.row)], fixedH);
            }
            if (lc.colspan == 1) {
                int fixedW = 0;
                if (lc.type == LayoutChild::SpacerNode) {
                    if (lc.orientation.toLower().contains("horizontal")) {
                        if (!spacerCanExpand_(lc)) {
                            fixedW = lc.sizeHintW;
                            colMinW[static_cast<size_t>(lc.col)] = std::max(colMinW[static_cast<size_t>(lc.col)], std::max(1, lc.sizeHintW));
                        }
                    } else if (!colHasNonSpacer[static_cast<size_t>(lc.col)]) {
                        fixedW = lc.sizeHintW;
                        colMinW[static_cast<size_t>(lc.col)] = std::max(colMinW[static_cast<size_t>(lc.col)], std::max(1, lc.sizeHintW));
                    }
                } else if (lc.type == LayoutChild::WidgetNode && lc.node) {
                    const SizeConstraints sc = readSizeConstraints(*lc.node);
                    if (sc.hasMaxW && sc.hasMinW && sc.maxW == sc.minW)
                        fixedW = sc.maxW;
                    else if (sc.hasMaxW && sc.maxW < 400)
                        fixedW = sc.maxW;
                }
                if (fixedW > 0)
                    colW[static_cast<size_t>(lc.col)] = std::max(colW[static_cast<size_t>(lc.col)], fixedW);
            }
        }

        // Distribute remaining space among flexible rows/cols (value == 0, not -1)
        int fixedHSum = 0, flexRowCnt = 0, activeRows = 0;
        int fixedWSum = 0, flexColCnt = 0, activeCols = 0;
        for (int r = 0; r < numRows; ++r) {
            if (rowH[static_cast<size_t>(r)] < 0) continue; // empty row
            activeRows++;
            if (rowH[static_cast<size_t>(r)] > 0) fixedHSum += rowH[static_cast<size_t>(r)]; else flexRowCnt++;
        }
        for (int c = 0; c < numCols; ++c) {
            if (colW[static_cast<size_t>(c)] < 0) continue;
            activeCols++;
            if (colW[static_cast<size_t>(c)] > 0) fixedWSum += colW[static_cast<size_t>(c)]; else flexColCnt++;
        }
        const int spacingH = std::max(0, activeRows - 1) * lp.verticalSpacing;
        const int spacingW = std::max(0, activeCols - 1) * lp.horizontalSpacing;
        const int flexH = flexRowCnt > 0 ? std::max(24, (innerH - fixedHSum - spacingH) / flexRowCnt) : 24;
        const int flexW = flexColCnt > 0 ? std::max(40, (innerW - fixedWSum - spacingW) / flexColCnt) : 40;
        for (int r = 0; r < numRows; ++r)
            if (rowH[static_cast<size_t>(r)] == 0) rowH[static_cast<size_t>(r)] = flexH;
        for (int c = 0; c < numCols; ++c)
            if (colW[static_cast<size_t>(c)] == 0) colW[static_cast<size_t>(c)] = flexW;

        std::vector<int> activeRowIndices;
        std::vector<int> activeColIndices;
        activeRowIndices.reserve(static_cast<size_t>(numRows));
        activeColIndices.reserve(static_cast<size_t>(numCols));
        for (int r = 0; r < numRows; ++r)
            if (rowH[static_cast<size_t>(r)] >= 0) activeRowIndices.push_back(r);
        for (int c = 0; c < numCols; ++c)
            if (colW[static_cast<size_t>(c)] >= 0) activeColIndices.push_back(c);

        shrinkSegmentsToFit_(rowH, rowMinH, activeRowIndices, std::max(1, innerH - spacingH));
        shrinkSegmentsToFit_(colW, colMinW, activeColIndices, std::max(1, innerW - spacingW));

        // Cumulative row/col starts — empty rows contribute 0 height and no spacing
        std::vector<int> rowY(static_cast<size_t>(numRows + 1));
        std::vector<int> colX(static_cast<size_t>(numCols + 1));
        rowY[0] = innerY; colX[0] = innerX;
        for (int r = 0; r < numRows; ++r) {
            const int h   = (rowH[static_cast<size_t>(r)] < 0) ? 0 : rowH[static_cast<size_t>(r)];
            const int sp  = (rowH[static_cast<size_t>(r)] < 0) ? 0 : lp.verticalSpacing;
            rowY[static_cast<size_t>(r + 1)] = rowY[static_cast<size_t>(r)] + h + sp;
        }
        for (int c = 0; c < numCols; ++c) {
            const int w2  = (colW[static_cast<size_t>(c)] < 0) ? 0 : colW[static_cast<size_t>(c)];
            const int sp  = (colW[static_cast<size_t>(c)] < 0) ? 0 : lp.horizontalSpacing;
            colX[static_cast<size_t>(c + 1)] = colX[static_cast<size_t>(c)] + w2 + sp;
        }

        for (int i = 0; i < n; ++i) {
            const LayoutChild& lc = items[static_cast<size_t>(i)];
            if (!layoutChildParticipates_(lc)) {
                continue;
            }
            const int x = colX[static_cast<size_t>(lc.col)];
            const int y = rowY[static_cast<size_t>(lc.row)];
            const int w = colX[static_cast<size_t>(lc.col + lc.colspan)] - x - lp.horizontalSpacing;
            const int h = rowY[static_cast<size_t>(lc.row  + lc.rowspan)] - y - lp.verticalSpacing;
            outRects[static_cast<size_t>(i)] = {x, y, std::max(1, w), std::max(1, h)};
        }
    } else {
        // Fallback: VBox equal distribution
        const int spacingTotal = lp.verticalSpacing * std::max(0, static_cast<int>(activeIndices.size()) - 1);
        const int perH = !activeIndices.empty() ? std::max(24, (innerH - spacingTotal) / static_cast<int>(activeIndices.size())) : innerH;
        int curY = innerY;
        for (size_t activePos = 0; activePos < activeIndices.size(); ++activePos) {
            const int idx = activeIndices[activePos];
            outRects[static_cast<size_t>(idx)] = {innerX, curY, innerW, perH};
            curY += perH;
            if (activePos + 1 < activeIndices.size()) {
                curY += lp.verticalSpacing;
            }
        }
    }
}

// ---- Forward declarations -------------------------------------------------
void emitWidgetNode(std::ostringstream& oss, const SwXmlNode& widgetNode,
                    const Rect& rect, int indLvl,
                    bool allowGeometryOverride = true);
void emitLayoutContent(std::ostringstream& oss, const SwXmlNode& layoutNode,
                       const Rect& parentRect, int indLvl);

// ---------------------------------------------------------------------------
// emitLayoutContent
// Process a <layout> node and emit child widgets into parentRect.
// Nested <layout> items are recursed into without a wrapping widget element.
// ---------------------------------------------------------------------------
void emitLayoutContent(std::ostringstream& oss, const SwXmlNode& layoutNode,
                       const Rect& parentRect, int indLvl) {
    std::vector<LayoutChild> items;
    collectFromLayout(layoutNode, items);
    if (items.empty()) return;

    std::vector<Rect> rects;
    computeChildRects(layoutNode, items, parentRect, rects);

    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const LayoutChild& lc = items[static_cast<size_t>(i)];
        if (!layoutChildParticipates_(lc)) {
            continue;
        }
        const Rect r = rects[static_cast<size_t>(i)];

        if (lc.type == LayoutChild::SpacerNode) {
            std::string spacerName;
            if (lc.node && !lc.node->attr("name").isEmpty()) {
                spacerName = lc.node->attr("name").toStdString();
            } else {
                spacerName = "spacer_" + std::to_string(i);
            }

            const bool vertical = lc.orientation.toLower().contains("vertical");
            const SwString primaryPolicy = normalizedSpacerPolicy_(lc.sizeType);
            const SwString horizontalPolicy = vertical ? SwString("Minimum") : primaryPolicy;
            const SwString verticalPolicy = vertical ? primaryPolicy : SwString("Minimum");

            writeIndent(oss, indLvl);
            oss << "<widget class=\"SwSpacer\" name=\"" << xmlEscapeImp(spacerName) << "\">\n";
            emitGeometry(oss, r, indLvl + 2);
            writeIndent(oss, indLvl + 2);
            oss << "<property name=\"__SwCreator_DesignWidget\"><bool>true</bool></property>\n";
            writeIndent(oss, indLvl + 2);
            oss << "<property name=\"__SwCreator_IsSpacer\"><bool>true</bool></property>\n";
            writeIndent(oss, indLvl + 2);
            oss << "<property name=\"Orientation\"><string>" << (vertical ? "Vertical" : "Horizontal") << "</string></property>\n";
            writeIndent(oss, indLvl + 2);
            oss << "<property name=\"HorizontalPolicy\"><string>" << xmlEscapeImp(horizontalPolicy.toStdString()) << "</string></property>\n";
            writeIndent(oss, indLvl + 2);
            oss << "<property name=\"VerticalPolicy\"><string>" << xmlEscapeImp(verticalPolicy.toStdString()) << "</string></property>\n";
            writeIndent(oss, indLvl + 2);
            oss << "<property name=\"SizeHintWidth\"><number>" << lc.sizeHintW << "</number></property>\n";
            writeIndent(oss, indLvl + 2);
            oss << "<property name=\"SizeHintHeight\"><number>" << lc.sizeHintH << "</number></property>\n";
            writeIndent(oss, indLvl + 2);
            oss << "<property name=\"styleSheet\"><string>SwSpacer { background-color: rgba(0,0,0,0); border-width: 0px; }</string></property>\n";
            writeIndent(oss, indLvl);
            oss << "</widget>\n";
        } else if (lc.type == LayoutChild::WidgetNode) {
            emitWidgetNode(oss, *lc.node, r, indLvl, false);
        } else if (lc.type == LayoutChild::LayoutNode) {
            // Nested layout: recurse into it, mapping its children into r
            emitLayoutContent(oss, *lc.node, r, indLvl);
        }
    }
}

// ---------------------------------------------------------------------------
// emitWidgetNode
// Emit a <widget> XML element for one Qt widget, then recurse into its children.
// rect is in the parent's local coordinate space.
// ---------------------------------------------------------------------------
void emitWidgetNode(std::ostringstream& oss, const SwXmlNode& widgetNode,
                    const Rect& inheritedRect, int indLvl,
                    bool allowGeometryOverride) {
    const SwString qtClass = widgetNode.attr("class", "QWidget");
    const SwString name    = widgetNode.attr("name");
    const char*    swClass = mapQtClassToSw(qtClass);

    Rect myRect = inheritedRect;
    if (allowGeometryOverride) {
        readWidgetGeometry(widgetNode, myRect);
    }

    writeIndent(oss, indLvl);
    oss << "<widget class=\"" << xmlEscapeImp(std::string(swClass)) << "\"";
    if (!name.isEmpty()) {
        oss << " name=\"" << xmlEscapeImp(name.toStdString()) << "\"";
    }
    oss << ">\n";

    emitGeometry(oss, myRect, indLvl + 2);
    writeIndent(oss, indLvl + 2);
    oss << "<property name=\"__SwCreator_DesignWidget\"><bool>true</bool></property>\n";

    // Emit filtered properties
    const SwVector<const SwXmlNode*> props = widgetNode.childrenNamed("property");
    bool textEmitted = false;
    for (const SwXmlNode* p : props) {
        const std::string qtPropName = p->attr("name").toStdString();
        if (qtPropName == "geometry") continue;
        const std::string swPropName = mapQtPropertyName(qtPropName);
        if (swPropName.empty()) continue;
        if (swPropName == "text") {
            if (textEmitted) continue;
            textEmitted = true;
        }
        emitProperty(oss, swPropName, *p, indLvl + 2);
    }

    // Process children
    const std::string cls = qtClass.toStdString();

    if (cls == "QSplitter") {
        // QSplitter: direct <widget> children, no <layout> wrapper
        bool isHorizontal = true;
        for (const SwXmlNode* p : props) {
            if (p->attr("name") == "orientation") {
                const SwXmlNode* en = p->firstChild("enum");
                if (en && en->text.contains("Vertical")) {
                    isHorizontal = false;
                }
            }
        }
        // Emit the orientation so SwUiLoader can configure the splitter correctly
        if (!isHorizontal) {
            writeIndent(oss, indLvl + 2);
            oss << "<property name=\"Orientation\"><string>Vertical</string></property>\n";
        }
        std::vector<const SwXmlNode*> splitterKids;
        for (const SwXmlNode& child : widgetNode.children) {
            if (child.name == "widget") splitterKids.push_back(&child);
        }
        const int nc = static_cast<int>(splitterKids.size());
        for (int i = 0; i < nc; ++i) {
            Rect cr;
            if (isHorizontal) {
                const int cw = nc > 0 ? myRect.w / nc : myRect.w;
                cr = {i * cw, 0, cw, myRect.h};
            } else {
                const int ch = nc > 0 ? myRect.h / nc : myRect.h;
                cr = {0, i * ch, myRect.w, ch};
            }
            emitWidgetNode(oss, *splitterKids[static_cast<size_t>(i)], cr, indLvl + 2, false);
        }
    } else {
        // Normal widget: find <layout> child and recurse, else emit direct <widget> children
        bool layoutProcessed = false;
        for (const SwXmlNode& child : widgetNode.children) {
            if (child.name == "layout") {
                // Pass local rect {0,0,w,h} so children get parent-relative coords
                emitLayoutContent(oss, child, {0, 0, myRect.w, myRect.h}, indLvl + 2);
                layoutProcessed = true;
                break;
            }
        }
        if (!layoutProcessed) {
            // Emit any direct widget children (rare outside QSplitter)
            for (const SwXmlNode& child : widgetNode.children) {
                if (child.name == "widget") {
                    Rect cr = {0, 0, myRect.w, myRect.h};
                    readWidgetGeometry(child, cr);
                    emitWidgetNode(oss, child, cr, indLvl + 2, true);
                }
            }
        }
    }

    writeIndent(oss, indLvl);
    oss << "</widget>\n";
}

// ---- QMainWindow: find centralWidget ------------------------------------

const SwXmlNode* findCentralWidget(const SwXmlNode& mainWindowNode) {
    for (const SwXmlNode& child : mainWindowNode.children) {
        if (child.name == "widget" && child.attr("name") == "centralWidget") {
            return &child;
        }
    }
    // Fallback: first widget child
    for (const SwXmlNode& child : mainWindowNode.children) {
        if (child.name == "widget") return &child;
    }
    return nullptr;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// SwCreatorQtUiImporter implementation
// ---------------------------------------------------------------------------

SwString SwCreatorQtUiImporter::importFromQtUi(const SwString& qtUiXml, SwString* outError) {
    if (qtUiXml.isEmpty()) {
        if (outError) { *outError = "Empty Qt .ui XML"; }
        return SwString();
    }

    SwXmlDocument::ParseResult pr = SwXmlDocument::parse(qtUiXml);
    if (!pr.ok) {
        if (outError) { *outError = "XML parse error: " + pr.error; }
        return SwString();
    }

    const SwXmlNode& uiRoot = pr.root;
    if (uiRoot.name != "ui") {
        if (outError) { *outError = "Root element is not <ui> (got <" + uiRoot.name + ">)"; }
        return SwString();
    }

    const SwXmlNode* topWidget = uiRoot.firstChild("widget");
    if (!topWidget) {
        if (outError) { *outError = "No <widget> child found inside <ui>"; }
        return SwString();
    }

    // Promote QMainWindow's centralWidget as root
    const SwXmlNode* rootWidget = topWidget;
    if (topWidget->attr("class") == "QMainWindow") {
        const SwXmlNode* central = findCentralWidget(*topWidget);
        if (central) rootWidget = central;
    }

    // Root rect — read from the original QMainWindow if needed
    Rect rootRect{0, 0, 800, 600};
    if (!readWidgetGeometry(*rootWidget, rootRect)) {
        readWidgetGeometry(*topWidget, rootRect);
    }
    rootRect.x = 0;
    rootRect.y = 0;

    // Cap width only: the canvas can scroll vertically, but 1800+ px wide forms
    // would hide all right-side content. Fixed widget sizes don't scale, so we
    // only shrink the width; height is kept as-is.
    const int MAX_IMPORT_W = 1200;
    if (rootRect.w > MAX_IMPORT_W) {
        rootRect.w = MAX_IMPORT_W;
    }

    std::ostringstream oss;
    oss << "<swui>\n";
    oss << "  <widget class=\"SwWidget\" name=\"Form\">\n";
    oss << "    <property name=\"__SwCreator_DesignWidget\"><bool>true</bool></property>\n";
    oss << "    <property name=\"geometry\"><rect>"
        << "<x>0</x><y>0</y>"
        << "<width>" << rootRect.w << "</width>"
        << "<height>" << rootRect.h << "</height>"
        << "</rect></property>\n";

    // Emit the root widget's layout content as children of Form
    bool layoutEmitted = false;
    for (const SwXmlNode& child : rootWidget->children) {
        if (child.name == "layout") {
            emitLayoutContent(oss, child, {0, 0, rootRect.w, rootRect.h}, 4);
            layoutEmitted = true;
            break;
        }
    }
    if (!layoutEmitted) {
        // Fallback: emit direct widget children of root
        for (const SwXmlNode& child : rootWidget->children) {
            if (child.name == "widget") {
                Rect cr = {0, 0, rootRect.w, rootRect.h};
                readWidgetGeometry(child, cr);
                emitWidgetNode(oss, child, cr, 4);
            }
        }
    }

    oss << "  </widget>\n";
    oss << "</swui>\n";

    return SwString(oss.str());
}

bool SwCreatorQtUiImporter::importToCanvas(const SwString& qtUiXml,
                                           SwCreatorFormCanvas* canvas,
                                           SwString* outError) {
    if (!canvas) {
        if (outError) { *outError = "Missing canvas"; }
        return false;
    }

    const SwString swuiXml = importFromQtUi(qtUiXml, outError);
    if (swuiXml.isEmpty()) return false;

    return SwCreatorSwuiSerializer::deserializeCanvas(swuiXml, canvas, outError);
}
