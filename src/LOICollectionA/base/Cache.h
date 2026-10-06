#pragma once

#include <list>
#include <mutex>
#include <memory>
#include <optional>
#include <functional>
#include <shared_mutex>
#include <unordered_map>

template<typename Key, typename Value>
class LRUCache {
private:
    struct CacheEntry {
        std::shared_ptr<Value> mValue;
        
        explicit CacheEntry(std::shared_ptr<Value> val) : mValue(std::move(val)) {}
    };

    using EntryPtr = std::shared_ptr<CacheEntry>;
    using ListIterator = typename std::list<Key>::iterator;

    std::list<Key> mCaches;
    std::unordered_map<Key, std::pair<EntryPtr, ListIterator>> mCacheMap;
    size_t mCacheCapacity;

    mutable std::shared_mutex mMutex;

    void promote(const Key& key, std::pair<EntryPtr, ListIterator>& entryPair) {
        if (mCaches.size() >= mCacheCapacity) {
            Key mLastKey = mCaches.back();
            mCacheMap.erase(mLastKey);
            mCaches.pop_back();
        }

        mCaches.push_front(key);
        mCacheMap[key] = { entryPair.first, mCaches.begin() };
    }

public:
    explicit LRUCache(size_t cacheCapacity) : mCacheCapacity(cacheCapacity) {
        mCacheMap.reserve(cacheCapacity);
    }

    std::optional<std::shared_ptr<Value>> get(const Key& key) {
        std::unique_lock lock(mMutex);
        
        auto mCacheIt = mCacheMap.find(key);
        if (mCacheIt != mCacheMap.end()) {
            mCaches.erase(mCacheIt->second.second);
            mCaches.push_front(key);

            mCacheIt->second.second = mCaches.begin();

            return mCacheIt->second.first->mValue;
        }
        
        return std::nullopt;
    }

    void put(const Key& key, std::shared_ptr<Value> value) {
        std::unique_lock lock(mMutex);
        
        auto mCacheIt = mCacheMap.find(key);
        if (mCacheIt != mCacheMap.end()) {
            mCacheIt->second.first->mValue =std::move(value);
            mCaches.erase(mCacheIt->second.second);
            mCaches.push_front(key);

            mCacheIt->second.second = mCaches.begin();

            return;
        }
        
        auto entry = std::make_shared<CacheEntry>(std::move(value));
        
        if (mCaches.size() >= mCacheCapacity) {
            Key mLastKey = mCaches.back();
            mCacheMap.erase(mLastKey);
            mCaches.pop_back();
        }
        
        mCaches.push_front(key);
        mCacheMap[key] = { entry, mCaches.begin() };
    }

    void put(const Key& key, Value value) {
        put(key, std::make_shared<Value>(std::move(value)));
    }
    
    bool update(const Key& key, std::function<void(std::shared_ptr<Value>)> modifier) {
        std::unique_lock lock(mMutex);

        auto mCacheIt = mCacheMap.find(key);
        if (mCacheIt != mCacheMap.end()) {
            modifier(mCacheIt->second.first->mValue);

            mCaches.erase(mCacheIt->second.second);
            mCaches.push_front(key);

            mCacheIt->second.second = mCaches.begin();

            return true;
        }

        return false;
    }

    bool contains(const Key& key) const {
        std::shared_lock lock(mMutex);
        
        return mCacheMap.find(key) != mCacheMap.end();
    }

    bool erase(const Key& key) {
        std::unique_lock lock(mMutex);
        
        bool found = false;
        
        auto mCacheIt = mCacheMap.find(key);
        if (mCacheIt != mCacheMap.end()) {
            mCaches.erase(mCacheIt->second.second);
            mCacheMap.erase(mCacheIt);

            found = true;
        }
        
        return found;
    }

    void clear() {
        std::unique_lock lock(mMutex);

        mCaches.clear();
        mCacheMap.clear();
    }

    size_t size() const {
        std::shared_lock lock(mMutex);
        return mCaches.size();
    }

    bool empty() const {
        std::shared_lock lock(mMutex);
        return mCaches.empty();
    }

    std::vector<Key> caches() const {
        std::shared_lock lock(mMutex);
        return std::vector<Key>(mCaches.begin(), mCaches.end());
    }
};

template<typename Key, typename Value>
class LRUKCache {
private:
    struct CacheEntry {
        std::shared_ptr<Value> mValue;
        
        size_t mAccessCount;
        
        explicit CacheEntry(std::shared_ptr<Value> val) : mValue(std::move(val)), mAccessCount(0) {}
    };

    using EntryPtr = std::shared_ptr<CacheEntry>;
    using ListIterator = typename std::list<Key>::iterator;

    std::list<Key> mCaches;
    std::unordered_map<Key, std::pair<EntryPtr, ListIterator>> mCacheMap;
    size_t mCacheCapacity;

    std::list<Key> mHistorys;
    std::unordered_map<Key, std::pair<EntryPtr, ListIterator>> mHistoryMap;
    size_t mHistoryCapacity;

    const size_t mK;
    
    mutable std::shared_mutex mMutex;

public:
    explicit LRUKCache(size_t cacheCapacity, size_t historyCapacity, size_t k = 2)
        : mCacheCapacity(cacheCapacity), mHistoryCapacity(historyCapacity), mK(k) {
        if (k < 2)
            throw std::invalid_argument("k must be greater than or equal to 2");

        mCacheMap.reserve(cacheCapacity);
        mHistoryMap.reserve(historyCapacity);
    }

