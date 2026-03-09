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

#pragma once

/**
 * @file src/core/types/SwJsonDocument.h
 * @ingroup core_types
 * @brief Declares the public interface exposed by SwJsonDocument in the CoreSw fundamental types
 * layer.
 *
 * This header belongs to the CoreSw fundamental types layer. It provides value types, containers,
 * text and binary helpers, and lightweight serialization primitives shared across the stack.
 *
 * Within that layer, this file focuses on the JSON document interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwJsonDocument.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Interfaces in this area are intentionally reused by runtime, IO, GUI, remote, and media modules
 * to keep cross-module semantics consistent.
 *
 */


#include "SwJsonObject.h"
#include "SwJsonArray.h"
#include "SwString.h"
#include "SwList.h"
#include "SwDebug.h"
#include <sstream>
#include <cctype>
#include <cstdlib>
#include <cerrno>
#include <cstdint>
#include <algorithm>
#include <exception>
#include <limits>
static constexpr const char* kSwLogCategory_SwJsonDocument = "sw.core.types.swjsondocument";


class SwJsonDocument {
public:
    enum class JsonFormat { Compact, Pretty };

    /**
     * @brief Default constructor for the SwJsonDocument class.
     *
     * Initializes an empty JSON document with no root value.
     */
    SwJsonDocument() = default;

    /**
     * @brief Constructor to initialize a SwJsonDocument with a JSON object.
     *
     * Sets the root of the document to the specified JSON object.
     *
     * @param object A SwJsonObject to set as the root of the document.
     */
    SwJsonDocument(const SwJsonObject& object) : rootValue_(object) {}

    /**
     * @brief Constructor to initialize a SwJsonDocument with a JSON array.
     *
     * Sets the root of the document to the specified JSON array.
     *
     * @param array A SwJsonArray to set as the root of the document.
     */
    SwJsonDocument(const SwJsonArray& array) : rootValue_(array) {}

    /**
     * @brief Sets the root of the JSON document to a JSON object.
     *
     * Replaces the current root value with the specified JSON object.
     *
     * @param object The SwJsonObject to set as the root of the document.
     */
    void setObject(const SwJsonObject& object) {
        rootValue_ = SwJsonValue(object);
    }

    /**
     * @brief Sets the root of the JSON document to a JSON array.
     *
     * Replaces the current root value with the specified JSON array.
     *
     * @param array The SwJsonArray to set as the root of the document.
     */
    void setArray(const SwJsonArray& array) {
        rootValue_ = SwJsonValue(array);
    }

    /**
     * @brief Checks if the root of the JSON document is a JSON object.
     *
     * Determines whether the root value of the document is of type SwJsonObject.
     *
     * @return `true` if the root is a JSON object, `false` otherwise.
     */
    bool isObject() const { return rootValue_.isObject(); }

    /**
     * @brief Checks if the root of the JSON document is a JSON array.
     *
     * Determines whether the root value of the document is of type SwJsonArray.
     *
     * @return `true` if the root is a JSON array, `false` otherwise.
     */
    bool isArray() const { return rootValue_.isArray(); }

    /**
     * @brief Returns the current object.
     * @return The current object.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwJsonObject object() const {
        if (!rootValue_.isObject()) {
            swCError(kSwLogCategory_SwJsonDocument) << "Root is not a JSON object.";
            return SwJsonObject(); // Retourne un objet vide si ce n'est pas un objet
        }
        return rootValue_.toObject(); // Retourne l'objet JSON
    }

    /**
     * @brief Retrieves the root of the JSON document as a JSON object.
     *
     * Returns the root value of the document as a SwJsonObject.
     * If the root is not a JSON object, an empty SwJsonObject is returned,
     * and an error message is logged.
     *
     * @return The root value as a SwJsonObject. Returns an empty object if the root is not of type SwJsonObject.
     */
    SwJsonArray array() const {
        if (!rootValue_.isArray()) {
            swCError(kSwLogCategory_SwJsonDocument) << "Root is not a JSON array.";
            return SwJsonArray(); // Retourne un tableau vide si ce n'est pas un tableau
        }
        return rootValue_.toArray(); // Retourne le tableau JSON
    }

