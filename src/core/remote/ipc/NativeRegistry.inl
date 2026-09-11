// Internal process-wide native signal broker, included inside namespace sw::ipc.
// The small SHM rendezvous stores an address only for this PID and process start
// identity. Signal values and callbacks remain exclusively in process memory.
namespace detail {
struct NativeSignalBroker {
    std::mutex mutex;
    std::map<std::string, std::weak_ptr<void>> channels;
    typedef std::function<bool(std::function<void()>)> ThreadPoster;
    std::map<std::thread::id, std::shared_ptr<ThreadPoster>> threads;
};

inline uint64_t nativeProcessStartIdentity_() {
#ifdef _WIN32
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!::GetProcessTimes(::GetCurrentProcess(), &creation, &exit, &kernel, &user))
        throw std::runtime_error("Native signal broker: cannot identify process creation");
    return (static_cast<uint64_t>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
#elif defined(__APPLE__)
    struct proc_bsdinfo information{};
    if (::proc_pidinfo(static_cast<int>(currentPid()), PROC_PIDTBSDINFO, 0, &information, sizeof information) !=
        static_cast<int>(sizeof information))
        throw std::runtime_error("Native signal broker: cannot identify process creation");
    return information.pbi_start_tvsec * 1000000ull + information.pbi_start_tvusec;
#else
    const int descriptor = ::open("/proc/self/stat", O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) throw std::runtime_error("Native signal broker: cannot read process identity");
    char text[4096];
    ssize_t size;
    do { size = ::read(descriptor, text, sizeof(text) - 1); } while (size < 0 && errno == EINTR);
    ::close(descriptor);
    if (size <= 0) throw std::runtime_error("Native signal broker: empty process identity");
    text[size] = '\0';
    char* field = std::strrchr(text, ')');
    if (!field || field[1] != ' ') throw std::runtime_error("Native signal broker: malformed process identity");
    field += 2; // /proc stat field 3; command names may contain spaces and parentheses.
    for (int number = 3; number < 22; ++number) {
        field = std::strchr(field, ' ');
        if (!field) throw std::runtime_error("Native signal broker: missing process start time");
        while (*field == ' ') ++field;
    }
    char* end = nullptr;
    errno = 0;
    const uint64_t identity = std::strtoull(field, &end, 10);
    if (errno || !identity || end == field || (*end && *end != ' '))
        throw std::runtime_error("Native signal broker: invalid process start time");
    return identity;
#endif
}

inline void pinNativeSignalCode_(const void* address) {
#ifdef _WIN32
    HMODULE module = NULL;
    if (!::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                             reinterpret_cast<LPCSTR>(address), &module))
        throw std::runtime_error("Native signal broker: cannot retain owning module");
#else
    Dl_info info{};
    if (!::dladdr(address, &info) || !info.dli_fname)
        throw std::runtime_error("Native signal broker: cannot locate owning module");
#ifdef __linux__
    // A main executable's recorded path can be relative to an earlier working
    // directory. Identify its loaded image directly instead of resolving paths.
    struct ImageLookup { uintptr_t address; bool executable; } lookup{reinterpret_cast<uintptr_t>(address), false};
    ::dl_iterate_phdr([](struct dl_phdr_info* image, size_t, void* context) -> int {
        auto& lookup = *static_cast<ImageLookup*>(context);
        for (size_t i = 0; i < image->dlpi_phnum; ++i) {
            const auto& segment = image->dlpi_phdr[i];
            if (segment.p_type != PT_LOAD) continue;
            const uintptr_t begin = image->dlpi_addr + segment.p_vaddr;
            if (lookup.address >= begin && lookup.address - begin < segment.p_memsz) {
                lookup.executable = !image->dlpi_name || !*image->dlpi_name;
                return 1;
            }
        }
        return 0;
    }, &lookup);
    if (lookup.executable) return;
#endif
    // Executables already remain loaded for the lifetime of the process, and
    // dlopen cannot acquire an additional reference to a PIE executable.
    struct stat executable{}, owner{};
#ifdef __APPLE__
    char executablePath[PROC_PIDPATHINFO_MAXSIZE];
    if (::proc_pidpath(static_cast<int>(currentPid()), executablePath, sizeof executablePath) <= 0)
        throw std::runtime_error("Native signal broker: cannot locate executable");
#else
    const char* executablePath = "/proc/self/exe";
#endif
    if (::stat(executablePath, &executable) == 0 && ::stat(info.dli_fname, &owner) == 0 &&
        executable.st_dev == owner.st_dev && executable.st_ino == owner.st_ino) return;
#ifdef RTLD_NODELETE
    void* module = ::dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE);
