#pragma once
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

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#endif

#include "SwSharedMemorySignal.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace sw {
namespace ipc {

namespace detail {

inline uint64_t atomic_exchange_u64(uint64_t* p, uint64_t v) {
#ifdef _WIN32
    return static_cast<uint64_t>(::InterlockedExchange64(reinterpret_cast<volatile LONG64*>(p), static_cast<LONG64>(v)));
#elif defined(__clang__) || defined(__GNUC__)
    return __atomic_exchange_n(p, v, __ATOMIC_ACQ_REL);
#else
    uint64_t old = *p;
    *p = v;
    return old;
#endif
}

inline void atomic_store_u64(uint64_t* p, uint64_t v) {
#ifdef _WIN32
    (void)::InterlockedExchange64(reinterpret_cast<volatile LONG64*>(p), static_cast<LONG64>(v));
#elif defined(__clang__) || defined(__GNUC__)
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
#else
    *p = v;
#endif
}

inline uint64_t atomic_fetch_add_u64(uint64_t* p, uint64_t v) {
#ifdef _WIN32
    return static_cast<uint64_t>(
        ::InterlockedExchangeAdd64(reinterpret_cast<volatile LONG64*>(p), static_cast<LONG64>(v)));
#elif defined(__clang__) || defined(__GNUC__)
    return __atomic_fetch_add(p, v, __ATOMIC_ACQ_REL);
#else
    uint64_t old = *p;
    *p += v;
    return old;
#endif
}

inline bool atomic_cas_u64(uint64_t* p, uint64_t expected, uint64_t desired) {
#ifdef _WIN32
    return static_cast<uint64_t>(::InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(p),
                                                                static_cast<LONG64>(desired),
                                                                static_cast<LONG64>(expected))) == expected;
#elif defined(__clang__) || defined(__GNUC__)
    return __atomic_compare_exchange_n(p, &expected, desired, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
#else
    if (*p != expected) return false;
    *p = desired;
    return true;
#endif
}

inline uint32_t atomic_load_u32(const uint32_t* p) {
#ifdef _WIN32
    return static_cast<uint32_t>(
        ::InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(const_cast<uint32_t*>(p)), 0, 0));
#elif defined(__clang__) || defined(__GNUC__)
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
#else
    return *p;
#endif
}

inline uint32_t atomic_fetch_add_u32(uint32_t* p, uint32_t v) {
#ifdef _WIN32
    return static_cast<uint32_t>(::InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(p), static_cast<LONG>(v)));
#elif defined(__clang__) || defined(__GNUC__)
    return __atomic_fetch_add(p, v, __ATOMIC_ACQ_REL);
#else
    uint32_t old = *p;
    *p += v;
    return old;
#endif
}

inline uint32_t atomic_fetch_sub_u32(uint32_t* p, uint32_t v) {
#ifdef _WIN32
    return static_cast<uint32_t>(
        ::InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(p), -static_cast<LONG>(v)));
#elif defined(__clang__) || defined(__GNUC__)
    return __atomic_fetch_sub(p, v, __ATOMIC_ACQ_REL);
#else
    uint32_t old = *p;
    *p -= v;
    return old;
#endif
}

inline size_t alignUpSize(size_t v, size_t align) {
    if (align == 0) return v;
    const size_t r = v % align;
    return (r == 0) ? v : (v + (align - r));
}

template <typename T, typename = void>
struct NoCopyMetaTypeName_ {
    static const char* name() { return typeid(T).name(); }
};

template <typename T>
struct NoCopyMetaTypeName_<T, decltype((void)T::kTypeName, void())> {
    static const char* name() { return T::kTypeName; }
};

class ShmMappingDyn {
public:
    static std::shared_ptr<ShmMappingDyn> openExisting(const SwString& shmName) {
        bool created = false;
        return openOrCreate_(shmName, /*createBytes=*/0, /*allowCreate=*/false, created);
    }

    static std::shared_ptr<ShmMappingDyn> openOrCreate(const SwString& shmName, size_t createBytes, bool& createdOut) {
        createdOut = false;
        return openOrCreate_(shmName, createBytes, /*allowCreate=*/true, createdOut);
    }

#ifndef _WIN32
    static void destroy(const SwString& shmName) {
        ::shm_unlink(shmName.toStdString().c_str());
    }
#endif

    ~ShmMappingDyn() {
#ifdef _WIN32
        if (mem_) {
            ::UnmapViewOfFile(mem_);
        }
        if (hMap_) {
            ::CloseHandle(hMap_);
        }
#else
        if (mem_ && mem_ != MAP_FAILED) {
            ::munmap(mem_, size_);
        }
#endif
    }

    void* data() const { return mem_; }
    size_t size() const { return size_; }
    const SwString& name() const { return name_; }

private:
    static std::shared_ptr<ShmMappingDyn> openOrCreate_(const SwString& shmName,
                                                       size_t createBytes,
                                                       bool allowCreate,
                                                       bool& createdOut) {
        if (shmName.isEmpty()) throw std::runtime_error("ShmMappingDyn: empty shm name");

#ifdef _WIN32
        const std::string nameA = shmName.toStdString();
        HANDLE hMap = NULL;

        if (allowCreate) {
            if (createBytes == 0) throw std::runtime_error("ShmMappingDyn: createBytes=0");
            if (createBytes > 0xFFFFFFFFu) throw std::runtime_error("ShmMappingDyn: mapping >4GB not supported");
            hMap = ::CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                        static_cast<DWORD>(createBytes), nameA.c_str());
        } else {
            hMap = ::OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, nameA.c_str());
        }

        if (!hMap) {
            throw std::runtime_error(allowCreate ? "CreateFileMapping failed" : "OpenFileMapping failed");
        }

        if (allowCreate) {
            const DWORD lastErr = ::GetLastError();
            createdOut = (lastErr != ERROR_ALREADY_EXISTS);
        }

        void* mem = ::MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (!mem) {
            ::CloseHandle(hMap);
            throw std::runtime_error("MapViewOfFile failed");
        }

        size_t mappedBytes = 0;
        MEMORY_BASIC_INFORMATION mbi;
        std::memset(&mbi, 0, sizeof(mbi));
        if (::VirtualQuery(mem, &mbi, sizeof(mbi)) == 0) {
            ::UnmapViewOfFile(mem);
            ::CloseHandle(hMap);
            throw std::runtime_error("VirtualQuery failed");
        }
        mappedBytes = static_cast<size_t>(mbi.RegionSize);

        return std::shared_ptr<ShmMappingDyn>(new ShmMappingDyn(shmName, mem, mappedBytes, hMap));
