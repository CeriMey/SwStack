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
        IteratorWrapper() = default;
        explicit IteratorWrapper(iterator it) : it_(it) {}

        IteratorWrapper& operator++() { ++it_; return *this; }
        IteratorWrapper operator++(int) { IteratorWrapper tmp(*this); ++(*this); return tmp; }

        bool operator==(const IteratorWrapper& other) const { return it_ == other.it_; }
        bool operator!=(const IteratorWrapper& other) const { return it_ != other.it_; }

        key_type key() const { return it_->first; }
        mapped_type& value() const { return it_->second; }

        iterator base() const { return it_; }

    private:
        iterator it_{};
        friend class ConstIteratorWrapper;
    };

    class ConstIteratorWrapper {
    public:
        ConstIteratorWrapper() = default;
        explicit ConstIteratorWrapper(const_iterator it) : it_(it) {}
        ConstIteratorWrapper(const IteratorWrapper& other) : it_(other.it_) {}

        ConstIteratorWrapper& operator++() { ++it_; return *this; }
        ConstIteratorWrapper operator++(int) { ConstIteratorWrapper tmp(*this); ++(*this); return tmp; }

        bool operator==(const ConstIteratorWrapper& other) const { return it_ == other.it_; }
        bool operator!=(const ConstIteratorWrapper& other) const { return it_ != other.it_; }

        key_type key() const { return it_->first; }
        const mapped_type& value() const { return it_->second; }

        const_iterator base() const { return it_; }

    private:
        const_iterator it_{};
    };

    SwHash() = default;
    SwHash(std::initializer_list<value_type> init) : m_hash(init) {}

    template<typename InputIt>
    SwHash(InputIt first, InputIt last) : m_hash(first, last) {}

    SwHash(const SwHash& other) = default;
    SwHash(SwHash&& other) noexcept = default;

    SwHash& operator=(const SwHash& other) = default;
    SwHash& operator=(SwHash&& other) noexcept = default;

    SwHash& operator=(std::initializer_list<value_type> init) {
        m_hash = init;
        return *this;
    }

    mapped_type& operator[](const key_type& key) {
        return m_hash[key];
    }

    mapped_type& operator[](key_type&& key) {
        return m_hash[std::move(key)];
    }

    const mapped_type& operator[](const key_type& key) const {
        auto it = m_hash.find(key);
        if (it != m_hash.end()) {
            return it->second;
        }
        static const mapped_type defaultValue = mapped_type();
        return defaultValue;
    }

    mapped_type value(const key_type& key, const mapped_type& defaultValue = mapped_type()) const {
        auto it = m_hash.find(key);
        return it != m_hash.end() ? it->second : defaultValue;
    }

    mapped_type take(const key_type& key) {
        auto it = m_hash.find(key);
        if (it == m_hash.end()) {
            return mapped_type();
        }
        mapped_type result = std::move(it->second);
        m_hash.erase(it);
        return result;
    }

    void insert(const key_type& key, const mapped_type& value) {
        m_hash[key] = value;
    }

    void insert(key_type&& key, mapped_type&& value) {
        m_hash.emplace(std::move(key), std::move(value));
    }

    void insert(const value_type& value) {
        m_hash.insert(value);
    }

    void insert(value_type&& value) {
        m_hash.insert(std::move(value));
    }

    template<typename InputIt>
    void insert(InputIt first, InputIt last) {
        m_hash.insert(first, last);
    }

    iterator find(const key_type& key) {
        return m_hash.find(key);
    }

    const_iterator find(const key_type& key) const {
        return m_hash.find(key);
    }

    bool contains(const key_type& key) const {
        return m_hash.find(key) != m_hash.end();
    }

    size_type count(const key_type& key) const {
        return m_hash.count(key);
    }

    void remove(const key_type& key) {
        m_hash.erase(key);
    }

    bool isEmpty() const {
        return m_hash.empty();
    }

    size_type size() const {
        return m_hash.size();
    }

    void reserve(size_type capacity) {
        m_hash.reserve(capacity);
    }

    void clear() {
        m_hash.clear();
    }

    iterator begin() { return m_hash.begin(); }
    iterator end() { return m_hash.end(); }
    const_iterator begin() const { return m_hash.begin(); }
    const_iterator end() const { return m_hash.end(); }
    const_iterator cbegin() const { return m_hash.cbegin(); }
    const_iterator cend() const { return m_hash.cend(); }

    IteratorWrapper constFind(const key_type& key) {
        return IteratorWrapper(m_hash.find(key));
    }

    ConstIteratorWrapper constFind(const key_type& key) const {
        return ConstIteratorWrapper(m_hash.find(key));
    }

    IteratorWrapper beginWrap() { return IteratorWrapper(m_hash.begin()); }
    IteratorWrapper endWrap() { return IteratorWrapper(m_hash.end()); }
    ConstIteratorWrapper beginWrap() const { return ConstIteratorWrapper(m_hash.begin()); }
    ConstIteratorWrapper endWrap() const { return ConstIteratorWrapper(m_hash.end()); }

    SwList<key_type> keys() const {
        SwList<key_type> result;
        for (const auto& entry : m_hash) {
            result.append(entry.first);
        }
        return result;
    }

    SwList<mapped_type> values() const {
        SwList<mapped_type> result;
        for (const auto& entry : m_hash) {
            result.append(entry.second);
        }
        return result;
    }

    SwList<mapped_type> values(const key_type& key) const {
        SwList<mapped_type> result;
        auto range = m_hash.equal_range(key);
        for (auto it = range.first; it != range.second; ++it) {
            result.append(it->second);
        }
        return result;
    }

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
