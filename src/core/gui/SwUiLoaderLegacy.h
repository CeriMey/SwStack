#pragma once

/**
 * @file src/core/gui/SwUiLoaderLegacy.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwUiLoaderLegacy in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the ui loader legacy interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
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
#include "SwChartView.h"
#include "SwMainWindow.h"
#include "core/io/SwFile.h"
#include "SwXmlDocument.h"

#include <cctype>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace swui {

using XmlNode = SwXmlNode;

#if 0
class XmlParser {
public:
    struct ParseResult {
        XmlNode root;
        std::string error;
        bool ok{false};
    };

    /**
     * @brief Performs the `parse` operation.
     * @param xml Value passed to the method.
     * @return The requested parse.
     */
    static ParseResult parse(const std::string& xml) {
        XmlParser p(xml);
        return p.parseImpl();
    }

    /**
     * @brief Performs the `trimInPlace` operation.
     * @param s Value passed to the method.
     * @return The requested trim In Place.
     */
    static void trimInPlace(std::string& s) {
        size_t a = 0;
        while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) {
            ++a;
        }
        size_t b = s.size();
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
            --b;
        }
        if (a == 0 && b == s.size()) {
            return;
        }
        s = s.substr(a, b - a);
    }

private:
    explicit XmlParser(const std::string& xml)
        : m_xml(xml) {}

    static bool isNameStart(char c) {
        return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == ':';
    }

    static bool isNameChar(char c) {
        return isNameStart(c) || std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '.';
    }

    static bool startsWith(const std::string& s, size_t pos, const char* literal) {
        if (!literal) {
            return false;
        }
        const size_t n = std::strlen(literal);
        if (pos + n > s.size()) {
            return false;
        }
        return s.compare(pos, n, literal) == 0;
    }

    static std::string decodeEntities(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            char c = in[i];
            if (c != '&') {
                out.push_back(c);
                continue;
            }
            const size_t semi = in.find(';', i + 1);
            if (semi == std::string::npos) {
                out.push_back(c);
                continue;
            }
            const std::string ent = in.substr(i + 1, semi - (i + 1));
            if (ent == "lt") {
                out.push_back('<');
            } else if (ent == "gt") {
                out.push_back('>');
            } else if (ent == "amp") {
                out.push_back('&');
            } else if (ent == "quot") {
                out.push_back('"');
            } else if (ent == "apos") {
                out.push_back('\'');
            } else {
                out.append("&");
                out.append(ent);
                out.append(";");
            }
            i = semi;
        }
        return out;
    }

    ParseResult parseImpl() {
        ParseResult r;
        skipWhitespace();

        // Skip BOM if any.
        if (startsWith(m_xml, m_pos, "\xEF\xBB\xBF")) {
            m_pos += 3;
        }

        // Skip XML prolog / comments / doctype.
        while (m_pos < m_xml.size()) {
            skipWhitespace();
            if (startsWith(m_xml, m_pos, "<?")) {
                if (!skipUntil("?>")) {
                    r.error = "Unterminated XML processing instruction";
                    return r;
                }
                continue;
            }
            if (startsWith(m_xml, m_pos, "<!--")) {
                if (!skipUntil("-->")) {
                    r.error = "Unterminated XML comment";
                    return r;
                }
                continue;
            }
            if (startsWith(m_xml, m_pos, "<!DOCTYPE")) {
                if (!skipUntil(">")) {
                    r.error = "Unterminated DOCTYPE";
                    return r;
                }
                continue;
            }
            break;
        }

        skipWhitespace();
        XmlNode rootNode;
        if (!parseElement(rootNode, r.error)) {
            return r;
        }
        r.root = std::move(rootNode);
        r.ok = true;
        return r;
    }

    void skipWhitespace() {
        while (m_pos < m_xml.size() && std::isspace(static_cast<unsigned char>(m_xml[m_pos]))) {
            ++m_pos;
        }
    }

    bool skipUntil(const char* endToken) {
        const size_t end = m_xml.find(endToken, m_pos);
        if (end == std::string::npos) {
            return false;
        }
        m_pos = end + std::strlen(endToken);
        return true;
    }

    bool parseName(std::string& outName) {
        if (m_pos >= m_xml.size()) {
            return false;
        }
        if (!isNameStart(m_xml[m_pos])) {
            return false;
        }
        size_t start = m_pos;
        ++m_pos;
        while (m_pos < m_xml.size() && isNameChar(m_xml[m_pos])) {
            ++m_pos;
        }
        outName = m_xml.substr(start, m_pos - start);
        return true;
    }

    bool parseAttributeValue(std::string& outValue, std::string& outError) {
        skipWhitespace();
        if (m_pos >= m_xml.size()) {
            outError = "Unexpected end of input while parsing attribute value";
            return false;
        }
        const char quote = m_xml[m_pos];
        if (quote != '"' && quote != '\'') {
            outError = "Expected quoted attribute value";
            return false;
        }
        ++m_pos;
        const size_t start = m_pos;
        const size_t end = m_xml.find(quote, m_pos);
        if (end == std::string::npos) {
            outError = "Unterminated attribute value";
            return false;
        }
        outValue = decodeEntities(m_xml.substr(start, end - start));
        m_pos = end + 1;
        return true;
    }

    bool parseAttributes(std::map<std::string, std::string>& out, bool& outSelfClosing, std::string& outError) {
        outSelfClosing = false;
        while (true) {
            skipWhitespace();
            if (m_pos >= m_xml.size()) {
                outError = "Unexpected end of input while parsing attributes";
                return false;
            }

            if (startsWith(m_xml, m_pos, "/>")) {
                m_pos += 2;
                outSelfClosing = true;
                return true;
            }
            if (m_xml[m_pos] == '>') {
                ++m_pos;
                return true;
            }

            std::string key;
            if (!parseName(key)) {
                outError = "Invalid attribute name";
                return false;
            }
            skipWhitespace();
            if (m_pos >= m_xml.size() || m_xml[m_pos] != '=') {
                outError = "Expected '=' after attribute name";
                return false;
            }
            ++m_pos;

            std::string value;
            if (!parseAttributeValue(value, outError)) {
                return false;
            }
            out[key] = value;
        }
    }

    bool parseElement(XmlNode& out, std::string& outError) {
        skipWhitespace();
        if (m_pos >= m_xml.size() || m_xml[m_pos] != '<') {
            outError = "Expected '<' to start element";
            return false;
        }

        if (startsWith(m_xml, m_pos, "<!--")) {
            if (!skipUntil("-->")) {
                outError = "Unterminated XML comment";
                return false;
            }
            return parseElement(out, outError);
        }

        if (startsWith(m_xml, m_pos, "<?")) {
            if (!skipUntil("?>")) {
                outError = "Unterminated XML processing instruction";
                return false;
            }
            return parseElement(out, outError);
        }

        ++m_pos; // '<'

        if (m_pos < m_xml.size() && m_xml[m_pos] == '/') {
            outError = "Unexpected closing tag";
            return false;
        }

        std::string name;
        if (!parseName(name)) {
            outError = "Invalid element name";
            return false;
        }
        out.name = std::move(name);

        bool selfClosing = false;
        if (!parseAttributes(out.attributes, selfClosing, outError)) {
            return false;
        }
        if (selfClosing) {
            return true;
        }

        // Parse content: children and/or text until matching end tag.
        std::string collectedText;
        while (m_pos < m_xml.size()) {
            if (startsWith(m_xml, m_pos, "</")) {
                m_pos += 2;
                std::string endName;
                if (!parseName(endName)) {
                    outError = "Invalid closing tag name";
                    return false;
                }
                skipWhitespace();
                if (m_pos >= m_xml.size() || m_xml[m_pos] != '>') {
                    outError = "Expected '>' after closing tag name";
                    return false;
                }
                ++m_pos;
                if (endName != out.name) {
                    outError = "Mismatched closing tag: expected </" + out.name + ">, got </" + endName + ">";
                    return false;
                }
                break;
            }

            if (m_pos < m_xml.size() && m_xml[m_pos] == '<') {
                XmlNode child;
                if (!parseElement(child, outError)) {
                    return false;
                }
                out.children.push_back(std::move(child));
                continue;
            }

            // Text node.
            const size_t nextTag = m_xml.find('<', m_pos);
            const size_t end = (nextTag == std::string::npos) ? m_xml.size() : nextTag;
            collectedText.append(m_xml.substr(m_pos, end - m_pos));
            m_pos = end;
        }

        out.text = decodeEntities(collectedText);
        return true;
    }

    const std::string& m_xml;
    size_t m_pos{0};
};


