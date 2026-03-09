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

#ifndef SWLIST_H
#define SWLIST_H

/**
 * @file src/core/types/SwList.h
 * @ingroup core_types
 * @brief Declares the public interface exposed by SwList in the CoreSw fundamental types layer.
 *
 * This header belongs to the CoreSw fundamental types layer. It provides value types, containers,
 * text and binary helpers, and lightweight serialization primitives shared across the stack.
 *
 * Within that layer, this file focuses on the list interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwList.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Interfaces in this area are intentionally reused by runtime, IO, GUI, remote, and media modules
 * to keep cross-module semantics consistent.
 *
 */

#include <vector>
#include <initializer_list>
#include <unordered_set>
#include <stdexcept>
#include <iterator>
#include <algorithm>
#include <sstream>
#include <functional>


template<typename T>
class SwList {
public:
    // Constructeurs
    /**
     * @brief Constructs a `SwList` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwList() = default;
    /**
     * @brief Constructs a `SwList` instance.
     * @param other Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwList(const SwList& other) = default;
    /**
     * @brief Constructs a `SwList` instance.
     * @param other Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwList(SwList&& other) noexcept = default;
    /**
     * @brief Constructs a `SwList` instance.
     * @param init Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwList(std::initializer_list<T> init) : data_(init) {}
    /**
     * @brief Constructs a `SwList` instance.
     * @param vec Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwList(const std::vector<T>& vec) : data_(vec) {}
    /**
     * @brief Constructs a `SwList` instance.
     * @param singleValue Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwList(const T& singleValue) : data_(1, singleValue) {}
    template<typename Iter>
    /**
     * @brief Constructs a `SwList` instance.
     * @param begin Value passed to the method.
     * @param end Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwList(Iter begin, Iter end) : data_(begin, end) {}
    // Destructeur
    /**
     * @brief Destroys the `SwList` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwList() = default;

    // Op�rateurs
    /**
     * @brief Performs the `operator=` operation.
     * @param other Value passed to the method.
     * @return The requested operator =.
     */
    SwList& operator=(const SwList& other) = default;
    /**
     * @brief Performs the `operator=` operation.
     * @param other Value passed to the method.
     * @return The requested operator =.
     */
    SwList& operator=(SwList&& other) noexcept = default;

    // Déclaration explicite des types d'itérateurs
    typedef typename std::vector<T>::iterator iterator;
    typedef typename std::vector<T>::const_iterator const_iterator;

    // Itérateurs existants
    /**
     * @brief Performs the `begin` operation.
     * @return The requested begin.
     */
    iterator begin() { return data_.begin(); }
    /**
     * @brief Performs the `end` operation.
     * @return The requested end.
     */
    iterator end() { return data_.end(); }
    /**
     * @brief Performs the `begin` operation.
     * @return The requested begin.
     */
    const_iterator begin() const { return data_.begin(); }
    /**
     * @brief Performs the `end` operation.
     * @return The requested end.
     */
    const_iterator end() const { return data_.end(); }

    /**
     * @brief Performs the `cbegin` operation.
     * @return The requested cbegin.
     */
    const_iterator cbegin() const { return data_.cbegin(); }
    /**
     * @brief Performs the `cend` operation.
     * @return The requested cend.
     */
    const_iterator cend() const { return data_.cend(); }

    /**
     * @brief Performs the `rbegin` operation.
     * @return The requested rbegin.
     */
    typename std::vector<T>::reverse_iterator rbegin() { return data_.rbegin(); }
    /**
     * @brief Performs the `rend` operation.
     * @return The requested rend.
     */
    typename std::vector<T>::reverse_iterator rend() { return data_.rend(); }
    /**
     * @brief Performs the `rbegin` operation.
     * @return The requested rbegin.
     */
    typename std::vector<T>::const_reverse_iterator rbegin() const { return data_.rbegin(); }
    /**
     * @brief Performs the `rend` operation.
     * @return The requested rend.
     */
    typename std::vector<T>::const_reverse_iterator rend() const { return data_.rend(); }

    /**
     * @brief Performs the `operator<<` operation.
     * @param value Value passed to the method.
     * @return The requested operator <<.
     */
    SwList& operator<<(const T& value) {
        append(value);
        return *this;
    }

    /**
     * @brief Performs the `operator[]` operation.
     * @param index Value passed to the method.
     * @return The requested operator [].
     */
    T& operator[](size_t index) {
        return data_[index];
    }

    /**
     * @brief Performs the `operator[]` operation.
     * @param index Value passed to the method.
     * @return The requested operator [].
     */
    const T& operator[](size_t index) const {
        return data_[index];
    }

    /**
     * @brief Performs the `operator==` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator==(const SwList& other) const {
        return data_ == other.data_;
    }

    /**
     * @brief Performs the `operator!=` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator!=(const SwList& other) const {
        return data_ != other.data_;
    }

    /**
     * @brief Performs the `operator+` operation.
     * @param other Value passed to the method.
     * @return The requested operator +.
     */
    SwList operator+(const SwList& other) const {
        SwList result(*this);
        result.data_.insert(result.data_.end(), other.data_.begin(), other.data_.end());
        return result;
    }

