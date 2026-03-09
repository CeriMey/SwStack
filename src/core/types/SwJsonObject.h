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
 * @file src/core/types/SwJsonObject.h
 * @ingroup core_types
 * @brief Declares the public interface exposed by SwJsonObject in the CoreSw fundamental types
 * layer.
 *
 * This header belongs to the CoreSw fundamental types layer. It provides value types, containers,
 * text and binary helpers, and lightweight serialization primitives shared across the stack.
 *
 * Within that layer, this file focuses on the JSON object interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwJsonObject.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Interfaces in this area are intentionally reused by runtime, IO, GUI, remote, and media modules
 * to keep cross-module semantics consistent.
 *
 */


#include "SwJsonValue.h"
#include "SwJsonArray.h"
#include "SwString.h"
#include "SwList.h"

#include <map>
#include <memory>
#include <string>
#include <iterator>
/**
 * @class SwJsonObject
 * @brief Represents a JSON object with key-value pairs.
 */
class SwJsonObject {
public:
    using Container = std::map<std::string, SwJsonValue>;

    class ConstIterator;
    class Iterator {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = SwJsonValue;
        using difference_type = std::ptrdiff_t;
        using pointer = SwJsonValue*;
        using reference = SwJsonValue&;

        /**
         * @brief Constructs a `Iterator` instance.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        Iterator() = default;
        /**
         * @brief Constructs a `Iterator` instance.
         * @param it Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        explicit Iterator(Container::iterator it) : it_(it) {}

        /**
         * @brief Returns the current operator ++.
         * @return The current operator ++.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        Iterator& operator++() { ++it_; return *this; }
        /**
         * @brief Performs the `operator++` operation.
         * @param this Value passed to the method.
         * @return The requested operator ++.
         */
        Iterator operator++(int) { Iterator tmp(*this); ++(*this); return tmp; }
        /**
         * @brief Returns the current operator --.
         * @return The current operator --.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        Iterator& operator--() { --it_; return *this; }
        /**
         * @brief Performs the `operator--` operation.
         * @param this Value passed to the method.
         * @return The requested operator --.
         */
        Iterator operator--(int) { Iterator tmp(*this); --(*this); return tmp; }

        /**
         * @brief Performs the `operator==` operation.
         * @param other Value passed to the method.
         * @return `true` on success; otherwise `false`.
         */
        bool operator==(const Iterator& other) const { return it_ == other.it_; }
        /**
         * @brief Performs the `operator!=` operation.
         * @param other Value passed to the method.
         * @return `true` on success; otherwise `false`.
         */
        bool operator!=(const Iterator& other) const { return it_ != other.it_; }

        /**
         * @brief Performs the `key` operation.
         * @param first Value passed to the method.
         * @return The requested key.
         */
        SwString key() const { return SwString(it_->first); }
        /**
         * @brief Returns the current value.
         * @return The current value.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        SwJsonValue& value() const { return it_->second; }

        /**
         * @brief Returns the current operator *.
         * @return The current operator *.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        reference operator*() const { return it_->second; }
        /**
         * @brief Returns the current operator ->.
         * @return The current operator ->.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        pointer operator->() const { return &it_->second; }

        /**
         * @brief Returns the current base.
         * @return The current base.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        Container::iterator base() const { return it_; }

    private:
        Container::iterator it_{};
        friend class ConstIterator;
    };

    class ConstIterator {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = SwJsonValue;
        using difference_type = std::ptrdiff_t;
        using pointer = const SwJsonValue*;
        using reference = const SwJsonValue&;

        /**
         * @brief Constructs a `ConstIterator` instance.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        ConstIterator() = default;
        /**
         * @brief Constructs a `ConstIterator` instance.
         * @param it Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        explicit ConstIterator(Container::const_iterator it) : it_(it) {}
        /**
         * @brief Constructs a `ConstIterator` instance.
         * @param it_ Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        ConstIterator(const Iterator& other) : it_(other.it_) {}

        /**
         * @brief Returns the current operator ++.
         * @return The current operator ++.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        ConstIterator& operator++() { ++it_; return *this; }
        /**
         * @brief Performs the `operator++` operation.
         * @param this Value passed to the method.
         * @return The requested operator ++.
         */
        ConstIterator operator++(int) { ConstIterator tmp(*this); ++(*this); return tmp; }
        /**
         * @brief Returns the current operator --.
         * @return The current operator --.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        ConstIterator& operator--() { --it_; return *this; }
        /**
         * @brief Performs the `operator--` operation.
         * @param this Value passed to the method.
         * @return The requested operator --.
         */
        ConstIterator operator--(int) { ConstIterator tmp(*this); --(*this); return tmp; }