#else
    void* module = ::dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);
#endif
    if (!module) throw std::runtime_error("Native signal broker: cannot retain owning module");
    // Keep one loader reference for the process lifetime on systems without
    // NODELETE. Objects still stop/destruct normally when a plugin is removed.
#ifdef RTLD_NODELETE
    ::dlclose(module);
#endif
#endif
}

#ifndef _WIN32
struct NativeSignalCleanup {
    uint32_t pid;
    std::string shmName;
    std::string lockName;
};
inline NativeSignalCleanup*& nativeSignalCleanup_() {
    static NativeSignalCleanup* value = nullptr;
    return value;
}
inline void cleanupNativeSignalBroker_() {
    const NativeSignalCleanup* cleanup = nativeSignalCleanup_();
    if (!cleanup || cleanup->pid != currentPid()) return;
    ::shm_unlink(cleanup->shmName.c_str());
    if (!cleanup->lockName.empty()) ::unlink(cleanup->lockName.c_str());
}
#endif

inline NativeSignalBroker* openNativeSignalBroker_() {
    struct Rendezvous {
        uint32_t magic, version, pid, pointerBytes;
        uint64_t start;
        uintptr_t broker;
    };
    const uint32_t pid = currentPid();
    const uint64_t start = nativeProcessStartIdentity_();
    const uint32_t magic = 0x4e534231u;
#ifdef _WIN32
    const std::string name = "Local\\sw_native_signal_v1_" + std::to_string(pid) + "_" + std::to_string(start);
    struct Handles {
        HANDLE mapping{NULL}, mutex{NULL};
        void* memory{nullptr};
        ~Handles() {
            if (memory) ::UnmapViewOfFile(memory);
            if (mapping) ::CloseHandle(mapping);
            if (mutex) { ::ReleaseMutex(mutex); ::CloseHandle(mutex); }
        }
    } handles;
    handles.mutex = ::CreateMutexA(NULL, FALSE, (name + "_init").c_str());
    if (!handles.mutex) throw std::runtime_error("Native signal broker: cannot open initialization mutex");
    const DWORD wait = ::WaitForSingleObject(handles.mutex, INFINITE);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
        throw std::runtime_error("Native signal broker: cannot lock initialization");
    handles.mapping = ::CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                            static_cast<DWORD>(sizeof(Rendezvous)), name.c_str());
    if (!handles.mapping) throw std::runtime_error("Native signal broker: cannot open rendezvous");
    handles.memory = ::MapViewOfFile(handles.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Rendezvous));
    if (!handles.memory) throw std::runtime_error("Native signal broker: cannot map rendezvous");
    Rendezvous* table = static_cast<Rendezvous*>(handles.memory);
#else
#ifdef __APPLE__
    const std::string name = "/sw_native_" + hex64(fnv1a64(std::to_string(pid) + ":" + std::to_string(start))).toStdString();
    const std::string lockName = "/tmp" + name + ".lock";
#else
    const std::string name = "/sw_native_signal_v1_" + std::to_string(pid) + "_" + std::to_string(start);
    const std::string lockName;
