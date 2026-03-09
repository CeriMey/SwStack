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

#ifndef SWHASH_H
#define SWHASH_H

/**
 * @file src/core/types/SwHash.h
 * @ingroup core_types
 * @brief Declares the public interface exposed by SwHash in the CoreSw fundamental types layer.
 *
 * This header belongs to the CoreSw fundamental types layer. It provides value types, containers,
 * text and binary helpers, and lightweight serialization primitives shared across the stack.
 *
 * Within that layer, this file focuses on the hash interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwHash.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Interfaces in this area are intentionally reused by runtime, IO, GUI, remote, and media modules
 * to keep cross-module semantics consistent.
 *
 */

#include <unordered_map>
#include <initializer_list>
#include <utility>

#include "SwList.h"

template<typename Key, typename T, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
class SwHash {
public:
    typedef Key key_type;
    typedef T mapped_type;
    typedef std::pair<const Key, T> value_type;
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;
    typedef Hash hasher;
    typedef KeyEqual key_equal;
    typedef typename std::unordered_map<Key, T, Hash, KeyEqual>::iterator iterator;
    typedef typename std::unordered_map<Key, T, Hash, KeyEqual>::const_iterator const_iterator;

    class ConstIteratorWrapper;
    class IteratorWrapper {
    public:
        /**
         * @brief Constructs a `IteratorWrapper` instance.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        IteratorWrapper() = default;
        /**
         * @brief Constructs a `IteratorWrapper` instance.
         * @param it Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        explicit IteratorWrapper(iterator it) : it_(it) {}

        /**
         * @brief Returns the current operator ++.
         * @return The current operator ++.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        IteratorWrapper& operator++() { ++it_; return *this; }
        /**
         * @brief Performs the `operator++` operation.
         * @param this Value passed to the method.
         * @return The requested operator ++.
         */
        IteratorWrapper operator++(int) { IteratorWrapper tmp(*this); ++(*this); return tmp; }

        /**
         * @brief Performs the `operator==` operation.
         * @param other Value passed to the method.
         * @return `true` on success; otherwise `false`.
         */
        bool operator==(const IteratorWrapper& other) const { return it_ == other.it_; }
        /**
         * @brief Performs the `operator!=` operation.
         * @param other Value passed to the method.
         * @return `true` on success; otherwise `false`.
         */
        bool operator!=(const IteratorWrapper& other) const { return it_ != other.it_; }

        /**
         * @brief Returns the current key.
         * @return The current key.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        key_type key() const { return it_->first; }
        /**
         * @brief Returns the current value.
         * @return The current value.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        mapped_type& value() const { return it_->second; }

        /**
         * @brief Returns the current base.
         * @return The current base.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        iterator base() const { return it_; }

    private:
        iterator it_{};
        friend class ConstIteratorWrapper;
    };

    class ConstIteratorWrapper {
    public:
        /**
         * @brief Constructs a `ConstIteratorWrapper` instance.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        ConstIteratorWrapper() = default;
        /**
         * @brief Constructs a `ConstIteratorWrapper` instance.
         * @param it Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        explicit ConstIteratorWrapper(const_iterator it) : it_(it) {}
        /**
         * @brief Constructs a `ConstIteratorWrapper` instance.
         * @param it_ Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        ConstIteratorWrapper(const IteratorWrapper& other) : it_(other.it_) {}

        /**
         * @brief Returns the current operator ++.
         * @return The current operator ++.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        ConstIteratorWrapper& operator++() { ++it_; return *this; }
        /**
         * @brief Performs the `operator++` operation.
         * @param this Value passed to the method.
         * @return The requested operator ++.
         */
        ConstIteratorWrapper operator++(int) { ConstIteratorWrapper tmp(*this); ++(*this); return tmp; }

        /**
         * @brief Performs the `operator==` operation.
         * @param other Value passed to the method.
         * @return `true` on success; otherwise `false`.
         */
        bool operator==(const ConstIteratorWrapper& other) const { return it_ == other.it_; }
        /**
         * @brief Performs the `operator!=` operation.
         * @param other Value passed to the method.
         * @return `true` on success; otherwise `false`.
         */
        bool operator!=(const ConstIteratorWrapper& other) const { return it_ != other.it_; }

