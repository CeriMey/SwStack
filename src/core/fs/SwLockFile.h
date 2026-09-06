#pragma once

#include "SwStandardPaths.h"
#include "SwString.h"
#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/** Nonblocking, process-owned file lock. Closing or process death releases it.
 * The lock file is deliberately never unlinked: removing a locked inode would
 * allow another process to create a second, independently locked inode.
 */
class SwLockFile {
public:
    explicit SwLockFile(const SwString& path) : path_(path) {}
    ~SwLockFile() { unlock(); }
    SwLockFile(const SwLockFile&) = delete;
    SwLockFile& operator=(const SwLockFile&) = delete;

    static SwString userRuntimePath(const SwString& name) {
        if (name.isEmpty() || name.toStdString().find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.") != std::string::npos)
            return SwString();
        auto directory = SwStandardPaths::writableLocation(SwStandardPaths::TempLocation);
        if (directory.isEmpty()) return SwString();
        if (!directory.endsWith("/")) directory += "/";
#ifdef _WIN32
        // Windows TempLocation is scoped to the current user profile.
        return directory + name + ".lock";
#else
        return directory + name + "-" + SwString::number(static_cast<unsigned long long>(::getuid())) + ".lock";
#endif
    }

    bool tryLock() {
        if (isLocked()) return true;
        error_ = SwString();
        if (path_.isEmpty()) { error_ = "Empty lock path"; return false; }
#ifdef _WIN32
        handle_ = ::CreateFileW(path_.toStdWString().c_str(), GENERIC_READ | GENERIC_WRITE,
                               0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            error_ = "Cannot acquire file lock, Windows error " + SwString::number(::GetLastError());
            return false;
        }
#else
        fd_ = ::open(path_.toStdString().c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd_ < 0) { error_ = std::strerror(errno); return false; }
        struct stat info{};
        if (::fstat(fd_, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != ::getuid()) {
            error_ = "Lock must be a regular file owned by the current user";
            ::close(fd_); fd_ = -1;
            return false;
        }
        if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
            error_ = std::strerror(errno);
            ::close(fd_); fd_ = -1;
            return false;
        }
#endif
        return true;
    }

    void unlock() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) { ::CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; }
#else
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
#endif
    }
    bool isLocked() const {
#ifdef _WIN32
        return handle_ != INVALID_HANDLE_VALUE;
#else
        return fd_ >= 0;
#endif
    }
    const SwString& fileName() const { return path_; }
    const SwString& errorString() const { return error_; }

private:
    SwString path_, error_;
#ifdef _WIN32
    HANDLE handle_{INVALID_HANDLE_VALUE};
#else
    int fd_{-1};
#endif
};