#endif
    struct Mapping {
        int descriptor{-1};
        int lockDescriptor{-1};
        void* memory{MAP_FAILED};
        ~Mapping() {
            if (memory != MAP_FAILED) ::munmap(memory, sizeof(Rendezvous));
            if (lockDescriptor >= 0) {
                ::flock(lockDescriptor, LOCK_UN);
                if (lockDescriptor != descriptor) ::close(lockDescriptor);
            }
            if (descriptor >= 0) ::close(descriptor);
        }
    } mapping;
    mapping.descriptor = ::shm_open(name.c_str(), O_RDWR | O_CREAT, 0600);
    if (mapping.descriptor < 0) throw std::runtime_error("Native signal broker: cannot open rendezvous");
#ifdef __APPLE__
    mapping.lockDescriptor = ::open(lockName.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (mapping.lockDescriptor < 0) throw std::runtime_error("Native signal broker: cannot open initialization lock");
#else
    mapping.lockDescriptor = mapping.descriptor;
#endif
    int result;
    do { result = ::flock(mapping.lockDescriptor, LOCK_EX); } while (result != 0 && errno == EINTR);
    if (result != 0) throw std::runtime_error("Native signal broker: cannot lock initialization");
    struct stat status{};
    if (::fstat(mapping.descriptor, &status) != 0)
        throw std::runtime_error("Native signal broker: cannot inspect rendezvous");
    if (!status.st_size) {
        if (reserveSharedMemory_(mapping.descriptor, static_cast<off_t>(sizeof(Rendezvous))) != 0)
            throw std::runtime_error("Native signal broker: cannot reserve rendezvous");
    } else if (status.st_size != sizeof(Rendezvous))
        throw std::runtime_error("Native signal broker: incompatible rendezvous size");
    mapping.memory = ::mmap(NULL, sizeof(Rendezvous), PROT_READ | PROT_WRITE, MAP_SHARED, mapping.descriptor, 0);
    if (mapping.memory == MAP_FAILED) throw std::runtime_error("Native signal broker: cannot map rendezvous");
    Rendezvous* table = static_cast<Rendezvous*>(mapping.memory);
#endif
    if (!table->magic && !table->broker) {
#ifndef _WIN32
        // Only the creating module registers cleanup; its code stays pinned.
        pinNativeSignalCode_(reinterpret_cast<const void*>(&cleanupNativeSignalBroker_));
        nativeSignalCleanup_() = new NativeSignalCleanup{pid, name, lockName};
        if (std::atexit(&cleanupNativeSignalBroker_) != 0)
            throw std::runtime_error("Native signal broker: cannot register process cleanup");
#endif
        NativeSignalBroker* broker = new NativeSignalBroker;
        table->version = 1; table->pid = pid; table->pointerBytes = sizeof(uintptr_t); table->start = start;
        table->broker = reinterpret_cast<uintptr_t>(broker);
        table->magic = magic;
    }
    if (table->magic != magic || table->version != 1 || table->pid != pid || table->start != start ||
        table->pointerBytes != sizeof(uintptr_t) || !table->broker)
        throw std::runtime_error("Native signal broker: stale or incompatible rendezvous");
    NativeSignalBroker* broker = reinterpret_cast<NativeSignalBroker*>(table->broker);
#ifdef _WIN32
    // The kernel removes a named mapping when its last handle closes. Retain one
    // process-lifetime handle so a later-loaded DLL finds the same rendezvous.
    handles.mapping = NULL;
#endif
    return broker;
}

inline NativeSignalBroker& nativeSignalBroker_() {
    struct Cached {
        uint32_t pid;
        NativeSignalBroker* broker;
    };
    // Atomic, constant initialization also permits creating a fresh broker after
    // fork without touching an inherited mutex from another parent thread.
    static std::atomic<Cached*> cached{nullptr};
    Cached* value = cached.load(std::memory_order_acquire);
    const uint32_t pid = currentPid();
    if (!value || value->pid != pid) {
        Cached* fresh = new Cached{pid, openNativeSignalBroker_()};
        while (!cached.compare_exchange_weak(value, fresh, std::memory_order_acq_rel)) {
            if (value && value->pid == pid) { delete fresh; return *value->broker; }
        }
        value = fresh; // Parent caches are intentionally untouched after fork.
    }
    return *value->broker;
}