    /**
     * @brief Retrieves the root value of the JSON document as a SwJsonValue.
     *
     * Returns the root value of the document, regardless of its type (object, array, string, etc.).
     *
     * @return The root value of the document as a SwJsonValue.
     */
    SwJsonValue toJsonValue() const {
        return rootValue_; // Retourne directement la valeur racine
    }

    /**
     * @brief Finds or creates a JSON value at the specified path within the document.
     *
     * Traverses the JSON document using a path to locate a specific value.
     * - The path can include nested keys separated by '/'.
     * - If the path is not found, an error is logged, and a default invalid value is returned unless `createIfNotExist` is true.
     * - If `createIfNotExist` is true, missing keys along the path are created as JSON objects.
     *
     * @param rawPath The path to the JSON value, with nested keys separated by '/'.
     * @param createIfNotExist A flag indicating whether to create missing keys as JSON objects (default: false).
     *
     * @return A reference to the located SwJsonValue. If the path is not found and creation is not allowed, returns a default invalid value.
     *
     * @note The path supports replacing '\\' with '/' to accommodate different path formats.
     */
    SwJsonValue& find(const SwString& rawPath, bool createIfNotExist = false) {
        // Remplacer les '\\' par '/'
        SwString path = rawPath;
        path.replace("\\", "/");

        SwList<SwString> tokens = path.split('/');

        SwJsonValue* current = &rootValue_;

        for (const SwString& token : tokens) {
            if (!current->isObject()) {
                if (createIfNotExist) {
                    current->setObject(std::make_shared<SwJsonObject>()); // Convertir en objet si nécessaire
                } else {
                    swCError(kSwLogCategory_SwJsonDocument) << "Path not found: '" << token << "' - Current value is not an object.";
                    static SwJsonValue invalidValue; // Valeur par défaut pour les cas d'échec
                    return invalidValue;
                }
            }

            if (!current->toObjectPtr()->contains(token)) {
                if (createIfNotExist) {
                    (*current->toObjectPtr())[token] = SwJsonValue(std::make_shared<SwJsonObject>());
                } else {
                    swCError(kSwLogCategory_SwJsonDocument) << "Path not found: '" << token << "' - Key does not exist.";
                    static SwJsonValue invalidValue; // Valeur par défaut pour les cas d'échec
                    return invalidValue;
                }
            }

            current = &(*current->toObjectPtr())[token];
        }

        return *current;
    }

    /**
     * @brief Serializes the JSON document to a string.
     *
     * Converts the JSON document into a string representation in either compact or pretty format.
     * - Optionally applies encryption to the serialized string using the provided encryption key.
     *
     * @param format The desired JSON format, either `JsonFormat::Compact` (default) or `JsonFormat::Pretty`.
     * @param encryptionKey An optional key for encrypting the JSON string (default: empty string for no encryption).
     *
     * @return A SwString containing the serialized JSON document.
     */
    SwString toJson(JsonFormat format = JsonFormat::Compact, const SwString& encryptionKey = "") const {
        SwString result;

        generateJson(rootValue_, result, format == JsonFormat::Pretty, 0, encryptionKey);

        return result;
    }

    /**
     * @brief Creates a SwJsonDocument from a JSON string.
     *
     * Parses a JSON string and constructs a SwJsonDocument object.
     * - Optionally decrypts the JSON string using the provided decryption key before parsing.
     *
     * @param jsonString The JSON string to parse.
     * @param decryptionKey An optional key for decrypting the JSON string (default: empty string for no decryption).
     *
     * @return A SwJsonDocument constructed from the parsed JSON string. If parsing fails, an empty document is returned.
     */
    static SwJsonDocument fromJson(const std::string& jsonString, const SwString& decryptionKey = "") {
        SwJsonDocument doc;
        SwString errorMessage;
        doc.loadFromJson(jsonString, errorMessage, decryptionKey);
        return doc;
    }

