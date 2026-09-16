// Internal RingQueueDynamic mapping and synchronization implementation.
struct MissingMapping : std::runtime_error {
    MissingMapping() : std::runtime_error("RingQueueDynamic: mapping absent") {}
};
struct MappingPending : std::runtime_error {
    MappingPending() : std::runtime_error("RingQueueDynamic: mapping initialization pending") {}
};
class DynamicMapping {
public:
#ifdef _WIN32
    DynamicMapping(void* memory, size_t size, HANDLE mapping, HANDLE mutex)
        : memory_(memory), size_(size), mapping_(mapping), mutex_(mutex) {}
#else
    DynamicMapping(void* memory, size_t size) : memory_(memory), size_(size) {}
#endif
    ~DynamicMapping() {
#ifdef _WIN32
        if (memory_) ::UnmapViewOfFile(memory_);
        if (mapping_) ::CloseHandle(mapping_);
        if (mutex_) ::CloseHandle(mutex_);
#else
        if (memory_ && memory_ != MAP_FAILED) ::munmap(memory_, size_);
#endif
    }
#if defined(__linux__) && !defined(__ANDROID__)
    void retainLease(const std::shared_ptr<detail::SharedMemoryLease>& lease) { lease_ = lease; }
#endif
    Header* header() const { return static_cast<Header*>(memory_); }
    size_t mappedSize() const { return size_; }
    void configure(size_t offset, size_t stride) { offset_ = offset; stride_ = stride; }
    DynamicSlot* slotAt(size_t index) const {
        return reinterpret_cast<DynamicSlot*>(static_cast<uint8_t*>(memory_) + offset_ + stride_ * index);
    }
    uint8_t* slotData(DynamicSlot* slot) const { return reinterpret_cast<uint8_t*>(slot) + sizeof(DynamicSlot); }
    bool lock() {
#ifdef _WIN32
        const DWORD result = ::WaitForSingleObject(mutex_, INFINITE);
        return result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
#else
        const int result = pthread_mutex_lock(&header()->mtx);
#if defined(__linux__)
        if (result == EOWNERDEAD) return pthread_mutex_consistent(&header()->mtx) == 0;
#endif
        return result == 0;
#endif
    }
    void unlock() {
#ifdef _WIN32
        ::ReleaseMutex(mutex_);
#else
        pthread_mutex_unlock(&header()->mtx);
#endif
    }
private:
#if defined(__linux__) && !defined(__ANDROID__)
    std::shared_ptr<detail::SharedMemoryLease> lease_;
#endif
    void* memory_{nullptr};
    size_t size_{0}, offset_{0}, stride_{0};
#ifdef _WIN32
    HANDLE mapping_{NULL}, mutex_{NULL};
#endif
};
class MappingLock {
public:
    explicit MappingLock(const std::shared_ptr<DynamicMapping>& mapping)
        : mapping_(mapping), locked_(mapping_ && mapping_->lock()) {}
    ~MappingLock() { if (locked_) mapping_->unlock(); }
    explicit operator bool() const { return locked_; }
    MappingLock(const MappingLock&) = delete;
    MappingLock& operator=(const MappingLock&) = delete;
private:
    std::shared_ptr<DynamicMapping> mapping_;
    bool locked_;
};
static bool computeLayout_(uint32_t capacity, uint32_t payload, size_t& offset, size_t& stride, size_t& total) {
    if (!capacity || !payload) return false;
    const size_t alignment = alignof(DynamicSlot);
    const size_t maximum = std::numeric_limits<size_t>::max();
    if (static_cast<size_t>(payload) > maximum - sizeof(DynamicSlot) - alignment) return false;
    offset = (sizeof(Header) + alignment - 1) / alignment * alignment;
    stride = (sizeof(DynamicSlot) + static_cast<size_t>(payload) + alignment - 1) / alignment * alignment;
    if (static_cast<size_t>(capacity) > (maximum - offset) / stride) return false;
    total = offset + static_cast<size_t>(capacity) * stride;
#ifndef _WIN32
    if (total > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) return false;
#endif
    return true;
}
static std::shared_ptr<DynamicMapping> openMapping_(const SwString& name, uint64_t typeId,
    uint32_t capacity, uint32_t payload, bool allowCreate, bool discoverExisting,
    DeliveryMode channelMode = DeliveryMode::Replay) {
    size_t expectedOffset = 0, expectedStride = 0, expectedSize = 0;
    if (allowCreate && channelMode != DeliveryMode::Replay && channelMode != DeliveryMode::LatestOnly)
        throw std::runtime_error("RingQueueDynamic: channel mode must be Replay or LatestOnly");
    if (allowCreate && !computeLayout_(capacity, payload, expectedOffset, expectedStride, expectedSize))
        throw std::runtime_error("RingQueueDynamic: invalid capacity/maxPayload");
    const std::string nameBytes = name.toStdString();
#if defined(__linux__) && !defined(__ANDROID__)
    std::shared_ptr<detail::SharedMemoryLease> lease;
    detail::SharedMemoryLease::recoverAtProcessStart();
    detail::SharedMemoryNamespaceLock namespaceLock;
    lease = std::make_shared<detail::SharedMemoryLease>(nameBytes);
#endif
    bool initialize = false;
#ifdef _WIN32
    // Serialize both mapping initialization and validation with a named mutex.
    struct InitGuard {
        HANDLE mutex{NULL};
        ~InitGuard() { if (mutex) { ::ReleaseMutex(mutex); ::CloseHandle(mutex); } }
    } init;
    init.mutex = ::CreateMutexA(NULL, FALSE, (nameBytes + "_init").c_str());
    if (!init.mutex) throw std::runtime_error("RingQueueDynamic: CreateMutex(init) failed");
    const DWORD wait = ::WaitForSingleObject(init.mutex, INFINITE);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
        throw std::runtime_error("RingQueueDynamic: initialization lock failed");
    HANDLE mapping = NULL;
    if (allowCreate) {
        const uint64_t size64 = static_cast<uint64_t>(expectedSize);
        mapping = ::CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
            static_cast<DWORD>(size64 >> 32), static_cast<DWORD>(size64), nameBytes.c_str());
        initialize = mapping && ::GetLastError() != ERROR_ALREADY_EXISTS;
    } else mapping = ::OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, nameBytes.c_str());
    if (!mapping) {
        if (!allowCreate && ::GetLastError() == ERROR_FILE_NOT_FOUND) throw MissingMapping();
        throw std::runtime_error("RingQueueDynamic: cannot open mapping");
    }
    void* memory = ::MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!memory) { ::CloseHandle(mapping); throw std::runtime_error("RingQueueDynamic: MapViewOfFile failed"); }
    MEMORY_BASIC_INFORMATION info{};
    if (!::VirtualQuery(memory, &info, sizeof info) || info.RegionSize < sizeof(Header)) {
        ::UnmapViewOfFile(memory); ::CloseHandle(mapping);
        throw std::runtime_error("RingQueueDynamic: mapping too small");
    }
    HANDLE mutex = ::CreateMutexA(NULL, FALSE, (nameBytes + "_mtx").c_str());
    if (!mutex) {
        ::UnmapViewOfFile(memory); ::CloseHandle(mapping);
        throw std::runtime_error("RingQueueDynamic: CreateMutex failed");
    }
    auto result = std::make_shared<DynamicMapping>(memory, static_cast<size_t>(info.RegionSize), mapping, mutex);