#else
        const std::string nameA = shmName.toStdString();
        int fd = -1;

        if (!allowCreate) {
            fd = ::shm_open(nameA.c_str(), O_RDWR, 0666);
            if (fd < 0) throw std::runtime_error("shm_open(existing) failed");
        } else {
            fd = ::shm_open(nameA.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
            if (fd >= 0) {
                createdOut = true;
                if (createBytes == 0) {
                    ::close(fd);
                    ::shm_unlink(nameA.c_str());
                    throw std::runtime_error("ShmMappingDyn: createBytes=0");
                }
                if (::ftruncate(fd, static_cast<off_t>(createBytes)) != 0) {
                    ::close(fd);
                    ::shm_unlink(nameA.c_str());
                    throw std::runtime_error("ftruncate(shm) failed");
                }
            } else if (errno == EEXIST) {
                fd = ::shm_open(nameA.c_str(), O_RDWR, 0666);
                if (fd < 0) throw std::runtime_error("shm_open(existing) failed");
            } else {
                throw std::runtime_error("shm_open failed");
            }
        }

        struct stat st;
        std::memset(&st, 0, sizeof(st));
        if (::fstat(fd, &st) != 0) {
            ::close(fd);
            throw std::runtime_error("fstat(shm) failed");
        }
        const size_t bytes = static_cast<size_t>(st.st_size);
        if (bytes == 0) {
            ::close(fd);
            throw std::runtime_error("ShmMappingDyn: shm size is 0");
        }

        void* mem = ::mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (mem == MAP_FAILED) throw std::runtime_error("mmap failed");

        return std::shared_ptr<ShmMappingDyn>(new ShmMappingDyn(shmName, mem, bytes));
#endif
    }

#ifdef _WIN32
    ShmMappingDyn(const SwString& name, void* mem, size_t bytes, HANDLE hMap)
        : name_(name), mem_(mem), size_(bytes), hMap_(hMap) {}
#else
    ShmMappingDyn(const SwString& name, void* mem, size_t bytes)
        : name_(name), mem_(mem), size_(bytes) {}
#endif

    SwString name_;
    void* mem_{nullptr};
    size_t size_{0};
#ifdef _WIN32
    HANDLE hMap_{NULL};
#endif
};

} // namespace detail

// Generic shared-memory ring buffer for large payloads (0-copy reader, best-effort 0-copy producer).
// - Data: fixed-size slots (maxBytes), variable payload size per publish (<= maxBytes)
// - Metadata: user-defined trivially-copyable struct MetaT (no pointers/vtable)
// - Multi-consumer keep-up: consumers register a cursor (next seq needed). Publisher never overwrites data
//   still needed by any active consumer; it will drop publishes instead.
template <typename MetaT, uint32_t MaxConsumers = 64>
class NoCopyRingBuffer {
public:
    static_assert(MaxConsumers > 0, "NoCopyRingBuffer: MaxConsumers must be > 0");
    static_assert(std::is_trivially_copyable<MetaT>::value, "NoCopyRingBuffer: MetaT must be trivially copyable");

    class Consumer;
    class ReadLease;
    class WriteLease;

    NoCopyRingBuffer() = default;
    NoCopyRingBuffer(const NoCopyRingBuffer&) = delete;
    NoCopyRingBuffer& operator=(const NoCopyRingBuffer&) = delete;
    NoCopyRingBuffer(NoCopyRingBuffer&&) noexcept = default;
    NoCopyRingBuffer& operator=(NoCopyRingBuffer&&) noexcept = default;

    NoCopyRingBuffer(Registry& reg, const SwString& streamName, uint32_t capacity, uint32_t maxBytes);

    static NoCopyRingBuffer create(Registry& reg, const SwString& streamName, uint32_t capacity, uint32_t maxBytes);
    static NoCopyRingBuffer open(Registry& reg, const SwString& streamName);

    static SwStringList streamsInRegistry(const SwString& domain, const SwString& object);

    bool isValid() const { return map_ != nullptr; }

    uint32_t capacity() const { return header_() ? header_()->capacity : 0; }
    uint32_t maxBytesPerItem() const { return header_() ? header_()->maxBytes : 0; }
    uint64_t lastSeq() const { return header_() ? detail::atomic_load_u64(&header_()->lastSeq) : 0; }
    uint64_t droppedCount() const { return header_() ? detail::atomic_load_u64(&header_()->dropped) : 0; }
    uint32_t maxConsumers() const { return header_() ? header_()->maxConsumers : 0; }
    uint32_t consumerTtlMs() const { return header_() ? header_()->consumerTtlMs : 0; }

    SwString shmName() const { return shmName_; }
    SwString notifySignalName() const { return notifySignalName_; }
    SwString registrySignalName() const { return rbRegistrySignalName_; }

    Consumer consumer() const;

    WriteLease beginWrite();
    bool publishCopy(const void* data, uint32_t bytes, const MetaT& meta);

    ReadLease acquire(uint64_t seq) const;
    ReadLease readLatest() const;

    template <typename Fn>
    sw::ipc::Signal<uint64_t>::Subscription connect(Consumer& c, Fn cb, bool fireInitial = true, int timeoutMs = 0) {
        if (!notify_ || !c.isValid()) return sw::ipc::Signal<uint64_t>::Subscription();
        return notify_->connect([&c, cb](uint64_t seq) mutable {
            ReadLease v = c.acquire(seq);
            if (!v.isValid()) return;
            cb(v.seq(), std::move(v));
        }, fireInitial, timeoutMs);
    }

    sw::ipc::Signal<uint64_t>* notifier() { return notify_.get(); }
    const sw::ipc::Signal<uint64_t>* notifier() const { return notify_.get(); }

private:
    static const uint32_t kMagic = 0x4E434231u;   // 'NCB1'
    static const uint32_t kVersion = 1;
    static const uint64_t kClaimingConsumerId = 1;
    static const uint32_t kDefaultConsumerTtlMs = 5000;

