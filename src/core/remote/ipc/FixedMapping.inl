// Fixed RPC queue mapping; included inside namespace sw::ipc.
template <typename Layout>
class ShmMappingT {
public:
    /**
     * @brief Opens the or Create handled by the object.
     * @param shmName Value passed to the method.
     * @param expectedTypeId Value passed to the method.
     * @return The requested or Create.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    static std::shared_ptr<ShmMappingT> openOrCreate(const SwString& shmName, uint64_t expectedTypeId) {
        bool created = false;
#if defined(__linux__) && !defined(__ANDROID__)
        std::shared_ptr<detail::SharedMemoryLease> lease;
        detail::SharedMemoryLease::recoverAtProcessStart();
        detail::SharedMemoryNamespaceLock namespaceLock;
        lease = std::make_shared<detail::SharedMemoryLease>(shmName.toStdString());
#endif

#ifdef _WIN32
        const std::string nameA = shmName.toStdString();
        HANDLE hMap = ::CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                           static_cast<DWORD>(sizeof(Layout)), nameA.c_str());
        const DWORD lastErr = ::GetLastError();
        if (!hMap) {
            throw std::runtime_error("CreateFileMapping failed");
        }
        created = (lastErr != ERROR_ALREADY_EXISTS);

        void* mem = ::MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Layout));
        if (!mem) {
            ::CloseHandle(hMap);
            throw std::runtime_error("MapViewOfFile failed");
        }

        std::shared_ptr<ShmMappingT> mapping(new ShmMappingT(shmName, mem, hMap));
#else
        const std::string nameA = shmName.toStdString();
        int fd = ::shm_open(nameA.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
        if (fd >= 0) {
            created = true;
            detail::ensureSharedMemoryPermissions_(fd);
            if (sw::ipc::detail::reserveSharedMemory_(fd, sizeof(Layout)) != 0) {
                ::close(fd);
                ::shm_unlink(nameA.c_str());
                throw std::runtime_error("Cannot reserve shared memory (check /dev/shm capacity)");
            }
        } else if (errno == EEXIST) {
            fd = ::shm_open(nameA.c_str(), O_RDWR, 0666);
            if (fd < 0) throw std::runtime_error("shm_open(existing) failed");
            detail::ensureSharedMemoryPermissions_(fd);
        } else {
            throw std::runtime_error("shm_open failed");
        }

        void* mem = ::mmap(NULL, sizeof(Layout), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (mem == MAP_FAILED) throw std::runtime_error("mmap failed");

        std::shared_ptr<ShmMappingT> mapping(new ShmMappingT(shmName, mem));
#if defined(__linux__) && !defined(__ANDROID__)
        mapping->lease_ = lease;
#endif
#endif

        Layout* L = mapping->layout();

#ifdef _WIN32
        // Serialize initialization/validation to avoid races between concurrent creators/openers.
        const std::string initName = shmName.toStdString() + "_init";
        HANDLE initMtx = ::CreateMutexA(NULL, FALSE, initName.c_str());
        if (!initMtx) {
            throw std::runtime_error("CreateMutex(init) failed");
        }
        ::WaitForSingleObject(initMtx, INFINITE);
#endif

        if (created || (L->magic == 0 && L->version == 0)) {
            std::memset(L, 0, sizeof(Layout));
            L->magic = Layout::kMagic;
            L->version = Layout::kVersion;
            L->typeId = expectedTypeId;

#ifndef _WIN32
            pthread_mutexattr_t ma;
            pthread_condattr_t ca;
            pthread_mutexattr_init(&ma);
            pthread_condattr_init(&ca);
            pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED);
            pthread_condattr_setpshared(&ca, PTHREAD_PROCESS_SHARED);
            pthread_mutex_init(&L->mtx, &ma);
            pthread_cond_init(&L->cv, &ca);
            pthread_mutexattr_destroy(&ma);
            pthread_condattr_destroy(&ca);
#endif

            Layout::initLayout(L);
        } else {
            if (L->magic != Layout::kMagic || L->version != Layout::kVersion) {
#ifdef _WIN32
                ::ReleaseMutex(initMtx);
                ::CloseHandle(initMtx);
#endif
                throw std::runtime_error("SHM layout mismatch (magic/version)");
            }
            if (L->typeId != expectedTypeId) {
#ifdef _WIN32
                ::ReleaseMutex(initMtx);
                ::CloseHandle(initMtx);
#endif
                throw std::runtime_error("SHM type mismatch (subscriber/publisher types differ)");
            }
        }

#ifdef _WIN32
        ::ReleaseMutex(initMtx);
        ::CloseHandle(initMtx);
#endif

#if defined(__linux__) && !defined(__ANDROID__)
        lease->commit();
#endif
        return mapping;
    }

#ifndef _WIN32
    /**
     * @brief Destroys the specified destroy.
     * @param shmName Value passed to the method.
     * @return The requested destroy.
     */
    static void destroy(const SwString& shmName) {
        ::shm_unlink(shmName.toStdString().c_str());
    }
#endif

    /**
     * @brief Destroys the `ShmMappingT` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~ShmMappingT() {
#ifdef _WIN32
        if (mem_) {
            ::UnmapViewOfFile(mem_);
        }
        if (hMap_) {
            ::CloseHandle(hMap_);
        }
#else
        if (mem_ && mem_ != MAP_FAILED) {
            ::munmap(mem_, sizeof(Layout));
        }
#endif
    }

    /**
     * @brief Performs the `layout` operation.
     * @param mem_ Value passed to the method.
     * @return The requested layout.
     */
    Layout* layout() const { return static_cast<Layout*>(mem_); }
    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwString& name() const { return name_; }

private:
#ifdef _WIN32
    ShmMappingT(const SwString& name, void* mem, HANDLE hMap)
        : name_(name), mem_(mem), hMap_(hMap) {}
#else
    ShmMappingT(const SwString& name, void* mem)
        : name_(name), mem_(mem) {}
#endif

#if defined(__linux__) && !defined(__ANDROID__)
    std::shared_ptr<detail::SharedMemoryLease> lease_;
#endif
    SwString name_;
    void* mem_{nullptr};
#ifdef _WIN32
    HANDLE hMap_{NULL};
#endif
};