#else
    // flock is tied to the open descriptor, so independent constructors in the
    // same process and in other processes share the same initialization lock.
    struct FileGuard {
        int fd{-1}, lockFd{-1};
        ~FileGuard() {
            if (lockFd >= 0) { ::flock(lockFd, LOCK_UN); if (lockFd != fd) ::close(lockFd); }
            if (fd >= 0) ::close(fd);
        }
    } file;
    file.fd = ::shm_open(nameBytes.c_str(), O_RDWR | (allowCreate ? O_CREAT : 0), 0666);
    if (file.fd < 0) {
        if (!allowCreate && errno == ENOENT) throw MissingMapping();
        throw std::runtime_error("RingQueueDynamic: shm_open failed");
    }
#ifdef __APPLE__
    // Darwin POSIX SHM descriptors cannot be flocked; use a regular lock file.
    const std::string lockName = "/tmp/sw_ring_" + detail::hex64(detail::fnv1a64(nameBytes)).toStdString() + ".lock";
    file.lockFd = ::open(lockName.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0666);
    if (file.lockFd < 0) throw std::runtime_error("RingQueueDynamic: cannot open initialization lock");
#else
    file.lockFd = file.fd;
#endif
    int lockResult;
    do { lockResult = ::flock(file.lockFd, LOCK_EX); } while (lockResult != 0 && errno == EINTR);
    if (lockResult != 0) throw std::runtime_error("RingQueueDynamic: initialization lock failed");
    struct stat statValue{};
    if (::fstat(file.fd, &statValue) != 0) throw std::runtime_error("RingQueueDynamic: fstat failed");
    if (statValue.st_size == 0 && allowCreate) {
        detail::ensureSharedMemoryPermissions_(file.fd);
        if (detail::reserveSharedMemory_(file.fd, static_cast<off_t>(expectedSize)) != 0)
            throw std::runtime_error("RingQueueDynamic: cannot reserve shared memory");
        statValue.st_size = static_cast<off_t>(expectedSize);
        initialize = true;
    }
    // A publisher may have created the name before acquiring this file lock.
    // Readers wait for its declaration rather than rejecting that empty shell.
    if (statValue.st_size == 0 && !allowCreate) throw MappingPending();
    if (statValue.st_size < static_cast<off_t>(sizeof(Header)) ||
        static_cast<uint64_t>(statValue.st_size) > std::numeric_limits<size_t>::max())
        throw std::runtime_error("RingQueueDynamic: mapping too small or too large");
    const size_t mappedSize = static_cast<size_t>(statValue.st_size);
    void* memory = ::mmap(NULL, mappedSize, PROT_READ | PROT_WRITE, MAP_SHARED, file.fd, 0);
    if (memory == MAP_FAILED) throw std::runtime_error("RingQueueDynamic: mmap failed");
    auto result = std::make_shared<DynamicMapping>(memory, mappedSize);