template <class T>
std::shared_ptr<T> nativeSignalChannel(const std::string& key) {
    // All module code which can own shared_ptr control blocks remains mapped.
    static const bool pinned = [] {
        pinNativeSignalCode_(reinterpret_cast<const void*>(&nativeSignalChannel<T>));
        return true;
    }();
    (void)pinned;
    NativeSignalBroker& broker = nativeSignalBroker_();
    const std::string typedKey = std::string(typeid(T).name()) + ":" + key;
    std::lock_guard<std::mutex> lock(broker.mutex);
    auto& weak = broker.channels[typedKey];
    auto existing = weak.lock();
    if (existing) return std::static_pointer_cast<T>(existing);
    auto channel = std::make_shared<T>();
    weak = channel;
    return channel;
}
// Extra runtime services live behind a typed channel retained by the existing
// thread poster. NativeSignalBroker itself keeps the same shared ABI layout.
struct NativeThreadTimers {
    using Cancel = std::function<void()>;
    using Factory = std::function<Cancel(std::function<void()>, int64_t)>;
    std::mutex mutex;
    std::map<std::thread::id, Factory> factories;
};

// Keep the actual runtime owner responsible for its live-thread and timer
// lookups. A hidden receiver module can have no local runtime/TLS instance.
inline void registerNativeSignalThread() {
    ThreadHandle* thread = ThreadHandle::currentThread();
    if (!thread) return;
    static const bool pinned = [] {
        pinNativeSignalCode_(reinterpret_cast<const void*>(&registerNativeSignalThread));
        return true;
    }();
    (void)pinned;
    const std::thread::id id = thread->threadId();
    auto timers = nativeSignalChannel<NativeThreadTimers>("native-thread-timers-v1");
    {
        std::lock_guard<std::mutex> lock(timers->mutex);
        timers->factories[id] = [thread](std::function<void()> callback, int64_t interval) -> NativeThreadTimers::Cancel {
            if (!ThreadHandle::isLive(thread)) return {};
            SwCoreApplication* app = thread->application();
            if (!app) return {};
            const int timer = app->addTimer(std::move(callback), interval, true);
            return [thread, app, timer] {
                if (ThreadHandle::isLive(thread) && thread->application() == app) app->removeTimer(timer);
            };
        };
    }
    auto poster = std::make_shared<NativeSignalBroker::ThreadPoster>([thread, timers](std::function<void()> task) {
        return ThreadHandle::postTaskOnLaneReliableIfLive(thread, std::move(task), SwFiberLane::Normal);
    });
    NativeSignalBroker& broker = nativeSignalBroker_();
    std::lock_guard<std::mutex> lock(broker.mutex);
    broker.threads[id] = std::move(poster);
}

inline NativeThreadTimers::Cancel startNativeSignalTimer(std::thread::id id, std::function<void()> task,
                                                         int64_t intervalMicroseconds) {
    auto timers = nativeSignalChannel<NativeThreadTimers>("native-thread-timers-v1");
    NativeThreadTimers::Factory factory;
    {
        std::lock_guard<std::mutex> lock(timers->mutex);
        const auto it = timers->factories.find(id);
        if (it == timers->factories.end()) return {};
        factory = it->second;
    }
    return factory(std::move(task), intervalMicroseconds);
}

inline bool postNativeSignalThread(std::thread::id id, std::function<void()> task) {
    if (!task) return false;
    std::shared_ptr<NativeSignalBroker::ThreadPoster> poster;
    NativeSignalBroker& broker = nativeSignalBroker_();
    {
        std::lock_guard<std::mutex> lock(broker.mutex);
        const auto it = broker.threads.find(id);
        if (it == broker.threads.end()) return false;
        poster = it->second;
    }
    // Scheduling can enter runtime code; it must never hold the broker mutex.
    return (*poster)(std::move(task));
}
} // namespace detail