        /**
         * @brief Returns the current key.
         * @return The current key.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        key_type key() const { return it_->first; }
        /**
         * @brief Returns the current value.
         * @return The current value.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        const mapped_type& value() const { return it_->second; }

        /**
         * @brief Returns the current base.
         * @return The current base.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        const_iterator base() const { return it_; }

    private:
        const_iterator it_{};
    };

    /**
     * @brief Constructs a `SwHash` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwHash() = default;
    /**
     * @brief Constructs a `SwHash` instance.
     * @param init Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwHash(std::initializer_list<value_type> init) : m_hash(init) {}

    template<typename InputIt>
    /**
     * @brief Constructs a `SwHash` instance.
     * @param first Value passed to the method.
     * @param last Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwHash(InputIt first, InputIt last) : m_hash(first, last) {}

    /**
     * @brief Constructs a `SwHash` instance.
     * @param other Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwHash(const SwHash& other) = default;
    /**
     * @brief Constructs a `SwHash` instance.
     * @param other Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwHash(SwHash&& other) noexcept = default;

    /**
     * @brief Performs the `operator=` operation.
     * @param other Value passed to the method.
     * @return The requested operator =.
     */
    SwHash& operator=(const SwHash& other) = default;
    /**
     * @brief Performs the `operator=` operation.
     * @param other Value passed to the method.
     * @return The requested operator =.
     */
    SwHash& operator=(SwHash&& other) noexcept = default;

    /**
     * @brief Performs the `operator=` operation.
     * @param init Value passed to the method.
     * @return The requested operator =.
     */
    SwHash& operator=(std::initializer_list<value_type> init) {
        m_hash = init;
        return *this;
    }

    /**
     * @brief Performs the `operator[]` operation.
     * @param key Value passed to the method.
     * @return The requested operator [].
     */
    mapped_type& operator[](const key_type& key) {
        return m_hash[key];
    }

    /**
     * @brief Performs the `operator[]` operation.
     * @param key Value passed to the method.
     * @return The requested operator [].
     */
    mapped_type& operator[](key_type&& key) {
        return m_hash[std::move(key)];
    }

    /**
     * @brief Performs the `operator[]` operation.
     * @param key Value passed to the method.
     * @return The requested operator [].
     */
    const mapped_type& operator[](const key_type& key) const {
        auto it = m_hash.find(key);
        if (it != m_hash.end()) {
            return it->second;
        }
        static const mapped_type defaultValue = mapped_type();
        return defaultValue;
    }

    /**
     * @brief Performs the `value` operation.
     * @param key Value passed to the method.
     * @param defaultValue Value passed to the method.
     * @return The requested value.
     */
    mapped_type value(const key_type& key, const mapped_type& defaultValue = mapped_type()) const {
        auto it = m_hash.find(key);
        return it != m_hash.end() ? it->second : defaultValue;
    }

    /**
     * @brief Performs the `take` operation.
     * @param key Value passed to the method.
     * @return The resulting take.
     */
    mapped_type take(const key_type& key) {
        auto it = m_hash.find(key);
        if (it == m_hash.end()) {
            return mapped_type();
        }
        mapped_type result = std::move(it->second);
        m_hash.erase(it);
        return result;
    }

    /**
     * @brief Performs the `insert` operation.
     * @param key Value passed to the method.
     * @param value Value passed to the method.
     */
    void insert(const key_type& key, const mapped_type& value) {
        m_hash[key] = value;
    }

    /**
     * @brief Performs the `insert` operation.
     * @param key Value passed to the method.
     * @param value Value passed to the method.
     */
    void insert(key_type&& key, mapped_type&& value) {
        m_hash.emplace(std::move(key), std::move(value));
    }

    /**
     * @brief Performs the `insert` operation.
     * @param value Value passed to the method.
     */
    void insert(const value_type& value) {
        m_hash.insert(value);
    }

    /**
     * @brief Performs the `insert` operation.
     * @param value Value passed to the method.
     */
    void insert(value_type&& value) {
        m_hash.insert(std::move(value));
    }

    template<typename InputIt>
    /**
     * @brief Performs the `insert` operation.
     * @param first Value passed to the method.
     * @param last Value passed to the method.
     */
    void insert(InputIt first, InputIt last) {
        m_hash.insert(first, last);
    }

    /**
     * @brief Performs the `find` operation.
     * @param key Value passed to the method.
     * @return The requested find.
     */
    iterator find(const key_type& key) {
        return m_hash.find(key);
    }

    /**
     * @brief Performs the `find` operation.
     * @param key Value passed to the method.
     * @return The requested find.
     */
    const_iterator find(const key_type& key) const {
        return m_hash.find(key);
    }

    /**
     * @brief Performs the `contains` operation.
     * @param key Value passed to the method.
     * @return `true` when the object reports contains; otherwise `false`.
     */
    bool contains(const key_type& key) const {
        return m_hash.find(key) != m_hash.end();
    }

    /**
     * @brief Performs the `count` operation.
     * @param key Value passed to the method.
     * @return The current count value.
     */
    size_type count(const key_type& key) const {
        return m_hash.count(key);
    }

