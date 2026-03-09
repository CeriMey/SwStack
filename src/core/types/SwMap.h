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

/**
 * @file src/core/types/SwMap.h
 * @ingroup core_types
 * @brief Declares the public interface exposed by SwMap in the CoreSw fundamental types layer.
 *
 * This header belongs to the CoreSw fundamental types layer. It provides value types, containers,
 * text and binary helpers, and lightweight serialization primitives shared across the stack.
 *
 * Within that layer, this file focuses on the map interface. The declarations exposed here define
 * the stable surface that adjacent code can rely on while the implementation remains free to
 * evolve behind the header.
 *
 * The main declarations in this header are SwMap.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Interfaces in this area are intentionally reused by runtime, IO, GUI, remote, and media modules
 * to keep cross-module semantics consistent.
 *
 */

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

        /**
         * @brief Constructs a `iterator` instance.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        iterator() = default;
        /**
         * @brief Constructs a `iterator` instance.
         * @param it Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        explicit iterator(typename std::map<Key, T>::iterator it) : it_(it) {}

        /**
         * @brief Returns the current operator ++.
         * @return The current operator ++.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        iterator& operator++() { ++it_; return *this; }
        /**
         * @brief Performs the `operator++` operation.
         * @param this Value passed to the method.
         * @return The requested operator ++.
         */
        iterator operator++(int) { iterator tmp(*this); ++(*this); return tmp; }
        /**
         * @brief Returns the current operator --.
         * @return The current operator --.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        iterator& operator--() { --it_; return *this; }
        /**
         * @brief Performs the `operator--` operation.
         * @param this Value passed to the method.
         * @return The requested operator --.
         */
        iterator operator--(int) { iterator tmp(*this); --(*this); return tmp; }

        /**
         * @brief Performs the `operator==` operation.
         * @param other Value passed to the method.
         * @return `true` on success; otherwise `false`.
         */
        bool operator==(const iterator& other) const { return it_ == other.it_; }
        /**
         * @brief Performs the `operator!=` operation.
         * @param other Value passed to the method.
         * @return `true` on success; otherwise `false`.
         */
        bool operator!=(const iterator& other) const { return it_ != other.it_; }

        /**
         * @brief Returns the current operator *.
         * @return The current operator *.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        reference operator*() const { return *it_; }
        /**
         * @brief Performs the `operator->` operation.
         * @param it_ Value passed to the method.
         * @return The requested operator ->.
         */
        pointer operator->() const { return &(*it_); }

        /**
         * @brief Returns the current key.
         * @return The current key.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        Key key() const { return it_->first; }
        /**
         * @brief Returns the current value.
         * @return The current value.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        T& value() const { return it_->second; }

        /**
         * @brief Returns the current base.
         * @return The current base.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
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

        /**
         * @brief Constructs a `const_iterator` instance.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        const_iterator() = default;
        /**
         * @brief Constructs a `const_iterator` instance.
         * @param it Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        explicit const_iterator(typename std::map<Key, T>::const_iterator it) : it_(it) {}
        /**
         * @brief Constructs a `const_iterator` instance.
         * @param it_ Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        const_iterator(const iterator& other) : it_(other.it_) {}

        /**
         * @brief Returns the current operator ++.
         * @return The current operator ++.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        const_iterator& operator++() { ++it_; return *this; }
        /**
         * @brief Performs the `operator++` operation.
         * @param this Value passed to the method.
         * @return The requested operator ++.
         */
        const_iterator operator++(int) { const_iterator tmp(*this); ++(*this); return tmp; }
        /**
         * @brief Returns the current operator --.
         * @return The current operator --.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        const_iterator& operator--() { --it_; return *this; }
        /**
         * @brief Performs the `operator--` operation.
         * @param this Value passed to the method.
         * @return The requested operator --.
         */
        const_iterator operator--(int) { const_iterator tmp(*this); --(*this); return tmp; }

        /**
         * @brief Performs the `operator==` operation.
         * @param other Value passed to the method.
         * @return `true` on success; otherwise `false`.
         */
        bool operator==(const const_iterator& other) const { return it_ == other.it_; }
        /**
         * @brief Performs the `operator!=` operation.
         * @param other Value passed to the method.
         * @return `true` on success; otherwise `false`.
         */
        bool operator!=(const const_iterator& other) const { return it_ != other.it_; }

        /**
         * @brief Returns the current operator *.
         * @return The current operator *.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        reference operator*() const { return *it_; }
        /**
         * @brief Performs the `operator->` operation.
         * @param it_ Value passed to the method.
         * @return The requested operator ->.
         */
        pointer operator->() const { return &(*it_); }

        /**
         * @brief Returns the current key.
         * @return The current key.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        Key key() const { return it_->first; }
        /**
         * @brief Returns the current value.
         * @return The current value.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        const T& value() const { return it_->second; }

        /**
         * @brief Returns the current base.
         * @return The current base.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        typename std::map<Key, T>::const_iterator base() const { return it_; }

    private:
        typename std::map<Key, T>::const_iterator it_{};
    };

    // Constructors
    /**
     * @brief Constructs a `SwMap` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwMap() = default;
    /**
     * @brief Constructs a `SwMap` instance.
     * @param initList Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwMap(std::initializer_list<std::pair<const Key, T>> initList) : m_map(initList) {}

    template<typename InputIterator>
    /**
     * @brief Constructs a `SwMap` instance.
     * @param first Value passed to the method.
     * @param last Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwMap(InputIterator first, InputIterator last) : m_map(first, last) {}

    // Copy & Move Constructors
    /**
     * @brief Constructs a `SwMap` instance.
     * @param other Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwMap(const SwMap& other) = default;
    /**
     * @brief Constructs a `SwMap` instance.
     * @param other Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwMap(SwMap&& other) noexcept = default;

    /**
     * @brief Performs the `operator==` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator==(const SwMap& other) const { return m_map == other.m_map; }
    /**
     * @brief Performs the `operator!=` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator!=(const SwMap& other) const { return m_map != other.m_map; }

    // Assignment Operators
    /**
     * @brief Performs the `operator=` operation.
     * @param other Value passed to the method.
     * @return The requested operator =.
     */
    SwMap& operator=(const SwMap& other) = default;
    /**
     * @brief Performs the `operator=` operation.
     * @param other Value passed to the method.
     * @return The requested operator =.
     */
    SwMap& operator=(SwMap&& other) noexcept = default;
    /**
     * @brief Performs the `operator=` operation.
     * @param initList Value passed to the method.
     * @return The requested operator =.
     */
    SwMap& operator=(std::initializer_list<std::pair<const Key, T>> initList) {
        m_map = initList;
        return *this;
    }

    // Element Access
    /**
     * @brief Performs the `operator[]` operation.
     * @param key Value passed to the method.
     * @return The requested operator [].
     */
    T& operator[](const Key& key) {
        // Vérifie si la clé existe dans la map
        auto it = m_map.find(key);
        if (it == m_map.end()) {
            // Crée une valeur par défaut si la clé n'existe pas
            m_map[key] = T(); // Appelle le constructeur par défaut de T
        }
        return m_map[key];
    }


    /**
     * @brief Performs the `operator[]` operation.
     * @param key Value passed to the method.
     * @return The requested operator [].
     */
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
    /**
     * @brief Performs the `insert` operation.
     * @param key Value passed to the method.
     * @param value Value passed to the method.
     */
    void insert(const Key& key, const T& value) {
        m_map[key] = value;
    }

    /**
     * @brief Performs the `insert` operation.
     * @param pair Value passed to the method.
     */
    void insert(const std::pair<Key, T>& pair) {
        m_map.insert(pair);
    }

    template<typename InputIterator>
    /**
     * @brief Performs the `insert` operation.
     * @param first Value passed to the method.
     * @param last Value passed to the method.
     */
    void insert(InputIterator first, InputIterator last) {
        m_map.insert(first, last);
    }

    // Removal
    /**
     * @brief Removes the specified remove.
     * @param key Value passed to the method.
     */
    void remove(const Key& key) {
        m_map.erase(key);
    }

    /**
     * @brief Performs the `erase` operation.
     * @param pos Position used by the operation.
     * @return The requested erase.
     */
    iterator erase(iterator pos) {
        return iterator(m_map.erase(pos.base()));
    }

    // Queries
    /**
     * @brief Performs the `contains` operation.
     * @param key Value passed to the method.
     * @return `true` when the object reports contains; otherwise `false`.
     */
    bool contains(const Key& key) const {
        return m_map.find(key) != m_map.end();
    }

    /**
     * @brief Returns whether the object reports empty.
     * @return `true` when the object reports empty; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isEmpty() const {
        return m_map.empty();
    }

    /**
     * @brief Returns the current empty.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool empty() const {
        return m_map.empty();
    }

    /**
     * @brief Performs the `count` operation.
     * @param key Value passed to the method.
     * @return The current count value.
     */
    size_t count(const Key& key) const {
        return m_map.count(key);
    }

    /**
     * @brief Returns the current size.
     * @return The current size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::size_t size() const {
        return m_map.size();
    }

    /**
     * @brief Clears the current object state.
     */
    void clear() {
        m_map.clear();
    }

    // Iterators
    /**
     * @brief Returns the current begin.
     * @return The current begin.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    iterator begin() {
        return iterator(m_map.begin());
    }

    /**
     * @brief Returns the current begin.
     * @return The current begin.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const_iterator begin() const {
        return const_iterator(m_map.begin());
    }

    /**
     * @brief Returns the current end.
     * @return The current end.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    iterator end() {
        return iterator(m_map.end());
    }

    /**
     * @brief Returns the current end.
     * @return The current end.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const_iterator end() const {
        return const_iterator(m_map.end());
    }

    /**
     * @brief Performs the `cbegin` operation.
     * @return The requested cbegin.
     */
    const_iterator cbegin() const { return const_iterator(m_map.begin()); }
    /**
     * @brief Performs the `cend` operation.
     * @return The requested cend.
     */
    const_iterator cend() const { return const_iterator(m_map.end()); }

    /**
     * @brief Performs the `find` operation.
     * @param key Value passed to the method.
     * @return The requested find.
     */
    iterator find(const Key& key) {
        return iterator(m_map.find(key));
    }

    /**
     * @brief Performs the `find` operation.
     * @param key Value passed to the method.
     * @return The requested find.
     */
    const_iterator find(const Key& key) const {
        return const_iterator(m_map.find(key));
    }

    /**
     * @brief Performs the `constFind` operation.
     * @param key Value passed to the method.
     * @return The requested const Find.
     */
    iterator constFind(const Key& key) {
        return iterator(m_map.find(key));
    }

    /**
     * @brief Performs the `constFind` operation.
     * @param key Value passed to the method.
     * @return The requested const Find.
     */
    const_iterator constFind(const Key& key) const {
        return const_iterator(m_map.find(key));
    }

    // Keys and Values
    /**
     * @brief Returns the current keys.
     * @return The current keys.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwList<Key> keys() const {
        SwList<Key> result;
        for (const auto& pair : m_map) {
            result.append(pair.first);
        }
        return result;
    }

    /**
     * @brief Returns the current values.
     * @return The current values.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwList<T> values() const {
        SwList<T> result;
        for (const auto& pair : m_map) {
            result.append(pair.second);
        }
        return result;
    }

    /**
     * @brief Performs the `values` operation.
     * @param key Value passed to the method.
     * @return The requested values.
     */
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