    struct ConsumerEntry;
    struct Header;
    struct SlotMeta;

    static const char* metaTypeName_() { return detail::NoCopyMetaTypeName_<MetaT>::name(); }
    static uint64_t metaTypeId_() { return detail::fnv1a64(std::string(metaTypeName_())); }
    static uint64_t typeId_() {
        return detail::fnv1a64(std::string("sw::ipc::NoCopyRingBufferV1|") + std::string(metaTypeName_()));
    }

    static SwString makeNotifySignalName_(const SwString& streamName) { return SwString("__rb__|") + streamName; }
    static SwString makeRbRegistrySignalName_(const SwString& streamName) { return SwString("__rbmap__|") + streamName; }

    static SwString registryTypeName_(uint32_t capacity, uint32_t maxBytes, const SwString& notifySig);
    static size_t computeTotalBytes_(uint32_t capacity,
                                     uint32_t maxBytes,
                                     uint32_t& metaOffsetOut,
                                     uint32_t& dataOffsetOut,
                                     uint32_t& strideOut);

    static ConsumerEntry* consumerEntry_(Header* H, uint32_t index);

    void initCreate_(Registry& reg, const SwString& streamName, uint32_t capacity, uint32_t maxBytes);
    void initOpen_(Registry& reg, const SwString& streamName);
    void validateHeaderOrThrow_(const Header& H) const;

    Header* header_() const {
        if (!map_) return nullptr;
        return static_cast<Header*>(map_->data());
    }

    SlotMeta* slotMeta_(uint32_t index) const;
    static SlotMeta* slotMeta_(Header* H, uint32_t index);

    uint8_t* slotData_(uint32_t index) const;
    static uint8_t* slotData_(Header* H, uint32_t index);

    uint64_t minConsumerCursor_() const;
    static void keepUpInMapping_(Header* H, uint32_t consumerIndex, uint64_t consumerId, uint64_t nextSeq);

    bool commitWrite_(WriteLease& w, uint32_t bytes);
    void cancelWrite_(WriteLease& w);

    Registry* reg_{nullptr};
    SwString streamName_;
    SwString rbRegistrySignalName_;
    SwString notifySignalName_;
    SwString shmName_;

    std::shared_ptr<detail::ShmMappingDyn> map_;
    std::unique_ptr<sw::ipc::Signal<uint64_t>> notify_;
};

template <typename MetaT, uint32_t MaxConsumers>
struct NoCopyRingBuffer<MetaT, MaxConsumers>::ConsumerEntry {
    uint64_t id{0};          // 0=free, 1=claiming
    uint32_t pid{0};
    uint32_t reserved{0};
    uint64_t cursor{0};      // next seq needed
    uint64_t lastSeenMs{0};  // heartbeat
};

template <typename MetaT, uint32_t MaxConsumers>
struct NoCopyRingBuffer<MetaT, MaxConsumers>::Header {
    uint32_t magic{0};
    uint32_t version{0};
    uint64_t typeId{0};
    uint64_t metaTypeId{0};
    uint64_t totalBytes{0};
    uint32_t capacity{0};
    uint32_t maxBytes{0};
    uint32_t metaOffset{0};
    uint32_t dataOffset{0};
    uint32_t slotMetaBytes{0};
    uint32_t slotStrideBytes{0};
    uint32_t maxConsumers{0};
    uint32_t consumerTtlMs{0};
    uint64_t nextSeq{0};
    uint64_t lastSeq{0};
    uint64_t dropped{0};
    ConsumerEntry consumers[MaxConsumers]{};
    uint8_t reserved[64]{};
};

template <typename MetaT, uint32_t MaxConsumers>
struct NoCopyRingBuffer<MetaT, MaxConsumers>::SlotMeta {
    uint64_t seq{0};
    uint32_t refCount{0};
    uint32_t bytes{0};
    uint64_t publishTimeUs{0};
    MetaT meta{};
};

template <typename MetaT, uint32_t MaxConsumers>
class NoCopyRingBuffer<MetaT, MaxConsumers>::WriteLease {
public:
    WriteLease() = default;
    WriteLease(const WriteLease&) = delete;
    WriteLease& operator=(const WriteLease&) = delete;

    WriteLease(WriteLease&& o) noexcept { *this = std::move(o); }
    WriteLease& operator=(WriteLease&& o) noexcept {
        if (this == &o) return *this;
        cancel();
        owner_ = o.owner_;
        idx_ = o.idx_;
        seq_ = o.seq_;
        oldSeq_ = o.oldSeq_;
        slot_ = o.slot_;
        data_ = o.data_;
        capacityBytes_ = o.capacityBytes_;
        map_ = std::move(o.map_);
        committed_ = o.committed_;

        o.owner_ = nullptr;
        o.idx_ = 0;
        o.seq_ = 0;
        o.oldSeq_ = 0;
        o.slot_ = nullptr;
        o.data_ = nullptr;
        o.capacityBytes_ = 0;
        o.committed_ = true;
        return *this;
    }

    ~WriteLease() { cancel(); }

    bool isValid() const { return owner_ && slot_ && data_ && seq_ != 0 && !committed_; }
    uint64_t seq() const { return seq_; }
    uint8_t* data() { return data_; }
    const uint8_t* data() const { return data_; }
    uint32_t capacityBytes() const { return capacityBytes_; }
    MetaT& meta() { return slot_->meta; }
    const MetaT& meta() const { return slot_->meta; }

    bool commit(uint32_t bytes) {
        if (!isValid()) return false;
        return owner_->commitWrite_(*this, bytes);
    }

    void cancel() {
        if (!owner_ || committed_) return;
        owner_->cancelWrite_(*this);
        committed_ = true;
        owner_ = nullptr;
    }

private:
    friend class NoCopyRingBuffer;
    NoCopyRingBuffer* owner_{nullptr};
    uint32_t idx_{0};
    uint64_t seq_{0};
    uint64_t oldSeq_{0};
    SlotMeta* slot_{nullptr};
    uint8_t* data_{nullptr};
    uint32_t capacityBytes_{0};
    std::shared_ptr<detail::ShmMappingDyn> map_;
    bool committed_{true};
};

