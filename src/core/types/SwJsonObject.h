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

#include "SwJsonValue.h"
#include "SwString.h"

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

        Iterator() = default;
        explicit Iterator(Container::iterator it) : it_(it) {}

        Iterator& operator++() { ++it_; return *this; }
        Iterator operator++(int) { Iterator tmp(*this); ++(*this); return tmp; }
        Iterator& operator--() { --it_; return *this; }
        Iterator operator--(int) { Iterator tmp(*this); --(*this); return tmp; }

        bool operator==(const Iterator& other) const { return it_ == other.it_; }
        bool operator!=(const Iterator& other) const { return it_ != other.it_; }

        SwString key() const { return SwString(it_->first); }
        SwJsonValue& value() const { return it_->second; }

        reference operator*() const { return it_->second; }
        pointer operator->() const { return &it_->second; }

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

        ConstIterator() = default;
        explicit ConstIterator(Container::const_iterator it) : it_(it) {}
        ConstIterator(const Iterator& other) : it_(other.it_) {}

        ConstIterator& operator++() { ++it_; return *this; }
        ConstIterator operator++(int) { ConstIterator tmp(*this); ++(*this); return tmp; }
        ConstIterator& operator--() { --it_; return *this; }
        ConstIterator operator--(int) { ConstIterator tmp(*this); --(*this); return tmp; }

        bool operator==(const ConstIterator& other) const { return it_ == other.it_; }
        bool operator!=(const ConstIterator& other) const { return it_ != other.it_; }

        SwString key() const { return SwString(it_->first); }
        const SwJsonValue& value() const { return it_->second; }

        reference operator*() const { return it_->second; }
        pointer operator->() const { return &it_->second; }

        Container::const_iterator base() const { return it_; }

    private:
        Container::const_iterator it_{};
    };

    /**
     * @brief Default constructor for SwJsonObject.
     */
    SwJsonObject() = default;
    SwJsonObject(const std::shared_ptr<SwJsonObject>& other) {
        if (other) {
            data_ = other->data_;
        }
    }

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
    inline SwJsonValue& operator[](const char* key) {
        return data_[ key ? std::string(key) : std::string() ];
    }
    inline const SwJsonValue& operator[](const char* key) const {
        const std::string k = key ? std::string(key) : std::string();
        auto it = data_.find(k);
        static const SwJsonValue nullValue;
        return (it != data_.end()) ? it->second : nullValue;
    }

    // std::string
    inline SwJsonValue& operator[](const std::string& key) {
        return data_[key];
    }
    inline const SwJsonValue& operator[](const std::string& key) const {
        auto it = data_.find(key);
        static const SwJsonValue nullValue;
        return (it != data_.end()) ? it->second : nullValue;
    }

    // SwString
    inline SwJsonValue& operator[](const SwString& key) {
        return data_[ key.toStdString() ];
    }
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
    bool contains(const char* key) const {
        return contains(key ? std::string(key) : std::string());
    }
    bool contains(const SwString& key) const {
        return contains(key.toStdString());
    }

    /**
     * @brief Inserts or updates a key-value pair in the JSON object.
     *
     * @param key The key to insert or update.
     * @param value The value to associate with the key.
     */
    void insert(const std::string& key, const SwJsonValue& value) {
        data_[key] = value;
    }

    /**
     * @brief Removes a key-value pair from the JSON object.
     *
     * @param key The key to remove.
     * @return true if the key was successfully removed, false otherwise.
     */
    bool remove(const std::string& key) {
        return data_.erase(key) > 0;
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

    SwJsonValue value(const std::string& key, const SwJsonValue& defaultValue = SwJsonValue()) const {
        auto it = data_.find(key);
        return (it != data_.end()) ? it->second : defaultValue;
    }
    SwJsonValue value(const char* key, const SwJsonValue& defaultValue = SwJsonValue()) const {
        return value(key ? std::string(key) : std::string(), defaultValue);
    }
    SwJsonValue value(const SwString& key, const SwJsonValue& defaultValue = SwJsonValue()) const {
        return value(key.toStdString(), defaultValue);
    }

    /**
     * @brief Retrieves a list of all keys in the JSON object.
     *
     * @return A vector containing all keys.
     */
    std::vector<std::string> keys() const {
        std::vector<std::string> keyList;
        for (const auto& pair : data_) {
            keyList.push_back(pair.first);
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

    Iterator begin() { return Iterator(data_.begin()); }
    Iterator end() { return Iterator(data_.end()); }
    ConstIterator begin() const { return ConstIterator(data_.cbegin()); }
    ConstIterator end() const { return ConstIterator(data_.cend()); }
    ConstIterator cbegin() const { return ConstIterator(data_.cbegin()); }
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
            if (pair.second.isObject() && pair.second.toObject()) {
                os << pair.second.toObject()->toJsonString(compact, indentLevel + 1);
            } else if (pair.second.isArray() && pair.second.toArray()) {
                os << "[SwJsonValue(Array)]";
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