        /**
         * @brief Performs the `operator==` operation.
         * @param other Value passed to the method.
         * @return `true` on success; otherwise `false`.
         */
        bool operator==(const ConstIterator& other) const { return it_ == other.it_; }
        /**
         * @brief Performs the `operator!=` operation.
         * @param other Value passed to the method.
         * @return `true` on success; otherwise `false`.
         */
        bool operator!=(const ConstIterator& other) const { return it_ != other.it_; }

        /**
         * @brief Performs the `key` operation.
         * @param first Value passed to the method.
         * @return The requested key.
         */
        SwString key() const { return SwString(it_->first); }
        /**
         * @brief Returns the current value.
         * @return The current value.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        const SwJsonValue& value() const { return it_->second; }

        /**
         * @brief Returns the current operator *.
         * @return The current operator *.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        reference operator*() const { return it_->second; }
        /**
         * @brief Returns the current operator ->.
         * @return The current operator ->.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        pointer operator->() const { return &it_->second; }

        /**
         * @brief Returns the current base.
         * @return The current base.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        Container::const_iterator base() const { return it_; }

    private:
        Container::const_iterator it_{};
    };

    /**
     * @brief Default constructor for SwJsonObject.
     */
    SwJsonObject() = default;
    /**
     * @brief Constructs a `SwJsonObject` instance.
     * @param other Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwJsonObject(const std::shared_ptr<SwJsonObject>& other) {
        if (other) {
            data_ = other->data_;
        }
    }

    /**
     * @brief Performs the `operator=` operation.
     * @param other Value passed to the method.
     * @return The requested operator =.
     */
    SwJsonObject& operator=(const std::shared_ptr<SwJsonObject>& other) {
        if (other) {
            data_ = other->data_;
        } else {
            data_.clear();
        }
        return *this;
    }

    /**
     * @brief Provides mutable access to the value associated with the given key.
     *
     * @param key The key to access.
     * @return A reference to the SwJsonValue associated with the key.
     */
    // const char*
    /**
     * @brief Performs the `operator[]` operation.
     * @param key Value passed to the method.
     * @return The requested operator [].
     */
    inline SwJsonValue& operator[](const char* key) {
        return data_[ key ? std::string(key) : std::string() ];
    }
    /**
     * @brief Performs the `operator[]` operation.
     * @param key Value passed to the method.
     * @return The requested operator [].
     */
    inline const SwJsonValue& operator[](const char* key) const {
        const std::string k = key ? std::string(key) : std::string();
        auto it = data_.find(k);
        static const SwJsonValue nullValue;
        return (it != data_.end()) ? it->second : nullValue;
    }

    // std::string
    /**
     * @brief Performs the `operator[]` operation.
     * @param key Value passed to the method.
     * @return The requested operator [].
     */
    inline SwJsonValue& operator[](const std::string& key) {
        return data_[key];
    }
    /**
     * @brief Performs the `operator[]` operation.
     * @param key Value passed to the method.
     * @return The requested operator [].
     */
    inline const SwJsonValue& operator[](const std::string& key) const {
        auto it = data_.find(key);
        static const SwJsonValue nullValue;
        return (it != data_.end()) ? it->second : nullValue;
    }