template <typename MetaT, uint32_t MaxConsumers>
class NoCopyRingBuffer<MetaT, MaxConsumers>::ReadLease {
public:
    ReadLease() = default;
    ReadLease(const ReadLease&) = delete;
    ReadLease& operator=(const ReadLease&) = delete;

    ReadLease(ReadLease&& o) noexcept { *this = std::move(o); }
    ReadLease& operator=(ReadLease&& o) noexcept {
        if (this == &o) return *this;
        release_();
        map_ = std::move(o.map_);
        slot_ = o.slot_;
        data_ = o.data_;
        seq_ = o.seq_;
        consumerIndex_ = o.consumerIndex_;
        consumerId_ = o.consumerId_;

        o.slot_ = nullptr;
        o.data_ = nullptr;
        o.seq_ = 0;
        o.consumerIndex_ = kInvalidIndex;
        o.consumerId_ = 0;
        return *this;
    }

    ~ReadLease() { release_(); }

    bool isValid() const { return map_ && slot_ && data_ && seq_ != 0; }
    uint64_t seq() const { return seq_; }
    const uint8_t* data() const { return data_; }
    uint8_t* data() { return data_; }
    uint32_t bytes() const { return slot_ ? slot_->bytes : 0; }
    const MetaT& meta() const { return slot_->meta; }
    MetaT& meta() { return slot_->meta; }
    uint64_t publishTimeUs() const { return slot_ ? slot_->publishTimeUs : 0; }

private:
    friend class NoCopyRingBuffer;
    friend class Consumer;

    static const uint32_t kInvalidIndex = 0xFFFFFFFFu;

    ReadLease(std::shared_ptr<detail::ShmMappingDyn> map,
              SlotMeta* slot,
              uint8_t* data,
              uint64_t seq,
              uint32_t consumerIndex,
              uint64_t consumerId)
        : map_(std::move(map))
        , slot_(slot)
        , data_(data)
        , seq_(seq)
        , consumerIndex_(consumerIndex)
        , consumerId_(consumerId) {}

    void release_() {
        if (!slot_) return;
        (void)detail::atomic_fetch_sub_u32(&slot_->refCount, 1);

        if (consumerId_ != 0 && consumerIndex_ != kInvalidIndex) {
            Header* H = static_cast<Header*>(map_->data());
            keepUpInMapping_(H, consumerIndex_, consumerId_, seq_ + 1);
        }

        slot_ = nullptr;
        data_ = nullptr;
        seq_ = 0;
        consumerIndex_ = kInvalidIndex;
        consumerId_ = 0;
        map_.reset();
    }

    std::shared_ptr<detail::ShmMappingDyn> map_;
    SlotMeta* slot_{nullptr};
    uint8_t* data_{nullptr};
    uint64_t seq_{0};
    uint32_t consumerIndex_{kInvalidIndex};
    uint64_t consumerId_{0};
};

template <typename MetaT, uint32_t MaxConsumers>
class NoCopyRingBuffer<MetaT, MaxConsumers>::Consumer {
public:
    Consumer() = default;
    Consumer(const Consumer&) = delete;
    Consumer& operator=(const Consumer&) = delete;
    Consumer(Consumer&&) noexcept = default;
    Consumer& operator=(Consumer&&) noexcept = default;

    ~Consumer() { unregister(); }

    bool isValid() const { return map_ && consumerId_ != 0 && consumerIndex_ != ReadLease::kInvalidIndex; }

    uint64_t cursor() const {
        if (!isValid()) return 0;
        Header* H = static_cast<Header*>(map_->data());
        ConsumerEntry* e = consumerEntry_(H, consumerIndex_);
        if (!e) return 0;
        if (detail::atomic_load_u64(&e->id) != consumerId_) return 0;
        return detail::atomic_load_u64(&e->cursor);
    }

    void keepUp(uint64_t nextSeq) {
        if (!ensureRegistered_()) return;
        if (nextSeq == 0) return;

        Header* H = static_cast<Header*>(map_->data());
        ConsumerEntry* e = consumerEntry_(H, consumerIndex_);
        if (!e) return;

        for (;;) {
            const uint64_t cur = detail::atomic_load_u64(&e->cursor);
            if (nextSeq <= cur) break;
            if (detail::atomic_cas_u64(&e->cursor, cur, nextSeq)) break;
        }
        detail::atomic_store_u64(&e->lastSeenMs, detail::nowMs());
    }

    ReadLease acquire(uint64_t seq) {
        ReadLease out;
        if (!ensureRegistered_()) return out;
        if (seq == 0) return out;

        Header* H = static_cast<Header*>(map_->data());
        if (!H || H->capacity == 0) return out;

        ConsumerEntry* e = consumerEntry_(H, consumerIndex_);
        if (e) detail::atomic_store_u64(&e->lastSeenMs, detail::nowMs());

        const uint32_t idx = static_cast<uint32_t>(seq % static_cast<uint64_t>(H->capacity));
        SlotMeta* slot = slotMeta_(H, idx);
        if (!slot) return out;

        const uint64_t have = detail::atomic_load_u64(&slot->seq);
        if (have != seq) return out;

        (void)detail::atomic_fetch_add_u32(&slot->refCount, 1);
        const uint64_t have2 = detail::atomic_load_u64(&slot->seq);
        if (have2 != seq) {
            (void)detail::atomic_fetch_sub_u32(&slot->refCount, 1);
            return out;
        }

        uint8_t* data = slotData_(H, idx);
        if (!data) {
            (void)detail::atomic_fetch_sub_u32(&slot->refCount, 1);
            return out;
        }

        return ReadLease(map_, slot, data, seq, consumerIndex_, consumerId_);
    }

    ReadLease readLatest() {
        if (!ensureRegistered_()) return ReadLease();
        Header* H = static_cast<Header*>(map_->data());
        if (!H) return ReadLease();
        const uint64_t seq = detail::atomic_load_u64(&H->lastSeq);
        return acquire(seq);
    }