        /**
     * @brief Creates a SwJsonDocument from a JSON string.
     *
     * Parses a JSON string and constructs a SwJsonDocument object.
     * - Optionally decrypts the JSON string using the provided decryption key before parsing.
     *
     * @param jsonString The JSON string to parse.
     * @param errorMessage A reference to a string that will contain an error message if parsing fails.
     * @param decryptionKey An optional key for decrypting the JSON string (default: empty string for no decryption).
     *
     * @return A SwJsonDocument constructed from the parsed JSON string. If parsing fails, an empty document is returned.
     */
    static SwJsonDocument fromJson(const std::string& jsonString, SwString& errorMessage, const SwString& decryptionKey = "") {
        SwJsonDocument doc;
        doc.loadFromJson(jsonString, errorMessage, decryptionKey);
        return doc;
    }
    /**
     * @brief Loads a JSON document from a JSON string.
     *
     * Parses a JSON string and populates the current SwJsonDocument object.
     * - Optionally decrypts the JSON string using the provided decryption key before parsing.
     * - Validates that no unexpected characters remain after parsing.
     *
     * @param jsonString The JSON string to parse.
     * @param errorMessage A reference to a string that will contain an error message if parsing fails.
     * @param decryptionKey An optional key for decrypting the JSON string (default: empty string for no decryption).
     *
     * @return `true` if the JSON string was successfully parsed and loaded, `false` otherwise.
     *
     * @note On failure, the `errorMessage` parameter provides details about the error.
     */
    bool loadFromJson(const SwString& jsonString, SwString& errorMessage, const SwString& decryptionKey = "") {
        errorMessage.clear();
        size_t index = 0;

        // Accept UTF-8 BOM (common on Windows editors / PowerShell Set-Content default).
        if (jsonString.size() >= 3 &&
            static_cast<unsigned char>(jsonString[0]) == 0xEF &&
            static_cast<unsigned char>(jsonString[1]) == 0xBB &&
            static_cast<unsigned char>(jsonString[2]) == 0xBF) {
            index = 3;
        }
        rootValue_ = parseJson(jsonString, index, decryptionKey, errorMessage);

        if (!errorMessage.isEmpty()) {
            rootValue_ = SwJsonValue();
            return false;
        }

        while (index < jsonString.size() && std::isspace(static_cast<unsigned char>(jsonString[index]))) {
            ++index;
        }
        if (index < jsonString.size()) {
            errorMessage = "Unexpected characters at the end of JSON.";
            return false;
        }
        return true;
    }



private:
    SwJsonValue rootValue_; // Racine du document

    /**
     * @brief Recursively generates a JSON string from a SwJsonValue.
     *
     * @param value The JSON value to serialize.
     * @param output The resulting JSON string.
     * @param pretty Whether to format the JSON string with indentation.
     * @param indentLevel The current level of indentation for pretty formatting.
     * @param encryptionKey Optional key to encrypt string values.
     */
    void generateJson(const SwJsonValue& value, SwString& output, bool pretty, int indentLevel, const SwString& encryptionKey = "") const {
        SwString indent = pretty ? SwString(indentLevel * 2, ' ') : SwString("");
        SwString childIndent = pretty ? SwString((indentLevel + 1) * 2, ' ') : SwString("");

        auto processValue = [&](const SwString& val) -> SwString {
            return encryptionKey.isEmpty() ? val : val.encryptAES(encryptionKey);
        };

        auto appendEscaped = [&](const SwString& raw) {
            SwString processed = processValue(raw);
            SwString escaped = SwString(SwJsonValue::escapeString(processed.toStdString()));
            output += "\"";
            output += escaped;
            output += "\"";
        };

        if (value.isString()) {
            appendEscaped(SwString(value.toString()));
        } else if (value.isBool()) {
            SwString boolStr = value.toBool() ? "true" : "false";
            output += SwString("%1").arg(processValue(boolStr));
        } else if (value.isInt()) {
            SwString intStr = SwString::number(value.toLongLong());
            output += SwString("%1").arg(processValue(intStr));
        } else if (value.isDouble()) {
            SwString doubleStr = SwString::number(value.toDouble());
            output += SwString("%1").arg(processValue(doubleStr));
        } else if (value.isNull()) {
            output += "null";
        } else if (value.isObject()) {
            SwJsonObject obj = value.toObject();
            if(!obj.isEmpty()){
                output += pretty ? "{\n" : "{";
                bool first = true;
                for (const auto& pair : obj.data()) {
                    if (!first) output += pretty ? ",\n" : ",";
                    first = false;

                    if (pretty) output += childIndent;
                    SwString escapedKey = SwString(SwJsonValue::escapeString(pair.first));
                    output += "\"";
                    output += escapedKey;
                    output += "\": ";
                    generateJson(pair.second, output, pretty, indentLevel + 1, encryptionKey);
                }
                if (pretty && !obj.data().empty()) output += SwString("\n") + indent;
                output += "}";
            } else {
                output += "{}";
            }
        } else if (value.isArray()) {
            SwJsonArray arr = value.toArray();
            if(!arr.isEmpty()){
                output += pretty ? "[\n" : "[";
                for (size_t i = 0; i < arr.data().size(); ++i) {
                    if (i > 0) output += pretty ? ",\n" : ",";
                    if (pretty) output += childIndent;
                    generateJson(arr.data()[i], output, pretty, indentLevel + 1, encryptionKey);
                }
                if (pretty && !arr.data().empty()) output += SwString("\n") + indent;
                output += "]";
            } else {
                output += "[]";
            }
        }
    }

