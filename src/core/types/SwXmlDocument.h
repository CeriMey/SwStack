#pragma once

/**
 * @file src/core/types/SwXmlDocument.h
 * @ingroup core_types
 * @brief Declares the public interface exposed by SwXmlDocument in the CoreSw fundamental types
 * layer.
 *
 * This header belongs to the CoreSw fundamental types layer. It provides value types, containers,
 * text and binary helpers, and lightweight serialization primitives shared across the stack.
 *
 * Within that layer, this file focuses on the XML document interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwXmlNode and SwXmlDocument.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Interfaces in this area are intentionally reused by runtime, IO, GUI, remote, and media modules
 * to keep cross-module semantics consistent.
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

#include "SwMap.h"
#include "SwString.h"
#include "SwVector.h"

#include <cctype>
#include <cstdint>
#include <cstring>

static constexpr const char* kSwLogCategory_SwXmlDocument = "sw.core.types.swxmldocument";

struct SwXmlNode {
    SwString name;
    SwMap<SwString, SwString> attributes;
    SwString text;
    SwVector<SwXmlNode> children;

    /**
     * @brief Performs the `firstChild` operation.
     * @param childName Value passed to the method.
     * @return The requested first Child.
     */
    const SwXmlNode* firstChild(const SwString& childName) const {
        if (childName.isEmpty()) {
            return nullptr;
        }
        for (const auto& c : children) {
            if (c.name == childName) {
                return &c;
            }
        }
        return nullptr;
    }

    /**
     * @brief Performs the `firstChild` operation.
     * @param childName Value passed to the method.
     * @return The requested first Child.
     */
    const SwXmlNode* firstChild(const char* childName) const {
        if (!childName) {
            return nullptr;
        }
        return firstChild(SwString(childName));
    }

    /**
     * @brief Performs the `childrenNamed` operation.
     * @param childName Value passed to the method.
     * @return The requested children Named.
     */
    SwVector<const SwXmlNode*> childrenNamed(const SwString& childName) const {
        SwVector<const SwXmlNode*> out;
        if (childName.isEmpty()) {
            return out;
        }
        out.reserve(children.size());
        for (const auto& c : children) {
            if (c.name == childName) {
                out.push_back(&c);
            }
        }
        return out;
    }

    /**
     * @brief Performs the `childrenNamed` operation.
     * @param childName Value passed to the method.
     * @return The requested children Named.
     */
    SwVector<const SwXmlNode*> childrenNamed(const char* childName) const {
        if (!childName) {
            return {};
        }
        return childrenNamed(SwString(childName));
    }

    /**
     * @brief Performs the `attr` operation.
     * @param key Value passed to the method.
     * @param def Value passed to the method.
     * @return The requested attr.
     */
    SwString attr(const SwString& key, const SwString& def = SwString()) const {
        if (key.isEmpty()) {
            return def;
        }
        auto it = attributes.find(key);
        return it == attributes.end() ? def : it->second;
    }

    /**
     * @brief Performs the `attr` operation.
     * @param key Value passed to the method.
     * @param def Value passed to the method.
     * @return The requested attr.
     */
    SwString attr(const char* key, const SwString& def = SwString()) const {
        if (!key) {
            return def;
        }
        return attr(SwString(key), def);
    }
};

class SwXmlDocument {
public:
    struct ParseResult {
        SwXmlNode root;
        SwString error;
        bool ok{false};
    };