    void unregister() {
        if (!map_) return;

        if (consumerIndex_ != ReadLease::kInvalidIndex && consumerId_ != 0) {
            Header* H = static_cast<Header*>(map_->data());
            ConsumerEntry* e = consumerEntry_(H, consumerIndex_);
            if (e && detail::atomic_load_u64(&e->id) == consumerId_) {
                detail::atomic_store_u64(&e->id, 0);
            }
        }

        map_.reset();
        consumerIndex_ = ReadLease::kInvalidIndex;
        consumerId_ = 0;
    }

private:
    friend class NoCopyRingBuffer;

    Consumer(std::shared_ptr<detail::ShmMappingDyn> map, uint32_t consumerIndex, uint64_t consumerId)
        : map_(std::move(map)), consumerIndex_(consumerIndex), consumerId_(consumerId) {}

    static uint64_t makeConsumerId_() {
        static uint32_t counter = 0;
        const uint32_t pid = detail::currentPid();
        const uint32_t c = detail::atomic_fetch_add_u32(&counter, 1) + 1;
        return (static_cast<uint64_t>(pid) << 32) | static_cast<uint64_t>(c);
    }

    bool ensureRegistered_() {
        if (!map_) return false;
        Header* H = static_cast<Header*>(map_->data());
        if (!H) return false;

        if (consumerId_ != 0 && consumerIndex_ != ReadLease::kInvalidIndex) {
            ConsumerEntry* e = consumerEntry_(H, consumerIndex_);
            if (e && detail::atomic_load_u64(&e->id) == consumerId_) return true;
        }

        *this = registerInMapping_(map_);
        return isValid();
    }

    static Consumer registerInMapping_(const std::shared_ptr<detail::ShmMappingDyn>& map) {
        if (!map) return Consumer();

        Header* H = static_cast<Header*>(map->data());
        if (!H) return Consumer();

        const uint64_t myId = makeConsumerId_();
        const uint32_t myPid = detail::currentPid();
        const uint64_t now = detail::nowMs();
        const uint64_t ttl = static_cast<uint64_t>(H->consumerTtlMs ? H->consumerTtlMs : kDefaultConsumerTtlMs);

        const uint32_t n = (std::min<uint32_t>)(H->maxConsumers, MaxConsumers);

        // Prefer reusing an existing entry with our id.
        for (uint32_t i = 0; i < n; ++i) {
            ConsumerEntry* e = consumerEntry_(H, i);
            if (!e) continue;
            if (detail::atomic_load_u64(&e->id) != myId) continue;

            e->pid = myPid;
            detail::atomic_store_u64(&e->cursor, detail::atomic_load_u64(&H->lastSeq) + 1);
            detail::atomic_store_u64(&e->lastSeenMs, now);
            return Consumer(map, i, myId);
        }

        // Find a free/stale entry.
        for (uint32_t i = 0; i < n; ++i) {
            ConsumerEntry* e = consumerEntry_(H, i);
            if (!e) continue;

            const uint64_t id = detail::atomic_load_u64(&e->id);
            if (id == 0) {
                if (!detail::atomic_cas_u64(&e->id, 0, kClaimingConsumerId)) continue;
            } else if (id == kClaimingConsumerId) {
                continue;
            } else {
                const uint64_t seen = detail::atomic_load_u64(&e->lastSeenMs);
                const bool stale = (seen == 0) || (now > seen && (now - seen) > ttl);
                if (!stale) continue;
                if (!detail::atomic_cas_u64(&e->id, id, kClaimingConsumerId)) continue;
            }

            // Claimed: initialize, then publish id last.
            e->pid = myPid;
            e->reserved = 0;
            detail::atomic_store_u64(&e->cursor, detail::atomic_load_u64(&H->lastSeq) + 1);
            detail::atomic_store_u64(&e->lastSeenMs, now);
            detail::atomic_store_u64(&e->id, myId);
            return Consumer(map, i, myId);
        }

        return Consumer();
    }

    std::shared_ptr<detail::ShmMappingDyn> map_;
    uint32_t consumerIndex_{ReadLease::kInvalidIndex};
    uint64_t consumerId_{0};
};

template <typename MetaT, uint32_t MaxConsumers>
SwString NoCopyRingBuffer<MetaT, MaxConsumers>::registryTypeName_(uint32_t capacity,
                                                                  uint32_t maxBytes,
                                                                  const SwString& notifySig) {
    // Keep it short (RegistryEntry::typeName is 256 bytes).
    // Format: {"kind":"rb","v":1,"cap":100,"max":10485760,"metaBytes":48,"metaId":"...","notify":"__rb__|cam","cons":64}
    SwString s("{\"kind\":\"rb\",\"v\":1,\"cap\":");
    s += SwString::number(static_cast<int>(capacity));
    s += SwString(",\"max\":");
    s += SwString::number(static_cast<int>(maxBytes));
    s += SwString(",\"metaBytes\":");
    s += SwString::number(static_cast<int>(sizeof(MetaT)));
    s += SwString(",\"metaId\":\"");
    s += detail::hex64(metaTypeId_());
    s += SwString("\",\"notify\":\"");
    s += notifySig;
    s += SwString("\",\"cons\":");
    s += SwString::number(static_cast<int>(MaxConsumers));
    s += SwString("}");
    return s;
}

template <typename MetaT, uint32_t MaxConsumers>
size_t NoCopyRingBuffer<MetaT, MaxConsumers>::computeTotalBytes_(uint32_t capacity,
                                                                 uint32_t maxBytes,
                                                                 uint32_t& metaOffsetOut,
                                                                 uint32_t& dataOffsetOut,
                                                                 uint32_t& strideOut) {
    const size_t align = 64;
    const size_t headerBytes = detail::alignUpSize(sizeof(Header), align);
    const size_t metaBytes = detail::alignUpSize(static_cast<size_t>(capacity) * sizeof(SlotMeta), align);
    const size_t stride = detail::alignUpSize(static_cast<size_t>(maxBytes), align);
    const size_t dataBytes = static_cast<size_t>(capacity) * stride;

    metaOffsetOut = static_cast<uint32_t>(headerBytes);
    dataOffsetOut = static_cast<uint32_t>(headerBytes + metaBytes);
    strideOut = static_cast<uint32_t>(stride);
    return headerBytes + metaBytes + dataBytes;
}