    /**
     * @brief Parses a JSON string into a SwJsonValue.
     *
     * @param jsonString The JSON string to parse.
     * @param index A reference to the current position in the string during parsing.
     * @param decryptionKey Optional key to decrypt string values before parsing.
     * @param errorMessage Output string populated when a parsing error occurs.
     *
     * @return The parsed SwJsonValue. Returns an invalid value on error.
     */
    SwJsonValue parseJson(const SwString& jsonString,
                          size_t& index,
                          const SwString& decryptionKey,
                          SwString& errorMessage) const {
        auto skipWhitespace = [&](size_t& idx) {
            while (idx < jsonString.size() && std::isspace(static_cast<unsigned char>(jsonString[idx]))) {
                ++idx;
            }
        };

        auto tryDecryptPrimitive = [&](SwJsonValue& outValue) -> bool {
            if (decryptionKey.isEmpty()) {
                return false;
            }
            size_t startToken = index;
            size_t cursor = index;
            while (cursor < jsonString.size()) {
                char ch = jsonString[cursor];
                if (ch == ',' || ch == '}' || ch == ']' || std::isspace(static_cast<unsigned char>(ch))) {
                    break;
                }
                ++cursor;
            }
            if (cursor == startToken) {
                return false;
            }

            SwString encryptedValue = jsonString.mid(static_cast<int>(startToken), static_cast<int>(cursor - startToken));
            SwString decryptedValue;
            try {
                decryptedValue = encryptedValue.decryptAES(decryptionKey);
            } catch (const std::exception&) {
                return false;
            }

            auto interpretDecrypted = [&](const SwString& decrypted) -> SwJsonValue {
                if (decrypted == "true") return SwJsonValue(true);
                if (decrypted == "false") return SwJsonValue(false);
                if (decrypted == "null") return SwJsonValue();

                std::string decryptedStd = decrypted.toStdString();
                if (!decryptedStd.empty()) {
                    errno = 0;
                    char* endPtr = nullptr;
                    if (decryptedStd.find_first_of(".eE") != std::string::npos) {
                        double dblValue = std::strtod(decryptedStd.c_str(), &endPtr);
                        if (endPtr == decryptedStd.c_str() + decryptedStd.size() && errno == 0) {
                            return SwJsonValue(dblValue);
                        }
                    } else {
                        long long intValue = std::strtoll(decryptedStd.c_str(), &endPtr, 10);
                        if (endPtr == decryptedStd.c_str() + decryptedStd.size() && errno == 0) {
                            return SwJsonValue(static_cast<long long>(intValue));
                        }
                    }
                }
                return SwJsonValue(decryptedStd);
            };

            outValue = interpretDecrypted(decryptedValue);
            index = cursor;
            return true;
        };

        skipWhitespace(index);
        if (index >= jsonString.size()) {
            reportError(errorMessage, jsonString, index, "Unexpected end of JSON input");
            return SwJsonValue();
        }

        char c = jsonString[index];
        if (c == '{') {
            return parseObject(jsonString, index, decryptionKey, errorMessage);
        }
        if (c == '[') {
            return parseArray(jsonString, index, decryptionKey, errorMessage);
        }
        if (c == '\"') {
            SwString value = parseString(jsonString, index, errorMessage);
            if (!errorMessage.isEmpty()) return SwJsonValue();

            if (!decryptionKey.isEmpty()) {
                try {
                    value = value.decryptAES(decryptionKey);
                } catch (const std::exception&) {
                    reportError(errorMessage, jsonString, index, "Unable to decrypt string value");
                    return SwJsonValue();
                }
            }

            if (value == "true") return SwJsonValue(true);
            if (value == "false") return SwJsonValue(false);
            if (value == "null") return SwJsonValue();
            if (value.isInt()) return SwJsonValue(value.toLongLong());
            if (value.isFloat()) return SwJsonValue(value.toFloat());
            return SwJsonValue(value.toStdString());
        }

        if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == 't' || c == 'f' || c == 'n' || !decryptionKey.isEmpty()) {
            SwJsonValue decryptedValue;
            if (tryDecryptPrimitive(decryptedValue)) {
                return decryptedValue;
            }

            if (c == 't' || c == 'f') {
                SwString literal = parseLiteral(jsonString, index);
                if (literal == "true") return SwJsonValue(true);
                if (literal == "false") return SwJsonValue(false);
                reportError(errorMessage, jsonString, index, "Invalid boolean literal");
                return SwJsonValue();
            }

            if (c == 'n') {
                SwString literal = parseLiteral(jsonString, index);
                if (literal == "null") return SwJsonValue();
                reportError(errorMessage, jsonString, index, "Invalid literal, expected 'null'");
                return SwJsonValue();
            }

            return parseNumber(jsonString, index, errorMessage);
        }

