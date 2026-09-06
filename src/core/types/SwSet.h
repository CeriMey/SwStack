#pragma once
#include <functional>
#include <initializer_list>
#include <unordered_set>
#include <utility>

// Unique values with hash lookup. Iterators expose const values, so a key
// cannot be modified while it belongs to the set. Iteration order is unspecified.
template<class T, class Hash = std::hash<T>, class Equal = std::equal_to<T>>
class SwSet {
    using Storage = std::unordered_set<T, Hash, Equal>;
public:
    using value_type = T;
    using size_type = typename Storage::size_type;
    using const_iterator = typename Storage::const_iterator;
    using iterator = const_iterator;

    SwSet() = default;
    SwSet(std::initializer_list<T> values) : values_(values) {}
    template<class InputIterator>
    SwSet(InputIterator first, InputIterator last) : values_(first, last) {}

    bool isEmpty() const noexcept { return values_.empty(); }
    size_type size() const noexcept { return values_.size(); }
    size_type count() const noexcept { return size(); }
    bool contains(const T& value) const { return values_.find(value) != values_.end(); }
    iterator insert(const T& value) { return values_.insert(value).first; }
    iterator insert(T&& value) { return values_.insert(std::move(value)).first; }
    bool remove(const T& value) { return values_.erase(value) != 0; }
    iterator erase(const_iterator where) { return values_.erase(where); }
    void clear() noexcept { values_.clear(); }
    void reserve(size_type count) { values_.reserve(count); }
    const_iterator find(const T& value) const { return values_.find(value); }
    const_iterator begin() const noexcept { return values_.begin(); }
    const_iterator end() const noexcept { return values_.end(); }
    const_iterator cbegin() const noexcept { return values_.cbegin(); }
    const_iterator cend() const noexcept { return values_.cend(); }
    bool operator==(const SwSet& other) const { return values_ == other.values_; }
    bool operator!=(const SwSet& other) const { return !(*this == other); }
private:
    Storage values_;
};