template <typename MetaT, uint32_t MaxConsumers>
typename NoCopyRingBuffer<MetaT, MaxConsumers>::ConsumerEntry* NoCopyRingBuffer<MetaT, MaxConsumers>::consumerEntry_(
    Header* H,
    uint32_t index) {
    if (!H) return nullptr;
    if (index >= H->maxConsumers) return nullptr;
    if (index >= MaxConsumers) return nullptr;
    return &H->consumers[index];
}

template <typename MetaT, uint32_t MaxConsumers>
typename NoCopyRingBuffer<MetaT, MaxConsumers>::SlotMeta* NoCopyRingBuffer<MetaT, MaxConsumers>::slotMeta_(
    uint32_t index) const {
    Header* H = header_();
    return slotMeta_(H, index);
}

template <typename MetaT, uint32_t MaxConsumers>
typename NoCopyRingBuffer<MetaT, MaxConsumers>::SlotMeta* NoCopyRingBuffer<MetaT, MaxConsumers>::slotMeta_(Header* H,
                                                                                                           uint32_t index) {
    if (!H) return nullptr;
    if (index >= H->capacity) return nullptr;
    uint8_t* base = reinterpret_cast<uint8_t*>(H);
    return reinterpret_cast<SlotMeta*>(base + H->metaOffset + static_cast<size_t>(index) * sizeof(SlotMeta));
}

template <typename MetaT, uint32_t MaxConsumers>
uint8_t* NoCopyRingBuffer<MetaT, MaxConsumers>::slotData_(uint32_t index) const {
    Header* H = header_();
    return slotData_(H, index);
}

template <typename MetaT, uint32_t MaxConsumers>
uint8_t* NoCopyRingBuffer<MetaT, MaxConsumers>::slotData_(Header* H, uint32_t index) {
    if (!H) return nullptr;
    if (index >= H->capacity) return nullptr;
    uint8_t* base = reinterpret_cast<uint8_t*>(H);
    return base + H->dataOffset + static_cast<size_t>(index) * static_cast<size_t>(H->slotStrideBytes);
}

template <typename MetaT, uint32_t MaxConsumers>
uint64_t NoCopyRingBuffer<MetaT, MaxConsumers>::minConsumerCursor_() const {
    Header* H = header_();
    if (!H) return 0;

    uint64_t minCur = detail::atomic_load_u64(&H->lastSeq) + 1;
    const uint64_t now = detail::nowMs();
    const uint64_t ttl = static_cast<uint64_t>(H->consumerTtlMs ? H->consumerTtlMs : kDefaultConsumerTtlMs);

    const uint32_t n = (std::min<uint32_t>)(H->maxConsumers, MaxConsumers);
    for (uint32_t i = 0; i < n; ++i) {
        ConsumerEntry* e = consumerEntry_(H, i);
        if (!e) continue;

        const uint64_t id = detail::atomic_load_u64(&e->id);
        if (id == 0 || id == kClaimingConsumerId) continue;

        const uint64_t seen = detail::atomic_load_u64(&e->lastSeenMs);
        if (seen == 0) continue;
        if (now > seen && (now - seen) > ttl) continue;

        const uint64_t cur = detail::atomic_load_u64(&e->cursor);
        if (cur == 0) continue;
        if (cur < minCur) minCur = cur;
    }
    return minCur;
}

template <typename MetaT, uint32_t MaxConsumers>
void NoCopyRingBuffer<MetaT, MaxConsumers>::keepUpInMapping_(Header* H,
                                                             uint32_t consumerIndex,
                                                             uint64_t consumerId,
                                                             uint64_t nextSeq) {
    if (!H || nextSeq == 0) return;
    if (consumerIndex >= H->maxConsumers || consumerIndex >= MaxConsumers) return;

    ConsumerEntry* e = &H->consumers[consumerIndex];
    if (detail::atomic_load_u64(&e->id) != consumerId) return;

    for (;;) {
        const uint64_t cur = detail::atomic_load_u64(&e->cursor);
        if (nextSeq <= cur) break;
        if (detail::atomic_cas_u64(&e->cursor, cur, nextSeq)) break;
    }
    detail::atomic_store_u64(&e->lastSeenMs, detail::nowMs());
}

template <typename MetaT, uint32_t MaxConsumers>
bool NoCopyRingBuffer<MetaT, MaxConsumers>::commitWrite_(WriteLease& w, uint32_t bytes) {
    if (!isValid()) return false;
    if (w.owner_ != this || w.committed_) return false;
    if (!w.slot_ || !w.data_) return false;

    Header* H = header_();
    if (!H) return false;
    if (bytes == 0 || static_cast<uint64_t>(bytes) > static_cast<uint64_t>(H->maxBytes)) {
        cancelWrite_(w);
        w.committed_ = true;
        w.owner_ = nullptr;
        return false;
    }

    w.slot_->bytes = bytes;
    w.slot_->publishTimeUs = detail::nowMs() * 1000ull;

    detail::atomic_store_u64(&w.slot_->seq, w.seq_);
    detail::atomic_store_u64(&H->lastSeq, w.seq_);
    if (notify_) notify_->publish(w.seq_);

    w.committed_ = true;
    w.owner_ = nullptr;
    return true;
}

template <typename MetaT, uint32_t MaxConsumers>
void NoCopyRingBuffer<MetaT, MaxConsumers>::cancelWrite_(WriteLease& w) {
    Header* H = header_();
    if (H) (void)detail::atomic_fetch_add_u64(&H->dropped, 1);
    // Keep slot unpublished (seq=0). The old content was potentially overwritten by the producer.
    (void)w.oldSeq_;
}

