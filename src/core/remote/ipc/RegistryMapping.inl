// Included inside sw::ipc::detail after the registry layouts are declared.
// Registry metadata has its own generation: never reopen a non-robust mutex
// left behind by an older executable under the recoverable protocol's names.
inline SwString appsRegistrySegmentName_() {
#ifdef _WIN32
    return "sw_ipc_apps_r3";
#else
    return "/sw_ipc_apps_r3";
#endif
}

template<size_t N> inline void boundRegistryCounts_(RegistryLayout<N>* layout) {
    if (layout->count > N) layout->count = N;
}
template<size_t N> inline void boundRegistryCounts_(SubscribersLayout<N>* layout) {
    if (layout->count > N) layout->count = N;
}
template<size_t N> inline void boundRegistryCounts_(AppLayout<N>* layout) {
    if (layout->count > N) layout->count = N;
    for (uint32_t i = 0; i < layout->count; ++i)
        if (layout->apps[i].pidCount > 16) layout->apps[i].pidCount = 16;
}

#ifndef _WIN32
template<class Layout> void* openRegistryMemory_(const SwString& name) {
    struct File {
        int fd{-1}, lockFd{-1};
        ~File() {
            if (lockFd >= 0) {
                ::flock(lockFd, LOCK_UN);
                if (lockFd != fd) ::close(lockFd);
            }
            if (fd >= 0) ::close(fd);
        }
    } file;
    file.fd = ::shm_open(name.c_str(), O_RDWR | O_CREAT, 0666);
    if (file.fd < 0) throw std::system_error(errno, std::generic_category(), "IPC registry shm_open");
#ifdef __APPLE__
    const std::string lockName = "/tmp/sw_registry_" + hex64(fnv1a64(name.toStdString())).toStdString() + ".lock";
    file.lockFd = ::open(lockName.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0666);
    if (file.lockFd < 0) throw std::system_error(errno, std::generic_category(), "IPC registry init file");
#else
    file.lockFd = file.fd;
#endif
    int result;
    do { result = ::flock(file.lockFd, LOCK_EX); } while (result != 0 && errno == EINTR);
    if (result != 0) throw std::system_error(errno, std::generic_category(), "IPC registry init lock");
    struct stat info {};
    if (::fstat(file.fd, &info) != 0)
        throw std::system_error(errno, std::generic_category(), "IPC registry fstat");
    if (info.st_size == 0) {
        ensureSharedMemoryPermissions_(file.fd);
        if (reserveSharedMemory_(file.fd, sizeof(Layout)) != 0)
            throw std::system_error(errno, std::generic_category(), "IPC registry allocation");
    } else if (info.st_size != static_cast<off_t>(sizeof(Layout))) {
        throw std::runtime_error("IPC registry layout size mismatch");
    }
    void* memory = ::mmap(nullptr, sizeof(Layout), PROT_READ | PROT_WRITE, MAP_SHARED, file.fd, 0);
    if (memory == MAP_FAILED) throw std::system_error(errno, std::generic_category(), "IPC registry mmap");
    auto* layout = static_cast<Layout*>(memory);
    try {
        // The file lock covers creation and publication of magic. A creator
        // killed before publication leaves an uncommitted mapping to rebuild.
        if (layout->magic == 0) {
            std::memset(layout, 0, sizeof(Layout));
            pthread_mutexattr_t attributes;
            int error = ::pthread_mutexattr_init(&attributes);
            if (error) throw std::system_error(error, std::generic_category(), "IPC registry mutex attributes");
            error = ::pthread_mutexattr_setpshared(&attributes, PTHREAD_PROCESS_SHARED);
#if defined(__linux__)
            if (!error) error = ::pthread_mutexattr_setrobust(&attributes, PTHREAD_MUTEX_ROBUST);
#endif
            if (!error) error = ::pthread_mutex_init(&layout->mtx, &attributes);
            ::pthread_mutexattr_destroy(&attributes);
            if (error) throw std::system_error(error, std::generic_category(), "IPC registry mutex init");
            layout->version = Layout::kVersion;
            layout->reserved = Layout::kTimeBaseTag;
            layout->magic = Layout::kMagic;
        } else if (layout->magic != Layout::kMagic || layout->version != Layout::kVersion ||
                   layout->reserved != Layout::kTimeBaseTag) {
            throw std::runtime_error("IPC registry header mismatch");
        }
    } catch (...) {
        ::munmap(memory, sizeof(Layout));
        throw;
    }
    return memory;
}

template<class Layout, class Recover>
bool lockRegistryMemory_(Layout* layout, bool nonblocking, Recover recover) {
    const int error = nonblocking ? ::pthread_mutex_trylock(&layout->mtx) : ::pthread_mutex_lock(&layout->mtx);
    if (error == 0) return true;
    if (nonblocking && error == EBUSY) return false;
#if defined(__linux__)
    if (error == EOWNERDEAD) {
        // We own the mutex now. Bound possibly interrupted counters and remove
        // dead/incomplete entries before making the shared state available.
        try {
            boundRegistryCounts_(layout);
            recover(layout);
            const int consistent = ::pthread_mutex_consistent(&layout->mtx);
            if (consistent) throw std::system_error(consistent, std::generic_category(), "IPC registry mutex recovery");
        } catch (...) {
            ::pthread_mutex_unlock(&layout->mtx);
            throw;
        }
        return true;
    }
#endif
    throw std::system_error(error, std::generic_category(), "IPC registry mutex lock");
}
#endif