    /**
     * @brief Performs the `operator+=` operation.
     * @param other Value passed to the method.
     * @return The requested operator +=.
     */
    SwList& operator+=(const SwList& other) {
        data_.insert(data_.end(), other.data_.begin(), other.data_.end());
        return *this;
    }

    // M�thodes principales
    /**
     * @brief Performs the `append` operation.
     * @param value Value passed to the method.
     */
    void append(const T& value) {
        data_.push_back(value);
    }

    template<typename Iter>
    /**
     * @brief Performs the `append` operation.
     * @param begin Value passed to the method.
     * @param end Value passed to the method.
     */
    void append(Iter begin, Iter end) {
        data_.insert(data_.end(), begin, end);
    }

    /**
     * @brief Performs the `append` operation.
     * @param other Value passed to the method.
     */
    void append(const SwList<T>& other) {
        data_.insert(data_.end(), other.data_.begin(), other.data_.end());
    }

    /**
     * @brief Performs the `prepend` operation.
     * @param value Value passed to the method.
     */
    void prepend(const T& value) {
        data_.insert(data_.begin(), value);
    }
    /**
     * @brief Performs the `push_back` operation.
     * @param value Value passed to the method.
     */
    void push_back(const T& value) { data_.push_back(value); }

    /**
     * @brief Performs the `insert` operation.
     * @param index Value passed to the method.
     * @param value Value passed to the method.
     */
    void insert(size_t index, const T& value) {
        if (index > data_.size()) {
            throw std::out_of_range("Index out of range");
        }
        data_.insert(data_.begin() + index, value);
    }

    /**
     * @brief Removes the specified at.
     * @param index Value passed to the method.
     */
    void removeAt(size_t index) {
        if (index >= data_.size()) {
            throw std::out_of_range("Index out of range");
        }
        data_.erase(data_.begin() + index);
    }

    /**
     * @brief Clears the current object state.
     */
    void clear() {
        data_.clear();
    }

    /**
     * @brief Performs the `deleteAll` operation.
     */
    void deleteAll() {
        for (auto& element : data_) {
            delete element;
            element = nullptr;
        }
        data_.clear();
    }

    /**
     * @brief Returns the current size.
     * @return The current size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    size_t size() const {
        return data_.size();
    }

    /**
     * @brief Returns whether the object reports empty.
     * @return `true` when the object reports empty; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isEmpty() const {
        return data_.empty();
    }

    // Acc�s aux �l�ments
    /**
     * @brief Performs the `at` operation.
     * @param index Value passed to the method.
     * @return The requested at.
     */
    T& at(size_t index) {
        if (index >= data_.size()) {
            throw std::out_of_range("Index out of range");
        }
        return data_[index];
    }

    /**
     * @brief Performs the `at` operation.
     * @param index Value passed to the method.
     * @return The requested at.
     */
    const T& at(size_t index) const {
        if (index >= data_.size()) {
            throw std::out_of_range("Index out of range");
        }
        return data_[index];
    }

    T value(size_t index, const T& defaultValue = T()) const {
        if (index < data_.size()) {
            return data_[index];
        }
        return defaultValue;
    }


    /**
     * @brief Returns the current data.
     * @return The current data.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const T* data() const {
        return data_.data();
    }

    /**
     * @brief Returns the current data.
     * @return The current data.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    T* data() {
        return data_.data();
    }

    /**
     * @brief Performs the `reverse` operation.
     */
    void reverse() {
        std::reverse(data_.begin(), data_.end());
    }

    /**
     * @brief Removes the specified duplicates.
     */
    void removeDuplicates() {
        std::sort(data_.begin(), data_.end());
        data_.erase(std::unique(data_.begin(), data_.end()), data_.end());
    }

    /**
     * @brief Returns whether the object reports duplicates.
     * @return `true` when the object reports duplicates; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool hasDuplicates() const {
        std::unordered_set<T> seen;
        for (const auto& value : data_) {
            if (!seen.insert(value).second) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Performs the `filter` operation.
     * @param predicate Value passed to the method.
     * @return The requested filter.
     */
    SwList filter(std::function<bool(const T&)> predicate) const {
        SwList result;
        for (const auto& item : data_) {
            if (predicate(item)) {
                result.append(item);
            }
        }
        return result;
    }

    /**
     * @brief Performs the `reserve` operation.
     * @param capacity Value passed to the method.
     */
    void reserve(size_t capacity) {
        data_.reserve(capacity);
    }

    /**
     * @brief Returns the current capacity.
     * @return The current capacity.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    size_t capacity() const {
        return data_.capacity();
    }


    /**
     * @brief Returns the current to Vector.
     * @return The current to Vector.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::vector<T> toVector() const {
        return data_;
    }

    template<typename SeparatorType>
    /**
     * @brief Performs the `join` operation.
     * @param delimiter Value passed to the method.
     * @return The requested join.
     */
    std::string join(const SeparatorType& delimiter) const {
        if (data_.empty()) return "";

        std::ostringstream oss;
        auto it = data_.begin();
        oss << *it; // Premier �l�ment
        ++it;

        for (; it != data_.end(); ++it) {
            oss << delimiter << *it; // Ajout des d�limiteurs
        }
        return oss.str();
    }