template <typename MetaT, uint32_t MaxConsumers>
void NoCopyRingBuffer<MetaT, MaxConsumers>::initCreate_(Registry& reg,
                                                        const SwString& streamName,
                                                        uint32_t capacity,
                                                        uint32_t maxBytes) {
    if (capacity == 0) throw std::runtime_error("NoCopyRingBuffer: capacity=0");
    if (maxBytes == 0) throw std::runtime_error("NoCopyRingBuffer: maxBytes=0");

    reg_ = &reg;
    streamName_ = streamName;
    rbRegistrySignalName_ = makeRbRegistrySignalName_(streamName_);
    notifySignalName_ = makeNotifySignalName_(streamName_);

    shmName_ = detail::make_shm_name(reg.domain(), reg.object(), rbRegistrySignalName_);

    uint32_t metaOffset = 0, dataOffset = 0, stride = 0;
    const size_t totalBytes = computeTotalBytes_(capacity, maxBytes, metaOffset, dataOffset, stride);

    bool created = false;
    map_ = detail::ShmMappingDyn::openOrCreate(shmName_, totalBytes, created);
    if (!map_) throw std::runtime_error("NoCopyRingBuffer: failed to map SHM");
    if (map_->size() < totalBytes) throw std::runtime_error("NoCopyRingBuffer: SHM mapping smaller than requested layout");

    Header* H = header_();
    if (!H) throw std::runtime_error("NoCopyRingBuffer: null header");

    if (created || (H->magic == 0 && H->version == 0)) {
        std::memset(H, 0, sizeof(Header));
        H->magic = kMagic;
        H->version = kVersion;
        H->typeId = typeId_();
        H->metaTypeId = metaTypeId_();
        H->totalBytes = static_cast<uint64_t>(totalBytes);
        H->capacity = capacity;
        H->maxBytes = maxBytes;
        H->metaOffset = metaOffset;
        H->dataOffset = dataOffset;
        H->slotMetaBytes = static_cast<uint32_t>(sizeof(SlotMeta));
        H->slotStrideBytes = stride;
        H->maxConsumers = MaxConsumers;
        H->consumerTtlMs = kDefaultConsumerTtlMs;
        H->nextSeq = 0;
        H->lastSeq = 0;
        H->dropped = 0;

        SlotMeta* metas = reinterpret_cast<SlotMeta*>(reinterpret_cast<uint8_t*>(map_->data()) + H->metaOffset);
        for (uint32_t i = 0; i < capacity; ++i) {
            std::memset(&metas[i], 0, sizeof(SlotMeta));
        }
    } else {
        validateHeaderOrThrow_(*H);
        if (H->capacity != capacity || H->maxBytes != maxBytes) {
            throw std::runtime_error("NoCopyRingBuffer: SHM exists with different capacity/maxBytes");
        }
    }

    detail::RegistryTable<>::registerSignal(reg.domain(),
                                           reg.object(),
                                           rbRegistrySignalName_,
                                           shmName_,
                                           typeId_(),
                                           registryTypeName_(capacity, maxBytes, notifySignalName_));

    notify_.reset(new sw::ipc::Signal<uint64_t>(reg, notifySignalName_));
}

template <typename MetaT, uint32_t MaxConsumers>
void NoCopyRingBuffer<MetaT, MaxConsumers>::initOpen_(Registry& reg, const SwString& streamName) {
    reg_ = &reg;
    streamName_ = streamName;
    rbRegistrySignalName_ = makeRbRegistrySignalName_(streamName_);
    notifySignalName_ = makeNotifySignalName_(streamName_);

    shmName_ = detail::make_shm_name(reg.domain(), reg.object(), rbRegistrySignalName_);

    map_ = detail::ShmMappingDyn::openExisting(shmName_);
    if (!map_) throw std::runtime_error("NoCopyRingBuffer: ring buffer SHM not found");

    Header* H = header_();
    if (!H) throw std::runtime_error("NoCopyRingBuffer: null header");
    validateHeaderOrThrow_(*H);

    notify_.reset(new sw::ipc::Signal<uint64_t>(reg, notifySignalName_));
}

template <typename MetaT, uint32_t MaxConsumers>
void NoCopyRingBuffer<MetaT, MaxConsumers>::validateHeaderOrThrow_(const Header& H) const {
    if (H.magic != kMagic || H.version != kVersion) throw std::runtime_error("NoCopyRingBuffer: SHM layout mismatch");
    if (H.typeId != typeId_()) throw std::runtime_error("NoCopyRingBuffer: SHM type mismatch");
    if (H.metaTypeId != metaTypeId_()) throw std::runtime_error("NoCopyRingBuffer: SHM meta type mismatch");
    if (H.capacity == 0 || H.maxBytes == 0) throw std::runtime_error("NoCopyRingBuffer: invalid header");
    if (H.slotMetaBytes != sizeof(SlotMeta)) throw std::runtime_error("NoCopyRingBuffer: slot meta mismatch");
    if (H.maxConsumers == 0 || H.maxConsumers > MaxConsumers) throw std::runtime_error("NoCopyRingBuffer: consumer table mismatch");
    if (H.metaOffset == 0 || H.dataOffset == 0) throw std::runtime_error("NoCopyRingBuffer: invalid offsets");
    if (static_cast<uint64_t>(map_->size()) < H.totalBytes) throw std::runtime_error("NoCopyRingBuffer: truncated mapping");
}

template <typename MetaT, uint32_t MaxConsumers>
NoCopyRingBuffer<MetaT, MaxConsumers>::NoCopyRingBuffer(Registry& reg,
                                                        const SwString& streamName,
                                                        uint32_t capacity,
                                                        uint32_t maxBytes) {
    initCreate_(reg, streamName, capacity, maxBytes);
}

template <typename MetaT, uint32_t MaxConsumers>
NoCopyRingBuffer<MetaT, MaxConsumers> NoCopyRingBuffer<MetaT, MaxConsumers>::create(Registry& reg,
                                                                                    const SwString& streamName,
                                                                                    uint32_t capacity,
                                                                                    uint32_t maxBytes) {
    return NoCopyRingBuffer(reg, streamName, capacity, maxBytes);
}

template <typename MetaT, uint32_t MaxConsumers>
NoCopyRingBuffer<MetaT, MaxConsumers> NoCopyRingBuffer<MetaT, MaxConsumers>::open(Registry& reg,
                                                                                  const SwString& streamName) {
    NoCopyRingBuffer rb;
    rb.initOpen_(reg, streamName);
    return rb;
}