#endif

class XmlParser {
public:
    struct ParseResult {
        XmlNode root;
        std::string error;
        bool ok{false};
    };

    /**
     * @brief Performs the `parse` operation.
     * @param xml Value passed to the method.
     * @return The requested parse.
     */
    static ParseResult parse(const std::string& xml) {
        const auto parsed = SwXmlDocument::parse(xml);
        ParseResult out;
        out.root = std::move(parsed.root);
        out.error = parsed.error.toStdString();
        out.ok = parsed.ok;
        return out;
    }

    /**
     * @brief Performs the `trimInPlace` operation.
     * @param s Value passed to the method.
     * @return The requested trim In Place.
     */
    static void trimInPlace(std::string& s) {
        size_t a = 0;
        while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) {
            ++a;
        }
        size_t b = s.size();
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
            --b;
        }
        if (a == 0 && b == s.size()) {
            return;
        }
        s = s.substr(a, b - a);
    }
};

class UiFactory {
public:
    using WidgetCtor = std::function<SwWidget*(SwWidget* parent)>;

    /**
     * @brief Returns the current instance.
     * @return The current instance.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static UiFactory& instance() {
        static UiFactory f;
        return f;
    }

    /**
     * @brief Performs the `registerWidget` operation.
     * @param className Value passed to the method.
     * @param ctor Value passed to the method.
     */
    void registerWidget(const std::string& className, WidgetCtor ctor) {
        if (className.empty() || !ctor) {
            return;
        }
        m_widgetCtors[className] = std::move(ctor);
    }