    /**
     * @brief Removes the specified remove.
     * @param key Value passed to the method.
     */
    void remove(const key_type& key) {
        m_hash.erase(key);
    }

    /**
     * @brief Returns whether the object reports empty.
     * @return `true` when the object reports empty; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isEmpty() const {
        return m_hash.empty();
    }

    /**
     * @brief Returns the current size.
     * @return The current size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    size_type size() const {
        return m_hash.size();
    }

    /**
     * @brief Performs the `reserve` operation.
     * @param capacity Value passed to the method.
     */
    void reserve(size_type capacity) {
        m_hash.reserve(capacity);
    }

    /**
     * @brief Clears the current object state.
     */
    void clear() {
        m_hash.clear();
    }

    /**
     * @brief Performs the `begin` operation.
     * @return The requested begin.
     */
    iterator begin() { return m_hash.begin(); }
    /**
     * @brief Performs the `end` operation.
     * @return The requested end.
     */
    iterator end() { return m_hash.end(); }
    /**
     * @brief Performs the `begin` operation.
     * @return The requested begin.
     */
    const_iterator begin() const { return m_hash.begin(); }
    /**
     * @brief Performs the `end` operation.
     * @return The requested end.
     */
    const_iterator end() const { return m_hash.end(); }
    /**
     * @brief Performs the `cbegin` operation.
     * @return The requested cbegin.
     */
    const_iterator cbegin() const { return m_hash.cbegin(); }
    /**
     * @brief Performs the `cend` operation.
     * @return The requested cend.
     */
    const_iterator cend() const { return m_hash.cend(); }

    /**
     * @brief Performs the `constFind` operation.
     * @param key Value passed to the method.
     * @return The requested const Find.
     */
    IteratorWrapper constFind(const key_type& key) {
        return IteratorWrapper(m_hash.find(key));
    }

    /**
     * @brief Performs the `constFind` operation.
     * @param key Value passed to the method.
     * @return The requested const Find.
     */
    ConstIteratorWrapper constFind(const key_type& key) const {
        return ConstIteratorWrapper(m_hash.find(key));
    }

    /**
     * @brief Performs the `beginWrap` operation.
     * @return The requested begin Wrap.
     */
    IteratorWrapper beginWrap() { return IteratorWrapper(m_hash.begin()); }
    /**
     * @brief Performs the `endWrap` operation.
     * @return The requested end Wrap.
     */
    IteratorWrapper endWrap() { return IteratorWrapper(m_hash.end()); }
    /**
     * @brief Performs the `beginWrap` operation.
     * @return The requested begin Wrap.
     */
    ConstIteratorWrapper beginWrap() const { return ConstIteratorWrapper(m_hash.begin()); }
    /**
     * @brief Performs the `endWrap` operation.
     * @return The requested end Wrap.
     */
    ConstIteratorWrapper endWrap() const { return ConstIteratorWrapper(m_hash.end()); }

    /**
     * @brief Returns the current keys.
     * @return The current keys.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwList<key_type> keys() const {
        SwList<key_type> result;
        for (const auto& entry : m_hash) {
            result.append(entry.first);
        }
        return result;
    }

    /**
     * @brief Returns the current values.
     * @return The current values.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwList<mapped_type> values() const {
        SwList<mapped_type> result;
        for (const auto& entry : m_hash) {
            result.append(entry.second);
        }
        return result;
    }

    /**
     * @brief Performs the `values` operation.
     * @param key Value passed to the method.
     * @return The requested values.
     */
    SwList<mapped_type> values(const key_type& key) const {
        SwList<mapped_type> result;
        auto range = m_hash.equal_range(key);
        for (auto it = range.first; it != range.second; ++it) {
            result.append(it->second);
        }
        return result;
    }

    /**
     * @brief Performs the `swap` operation.
     */
    void swap(SwHash& other) noexcept(noexcept(m_hash.swap(other.m_hash))) {
        m_hash.swap(other.m_hash);
    }

    friend bool operator==(const SwHash& lhs, const SwHash& rhs) {
        return lhs.m_hash == rhs.m_hash;
    }

    friend bool operator!=(const SwHash& lhs, const SwHash& rhs) {
        return !(lhs == rhs);
    }

private:
    std::unordered_map<Key, T, Hash, KeyEqual> m_hash;
};

template<typename Key, typename T, typename Hash, typename KeyEqual>
inline void swap(SwHash<Key, T, Hash, KeyEqual>& lhs,
                 SwHash<Key, T, Hash, KeyEqual>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
    lhs.swap(rhs);
}

#endif // SWHASH_H