    // Renvoie une copie du premier �l�ment
    T first() const {
        if (data_.empty()) {
            throw std::runtime_error("Cannot access first element of an empty container");
        }
        return data_.front(); // Retourne une copie
    }

    // Renvoie une copie du dernier �l�ment
    T last() const {
        if (data_.empty()) {
            throw std::runtime_error("Cannot access last element of an empty container");
        }
        return data_.back(); // Retourne une copie
    }

    // Nouvelles fonctionnalit�s pour SwList
    /**
     * @brief Returns the current first Ref.
     * @return The current first Ref.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    T& firstRef() {
        if (data_.empty()) {
            throw std::runtime_error("Cannot access first element of an empty container");
        }
        return data_.front();
    }

    /**
     * @brief Returns the current first Ref.
     * @return The current first Ref.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const T& firstRef() const {
        if (data_.empty()) {
            throw std::runtime_error("Cannot access first element of an empty container");
        }
        return data_.front();
    }

    /**
     * @brief Returns the current last Ref.
     * @return The current last Ref.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    T& lastRef() {
        if (data_.empty()) {
            throw std::runtime_error("Cannot access last element of an empty container");
        }
        return data_.back();
    }

    /**
     * @brief Returns the current last Ref.
     * @return The current last Ref.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const T& lastRef() const {
        if (data_.empty()) {
            throw std::runtime_error("Cannot access last element of an empty container");
        }
        return data_.back();
    }


    /**
     * @brief Starts the s With managed by the object.
     * @param value Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    bool startsWith(const T& value) const {
        return !data_.empty() && data_.front() == value;
    }

    /**
     * @brief Performs the `endsWith` operation.
     * @param value Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool endsWith(const T& value) const {
        return !data_.empty() && data_.back() == value;
    }

    /**
     * @brief Performs the `mid` operation.
     * @param index Value passed to the method.
     * @param length Value passed to the method.
     * @return The requested mid.
     */
    SwList<T> mid(size_t index, size_t length = std::string::npos) const {
        if (index >= data_.size()) return SwList<T>(); // Renvoie une liste vide en cas d'erreur

        size_t end = (length == std::string::npos) ? data_.size() : min(index + length, data_.size());
        return SwList<T>(data_.begin() + index, data_.begin() + end);
    }

    /**
     * @brief Performs the `swap` operation.
     * @param index1 Value passed to the method.
     * @param index2 Value passed to the method.
     */
    void swap(size_t index1, size_t index2) {
        if (index1 < data_.size() && index2 < data_.size()) {
            std::swap(data_[index1], data_[index2]);
        }
    }

    /**
     * @brief Performs the `contains` operation.
     * @param value Value passed to the method.
     * @return `true` when the object reports contains; otherwise `false`.
     */
    bool contains(const T& value) const {
        return std::find(data_.begin(), data_.end(), value) != data_.end();
    }

    /**
     * @brief Performs the `count` operation.
     * @param value Value passed to the method.
     * @return The current count value.
     */
    size_t count(const T& value) const {
        return std::count(data_.begin(), data_.end(), value);
    }

    /**
     * @brief Removes the specified all.
     * @param value Value passed to the method.
     */
    void removeAll(const T& value) {
        data_.erase(std::remove(data_.begin(), data_.end(), value), data_.end());
    }

    /**
     * @brief Removes the specified one.
     * @param value Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool removeOne(const T& value) {
        auto it = std::find(data_.begin(), data_.end(), value);
        if (it != data_.end()) {
            data_.erase(it);
            return true;
        }
        return false;
    }

    /**
     * @brief Removes the specified first.
     */
    void removeFirst() {
        if (!data_.empty()) {
            data_.erase(data_.begin());
        }
    }

    /**
     * @brief Removes the specified last.
     */
    void removeLast() {
        if (!data_.empty()) {
            data_.pop_back();
        }
    }

    /**
     * @brief Performs the `replace` operation.
     * @param index Value passed to the method.
     * @param value Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool replace(size_t index, const T& value) {
        if (index < data_.size()) {
            data_[index] = value;
            return true; // Remplacement r�ussi
        }
        return false; // Remplacement �chou�
    }

    /**
     * @brief Performs the `indexOf` operation.
     * @param value Value passed to the method.
     * @return The requested index Of.
     */
    int indexOf(const T& value) const {
        auto it = std::find(data_.begin(), data_.end(), value);
        return (it != data_.end()) ? std::distance(data_.begin(), it) : -1; // -1 si non trouv�
    }

    /**
     * @brief Performs the `lastIndexOf` operation.
     * @param value Value passed to the method.
     * @return The requested last Index Of.
     */
    int lastIndexOf(const T& value) const {
        auto it = std::find(data_.rbegin(), data_.rend(), value);
        return (it != data_.rend()) ? std::distance(data_.begin(), it.base()) - 1 : -1; // -1 si non trouv�
    }

private:
    std::vector<T> data_;
};

#endif // SWLIST_H