    // SwString
    /**
     * @brief Performs the `operator[]` operation.
     * @param key Value passed to the method.
     * @return The requested operator [].
     */
    inline SwJsonValue& operator[](const SwString& key) {
        return data_[ key.toStdString() ];
    }
    /**
     * @brief Performs the `operator[]` operation.
     * @param key Value passed to the method.
     * @return The requested operator [].
     */
    inline const SwJsonValue& operator[](const SwString& key) const {
        auto it = data_.find(key.toStdString());
        static const SwJsonValue nullValue;
        return (it != data_.end()) ? it->second : nullValue;
    }

    /**
     * @brief Checks if the object contains the specified key.
     *
     * @param key The key to check.
     * @return true if the key exists, false otherwise.
     */
    bool contains(const std::string& key) const {
        return data_.find(key) != data_.end();
    }
    /**
     * @brief Performs the `contains` operation.
     * @param key Value passed to the method.
     * @return `true` when the object reports contains; otherwise `false`.
     */
    bool contains(const char* key) const {
        return contains(key ? std::string(key) : std::string());
    }
    /**
     * @brief Performs the `contains` operation.
     * @param key Value passed to the method.
     * @return `true` when the object reports contains; otherwise `false`.
     */
    bool contains(const SwString& key) const {
        return contains(key.toStdString());
    }

    /**
     * @brief Inserts or updates a key-value pair in the JSON object.
     *
     * @param key The key to insert or update.
     * @param value The value to associate with the key.
     */
    Iterator insert(const std::string& key, const SwJsonValue& value) {
        data_[key] = value;
        return Iterator(data_.find(key));
    }
    /**
     * @brief Performs the `insert` operation.
     * @param key Value passed to the method.
     * @param value Value passed to the method.
     * @return The requested insert.
     */
    Iterator insert(const char* key, const SwJsonValue& value) {
        return insert(key ? std::string(key) : std::string(), value);
    }
    /**
     * @brief Performs the `insert` operation.
     * @param key Value passed to the method.
     * @param value Value passed to the method.
     * @return The requested insert.
     */
    Iterator insert(const SwString& key, const SwJsonValue& value) {
        return insert(key.toStdString(), value);
    }

    /**
     * @brief Removes a key-value pair from the JSON object.
     *
     * @param key The key to remove.
     * @return true if the key was successfully removed, false otherwise.
     */
    void remove(const std::string& key) {
        data_.erase(key);
    }
    /**
     * @brief Removes the specified remove.
     * @param key Value passed to the method.
     */
    void remove(const char* key) {
        data_.erase(key ? std::string(key) : std::string());
    }
    /**
     * @brief Removes the specified remove.
     * @param key Value passed to the method.
     */
    void remove(const SwString& key) {
        data_.erase(key.toStdString());
    }

    /**
     * @brief Retrieves the number of key-value pairs in the JSON object.
     *
     * @return The number of key-value pairs.
     */
    size_t size() const {
        return data_.size();
    }

    /**
     * @brief Checks if the JSON object is empty.
     *
     * @return true if the object is empty, false otherwise.
     */
    bool isEmpty() const {
        return data_.empty();
    }

    /**
     * @brief Performs the `value` operation.
     * @param key Value passed to the method.
     * @param defaultValue Value passed to the method.
     * @return The requested value.
     */
    SwJsonValue value(const std::string& key, const SwJsonValue& defaultValue = SwJsonValue()) const {
        auto it = data_.find(key);
        return (it != data_.end()) ? it->second : defaultValue;
    }
    /**
     * @brief Performs the `value` operation.
     * @param key Value passed to the method.
     * @param defaultValue Value passed to the method.
     * @return The requested value.
     */
    SwJsonValue value(const char* key, const SwJsonValue& defaultValue = SwJsonValue()) const {
        return value(key ? std::string(key) : std::string(), defaultValue);
    }
    /**
     * @brief Performs the `value` operation.
     * @param key Value passed to the method.
     * @param defaultValue Value passed to the method.
     * @return The requested value.
     */
    SwJsonValue value(const SwString& key, const SwJsonValue& defaultValue = SwJsonValue()) const {
        return value(key.toStdString(), defaultValue);
    }

    /**
     * @brief Retrieves a list of all keys in the JSON object.
     *
     * @return A vector containing all keys.
     */
    SwList<SwString> keys() const {
        SwList<SwString> keyList;
        for (const auto& pair : data_) {
            keyList.append(SwString(pair.first));
        }
        return keyList;
    }

