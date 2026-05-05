//
// Created by dell on 2025/12/30.
//

#pragma once

#include <unordered_map>
#include <shared_mutex>
#include <functional>
#include <optional>

namespace zrt {

/**
 * @brief Thread-safe wrapper for std::unordered_map
 * @tparam Key Key type
 * @tparam Value Value type
 * @tparam Hash Hash function type
 * @tparam KeyEqual Key equality comparator type
 * @tparam Allocator Allocator type
 *
 * Uses std::shared_mutex for reader-writer lock pattern:
 * - Multiple threads can read concurrently
 * - Only one thread can write at a time
 */
template<
    typename Key,
    typename Value,
    typename Hash = std::hash<Key>,
    typename KeyEqual = std::equal_to<Key>,
    typename Allocator = std::allocator<std::pair<const Key, Value>>
>
class LockedHashMap {
public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<const Key, Value>;
    using size_type = std::size_t;
    using hash_map_type = std::unordered_map<Key, Value, Hash, KeyEqual, Allocator>;

    LockedHashMap() = default;
    ~LockedHashMap() = default;

    // Non-copyable and non-movable for safety
    LockedHashMap(const LockedHashMap&) = delete;
    LockedHashMap& operator=(const LockedHashMap&) = delete;
    LockedHashMap(LockedHashMap&&) = delete;
    LockedHashMap& operator=(LockedHashMap&&) = delete;

    /**
     * @brief Insert or update a key-value pair
     * @param key The key
     * @param value The value
     * @return true if inserted (new key), false if updated (existing key)
     */
    bool insert_or_assign(const Key& key, const Value& value) {
        std::lock_guard<std::shared_mutex> lock{m_mutex};
        auto [iter, inserted] = m_map.insert_or_assign(key, value);
        return inserted;
    }

    /**
     * @brief Insert a key-value pair only if key doesn't exist
     * @param key The key
     * @param value The value
     * @return true if inserted, false if key already exists
     */
    bool insert(const Key& key, const Value& value) {
        std::lock_guard<std::shared_mutex> lock{m_mutex};
        return m_map.insert({key, value}).second;
    }

    /**
     * @brief Emplace a key-value pair
     * @tparam Args Arguments to construct the value
     * @param key The key
     * @param args Arguments forwarded to Value constructor
     * @return true if inserted, false if key already exists
     */
    template<typename... Args>
    bool emplace(const Key& key, Args&&... args) {
        std::lock_guard<std::shared_mutex> lock{m_mutex};
        return m_map.emplace(key, Value{std::forward<Args>(args)...}).second;
    }

    /**
     * @brief Find a value by key
     * @param key The key to search
     * @return std::optional containing the value if found, empty otherwise
     */
    std::optional<Value> find(const Key& key) const {
        std::shared_lock<std::shared_mutex> lock{m_mutex};
        auto it = m_map.find(key);
        if (it != m_map.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /**
     * @brief Get value by key with default fallback
     * @param key The key to search
     * @param default_value Default value if key not found
     * @return The value if found, default_value otherwise
     */
    Value get_or_default(const Key& key, const Value& default_value) const {
        std::shared_lock<std::shared_mutex> lock{m_mutex};
        auto it = m_map.find(key);
        if (it != m_map.end()) {
            return it->second;
        }
        return default_value;
    }

    /**
     * @brief Access or insert element with the given key
     * @param key The key
     * @return The value associated with the key
     */
    Value operator[](const Key& key) {
        std::lock_guard<std::shared_mutex> lock{m_mutex};
        return m_map[key];
    }

    /**
     * @brief Check if key exists in the map
     * @param key The key to check
     * @return true if key exists, false otherwise
     */
    bool contains(const Key& key) const {
        std::shared_lock<std::shared_mutex> lock{m_mutex};
        return m_map.find(key) != m_map.end();
    }

    /**
     * @brief Erase element by key
     * @param key The key to erase
     * @return true if element was erased, false if key not found
     */
    bool erase(const Key& key) {
        std::lock_guard<std::shared_mutex> lock{m_mutex};
        return m_map.erase(key) > 0;
    }

    /**
     * @brief Get the number of elements
     * @return The size of the map
     */
    size_type size() const {
        std::shared_lock<std::shared_mutex> lock{m_mutex};
        return m_map.size();
    }

    /**
     * @brief Check if the map is empty
     * @return true if empty, false otherwise
     */
    bool empty() const {
        std::shared_lock<std::shared_mutex> lock{m_mutex};
        return m_map.empty();
    }

    /**
     * @brief Clear all elements
     */
    void clear() {
        std::lock_guard<std::shared_mutex> lock{m_mutex};
        m_map.clear();
    }

    /**
     * @brief Execute a function on each element (read-only)
     * @tparam Func Function type: void(const Key&, const Value&)
     * @param func The function to execute
     */
    template<typename Func>
    void for_each(Func&& func) const {
        std::shared_lock<std::shared_mutex> lock{m_mutex};
        for (const auto& [key, value] : m_map) {
            func(key, value);
        }
    }

    /**
     * @brief Execute a function on each element (mutable)
     * @tparam Func Function type: void(const Key&, Value&)
     * @param func The function to execute
     */
    template<typename Func>
    void for_each_mutable(Func&& func) {
        std::lock_guard<std::shared_mutex> lock{m_mutex};
        for (auto& [key, value] : m_map) {
            func(key, value);
        }
    }

    /**
     * @brief Update value if key exists using a function
     * @tparam Func Function type: Value(const Value&)
     * @param key The key to update
     * @param func Function that takes old value and returns new value
     * @return true if updated, false if key not found
     */
    template<typename Func>
    bool update_if_exists(const Key& key, Func&& func) {
        std::lock_guard<std::shared_mutex> lock{m_mutex};
        auto it = m_map.find(key);
        if (it != m_map.end()) {
            it->second = func(it->second);
            return true;
        }
        return false;
    }

    /**
     * @brief Get a snapshot of all keys
     * @return Vector containing all keys
     */
    std::vector<Key> keys() const {
        std::shared_lock<std::shared_mutex> lock{m_mutex};
        std::vector<Key> result{};
        result.reserve(m_map.size());
        for (const auto& [key, value] : m_map) {
            result.push_back(key);
        }
        return result;
    }

    /**
     * @brief Get a snapshot of all values
     * @return Vector containing all values
     */
    std::vector<Value> values() const {
        std::shared_lock<std::shared_mutex> lock{m_mutex};
        std::vector<Value> result{};
        result.reserve(m_map.size());
        for (const auto& [key, value] : m_map) {
            result.push_back(value);
        }
        return result;
    }

private:
    mutable std::shared_mutex m_mutex;
    hash_map_type m_map;
};

} // namespace zrt