template <typename MetaT, uint32_t MaxConsumers>
SwStringList NoCopyRingBuffer<MetaT, MaxConsumers>::streamsInRegistry(const SwString& domain, const SwString& object) {
    SwStringList out;
    if (domain.isEmpty() || object.isEmpty()) return out;

    const SwJsonArray all = shmRegistrySnapshot(domain);
    static const std::string kPrefix("__rbmap__|");

    std::vector<std::string> names;
    for (size_t i = 0; i < all.size(); ++i) {
        const SwJsonValue v = all[i];
        if (!v.isObject() || !v.toObject()) continue;
        const SwJsonObject o(*v.toObject());
        if (SwString(o["object"].toString()) != object) continue;

        const std::string sig = SwString(o["signal"].toString()).toStdString();
        if (sig.rfind(kPrefix, 0) != 0) continue;
        const std::string n = sig.substr(kPrefix.size());
        if (!n.empty()) names.push_back(n);
    }

    if (names.empty()) return out;
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    out.reserve(names.size());
    for (size_t i = 0; i < names.size(); ++i) out.append(SwString(names[i]));
    return out;
}

template <typename MetaT, uint32_t MaxConsumers>
typename NoCopyRingBuffer<MetaT, MaxConsumers>::Consumer NoCopyRingBuffer<MetaT, MaxConsumers>::consumer() const {
    return Consumer::registerInMapping_(map_);
}

template <typename MetaT, uint32_t MaxConsumers>
typename NoCopyRingBuffer<MetaT, MaxConsumers>::WriteLease NoCopyRingBuffer<MetaT, MaxConsumers>::beginWrite() {
    WriteLease lease;
    if (!isValid()) return lease;

    Header* H = header_();
    if (!H || H->capacity == 0 || H->maxBytes == 0) return lease;

    const uint64_t seq = detail::atomic_fetch_add_u64(&H->nextSeq, 1) + 1;
    const uint32_t idx = static_cast<uint32_t>(seq % static_cast<uint64_t>(H->capacity));

    SlotMeta* slot = slotMeta_(idx);
    if (!slot) return lease;

    const uint64_t oldSeq = detail::atomic_exchange_u64(&slot->seq, 0);

    const uint64_t minCursor = minConsumerCursor_();
    if (oldSeq != 0 && oldSeq >= minCursor) {
        detail::atomic_store_u64(&slot->seq, oldSeq);
        (void)detail::atomic_fetch_add_u64(&H->dropped, 1);
        return lease;
    }

    if (detail::atomic_load_u32(&slot->refCount) != 0) {
        detail::atomic_store_u64(&slot->seq, oldSeq);
        (void)detail::atomic_fetch_add_u64(&H->dropped, 1);
        return lease;
    }

    uint8_t* dst = slotData_(idx);
    if (!dst) {
        detail::atomic_store_u64(&slot->seq, oldSeq);
        (void)detail::atomic_fetch_add_u64(&H->dropped, 1);
        return lease;
    }

    // Clear per-slot metadata for the new publish while it is invisible (seq=0).
    slot->bytes = 0;
    slot->publishTimeUs = 0;
    std::memset(&slot->meta, 0, sizeof(MetaT));

    lease.owner_ = this;
    lease.idx_ = idx;
    lease.seq_ = seq;
    lease.oldSeq_ = oldSeq;
    lease.slot_ = slot;
    lease.data_ = dst;
    lease.capacityBytes_ = H->maxBytes;
    lease.map_ = map_;
    lease.committed_ = false;
    return lease;
}

template <typename MetaT, uint32_t MaxConsumers>
bool NoCopyRingBuffer<MetaT, MaxConsumers>::publishCopy(const void* data, uint32_t bytes, const MetaT& meta) {
    if (!data || bytes == 0) return false;
    if (!isValid()) return false;
    if (static_cast<uint64_t>(bytes) > static_cast<uint64_t>(maxBytesPerItem())) return false;

    WriteLease w = beginWrite();
    if (!w.isValid()) return false;

    std::memcpy(w.data(), data, bytes);
    w.meta() = meta;
    return w.commit(bytes);
}

template <typename MetaT, uint32_t MaxConsumers>
typename NoCopyRingBuffer<MetaT, MaxConsumers>::ReadLease NoCopyRingBuffer<MetaT, MaxConsumers>::acquire(uint64_t seq) const {
    ReadLease out;
    if (!isValid() || seq == 0) return out;

    Header* H = header_();
    if (!H || H->capacity == 0) return out;

    const uint32_t idx = static_cast<uint32_t>(seq % static_cast<uint64_t>(H->capacity));
    SlotMeta* slot = slotMeta_(idx);
    if (!slot) return out;

    const uint64_t have = detail::atomic_load_u64(&slot->seq);
    if (have != seq) return out;

    (void)detail::atomic_fetch_add_u32(&slot->refCount, 1);
    const uint64_t have2 = detail::atomic_load_u64(&slot->seq);
    if (have2 != seq) {
        (void)detail::atomic_fetch_sub_u32(&slot->refCount, 1);
        return out;
    }

    uint8_t* data = slotData_(idx);
    if (!data) {
        (void)detail::atomic_fetch_sub_u32(&slot->refCount, 1);
        return out;
    }

    return ReadLease(map_, slot, data, seq, ReadLease::kInvalidIndex, 0);
}

template <typename MetaT, uint32_t MaxConsumers>
typename NoCopyRingBuffer<MetaT, MaxConsumers>::ReadLease NoCopyRingBuffer<MetaT, MaxConsumers>::readLatest() const {
    return acquire(lastSeq());
}

// Convenience macro (requires a member named `ipcRegistry_` in the class).
// Example:
//   struct MyMeta { static constexpr const char* kTypeName = "MyMetaV1"; uint32_t w=0,h=0; };
//   SW_REGISTER_SHM_NOCOPY_RINGBUFFER(stream, MyMeta, 100, 10 * 1024 * 1024);
#define SW_REGISTER_SHM_NOCOPY_RINGBUFFER(name, MetaT, capacity, maxBytes) \
    ::sw::ipc::NoCopyRingBuffer<MetaT> name{ipcRegistry_, SwString(#name), static_cast<uint32_t>(capacity), static_cast<uint32_t>(maxBytes)}

} // namespace ipc
} // namespace sw
