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

#ifndef SWMAP_H
#define SWMAP_H
#include <map>
#include "SwList.h" // Inclusion de SwList

template<typename Key, typename T>
class SwMap {
public:
    class iterator {
    public:
        typedef std::bidirectional_iterator_tag iterator_category;
        typedef std::pair<const Key, T> value_type;
        typedef std::ptrdiff_t difference_type;
        typedef value_type* pointer;
        typedef value_type& reference;

        iterator() = default;
        explicit iterator(typename std::map<Key, T>::iterator it) : it_(it) {}

        iterator& operator++() { ++it_; return *this; }
        iterator operator++(int) { iterator tmp(*this); ++(*this); return tmp; }
        iterator& operator--() { --it_; return *this; }
        iterator operator--(int) { iterator tmp(*this); --(*this); return tmp; }

        bool operator==(const iterator& other) const { return it_ == other.it_; }
        bool operator!=(const iterator& other) const { return it_ != other.it_; }

        reference operator*() const { return *it_; }
        pointer operator->() const { return &(*it_); }

        Key key() const { return it_->first; }
        T& value() const { return it_->second; }

        typename std::map<Key, T>::iterator base() const { return it_; }

    private:
        typename std::map<Key, T>::iterator it_{};
        friend class const_iterator;
    };

    class const_iterator {
    public:
        typedef std::bidirectional_iterator_tag iterator_category;
        typedef std::pair<const Key, T> value_type;
        typedef std::ptrdiff_t difference_type;
        typedef const value_type* pointer;
        typedef const value_type& reference;

        const_iterator() = default;
        explicit const_iterator(typename std::map<Key, T>::const_iterator it) : it_(it) {}
        const_iterator(const iterator& other) : it_(other.it_) {}

        const_iterator& operator++() { ++it_; return *this; }
        const_iterator operator++(int) { const_iterator tmp(*this); ++(*this); return tmp; }
        const_iterator& operator--() { --it_; return *this; }
        const_iterator operator--(int) { const_iterator tmp(*this); --(*this); return tmp; }

        bool operator==(const const_iterator& other) const { return it_ == other.it_; }
        bool operator!=(const const_iterator& other) const { return it_ != other.it_; }

        reference operator*() const { return *it_; }
        pointer operator->() const { return &(*it_); }

        Key key() const { return it_->first; }
        const T& value() const { return it_->second; }

        typename std::map<Key, T>::const_iterator base() const { return it_; }

    private:
        typename std::map<Key, T>::const_iterator it_{};
    };

    // Constructors
    SwMap() = default;
    SwMap(std::initializer_list<std::pair<const Key, T>> initList) : m_map(initList) {}

    template<typename InputIterator>
    SwMap(InputIterator first, InputIterator last) : m_map(first, last) {}

    // Copy & Move Constructors
    SwMap(const SwMap& other) = default;
    SwMap(SwMap&& other) noexcept = default;

    bool operator==(const SwMap& other) const { return m_map == other.m_map; }
    bool operator!=(const SwMap& other) const { return m_map != other.m_map; }

    // Assignment Operators
    SwMap& operator=(const SwMap& other) = default;
    SwMap& operator=(SwMap&& other) noexcept = default;
    SwMap& operator=(std::initializer_list<std::pair<const Key, T>> initList) {
        m_map = initList;
        return *this;
    }

    // Element Access
    T& operator[](const Key& key) {
        // Vérifie si la clé existe dans la map
        auto it = m_map.find(key);
        if (it == m_map.end()) {
            // Crée une valeur par défaut si la clé n'existe pas
            m_map[key] = T(); // Appelle le constructeur par défaut de T
        }
        return m_map[key];
    }


    const T& operator[](const Key& key) const {
        auto it = m_map.find(key);
        if (it != m_map.end()) {
            return it->second;
        }

        // Retourner une valeur par défaut statique si la clé n'existe pas
        static const T defaultValue = T(); // Valeur par défaut
        return defaultValue;
    }


    T value(const Key& key, const T& defaultValue = T()) const {
        auto it = m_map.find(key);
        return (it != m_map.end()) ? it->second : defaultValue;
    }

    // Insertion
    void insert(const Key& key, const T& value) {
        m_map[key] = value;
    }

    void insert(const std::pair<Key, T>& pair) {
        m_map.insert(pair);
    }

    template<typename InputIterator>
    void insert(InputIterator first, InputIterator last) {
        m_map.insert(first, last);
    }

    // Removal
    void remove(const Key& key) {
        m_map.erase(key);
    }

    // Queries
    bool contains(const Key& key) const {
        return m_map.find(key) != m_map.end();
    }

    bool isEmpty() const {
        return m_map.empty();
    }

    std::size_t size() const {
        return m_map.size();
    }

    void clear() {
        m_map.clear();
    }

    // Iterators
    iterator begin() {
        return iterator(m_map.begin());
    }

    const_iterator begin() const {
        return const_iterator(m_map.begin());
    }

    iterator end() {
        return iterator(m_map.end());
    }

    const_iterator end() const {
        return const_iterator(m_map.end());
    }

    const_iterator cbegin() const { return const_iterator(m_map.begin()); }
    const_iterator cend() const { return const_iterator(m_map.end()); }

    iterator find(const Key& key) {
        return iterator(m_map.find(key));
    }

    const_iterator find(const Key& key) const {
        return const_iterator(m_map.find(key));
    }

    iterator constFind(const Key& key) {
        return iterator(m_map.find(key));
    }

    const_iterator constFind(const Key& key) const {
        return const_iterator(m_map.find(key));
    }

    // Keys and Values
    SwList<Key> keys() const {
        SwList<Key> result;
        for (const auto& pair : m_map) {
            result.append(pair.first);
        }
        return result;
    }

    SwList<T> values() const {
        SwList<T> result;
        for (const auto& pair : m_map) {
            result.append(pair.second);
        }
        return result;
    }

    SwList<T> values(const Key& key) const {
        SwList<T> result;
        auto it = m_map.find(key);
        if (it != m_map.end()) {
            result.append(it->second);
        }
        return result;
    }

private:
    std::map<Key, T> m_map;
};

#endif // SWMAP_H