    /**
     * @brief Performs the `parse` operation.
     * @param xml Value passed to the method.
     * @return The requested parse.
     */
    static ParseResult parse(const SwString& xml) {
        SwXmlDocument::ParseResult r;

        class Parser {
        public:
            /**
             * @brief Constructs a `Parser` instance.
             *
             * @details The instance is initialized and prepared for immediate use.
             */
            explicit Parser(const SwString& xml)
                : m_xml(xml)
                , m_raw(xml.toStdString()) {}

            /**
             * @brief Returns the current parse Impl.
             * @return The current parse Impl.
             *
             * @details The returned value reflects the state currently stored by the instance.
             */
            SwXmlDocument::ParseResult parseImpl() {
                SwXmlDocument::ParseResult r;
                skipWhitespace();

                // Skip UTF-8 BOM if any.
                if (startsWith_(m_raw, m_pos, "\xEF\xBB\xBF")) {
                    m_pos += 3;
                }

                // Skip XML prolog / comments / doctype.
                while (m_pos < m_raw.size()) {
                    skipWhitespace();
                    if (startsWith_(m_raw, m_pos, "<?")) {
                        if (!skipUntil_("?>")) {
                            r.error = "Unterminated XML processing instruction";
                            return r;
                        }
                        continue;
                    }
                    if (startsWith_(m_raw, m_pos, "<!--")) {
                        if (!skipUntil_("-->")) {
                            r.error = "Unterminated XML comment";
                            return r;
                        }
                        continue;
                    }
                    if (startsWith_(m_raw, m_pos, "<!DOCTYPE")) {
                        if (!skipUntil_(">")) {
                            r.error = "Unterminated DOCTYPE";
                            return r;
                        }
                        continue;
                    }
                    break;
                }

                skipWhitespace();
                SwXmlNode rootNode;
                if (!parseElement_(rootNode, r.error)) {
                    return r;
                }

                r.root = std::move(rootNode);
                r.ok = true;
                return r;
            }

        private:
            static bool isNameStart_(char c) {
                return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == ':';
            }

            static bool isNameChar_(char c) {
                return isNameStart_(c) || std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '.';
            }

            static bool startsWith_(const std::string& s, size_t pos, const char* literal) {
                if (!literal) {
                    return false;
                }
                const size_t n = std::strlen(literal);
                if (pos + n > s.size()) {
                    return false;
                }
                return s.compare(pos, n, literal) == 0;
            }

            static void appendUtf8_(SwString& out, uint32_t codepoint) {
                if (codepoint <= 0x7F) {
                    out.append(static_cast<char>(codepoint));
                } else if (codepoint <= 0x7FF) {
                    out.append(static_cast<char>(0xC0 | (codepoint >> 6)));
                    out.append(static_cast<char>(0x80 | (codepoint & 0x3F)));
                } else if (codepoint <= 0xFFFF) {
                    out.append(static_cast<char>(0xE0 | (codepoint >> 12)));
                    out.append(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                    out.append(static_cast<char>(0x80 | (codepoint & 0x3F)));
                } else if (codepoint <= 0x10FFFF) {
                    out.append(static_cast<char>(0xF0 | (codepoint >> 18)));
                    out.append(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
                    out.append(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                    out.append(static_cast<char>(0x80 | (codepoint & 0x3F)));
                }
            }

            static bool parseHexUint32_(const std::string& s, uint32_t& out) {
                if (s.empty()) {
                    return false;
                }
                uint32_t v = 0;
                for (char c : s) {
                    uint32_t d = 0;
                    if (c >= '0' && c <= '9') {
                        d = static_cast<uint32_t>(c - '0');
                    } else if (c >= 'a' && c <= 'f') {
                        d = static_cast<uint32_t>(10 + (c - 'a'));
                    } else if (c >= 'A' && c <= 'F') {
                        d = static_cast<uint32_t>(10 + (c - 'A'));
                    } else {
                        return false;
                    }
                    if (v > 0x10FFFFu / 16u) {
                        return false;
                    }
                    v = static_cast<uint32_t>(v * 16u + d);
                }
                out = v;
                return true;
            }

            static bool parseDecUint32_(const std::string& s, uint32_t& out) {
                if (s.empty()) {
                    return false;
                }
                uint32_t v = 0;
                for (char c : s) {
                    if (!std::isdigit(static_cast<unsigned char>(c))) {
                        return false;
                    }
                    const uint32_t d = static_cast<uint32_t>(c - '0');
                    if (v > 0x10FFFFu / 10u) {
                        return false;
                    }
                    v = static_cast<uint32_t>(v * 10u + d);
                }
                out = v;
                return true;
            }

            static SwString decodeEntities_(const SwString& in) {
                const std::string& raw = in.toStdString();
                SwString out;
                out.reserve(raw.size());

                for (size_t i = 0; i < raw.size(); ++i) {
                    const char c = raw[i];
                    if (c != '&') {
                        out.append(c);
                        continue;
                    }

                    const size_t semi = raw.find(';', i + 1);
                    if (semi == std::string::npos) {
                        out.append(c);
                        continue;
                    }

                    const std::string ent = raw.substr(i + 1, semi - (i + 1));
                    if (ent == "lt") {
                        out.append('<');
                    } else if (ent == "gt") {
                        out.append('>');
                    } else if (ent == "amp") {
                        out.append('&');
                    } else if (ent == "quot") {
                        out.append('"');
                    } else if (ent == "apos") {
                        out.append('\'');
                    } else if (!ent.empty() && ent[0] == '#') {
                        uint32_t codepoint = 0;
                        bool ok = false;
                        if (ent.size() >= 2 && (ent[1] == 'x' || ent[1] == 'X')) {
                            ok = parseHexUint32_(ent.substr(2), codepoint);
                        } else {
                            ok = parseDecUint32_(ent.substr(1), codepoint);
                        }

                        const bool valid = ok && codepoint <= 0x10FFFFu && !(codepoint >= 0xD800u && codepoint <= 0xDFFFu);
                        if (valid) {
                            appendUtf8_(out, codepoint);
                        } else {
                            out.append("&");
                            out.append(ent);
                            out.append(";");
                        }
                    } else {
                        out.append("&");
                        out.append(ent);
                        out.append(";");
                    }

                    i = semi;
                }

                return out;
            }

            void skipWhitespace() {
                while (m_pos < m_raw.size() && std::isspace(static_cast<unsigned char>(m_raw[m_pos]))) {
                    ++m_pos;
                }
            }

            bool skipUntil_(const char* endToken) {
                const size_t end = m_raw.find(endToken, m_pos);
                if (end == std::string::npos) {
                    return false;
                }
                m_pos = end + std::strlen(endToken);
                return true;
            }

            bool parseName_(SwString& outName) {
                if (m_pos >= m_raw.size()) {
                    return false;
                }
                if (!isNameStart_(m_raw[m_pos])) {
                    return false;
                }

                const size_t start = m_pos;
                ++m_pos;
                while (m_pos < m_raw.size() && isNameChar_(m_raw[m_pos])) {
                    ++m_pos;
                }

                outName = SwString(m_raw.substr(start, m_pos - start));
                return true;
            }

            bool parseAttributeValue_(SwString& outValue, SwString& outError) {
                skipWhitespace();
                if (m_pos >= m_raw.size()) {
                    outError = "Unexpected end of input while parsing attribute value";
                    return false;
                }

                const char quote = m_raw[m_pos];
                if (quote != '"' && quote != '\'') {
                    outError = "Expected quoted attribute value";
                    return false;
                }
                ++m_pos;

                const size_t start = m_pos;
                const size_t end = m_raw.find(quote, m_pos);
                if (end == std::string::npos) {
                    outError = "Unterminated attribute value";
                    return false;
                }

                outValue = decodeEntities_(SwString(m_raw.substr(start, end - start)));
                m_pos = end + 1;
                return true;
            }

            bool parseAttributes_(SwMap<SwString, SwString>& out, bool& outSelfClosing, SwString& outError) {
                outSelfClosing = false;
                while (true) {
                    skipWhitespace();
                    if (m_pos >= m_raw.size()) {
                        outError = "Unexpected end of input while parsing attributes";
                        return false;
                    }

                    if (startsWith_(m_raw, m_pos, "/>")) {
                        m_pos += 2;
                        outSelfClosing = true;
                        return true;
                    }
                    if (m_raw[m_pos] == '>') {
                        ++m_pos;
                        return true;
                    }

                    SwString key;
                    if (!parseName_(key)) {
                        outError = "Invalid attribute name";
                        return false;
                    }
                    skipWhitespace();
                    if (m_pos >= m_raw.size() || m_raw[m_pos] != '=') {
                        outError = "Expected '=' after attribute name";
                        return false;
                    }
                    ++m_pos;

                    SwString value;
                    if (!parseAttributeValue_(value, outError)) {
                        return false;
                    }
                    out[key] = value;
                }
            }

            bool parseElement_(SwXmlNode& out, SwString& outError) {
                skipWhitespace();
                if (m_pos >= m_raw.size() || m_raw[m_pos] != '<') {
                    outError = "Expected '<' to start element";
                    return false;
                }

                if (startsWith_(m_raw, m_pos, "<!--")) {
                    if (!skipUntil_("-->")) {
                        outError = "Unterminated XML comment";
                        return false;
                    }
                    return parseElement_(out, outError);
                }

                if (startsWith_(m_raw, m_pos, "<?")) {
                    if (!skipUntil_("?>")) {
                        outError = "Unterminated XML processing instruction";
                        return false;
                    }
                    return parseElement_(out, outError);
                }

                ++m_pos; // '<'

                if (m_pos < m_raw.size() && m_raw[m_pos] == '/') {
                    outError = "Unexpected closing tag";
                    return false;
                }

                SwString name;
                if (!parseName_(name)) {
                    outError = "Invalid element name";
                    return false;
                }
                out.name = std::move(name);

                bool selfClosing = false;
                if (!parseAttributes_(out.attributes, selfClosing, outError)) {
                    return false;
                }
                if (selfClosing) {
                    return true;
                }

                SwString collectedText;
                while (m_pos < m_raw.size()) {
                    if (startsWith_(m_raw, m_pos, "</")) {
                        m_pos += 2;
                        SwString endName;
                        if (!parseName_(endName)) {
                            outError = "Invalid closing tag name";
                            return false;
                        }
                        skipWhitespace();
                        if (m_pos >= m_raw.size() || m_raw[m_pos] != '>') {
                            outError = "Expected '>' after closing tag name";
                            return false;
                        }
                        ++m_pos;
                        if (endName != out.name) {
                            outError = SwString("Mismatched closing tag: expected </") + out.name +
                                       SwString(">, got </") + endName + SwString(">");
                            return false;
                        }
                        break;
                    }

                    if (m_raw[m_pos] == '<') {
                        if (startsWith_(m_raw, m_pos, "<!--")) {
                            if (!skipUntil_("-->")) {
                                outError = "Unterminated XML comment";
                                return false;
                            }
                            continue;
                        }
                        if (startsWith_(m_raw, m_pos, "<?")) {
                            if (!skipUntil_("?>")) {
                                outError = "Unterminated XML processing instruction";
                                return false;
                            }
                            continue;
                        }
                        if (startsWith_(m_raw, m_pos, "<![CDATA[")) {
                            const size_t start = m_pos + 9;
                            const size_t end = m_raw.find("]]>", start);
                            if (end == std::string::npos) {
                                outError = "Unterminated CDATA section";
                                return false;
                            }
                            collectedText.append(SwString(m_raw.substr(start, end - start)));
                            m_pos = end + 3;
                            continue;
                        }

                        SwXmlNode child;
                        if (!parseElement_(child, outError)) {
                            return false;
                        }
                        out.children.push_back(std::move(child));
                        continue;
                    }

                    const size_t nextTag = m_raw.find('<', m_pos);
                    const size_t end = (nextTag == std::string::npos) ? m_raw.size() : nextTag;
                    collectedText.append(SwString(m_raw.substr(m_pos, end - m_pos)));
                    m_pos = end;
                }

                out.text = decodeEntities_(collectedText);
                return true;
            }

            const SwString& m_xml;
            const std::string& m_raw;
            size_t m_pos{0};
        };

        Parser p(xml);
        return p.parseImpl();
    }

    /**
     * @brief Performs the `parse` operation.
     * @param xml Value passed to the method.
     * @return The requested parse.
     */
    static ParseResult parse(const std::string& xml) {
        return parse(SwString(xml));
    }
};