        reportError(errorMessage, jsonString, index, "Invalid JSON token");
        return SwJsonValue();
    }

    /**
     * @brief Parses a JSON object from a JSON string.
     *
     * @param jsonString The JSON string to parse.
     * @param index A reference to the current position in the string during parsing.
     * @param decryptionKey Optional key to decrypt string values before parsing.
     * @param errorMessage Output string populated when a parsing error occurs.
     *
     * @return The parsed SwJsonObject wrapped in a SwJsonValue. Returns an invalid value on error.
     */
    SwJsonValue parseObject(const SwString& jsonString,
                            size_t& index,
                            const SwString& decryptionKey,
                            SwString& errorMessage) const {
        SwJsonObject object;
        ++index;

        while (true) {
            while (index < jsonString.size() && std::isspace(static_cast<unsigned char>(jsonString[index]))) ++index;
            if (index >= jsonString.size()) {
                reportError(errorMessage, jsonString, index, "Unterminated JSON object");
                return SwJsonValue();
            }

            if (jsonString[index] == '}') {
                ++index;
                break;
            }

            if (jsonString[index] != '\"') {
                reportError(errorMessage, jsonString, index, "Expected string key in object");
                return SwJsonValue();
            }

            SwString key = parseString(jsonString, index, errorMessage);
            if (!errorMessage.isEmpty()) return SwJsonValue();

            while (index < jsonString.size() && std::isspace(static_cast<unsigned char>(jsonString[index]))) ++index;
            if (index >= jsonString.size() || jsonString[index] != ':') {
                reportError(errorMessage, jsonString, index, "Expected ':' after object key");
                return SwJsonValue();
            }
            ++index;

            SwJsonValue value = parseJson(jsonString, index, decryptionKey, errorMessage);
            if (!errorMessage.isEmpty()) return SwJsonValue();
            object.insert(key.toStdString(), value);

            while (index < jsonString.size() && std::isspace(static_cast<unsigned char>(jsonString[index]))) ++index;
            if (index >= jsonString.size()) {
                reportError(errorMessage, jsonString, index, "Unterminated JSON object");
                return SwJsonValue();
            }

            if (jsonString[index] == ',') {
                ++index;
                continue;
            }
            if (jsonString[index] == '}') {
                ++index;
                break;
            }

            reportError(errorMessage, jsonString, index, "Expected ',' or '}' in object");
            return SwJsonValue();
        }

        return SwJsonValue(object);
    }

    /**
     * @brief Parses a JSON array from a JSON string.
     *
     * @param jsonString The JSON string to parse.
     * @param index A reference to the current position in the string during parsing.
     * @param decryptionKey Optional key to decrypt string values before parsing.
     * @param errorMessage Output string populated when a parsing error occurs.
     *
     * @return The parsed SwJsonArray wrapped in a SwJsonValue. Returns an invalid value on error.
     */
    SwJsonValue parseArray(const SwString& jsonString,
                           size_t& index,
                           const SwString& decryptionKey,
                           SwString& errorMessage) const {
        SwJsonArray array;
        ++index;

        while (true) {
            while (index < jsonString.size() && std::isspace(static_cast<unsigned char>(jsonString[index]))) ++index;
            if (index >= jsonString.size()) {
                reportError(errorMessage, jsonString, index, "Unterminated JSON array");
                return SwJsonValue();
            }

            if (jsonString[index] == ']') {
                ++index;
                break;
            }

            array.append(parseJson(jsonString, index, decryptionKey, errorMessage));
            if (!errorMessage.isEmpty()) return SwJsonValue();

            while (index < jsonString.size() && std::isspace(static_cast<unsigned char>(jsonString[index]))) ++index;
            if (index >= jsonString.size()) {
                reportError(errorMessage, jsonString, index, "Unterminated JSON array");
                return SwJsonValue();
            }

            if (jsonString[index] == ',') {
                ++index;
                continue;
            }
            if (jsonString[index] == ']') {
                ++index;
                break;
            }

            reportError(errorMessage, jsonString, index, "Expected ',' or ']' in array");
            return SwJsonValue();
        }

        return SwJsonValue(array);
    }

    /**
     * @brief Parses a JSON string from a JSON input.
     *
     * @param jsonString The JSON string to parse.
     * @param index A reference to the current position in the string during parsing.
     * @param errorMessage Output string populated when a parsing error occurs.
     *
     * @return The parsed SwString. Returns an empty string on error.
     */
    SwString parseString(const SwString& jsonString, size_t& index, SwString& errorMessage) const {
        ++index;
        std::string result;

        while (index < jsonString.size()) {
            char c = jsonString[index];
            if (c == '\"') {
                ++index;
                return SwString(result);
            }

            if (c == '\\') {
                ++index;
                if (index >= jsonString.size()) {
                    reportError(errorMessage, jsonString, index, "Invalid escape sequence in string");
                    return SwString();
                }
                char escapeChar = jsonString[index];
                switch (escapeChar) {
                case '\"': result.push_back('\"'); ++index; break;
                case '\\': result.push_back('\\'); ++index; break;
                case '/':  result.push_back('/'); ++index; break;
                case 'b':  result.push_back('\b'); ++index; break;
                case 'f':  result.push_back('\f'); ++index; break;
                case 'n':  result.push_back('\n'); ++index; break;
                case 'r':  result.push_back('\r'); ++index; break;
                case 't':  result.push_back('\t'); ++index; break;
                case 'u': {
                    ++index;
                    std::uint32_t codePoint = parseUnicodeEscapeSequence(jsonString, index, errorMessage);
                    if (!errorMessage.isEmpty()) return SwString();

                    if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
                        if (index + 1 >= jsonString.size() || jsonString[index] != '\\' || jsonString[index + 1] != 'u') {
                            reportError(errorMessage, jsonString, index, "Missing low surrogate after high surrogate");
                            return SwString();
                        }
                        index += 2;
                        std::uint32_t low = parseUnicodeEscapeSequence(jsonString, index, errorMessage);
                        if (!errorMessage.isEmpty()) return SwString();
                        if (low < 0xDC00 || low > 0xDFFF) {
                            reportError(errorMessage, jsonString, index, "Invalid low surrogate in Unicode escape");
                            return SwString();
                        }
                        codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                    } else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
                        reportError(errorMessage, jsonString, index, "Unexpected low surrogate without leading high surrogate");
                        return SwString();
                    }

                    if (!appendUtf8FromCodePoint(codePoint, result, errorMessage, index, jsonString)) {
                        return SwString();
                    }
                    continue;
                }
                default:
                    reportError(errorMessage, jsonString, index, "Invalid escape character in string");
                    return SwString();
                }
                continue;
            }

            if (static_cast<unsigned char>(c) < 0x20) {
                reportError(errorMessage, jsonString, index, "Unescaped control character in string");
                return SwString();
            }

            result.push_back(c);
            ++index;
        }

        reportError(errorMessage, jsonString, index, "Unterminated string literal");
        return SwString();
    }

    /**
     * @brief Parses a JSON number from a JSON string.
     *
     * @param jsonString The JSON string to parse.
     * @param index A reference to the current position in the string during parsing.
     * @param errorMessage Output string populated when a parsing error occurs.
     *
     * @return The parsed SwJsonValue containing a number. Returns an invalid value on error.
     */
    SwJsonValue parseNumber(const SwString& jsonString, size_t& index, SwString& errorMessage) const {
        size_t startNumber = index;
        auto at = [&](size_t pos) -> char {
            return pos < jsonString.size() ? jsonString[pos] : '\0';
        };

        if (at(index) == '-') {
            ++index;
        }

        if (!std::isdigit(static_cast<unsigned char>(at(index)))) {
            reportError(errorMessage, jsonString, index, "Expected digit in number literal");
            return SwJsonValue();
        }

        bool leadingZero = (at(index) == '0');
        ++index;
        if (leadingZero && std::isdigit(static_cast<unsigned char>(at(index)))) {
            reportError(errorMessage, jsonString, index, "Leading zeros are not allowed in JSON numbers");
            return SwJsonValue();
        }
        while (std::isdigit(static_cast<unsigned char>(at(index)))) {
            ++index;
        }

        bool isFloating = false;
        if (at(index) == '.') {
            isFloating = true;
            ++index;
            if (!std::isdigit(static_cast<unsigned char>(at(index)))) {
                reportError(errorMessage, jsonString, index, "Expected digit after decimal point");
                return SwJsonValue();
            }
            while (std::isdigit(static_cast<unsigned char>(at(index)))) {
                ++index;
            }
        }

        if (at(index) == 'e' || at(index) == 'E') {
            isFloating = true;
            ++index;
            if (at(index) == '+' || at(index) == '-') {
                ++index;
            }
            if (!std::isdigit(static_cast<unsigned char>(at(index)))) {
                reportError(errorMessage, jsonString, index, "Expected digit in exponent");
                return SwJsonValue();
            }
            while (std::isdigit(static_cast<unsigned char>(at(index)))) {
                ++index;
            }
        }

        const size_t span = index - startNumber;
        const int startIdx = startNumber <= static_cast<size_t>(std::numeric_limits<int>::max())
                                 ? static_cast<int>(startNumber)
                                 : std::numeric_limits<int>::max();
        const int length = span <= static_cast<size_t>(std::numeric_limits<int>::max())
                               ? static_cast<int>(span)
                               : std::numeric_limits<int>::max();
        SwString lexeme = jsonString.mid(startIdx, length);
        std::string numberStd = lexeme.toStdString();
        errno = 0;

        if (isFloating) {
            char* endPtr = nullptr;
            double value = std::strtod(numberStd.c_str(), &endPtr);
            if (endPtr != numberStd.c_str() + numberStd.size() || errno == ERANGE) {
                reportError(errorMessage, jsonString, startNumber, "Invalid floating-point number");
                return SwJsonValue();
            }
            return SwJsonValue(value);
        }

        char* endPtr = nullptr;
        long long value = std::strtoll(numberStd.c_str(), &endPtr, 10);
        if (endPtr != numberStd.c_str() + numberStd.size() || errno == ERANGE) {
            reportError(errorMessage, jsonString, startNumber, "Invalid integer number");
            return SwJsonValue();
        }
        return SwJsonValue(static_cast<long long>(value));
    }

    /**
     * @brief Parses a JSON literal (e.g., "true", "false", "null") from a JSON string.
     *
     * @param jsonString The JSON string to parse.
     * @param index A reference to the current position in the string during parsing.
     *
     * @return The parsed SwString containing the literal value.
     */
    SwString parseLiteral(const SwString& jsonString, size_t& index) const {
        size_t start = index;
        while (index < jsonString.size() && std::isalpha(static_cast<int>(jsonString[index]))) {
            ++index;
        }
        const size_t span = index - start;
        const int startIdx = start <= static_cast<size_t>(std::numeric_limits<int>::max())
                                 ? static_cast<int>(start)
                                 : std::numeric_limits<int>::max();
        const int length = span <= static_cast<size_t>(std::numeric_limits<int>::max())
                               ? static_cast<int>(span)
                               : std::numeric_limits<int>::max();
        return jsonString.mid(startIdx, length);
    }

    std::uint32_t parseUnicodeEscapeSequence(const SwString& jsonString, size_t& index, SwString& errorMessage) const {
        if (index + 4 > jsonString.size()) {
            reportError(errorMessage, jsonString, index, "Incomplete Unicode escape sequence");
            return 0;
        }

        auto hexValue = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
            if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
            return -1;
        };

        std::uint32_t codeUnit = 0;
        for (int i = 0; i < 4; ++i) {
            int value = hexValue(jsonString[index + i]);
            if (value < 0) {
                reportError(errorMessage, jsonString, index + i, "Invalid hex digit in Unicode escape");
                return 0;
            }
            codeUnit = (codeUnit << 4) | static_cast<std::uint32_t>(value);
        }
        index += 4;
        return codeUnit;
    }

    bool appendUtf8FromCodePoint(std::uint32_t codePoint,
                                 std::string& output,
                                 SwString& errorMessage,
                                 size_t errorIndex,
                                 const SwString& jsonString) const {
        if (codePoint > 0x10FFFF) {
            reportError(errorMessage, jsonString, errorIndex, "Unicode code point out of range");
            return false;
        }

        if (codePoint <= 0x7F) {
            output.push_back(static_cast<char>(codePoint));
        } else if (codePoint <= 0x7FF) {
            output.push_back(static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else if (codePoint <= 0xFFFF) {
            output.push_back(static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else {
            output.push_back(static_cast<char>(0xF0 | ((codePoint >> 18) & 0x07)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        return true;
    }

    void reportError(SwString& errorMessage,
                     const SwString& jsonString,
                     size_t errorIndex,
                     const std::string& message) const {
        if (!errorMessage.isEmpty()) {
            return;
        }
        std::ostringstream os;
        os << message << " (index " << errorIndex << ")";
        size_t startContext = errorIndex > 20 ? errorIndex - 20 : 0;
        size_t endContext = (std::min)(jsonString.size(), errorIndex + 20);
        const size_t contextLen = endContext - startContext;
        const int ctxStart = startContext <= static_cast<size_t>(std::numeric_limits<int>::max())
                                 ? static_cast<int>(startContext)
                                 : std::numeric_limits<int>::max();
        const int ctxLen = contextLen <= static_cast<size_t>(std::numeric_limits<int>::max())
                               ? static_cast<int>(contextLen)
                               : std::numeric_limits<int>::max();
        os << " near \""
           << jsonString.mid(ctxStart, ctxLen).toStdString()
           << "\"";
        errorMessage = os.str();
    }
};

inline bool operator==(const SwJsonDocument& lhs, const SwJsonDocument& rhs) {
    return lhs.toJson() == rhs.toJson();
}

inline bool operator!=(const SwJsonDocument& lhs, const SwJsonDocument& rhs) {
    return !(lhs == rhs);
}