    /**
     * @brief Retrieves a list of all values in the JSON object.
     *
     * @return A vector containing all values as SwJsonValue.
     */
    std::vector<SwJsonValue> values() const {
        std::vector<SwJsonValue> valueList;
        for (const auto& pair : data_) {
            valueList.push_back(pair.second);
        }
        return valueList;
    }

    /**
     * @brief Compares two JSON objects for equality.
     *
     * @param other The other JSON object to compare.
     * @return true if the two objects are equal, false otherwise.
     */
    bool operator==(const SwJsonObject& other) const {
        return data_ == other.data_;
    }

    /**
     * @brief Compares two JSON objects for inequality.
     *
     * @param other The other JSON object to compare.
     * @return true if the two objects are not equal, false otherwise.
     */
    bool operator!=(const SwJsonObject& other) const {
        return !(*this == other);
    }

    /**
     * @brief Performs the `begin` operation.
     * @return The requested begin.
     */
    Iterator begin() { return Iterator(data_.begin()); }
    /**
     * @brief Performs the `end` operation.
     * @return The requested end.
     */
    Iterator end() { return Iterator(data_.end()); }
    /**
     * @brief Performs the `begin` operation.
     * @return The requested begin.
     */
    ConstIterator begin() const { return ConstIterator(data_.cbegin()); }
    /**
     * @brief Performs the `end` operation.
     * @return The requested end.
     */
    ConstIterator end() const { return ConstIterator(data_.cend()); }
    /**
     * @brief Performs the `cbegin` operation.
     * @return The requested cbegin.
     */
    ConstIterator cbegin() const { return ConstIterator(data_.cbegin()); }
    /**
     * @brief Performs the `cend` operation.
     * @return The requested cend.
     */
    ConstIterator cend() const { return ConstIterator(data_.cend()); }

    /**
     * @brief Converts the JSON object to a JSON-formatted string.
     *
     * @param compact If true, produces a compact JSON string. Otherwise, formats with indentation.
     * @param indentLevel The current level of indentation for nested objects.
     * @return A JSON-formatted string representation of the object.
     */
    std::string toJsonString(bool compact = true, int indentLevel = 0) const {
        std::ostringstream os;
        std::string indent(indentLevel * 2, ' '); // Indentation basée sur le niveau actuel
        std::string childIndent((indentLevel + 1) * 2, ' '); // Indentation pour les enfants

        os << "{";

        bool first = true;
        for (const auto& pair : data_) {
            if (!first) os << (compact ? "," : ",\n"); // Ajouter une virgule entre les éléments
            first = false;

            if (!compact) os << "\n" << childIndent; // Indenter pour chaque clé

            // Ajouter la clé
            os << "\"" << pair.first << "\": ";

            // Ajouter la valeur
            if (pair.second.isObject()) {
                os << pair.second.toObject().toJsonString(compact, indentLevel + 1);
            } else if (pair.second.isArray()) {
                os << pair.second.toArray().toJsonString(compact, indentLevel + 1);
            } else {
                os << pair.second.toJsonString();
            }
        }

        if (!compact && !data_.empty()) os << "\n" << indent;
        os << "}";
        return os.str();
    }

    /**
     * @brief Retrieves the underlying data as a map of key-value pairs.
     *
     * @return A map of strings to SwJsonValue objects.
     */
    inline Container data() const
    {
        return data_;
    }

private:
    Container data_; ///< Stores key-value pairs in the JSON object.
};

// Deferred inline implementations of SwJsonValue::toObject() and toArray()
// placed here because both SwJsonObject and SwJsonArray must be fully defined first.

inline SwJsonObject SwJsonValue::toObject() const {
    if (type_ == Type::Object && objectValue_) return *objectValue_;
    return SwJsonObject();
}

inline SwJsonArray SwJsonValue::toArray() const {
    if (type_ == Type::Array && arrayValue_) return *arrayValue_;
    return SwJsonArray();
}
