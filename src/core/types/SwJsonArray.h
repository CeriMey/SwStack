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

#include <memory>
#include <vector>
#include <string>
#include <sstream>

/**
 * @class SwJsonArray
 * @brief Represents a JSON array capable of holding multiple SwJsonValue elements.
 */
class SwJsonArray {
public:

    /**
     * @brief Default constructor, initializes an empty JSON array.
     */
    SwJsonArray() = default;
    SwJsonArray(const std::shared_ptr<SwJsonArray>& other) {
        if (other) {
            data_ = other->data_;
        }
    }

    SwJsonArray& operator=(const std::shared_ptr<SwJsonArray>& other) {
        if (other) {
            data_ = other->data_;
        } else {
            data_.clear();
        }
        return *this;
    }

    using iterator = std::vector<SwJsonValue>::iterator;
    using const_iterator = std::vector<SwJsonValue>::const_iterator;

    iterator begin() { return data_.begin(); }
    iterator end() { return data_.end(); }
    const_iterator begin() const { return data_.begin(); }
    const_iterator end() const { return data_.end(); }
    const_iterator cbegin() const { return data_.cbegin(); }
    const_iterator cend() const { return data_.cend(); }

    /**
     * @brief Accesses an element by its index.
     * @param index The position of the element to access.
     * @return A reference to the SwJsonValue at the specified index.
     * @throw std::out_of_range If the index is out of bounds.
     */
    SwJsonValue& operator[](size_t index) {
        if (index >= data_.size()) {
            throw std::out_of_range("Index out of bounds");
        }
        return data_[index];
    }

    /**
     * @brief Accesses a constant element by its index.
     * @param index The position of the element to access.
     * @return A constant reference to the SwJsonValue at the specified index.
     * @throw std::out_of_range If the index is out of bounds.
     */
    const SwJsonValue& operator[](size_t index) const {
        if (index >= data_.size()) {
            throw std::out_of_range("Index out of bounds");
        }
        return data_[index];
    }

    /**
     * @brief Appends a value to the end of the array.
     * @param value The SwJsonValue to add.
     */
    void append(const SwJsonValue& value) {
        data_.push_back(value);
    }

    /**
     * @brief Inserts a value at a specific position in the array.
     * @param index The position where the value should be inserted.
     * @param value The SwJsonValue to insert.
     * @throw std::out_of_range If the index is out of bounds.
     */
    void insert(size_t index, const SwJsonValue& value) {
        if (index > data_.size()) {
            throw std::out_of_range("Index out of bounds");
        }
        data_.insert(data_.begin() + index, value);
    }

    /**
     * @brief Removes a value at a specific index.
     * @param index The position of the value to remove.
     * @throw std::out_of_range If the index is out of bounds.
     */
    void remove(size_t index) {
        if (index >= data_.size()) {
            throw std::out_of_range("Index out of bounds");
        }
        data_.erase(data_.begin() + index);
    }

    /**
     * @brief Retrieves the number of elements in the array.
     * @return The size of the array.
     */
    size_t size() const {
        return data_.size();
    }

    /**
     * @brief Checks if the array is empty.
     * @return true if the array is empty, false otherwise.
     */
    bool isEmpty() const {
        return data_.empty();
    }

    /**
     * @brief Compares two JSON arrays for equality.
     * @param other The JSON array to compare with.
     * @return true if the arrays are equal, false otherwise.
     */
    bool operator==(const SwJsonArray& other) const {
        return data_ == other.data_;
    }

    /**
     * @brief Compares two JSON arrays for inequality.
     * @param other The JSON array to compare with.
     * @return true if the arrays are not equal, false otherwise.
     */
    bool operator!=(const SwJsonArray& other) const {
        return !(*this == other);
    }

    /**
     * @brief Converts the array to a JSON-formatted string.
     * @param compact If true, produces a compact JSON string. Otherwise, formats with indentation.
     * @param indentLevel The current level of indentation for nested structures.
     * @return A JSON-formatted string representation of the array.
     */
    std::string toJsonString(bool compact = true, int indentLevel = 0) const {
        std::ostringstream os;
        std::string indent(indentLevel * 2, ' '); // Indentation pour le niveau actuel
        std::string childIndent((indentLevel + 1) * 2, ' '); // Indentation pour les enfants

        os << (compact ? "[" : "[\n");
        for (size_t i = 0; i < data_.size(); ++i) {
            if (i > 0) os << (compact ? "," : ",\n"); // Pas de saut de ligne pour le premier élément

            if (!compact) os << childIndent; // Indenter chaque élément sauf le dernier

            const auto& value = data_[i];
            if (value.isObject() && value.toObject()) {
                os << indent << "{SwJsonValue(Object)}";
            } else if (value.isArray() && value.toArray()) {
                os << value.toArray()->toJsonString(compact, indentLevel + 1);
            } else {
                os << value.toJsonString();
            }
        }

        if (!compact && !data_.empty()) os << "\n" << indent; // Fermer avec indentation uniquement si le tableau n'est pas vide
        os << "]";
        return os.str();
    }

    /**
     * @brief Retrieves the internal data of the array.
     * @return A vector containing all SwJsonValue elements in the array.
     */
    std::vector<SwJsonValue> data() const
    {
        return data_;
    }

private:
    std::vector<SwJsonValue> data_; ///< Stores the elements of the array.
};


