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
    SwList() = default;
    SwList(const SwList& other) = default;
    SwList(SwList&& other) noexcept = default;
    SwList(std::initializer_list<T> init) : data_(init) {}
    SwList(const std::vector<T>& vec) : data_(vec) {}
    explicit SwList(const T& singleValue) : data_(1, singleValue) {}
    template<typename Iter>
    SwList(Iter begin, Iter end) : data_(begin, end) {}
    // Destructeur
    ~SwList() = default;

    // Op�rateurs
    SwList& operator=(const SwList& other) = default;
    SwList& operator=(SwList&& other) noexcept = default;

    // Déclaration explicite des types d'itérateurs
    typedef typename std::vector<T>::iterator iterator;
    typedef typename std::vector<T>::const_iterator const_iterator;

    // Itérateurs existants
    iterator begin() { return data_.begin(); }
    iterator end() { return data_.end(); }
    const_iterator begin() const { return data_.begin(); }
    const_iterator end() const { return data_.end(); }

    const_iterator cbegin() const { return data_.cbegin(); }
    const_iterator cend() const { return data_.cend(); }

    SwList& operator<<(const T& value) {
        append(value);
        return *this;
    }

    T& operator[](size_t index) {
        return data_[index];
    }

    const T& operator[](size_t index) const {
        return data_[index];
    }

    bool operator==(const SwList& other) const {
        return data_ == other.data_;
    }

    bool operator!=(const SwList& other) const {
        return data_ != other.data_;
    }

    SwList operator+(const SwList& other) const {
        SwList result(*this);
        result.data_.insert(result.data_.end(), other.data_.begin(), other.data_.end());
        return result;
    }

    SwList& operator+=(const SwList& other) {
        data_.insert(data_.end(), other.data_.begin(), other.data_.end());
        return *this;
    }

    // M�thodes principales
    void append(const T& value) {
        data_.push_back(value);
    }

    template<typename Iter>
    void append(Iter begin, Iter end) {
        data_.insert(data_.end(), begin, end);
    }

    void append(const SwList<T>& other) {
        data_.insert(data_.end(), other.data_.begin(), other.data_.end());
    }

    void prepend(const T& value) {
        data_.insert(data_.begin(), value);
    }
    void push_back(const T& value) { data_.push_back(value); }

    void insert(size_t index, const T& value) {
        if (index > data_.size()) {
            throw std::out_of_range("Index out of range");
        }
        data_.insert(data_.begin() + index, value);
    }

    void removeAt(size_t index) {
        if (index >= data_.size()) {
            throw std::out_of_range("Index out of range");
        }
        data_.erase(data_.begin() + index);
    }

    void clear() {
        data_.clear();
    }

    void deleteAll() {
        for (auto& element : data_) {
            delete element;
            element = nullptr;
        }
        data_.clear();
    }

    size_t size() const {
        return data_.size();
    }

    bool isEmpty() const {
        return data_.empty();
    }

    // Acc�s aux �l�ments
    T& at(size_t index) {
        if (index >= data_.size()) {
            throw std::out_of_range("Index out of range");
        }
        return data_[index];
    }

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


    const T* data() const {
        return data_.data();
    }

    T* data() {
        return data_.data();
    }

    void reverse() {
        std::reverse(data_.begin(), data_.end());
    }

    void removeDuplicates() {
        std::sort(data_.begin(), data_.end());
        data_.erase(std::unique(data_.begin(), data_.end()), data_.end());
    }

    bool hasDuplicates() const {
        std::unordered_set<T> seen;
        for (const auto& value : data_) {
            if (!seen.insert(value).second) {
                return true;
            }
        }
        return false;
    }

    SwList filter(std::function<bool(const T&)> predicate) const {
        SwList result;
        for (const auto& item : data_) {
            if (predicate(item)) {
                result.append(item);
            }
        }
        return result;
    }

    void reserve(size_t capacity) {
        data_.reserve(capacity);
    }

    size_t capacity() const {
        return data_.capacity();
    }


    std::vector<T> toVector() const {
        return data_;
    }

    template<typename SeparatorType>
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
    T& firstRef() {
        if (data_.empty()) {
            throw std::runtime_error("Cannot access first element of an empty container");
        }
        return data_.front();
    }

    const T& firstRef() const {
        if (data_.empty()) {
            throw std::runtime_error("Cannot access first element of an empty container");
        }
        return data_.front();
    }

    T& lastRef() {
        if (data_.empty()) {
            throw std::runtime_error("Cannot access last element of an empty container");
        }
        return data_.back();
    }

    const T& lastRef() const {
        if (data_.empty()) {
            throw std::runtime_error("Cannot access last element of an empty container");
        }
        return data_.back();
    }


    bool startsWith(const T& value) const {
        return !data_.empty() && data_.front() == value;
    }

    bool endsWith(const T& value) const {
        return !data_.empty() && data_.back() == value;
    }

    SwList<T> mid(size_t index, size_t length = std::string::npos) const {
        if (index >= data_.size()) return SwList<T>(); // Renvoie une liste vide en cas d'erreur

        size_t end = (length == std::string::npos) ? data_.size() : min(index + length, data_.size());
        return SwList<T>(data_.begin() + index, data_.begin() + end);
    }

    void swap(size_t index1, size_t index2) {
        if (index1 < data_.size() && index2 < data_.size()) {
            std::swap(data_[index1], data_[index2]);
        }
    }

    bool contains(const T& value) const {
        return std::find(data_.begin(), data_.end(), value) != data_.end();
    }

    size_t count(const T& value) const {
        return std::count(data_.begin(), data_.end(), value);
    }

    void removeAll(const T& value) {
        data_.erase(std::remove(data_.begin(), data_.end(), value), data_.end());
    }

    bool removeOne(const T& value) {
        auto it = std::find(data_.begin(), data_.end(), value);
        if (it != data_.end()) {
            data_.erase(it);
            return true;
        }
        return false;
    }

    void removeFirst() {
        if (!data_.empty()) {
            data_.erase(data_.begin());
        }
    }

    void removeLast() {
        if (!data_.empty()) {
            data_.pop_back();
        }
    }

    bool replace(size_t index, const T& value) {
        if (index < data_.size()) {
            data_[index] = value;
            return true; // Remplacement r�ussi
        }
        return false; // Remplacement �chou�
    }

    int indexOf(const T& value) const {
        auto it = std::find(data_.begin(), data_.end(), value);
        return (it != data_.end()) ? std::distance(data_.begin(), it) : -1; // -1 si non trouv�
    }

    int lastIndexOf(const T& value) const {
        auto it = std::find(data_.rbegin(), data_.rend(), value);
        return (it != data_.rend()) ? std::distance(data_.begin(), it.base()) - 1 : -1; // -1 si non trouv�
    }

private:
    std::vector<T> data_;
};

#endif // SWLIST_H
