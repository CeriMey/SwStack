#pragma once

// Linux named queues have a kernel-held lease for every live mapping. A
// process crash releases its leases too. A namespace lock serializes opening
// with last-close/unlink, preventing two live queues under the same name.
#if defined(__linux__) && !defined(__ANDROID__)
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <string>
#include <system_error>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace sw { namespace ipc { namespace detail {
class SharedMemoryNamespaceLock {
public:
    static std::string directory() {
        return "/tmp/sw-ipc-leases-" + std::to_string(::geteuid());
    }
    SharedMemoryNamespaceLock() {
        const auto path = directory();
        if (::mkdir(path.c_str(), 0700) && errno != EEXIST) fail("lease directory");
        struct stat info{};
        if (::lstat(path.c_str(), &info) || !S_ISDIR(info.st_mode) ||
            info.st_uid != ::geteuid() || (info.st_mode & 0077))
            throw std::runtime_error("IPC: unsafe lease directory");
        fd_ = ::open((path + "/.namespace").c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd_ < 0) fail("lease namespace");
        int result;
        do { result = ::flock(fd_, LOCK_EX); } while (result && errno == EINTR);
        if (result) { const int error = errno; ::close(fd_); fd_ = -1;
            throw std::system_error(error, std::generic_category(), "IPC namespace lock"); }
    }
    ~SharedMemoryNamespaceLock() { if (fd_ >= 0) ::close(fd_); }
    SharedMemoryNamespaceLock(const SharedMemoryNamespaceLock&) = delete;
    SharedMemoryNamespaceLock& operator=(const SharedMemoryNamespaceLock&) = delete;
private:
    static void fail(const char* message) { throw std::system_error(errno, std::generic_category(), message); }
    int fd_{-1};
};

class SharedMemoryLease {
public:
    // Caller holds SharedMemoryNamespaceLock through mapping initialization.
    explicit SharedMemoryLease(const std::string& name) : name_(name) {
        if (name.size() < 2 || name[0] != '/' || name.find('/', 1) != std::string::npos)
            throw std::runtime_error("IPC: invalid shared memory name");
        struct stat existing{};
        removeMapping_ = ::lstat(("/dev/shm" + name).c_str(), &existing) != 0 && errno == ENOENT;
        path_ = SharedMemoryNamespaceLock::directory() + name;
        fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        const bool created = fd_ >= 0;
        if (!created && errno == EEXIST)
            fd_ = ::open(path_.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
        if (fd_ < 0) throw std::system_error(errno, std::generic_category(), "IPC lease open");
        // An existing lease with no holders belongs to a crashed generation.
        // Never reuse its possibly abandoned mutex or queued requests.
        if (!created && ::flock(fd_, LOCK_EX | LOCK_NB) == 0) {
            unlinkOwned(name_); removeMapping_ = true;
        }
        if (::flock(fd_, LOCK_SH) != 0) {
            const int error = errno; ::close(fd_); fd_ = -1;
            throw std::system_error(error, std::generic_category(), "IPC mapping lease");
        }
    }
    ~SharedMemoryLease() {
        try {
            SharedMemoryNamespaceLock lock;
            // Use a separate description: closing our FD must preserve any
            // lease inherited by a still-running forked child.
            const int probe = ::open(path_.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
            ::close(fd_); fd_ = -1;
            if (probe >= 0) {
                if (::flock(probe, LOCK_EX | LOCK_NB) == 0) {
                    if (removeMapping_) unlinkOwned(name_);
                    ::unlink(path_.c_str());
                }
                ::close(probe);
            }
        } catch (...) { if (fd_ >= 0) ::close(fd_); }
    }
    void commit() { removeMapping_ = true; }
    SharedMemoryLease(const SharedMemoryLease&) = delete;
    SharedMemoryLease& operator=(const SharedMemoryLease&) = delete;

    // Only managed names have lease files. Unknown/older mappings are never
    // swept. All callers creating or closing mappings hold the namespace lock.
    static size_t recoverAbandoned() {
        SharedMemoryNamespaceLock lock;
        const auto directory = SharedMemoryNamespaceLock::directory();
        DIR* entries = ::opendir(directory.c_str());
        if (!entries) return 0;
        size_t count = 0;
        while (auto* entry = ::readdir(entries)) {
            const std::string base = entry->d_name;
            if (base.rfind("sw_sig_", 0) != 0 && base.rfind("sw_ipc_dispatch_tbl_v1_", 0) != 0) continue;
            const auto path = directory + "/" + base;
            const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
            if (fd < 0) continue;
            if (::flock(fd, LOCK_EX | LOCK_NB) == 0) {
                unlinkOwned("/" + base);
                if (::unlink(path.c_str()) == 0) ++count;
            }
            ::close(fd);
        }
        ::closedir(entries);
        return count;
    }
    static void recoverAtProcessStart() {
        // Each linked module may call this independently; kernel locks make
        // repeated collection harmless. No scan occurs on subsequent opens.
        static const bool recovered = [] { recoverAbandoned(); return true; }();
        (void)recovered;
    }
private:
    static void unlinkOwned(const std::string& name) {
        struct stat info{};
        if (::lstat(("/dev/shm" + name).c_str(), &info) == 0 &&
            S_ISREG(info.st_mode) && info.st_uid == ::geteuid()) ::shm_unlink(name.c_str());
    }
    bool removeMapping_{false};
    std::string name_, path_;
    int fd_{-1};
};
}}}
#endif