    /**
     * @brief Creates the requested widget.
     * @param className Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @return The resulting widget.
     */
    SwWidget* createWidget(const std::string& className, SwWidget* parent) const {
        auto it = m_widgetCtors.find(className);
        if (it != m_widgetCtors.end()) {
            return it->second(parent);
        }

        // Common aliases accepted by the loader (to accept near-identical .ui files).
        const std::string alias = qtAliasToSw_(className);
        if (alias != className) {
            it = m_widgetCtors.find(alias);
            if (it != m_widgetCtors.end()) {
                return it->second(parent);
            }
        }

        return nullptr;
    }

private:
    UiFactory() {
        // Register a minimal default set (MVP).
        registerWidget("SwWidget", [](SwWidget* p) { return new SwWidget(p); });
        registerWidget("SwFrame", [](SwWidget* p) { return new SwFrame(p); });
        registerWidget("Line", [](SwWidget* p) { return new SwFrame(p); }); // Designer "Line" separator
        registerWidget("SwLabel", [](SwWidget* p) { return new SwLabel(p); });
        registerWidget("SwPushButton", [](SwWidget* p) { return new SwPushButton("PushButton", p); });
        registerWidget("SwLineEdit", [](SwWidget* p) { return new SwLineEdit(p); });
        registerWidget("SwCheckBox", [](SwWidget* p) { return new SwCheckBox(p); });
        registerWidget("SwRadioButton", [](SwWidget* p) { return new SwRadioButton(p); });
        registerWidget("SwComboBox", [](SwWidget* p) { return new SwComboBox(p); });
        registerWidget("SwProgressBar", [](SwWidget* p) { return new SwProgressBar(p); });
        registerWidget("SwPlainTextEdit", [](SwWidget* p) { return new SwPlainTextEdit(p); });
        registerWidget("SwTextEdit", [](SwWidget* p) { return new SwTextEdit(p); });
        registerWidget("SwTabWidget", [](SwWidget* p) { return new SwTabWidget(p); });
        registerWidget("SwSplitter", [](SwWidget* p) { return new SwSplitter(SwSplitter::Orientation::Horizontal, p); });
        registerWidget("SwStackedWidget", [](SwWidget* p) { return new SwStackedWidget(p); });
        registerWidget("SwScrollArea", [](SwWidget* p) { return new SwScrollArea(p); });
        registerWidget("SwSpacer", [](SwWidget* p) { return new SwSpacer(p); });
        registerWidget("SwGroupBox", [](SwWidget* p) { return new SwGroupBox(p); });
        registerWidget("SwToolButton", [](SwWidget* p) { return new SwToolButton(p); });
        registerWidget("SwToolBox", [](SwWidget* p) { return new SwToolBox(p); });
        registerWidget("SwSpinBox", [](SwWidget* p) { return new SwSpinBox(p); });
        registerWidget("SwDoubleSpinBox", [](SwWidget* p) { return new SwDoubleSpinBox(p); });
        registerWidget("SwSlider", [](SwWidget* p) { return new SwSlider(SwSlider::Orientation::Horizontal, p); });
        registerWidget("SwTableWidget", [](SwWidget* p) { return new SwTableWidget(0, 0, p); });
        registerWidget("SwTreeWidget", [](SwWidget* p) { return new SwTreeWidget(1, p); });
        registerWidget("SwTableView", [](SwWidget* p) { return new SwTableView(p); });
        registerWidget("SwTreeView", [](SwWidget* p) { return new SwTreeView(p); });
        registerWidget("SwChartView", [](SwWidget* p) { return new SwChartView(p); });
    }