    std::optional<std::shared_ptr<Value>> get(const Key& key) {
        std::unique_lock lock(mMutex);
        
        auto mCacheIt = mCacheMap.find(key);
        if (mCacheIt != mCacheMap.end()) {
            mCaches.erase(mCacheIt->second.second);
            mCaches.push_front(key);

            mCacheIt->second.second = mCaches.begin();

            return mCacheIt->second.first->mValue;
        }
        
        auto mHistoryIt = mHistoryMap.find(key);
        if (mHistoryIt != mHistoryMap.end()) {
            auto entry = mHistoryIt->second.first;
            entry->mAccessCount++;
            
            if (entry->mAccessCount >= mK) {
                promote(key, mHistoryIt->second);
                
                mHistorys.erase(mHistoryIt->second.second);
                mHistoryMap.erase(mHistoryIt);
            } else {
                mHistorys.erase(mHistoryIt->second.second);
                mHistorys.push_front(key);
                mHistoryIt->second.second = mHistorys.begin();
            }
            
            return entry->mValue;
        }
        
        return std::nullopt;
    }

    void put(const Key& key, std::shared_ptr<Value> value) {
        std::unique_lock lock(mMutex);
        
        auto mCacheIt = mCacheMap.find(key);
        if (mCacheIt != mCacheMap.end()) {
            mCacheIt->second.first->mValue =std::move(value);
            mCaches.erase(mCacheIt->second.second);
            mCaches.push_front(key);

            mCacheIt->second.second = mCaches.begin();

            return;
        }
        
        auto mHistoryIt = mHistoryMap.find(key);
        if (mHistoryIt != mHistoryMap.end()) {
            auto entry = mHistoryIt->second.first;
            entry->mValue = std::move(value);
            entry->mAccessCount++;
            
            if (entry->mAccessCount >= mK) {
                promote(key, mHistoryIt->second);

                mHistorys.erase(mHistoryIt->second.second);
                mHistoryMap.erase(mHistoryIt);
            } else {
                mHistorys.erase(mHistoryIt->second.second);
                mHistorys.push_front(key);
                mHistoryIt->second.second = mHistorys.begin();
            }

            return;
        }
        
        auto entry = std::make_shared<CacheEntry>(std::move(value));
        entry->mAccessCount = 1;
        
        if (mHistorys.size() >= mHistoryCapacity) {
            Key lastKey = mHistorys.back();
            mHistoryMap.erase(lastKey);
            mHistorys.pop_back();
        }
        
        mHistorys.push_front(key);
        mHistoryMap[key] = {entry, mHistorys.begin()};
    }

    void put(const Key& key, Value value) {
        put(key, std::make_shared<Value>(std::move(value)));
    }
    
    bool update(const Key& key, std::function<void(std::shared_ptr<Value>)> modifier) {
        std::unique_lock lock(mMutex);

        auto mCacheIt = mCacheMap.find(key);
        if (mCacheIt != mCacheMap.end()) {
            modifier(mCacheIt->second.first->mValue);

            mCaches.erase(mCacheIt->second.second);
            mCaches.push_front(key);

            mCacheIt->second.second = mCaches.begin();

            return true;
        }

        auto mHistoryIt = mHistoryMap.find(key);
        if (mHistoryIt != mHistoryMap.end()) {
            auto entry = mHistoryIt->second.first;
            entry->mAccessCount++;

            modifier(entry->mValue);

            if (entry->mAccessCount >= mK) {
                promote(key, mHistoryIt->second);

                mHistorys.erase(mHistoryIt->second.second);
                mHistoryMap.erase(mHistoryIt);
            } else {
                mHistorys.erase(mHistoryIt->second.second);
                mHistorys.push_front(key);
                mHistoryIt->second.second = mHistorys.begin();
            }

            return true;
        }

        return false;
    }

    bool contains(const Key& key) const {
        std::shared_lock lock(mMutex);
        
        return mCacheMap.find(key) != mCacheMap.end();
    }

    bool erase(const Key& key) {
        std::unique_lock lock(mMutex);
        
        bool found = false;
        
        auto mCacheIt = mCacheMap.find(key);
        if (mCacheIt != mCacheMap.end()) {
            mCaches.erase(mCacheIt->second.second);
            mCacheMap.erase(mCacheIt);

            found = true;
        }
        
        auto mHistoryIt = mHistoryMap.find(key);
        if (mHistoryIt != mHistoryMap.end()) {
            mHistorys.erase(mHistoryIt->second.second);
            mHistoryMap.erase(mHistoryIt);

            found = true;
        }
        
        return found;
    }

    void clear() {
        std::unique_lock lock(mMutex);

        mCaches.clear();
        mCacheMap.clear();
        mHistorys.clear();
        mHistoryMap.clear();
    }
    
    size_t k() const {
        return mK;
    }

    bool empty() const {
        std::shared_lock lock(mMutex);
        return mCaches.empty() && mHistorys.empty();
    }

    std::vector<Key> caches() const {
        std::shared_lock lock(mMutex);
        return std::vector<Key>(mCaches.begin(), mCaches.end());
    }

    std::vector<Key> historys() const {
        std::shared_lock lock(mMutex);
        return std::vector<Key>(mHistorys.begin(), mHistorys.end());
    }

public:
    void promote(const Key& key, std::pair<EntryPtr, ListIterator>& entryPair) {
        if (mCaches.size() >= mCacheCapacity) {
            Key mLastKey = mCaches.back();
            mCacheMap.erase(mLastKey);
            mCaches.pop_back();
        }

        entryPair.first->mAccessCount = 0;
        
        mCaches.push_front(key);
        mCacheMap[key] = { entryPair.first, mCaches.begin() };
    }
};