#if defined(__linux__) && !defined(__ANDROID__)
    result->retainLease(lease);
#endif
#endif
    Header* header = result->header();
    if (!allowCreate && header->magic == 0 && header->version == 0) throw MappingPending();
    // A process may die after reserving a zero-filled mapping and before writing
    // its header. No initialized mapping is ever resized or silently reset.
    if (allowCreate && header->magic == 0 && header->version == 0) initialize = true;
    if (initialize) {
        if (result->mappedSize() < expectedSize)
            throw std::runtime_error("RingQueueDynamic: mapping smaller than requested layout");
        std::memset(memory, 0, expectedSize);
        header->version = Header::kVersion;
        header->typeId = typeId;
        header->capacity = capacity; header->maxPayload = payload;
        header->cursorCapacity = kMaxSubscriberCursors;
        header->defaultMode = static_cast<uint32_t>(channelMode);
#ifndef _WIN32
        pthread_mutexattr_t mutexAttributes;
        pthread_condattr_t conditionAttributes;
        pthread_mutexattr_init(&mutexAttributes);
        pthread_condattr_init(&conditionAttributes);
        int error = pthread_mutexattr_setpshared(&mutexAttributes, PTHREAD_PROCESS_SHARED);
        if (!error) error = pthread_condattr_setpshared(&conditionAttributes, PTHREAD_PROCESS_SHARED);
#if defined(__linux__)
        if (!error) error = pthread_mutexattr_setrobust(&mutexAttributes, PTHREAD_MUTEX_ROBUST);
#endif
        if (!error) error = pthread_mutex_init(&header->mtx, &mutexAttributes);
        if (!error) error = pthread_cond_init(&header->cv, &conditionAttributes);
        pthread_mutexattr_destroy(&mutexAttributes);
        pthread_condattr_destroy(&conditionAttributes);
        if (error) throw std::runtime_error("RingQueueDynamic: process-shared mutex initialization failed");
#endif
        header->magic = Header::kMagic;
    }
    if (header->magic != Header::kMagic || header->version != Header::kVersion)
        throw std::runtime_error("RingQueueDynamic: incompatible SHM layout version");
    if (header->typeId != typeId) throw std::runtime_error("RingQueueDynamic: SHM type mismatch");
    MappingLock validationLock(result);
    if (!validationLock) throw std::runtime_error("RingQueueDynamic: cannot validate locked mapping");
    if (header->cursorCapacity != kMaxSubscriberCursors || header->cursorCount > kMaxSubscriberCursors)
        throw std::runtime_error("RingQueueDynamic: invalid cursor layout");
    if (header->defaultMode > static_cast<uint32_t>(DeliveryMode::LatestOnly))
        throw std::runtime_error("RingQueueDynamic: invalid delivery mode");
    if (!discoverExisting && (header->capacity != capacity || header->maxPayload != payload ||
        header->defaultMode != static_cast<uint32_t>(channelMode)))
        throw std::runtime_error("RingQueueDynamic: channel configuration mismatch");
    size_t offset = 0, stride = 0, total = 0;
    if (!computeLayout_(header->capacity, header->maxPayload, offset, stride, total) || total > result->mappedSize())
        throw std::runtime_error("RingQueueDynamic: truncated or invalid SHM layout");
    result->configure(offset, stride);
#if defined(__linux__) && !defined(__ANDROID__)
    lease->commit();
#endif
    return result;
}