    static std::string qtAliasToSw_(const std::string& qtClass) {
        if (!qtClass.empty() && qtClass[0] == 'Q') {
            return std::string("Sw") + qtClass.substr(1);
        }
        return qtClass;
    }

    std::map<std::string, WidgetCtor> m_widgetCtors;
};

class UiLoader {
public:
    struct LoadResult {
        SwWidget* root{nullptr};
        std::string error;
        bool ok{false};
    };

    /**
     * @brief Performs the `loadFromString` operation on the associated resource.
     * @param xml Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @return The resulting from String.
     */
    static LoadResult loadFromString(const std::string& xml, SwWidget* parent = nullptr) {
        LoadResult out;

        const auto parsed = XmlParser::parse(xml);
        if (!parsed.ok) {
            out.error = parsed.error.empty() ? "XML parse error" : parsed.error;
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
        const std::string rootClass = widgetNode->attr("class");
        if (rootClass == "QMainWindow" || rootClass == "SwMainWindow") {
            if (!parent) {
                SwMainWindow* win = loadQtMainWindow_(*widgetNode, customWidgetExtends, out.error);
                if (!win) {
                    if (out.error.empty()) {
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
                if (out.error.empty()) {
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
            if (out.error.empty()) {
                out.error = "Failed to create root widget";
            }
            return out;
        }

        out.root = createdRoot;
        out.ok = true;
        return out;
    }

    /**
     * @brief Performs the `loadFromFile` operation on the associated resource.
     * @param filePath Path of the target file.
     * @param parent Optional parent object that owns this instance.
     * @return The resulting from File.
     */
    static LoadResult loadFromFile(const SwString& filePath, SwWidget* parent = nullptr) {
        LoadResult out;
        SwFile f(filePath);
        if (!f.open(SwFile::Read)) {
            out.error = "Failed to open file";
            return out;
        }
        const SwString content = f.readAll();
        f.close();
        return loadFromString(content.toStdString(), parent);
    }

private:
    static int toInt_(const std::string& s, int def = 0) {
        if (s.empty()) {
            return def;
        }
        char* end = nullptr;
        const long v = std::strtol(s.c_str(), &end, 10);
        if (!end || end == s.c_str()) {
            return def;
        }
        return static_cast<int>(v);
    }

    static double toDouble_(const std::string& s, double def = 0.0) {
        if (s.empty()) {
            return def;
        }
        char* end = nullptr;
        const double v = std::strtod(s.c_str(), &end);
        if (!end || end == s.c_str()) {
            return def;
        }
        return v;
    }

    static std::string toLowerAscii_(std::string s) {
        for (char& c : s) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return s;
    }

    static bool toBool_(std::string s, bool def = false) {
        XmlParser::trimInPlace(s);
        s = toLowerAscii_(std::move(s));
        if (s == "true" || s == "1") {
            return true;
        }
        if (s == "false" || s == "0") {
            return false;
        }
        return def;
    }

    static std::string trimCopy_(std::string s) {
        XmlParser::trimInPlace(s);
        return s;
    }

    static std::string propertyNameToSw_(const std::string& name) {
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

        // If it's already PascalCase (Sw-style), keep as-is.
        return name;
    }

    static void applyCommonProperty_(SwWidget* w, const std::string& rawName, const XmlNode& propNode) {
        if (!w) {
            return;
        }
        const std::string name = propertyNameToSw_(rawName);
        const std::string rawValue = textValue_(propNode);

        if (name == "geometry") {
            const XmlNode* rect = propNode.firstChild("rect");
            if (!rect) {
                return;
            }
            const int x = toInt_(trimCopy_(childText_(*rect, "x")));
            const int y = toInt_(trimCopy_(childText_(*rect, "y")));
            const int width = toInt_(trimCopy_(childText_(*rect, "width")));
            const int height = toInt_(trimCopy_(childText_(*rect, "height")));
            w->move(x, y);
            w->resize(width, height);
            return;
        }

        if (rawName == "minimumSize") {
            const XmlNode* size = propNode.firstChild("size");
            if (!size) {
                return;
            }
            const int wpx = toInt_(trimCopy_(childText_(*size, "width")));
            const int hpx = toInt_(trimCopy_(childText_(*size, "height")));
            w->setMinimumSize(wpx, hpx);
            return;
        }

        if (rawName == "maximumSize") {
            const XmlNode* size = propNode.firstChild("size");
            if (!size) {
                return;
            }
            const int wpx = toInt_(trimCopy_(childText_(*size, "width")));
            const int hpx = toInt_(trimCopy_(childText_(*size, "height")));
            w->setMaximumSize(wpx, hpx);
            return;
        }

        if (name == "ObjectName") {
            w->setObjectName(SwString(rawValue));
            return;
        }

        // Common widget-specific .ui properties (subset).
        if (rawName == "checkable") {
            const bool v = toBool_(rawValue, false);
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
        }

        if (rawName == "checked") {
            const bool v = toBool_(rawValue, false);
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
        }

        if (rawName == "value") {
            const int iv = toInt_(rawValue, 0);
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
                dspin->setValue(toDouble_(rawValue, dspin->value()));
                return;
            }
        }

        if (rawName == "minimum") {
            const int iv = toInt_(rawValue, 0);
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
                dspin->setMinimum(toDouble_(rawValue, dspin->minimum()));
                return;
            }
        }

        if (rawName == "maximum") {
            const int iv = toInt_(rawValue, 0);
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
                dspin->setMaximum(toDouble_(rawValue, dspin->maximum()));
                return;
            }
        }

        if (rawName == "orientation") {
            const std::string o = toLowerAscii_(trimCopy_(rawValue));
            const bool vertical = (o.find("vertical") != std::string::npos);

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
            if (auto* spacer = dynamic_cast<SwSpacer*>(w)) {
                spacer->setDirection(vertical ? SwSpacer::Direction::Vertical
                                              : SwSpacer::Direction::Horizontal);
                return;
            }
        }

        if (rawName == "handleWidth") {
            if (auto* splitter = dynamic_cast<SwSplitter*>(w)) {
                splitter->setHandleWidth(toInt_(rawValue, splitter->handleWidth()));
                return;
            }
        }

        if (rawName == "widgetResizable") {
            if (auto* scroll = dynamic_cast<SwScrollArea*>(w)) {
                scroll->setWidgetResizable(toBool_(rawValue, scroll->widgetResizable()));
                return;
            }
        }

        // Default: preserve the XML type when possible (helps dynamic properties).
        if (!propNode.children.isEmpty()) {
            for (const auto& c : propNode.children) {
                if (c.name == "bool") {
                    std::string t = c.text;
                    XmlParser::trimInPlace(t);
                    const SwString s(t);
                    const bool v = (s.toLower().trimmed() == "true" || s.trimmed() == "1");
                    w->setProperty(SwString(name), SwAny(v));
                    return;
                }
                if (c.name == "number") {
                    std::string t = c.text;
                    XmlParser::trimInPlace(t);
                    const int v = toInt_(t, 0);
                    w->setProperty(SwString(name), SwAny(v));
                    return;
                }
                if (c.name == "double") {
                    std::string t = c.text;
                    XmlParser::trimInPlace(t);
                    const double v = toDouble_(t, 0.0);
                    w->setProperty(SwString(name), SwAny(v));
                    return;
                }
                if (c.name == "enum" || c.name == "set") {
                    std::string t = c.text;
                    XmlParser::trimInPlace(t);
                    w->setProperty(SwString(name), SwAny(SwString(t)));
                    return;
                }
                if (c.name == "string" || c.name == "cstring") {
                    std::string t = c.text;
                    XmlParser::trimInPlace(t);
                    w->setProperty(SwString(name), SwAny(SwString(t)));
                    return;
                }
            }
        }

        // Fallback: treat as string and rely on SwAny conversions.
        w->setProperty(SwString(name), SwAny(SwString(rawValue)));
    }

    static std::string childText_(const XmlNode& node, const char* childName) {
        const XmlNode* c = node.firstChild(childName);
        if (!c) {
            return {};
        }
        std::string t = c->text;
        XmlParser::trimInPlace(t);
        return t;
    }

    static std::string textValue_(const XmlNode& propNode) {
        // Designer XML stores typed values:
        // <property name="text"><string>Hi</string></property>
        // <property name="enabled"><bool>true</bool></property>
        // We'll accept the first child element text if present, otherwise property.text.
        if (!propNode.children.isEmpty()) {
            for (const auto& c : propNode.children) {
                if (c.name == "string" || c.name == "cstring" || c.name == "number" || c.name == "double" || c.name == "bool" ||
                    c.name == "enum" || c.name == "set") {
                    std::string t = c.text;
                    XmlParser::trimInPlace(t);
                    return t;
                }
                if (c.name == "stringlist") {
                    // Join <string> children with commas (compatible with SwAny conversion rules).
                    std::string joined;
                    bool first = true;
                    for (const auto& s : c.children) {
                        if (s.name != "string") {
                            continue;
                        }
                        std::string t = s.text;
                        XmlParser::trimInPlace(t);
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

        std::string t = propNode.text;
        XmlParser::trimInPlace(t);
        return t;
    }

    static std::string attributeValue_(const XmlNode& widgetNode, const char* attributeName) {
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

    static std::map<std::string, std::string> parseCustomWidgets_(const XmlNode& uiRoot) {
        std::map<std::string, std::string> out;
        const XmlNode* customwidgets = uiRoot.firstChild("customwidgets");
        if (!customwidgets) {
            return out;
        }
        for (const auto* cw : customwidgets->childrenNamed("customwidget")) {
            if (!cw) {
                continue;
            }
            std::string cls = childText_(*cw, "class");
            std::string ext = childText_(*cw, "extends");
            XmlParser::trimInPlace(cls);
            XmlParser::trimInPlace(ext);
            if (!cls.empty() && !ext.empty()) {
                out[cls] = ext;
            }
        }
        return out;
    }

    static bool attachChildToContainer_(SwWidget* parent, SwWidget* child, const XmlNode& childWidgetNode, std::string& outError) {
        if (!parent || !child) {
            return true;
        }

        if (auto* tab = dynamic_cast<SwTabWidget*>(parent)) {
            std::string label = attributeValue_(childWidgetNode, "title");
            if (label.empty()) label = attributeValue_(childWidgetNode, "label");
            if (label.empty()) label = childWidgetNode.attr("name").toStdString();
            if (label.empty()) label = child->getObjectName().toStdString();
            if (label.empty()) label = child->className().toStdString();
            tab->addTab(child, SwString(label));
            return true;
        }

        if (auto* toolbox = dynamic_cast<SwToolBox*>(parent)) {
            std::string label = attributeValue_(childWidgetNode, "label");
            if (label.empty()) label = attributeValue_(childWidgetNode, "title");
            if (label.empty()) label = childWidgetNode.attr("name").toStdString();
            if (label.empty()) label = child->getObjectName().toStdString();
            if (label.empty()) label = child->className().toStdString();
            toolbox->addItem(child, SwString(label));
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

    static SwAbstractLayout* createLayout_(const std::string& className, SwWidget* parent, std::string& outError) {
        if (!parent) {
            outError = "Layout without parent widget";
            return nullptr;
        }

        // Aliases accepted by the loader.
        std::string cls = className;
        if (cls == "QVBoxLayout") cls = "SwVerticalLayout";
        if (cls == "QHBoxLayout") cls = "SwHorizontalLayout";
        if (cls == "QGridLayout") cls = "SwGridLayout";
        if (cls == "QFormLayout") cls = "SwFormLayout";

        if (cls == "SwVerticalLayout") return new SwVerticalLayout(parent);
        if (cls == "SwHorizontalLayout") return new SwHorizontalLayout(parent);
        if (cls == "SwGridLayout") return new SwGridLayout(parent);
        if (cls == "SwFormLayout") return new SwFormLayout(parent);

        outError = "Unsupported layout class: " + className;
        return nullptr;
    }

    static SwSizePolicy::Policy spacerPolicyFromString_(std::string value) {
        XmlParser::trimInPlace(value);
        const size_t sep = value.rfind(':');
        if (sep != std::string::npos) {
            value = value.substr(sep + 1);
        }
        XmlParser::trimInPlace(value);
        if (value == "Fixed") return SwSizePolicy::Fixed;
        if (value == "Minimum") return SwSizePolicy::Minimum;
        if (value == "Maximum") return SwSizePolicy::Maximum;
        if (value == "Preferred") return SwSizePolicy::Preferred;
        if (value == "MinimumExpanding") return SwSizePolicy::MinimumExpanding;
        if (value == "Expanding") return SwSizePolicy::Expanding;
        if (value == "Ignored") return SwSizePolicy::Ignored;
        return SwSizePolicy::Minimum;
    }

    static SwSpacerItem* loadSpacerItem_(const XmlNode& spacerNode) {
        int width = 40;
        int height = 20;
        std::string orientation = "Qt::Horizontal";
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

            const std::string rawName = prop->attr("name");
            if (rawName == "sizeHint") {
                const XmlNode* size = prop->firstChild("size");
                if (!size) {
                    continue;
                }
                width = std::max(0, toInt_(childText_(*size, "width"), width));
                height = std::max(0, toInt_(childText_(*size, "height"), height));
            } else if (rawName == "orientation") {
                orientation = trimCopy_(textValue_(*prop));
            } else if (rawName == "sizeType") {
                hasSizeType = true;
                sizeTypePolicy = spacerPolicyFromString_(trimCopy_(textValue_(*prop)));
            } else if (rawName == "horizontalSizeType") {
                explicitHorizontalPolicy = true;
                horizontalPolicy = spacerPolicyFromString_(trimCopy_(textValue_(*prop)));
            } else if (rawName == "verticalSizeType") {
                explicitVerticalPolicy = true;
                verticalPolicy = spacerPolicyFromString_(trimCopy_(textValue_(*prop)));
            }
        }

        std::string orientationLower = orientation;
        XmlParser::trimInPlace(orientationLower);
        orientationLower = toLowerAscii_(std::move(orientationLower));
        const bool vertical = orientationLower.find("vertical") != std::string::npos;
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

    static bool applyLayout_(SwWidget* parentWidget,
                             const XmlNode& widgetNode,
                             std::string& outError,
                             const std::map<std::string, std::string>& customWidgetExtends) {
        if (!parentWidget) {
            return false;
        }

        const XmlNode* layoutNode = widgetNode.firstChild("layout");
        if (!layoutNode) {
            return true;
        }

        const std::string layoutClass = layoutNode->attr("class");
        if (layoutClass.empty()) {
            outError = "Layout missing class attribute";
            return false;
        }

        std::unique_ptr<SwAbstractLayout> layout(createLayout_(layoutClass, parentWidget, outError));
        if (!layout) {
            return false;
        }

        // Apply common layout properties (spacing/margin).
        for (const auto* prop : layoutNode->childrenNamed("property")) {
            const std::string rawName = prop ? prop->attr("name") : std::string();
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
                    return false;
                }

                if (auto* grid = dynamic_cast<SwGridLayout*>(layout.get())) {
                    const int row = toInt_(item->attr("row", "0"), 0);
                    const int col = toInt_(item->attr("column", "0"), 0);
                    const int rowSpan = toInt_(item->attr("rowspan", "1"), 1);
                    const int colSpan = toInt_(item->attr("colspan", "1"), 1);
                    grid->addWidget(child, row, col, rowSpan, colSpan);
                } else if (auto* boxV = dynamic_cast<SwVerticalLayout*>(layout.get())) {
                    boxV->addWidget(child);
                } else if (auto* boxH = dynamic_cast<SwHorizontalLayout*>(layout.get())) {
                    boxH->addWidget(child);
                } else if (auto* form = dynamic_cast<SwFormLayout*>(layout.get())) {
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
                    outError = "Failed to create spacer item";
                    return false;
                }

                if (auto* grid = dynamic_cast<SwGridLayout*>(layout.get())) {
                    const int row = toInt_(item->attr("row", "0"), 0);
                    const int col = toInt_(item->attr("column", "0"), 0);
                    const int rowSpan = toInt_(item->attr("rowspan", "1"), 1);
                    const int colSpan = toInt_(item->attr("colspan", "1"), 1);
                    grid->addItem(spacer, row, col, rowSpan, colSpan);
                } else if (auto* boxV = dynamic_cast<SwVerticalLayout*>(layout.get())) {
                    boxV->addSpacerItem(spacer);
                } else if (auto* boxH = dynamic_cast<SwHorizontalLayout*>(layout.get())) {
                    boxH->addSpacerItem(spacer);
                } else if (auto* form = dynamic_cast<SwFormLayout*>(layout.get())) {
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

        parentWidget->setLayout(layout.release());
        return true;
    }

    static SwWidget* loadWidget_(const XmlNode& widgetNode,
                                 SwWidget* parent,
                                 std::string& outError,
                                 const std::map<std::string, std::string>& customWidgetExtends) {
        const std::string className = widgetNode.attr("class");
        if (className.empty()) {
            outError = "Widget missing class attribute";
            return nullptr;
        }

        SwWidget* w = UiFactory::instance().createWidget(className, parent);
        if (!w) {
            std::string resolved = className;
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
            outError = "Unknown widget class: " + className;
            return nullptr;
        }

        const std::string objName = widgetNode.attr("name");
        if (!objName.empty()) {
            w->setObjectName(SwString(objName));
        }

        int deferredCurrentIndex = -1;

        // Apply direct <property> children.
        for (const auto* prop : widgetNode.childrenNamed("property")) {
            if (!prop) {
                continue;
            }
            const std::string rawName = prop->attr("name");
            if (rawName.empty()) {
                continue;
            }

            // Some properties depend on children being added first.
            if (rawName == "currentIndex") {
                if (dynamic_cast<SwTabWidget*>(w) || dynamic_cast<SwStackedWidget*>(w) || dynamic_cast<SwToolBox*>(w)) {
                    deferredCurrentIndex = toInt_(trimCopy_(textValue_(*prop)), -1);
                    continue;
                }
            }

            applyCommonProperty_(w, rawName, *prop);
        }

        // Layout blocks take precedence; if present, children are created through <layout><item>...
        if (!applyLayout_(w, widgetNode, outError, customWidgetExtends)) {
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
                    return nullptr;
                }
                if (!attachChildToContainer_(w, child, *childWidgetNode, outError)) {
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

    static bool loadIntoExistingWidget_(SwWidget* target,
                                       const XmlNode& widgetNode,
                                       std::string& outError,
                                       const std::map<std::string, std::string>& customWidgetExtends) {
        if (!target) {
            outError = "Null target widget";
            return false;
        }

        const std::string objName = widgetNode.attr("name");
        if (!objName.empty()) {
            target->setObjectName(SwString(objName));
        }

        int deferredCurrentIndex = -1;

        for (const auto* prop : widgetNode.childrenNamed("property")) {
            if (!prop) {
                continue;
            }
            const std::string rawName = prop->attr("name");
            if (rawName.empty()) {
                continue;
            }

            if (rawName == "currentIndex") {
                if (dynamic_cast<SwTabWidget*>(target) || dynamic_cast<SwStackedWidget*>(target) || dynamic_cast<SwToolBox*>(target)) {
                    deferredCurrentIndex = toInt_(trimCopy_(textValue_(*prop)), -1);
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

    static bool loadQtMainWindowCentralWidgetInto_(SwWidget* target,
                                                  const XmlNode& mainWindowNode,
                                                  std::string& outError,
                                                  const std::map<std::string, std::string>& customWidgetExtends) {
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

    static SwMainWindow* loadQtMainWindow_(const XmlNode& mainWindowNode,
                                          const std::map<std::string, std::string>& customWidgetExtends,
                                          std::string& outError) {
        int x = 0;
        int y = 0;
        int w = 800;
        int h = 600;
        std::wstring title = L"Main Window";

        for (const auto* prop : mainWindowNode.childrenNamed("property")) {
            if (!prop) {
                continue;
            }
            const std::string rawName = prop->attr("name");
            if (rawName == "geometry") {
                const XmlNode* rect = prop->firstChild("rect");
                if (rect) {
                    x = toInt_(trimCopy_(childText_(*rect, "x")), x);
                    y = toInt_(trimCopy_(childText_(*rect, "y")), y);
                    w = toInt_(trimCopy_(childText_(*rect, "width")), w);
                    h = toInt_(trimCopy_(childText_(*rect, "height")), h);
                }
            } else if (rawName == "windowTitle") {
                std::string t = textValue_(*prop);
                XmlParser::trimInPlace(t);
                if (!t.empty()) {
                    title = SwString(t).toStdWString();
                }
            }
        }

        SwMainWindow* win = new SwMainWindow(title, w, h);

        const std::string objName = mainWindowNode.attr("name");
        if (!objName.empty()) {
            win->setObjectName(SwString(objName));
        }

        for (const auto* prop : mainWindowNode.childrenNamed("property")) {
            if (!prop) {
                continue;
            }
            const std::string rawName = prop->attr("name");
            if (rawName.empty()) {
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
};

} // namespace swui
