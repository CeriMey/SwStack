#pragma once

/**
 * @file src/core/platform/SwDbPlatform.h
 * @ingroup core_platform
 * @brief Low-level file helpers dedicated to embedded storage engines.
 */

#include "SwDir.h"
#include "SwFileInfo.h"
#include "SwString.h"
#include "SwList.h"
#include "SwDebug.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static constexpr const char* kSwLogCategory_SwDbPlatform = "sw.core.platform.swdbplatform";

namespace swDbPlatform {

inline SwString normalizePath(const SwString& path) {
    return SwDir::normalizePath(path);
}

inline SwString joinPath(const SwString& base, const SwString& child) {
    if (base.isEmpty()) {
        return child;
    }
    if (child.isEmpty()) {
        return base;
    }
    SwString result = base;
    result.replace("\\", "/");
    if (!result.endsWith("/")) {
        result += "/";
    }
    result += child;
    return normalizePath(result);
}

inline SwString parentPath(const SwString& path) {
    SwString normalized = normalizePath(path);
    normalized.replace("\\", "/");
    while (normalized.endsWith("/") && normalized.size() > 1) {
        normalized.chop(1);
    }
    const std::size_t slash = normalized.lastIndexOf('/');
    if (slash == std::string::npos || slash == 0u) {
        return SwString();
    }
    return normalized.left(static_cast<int>(slash));
}

inline SwString fileName(const SwString& path) {
    SwString normalized = normalizePath(path);
    normalized.replace("\\", "/");
    const std::size_t slash = normalized.lastIndexOf('/');
    if (slash == std::string::npos) {
        return normalized;
    }
    return normalized.mid(static_cast<int>(slash + 1));
}

inline bool ensureDirectory(const SwString& path) {
    if (path.isEmpty()) {
        return false;
    }
    return SwDir::mkpathAbsolute(path, true);
}

inline bool fileExists(const SwString& path) {
    SwFileInfo info(path.toStdString());
    return info.exists() && info.isFile();
}

inline bool directoryExists(const SwString& path) {
    return SwDir::exists(path);
}

inline bool removeFile(const SwString& path) {
#if defined(_WIN32)
    return ::DeleteFileW(path.toStdWString().c_str()) != 0;
#else
    return ::unlink(path.toStdString().c_str()) == 0;
#endif
}

inline SwList<SwString> listFiles(const SwString& directory,
                                  const SwString& prefix = SwString(),
                                  const SwString& suffix = SwString()) {
    SwList<SwString> result;
    if (!directoryExists(directory)) {
        return result;
    }
    SwDir dir(directory);
    SwStringList entries = dir.entryList(EntryType::Files);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const SwString entry = entries[i];
        if (!prefix.isEmpty() && !entry.startsWith(prefix)) {
            continue;
        }
        if (!suffix.isEmpty() && !entry.endsWith(suffix)) {
            continue;
        }
        result.append(joinPath(directory, entry));
    }
    std::sort(result.begin(), result.end(), [](const SwString& lhs, const SwString& rhs) {
        return lhs.toStdString() < rhs.toStdString();
    });
    return result;
}

class FileLock {
public:
    FileLock() {
#if defined(_WIN32)
        handle_ = INVALID_HANDLE_VALUE;
#else
        fd_ = -1;
#endif
    }

    ~FileLock() {
        unlock();
    }

    bool lockExclusive(const SwString& path, SwString* errOut = nullptr) {
        unlock();
        ensureDirectory(parentPath(path));
#if defined(_WIN32)
        handle_ = ::CreateFileW(path.toStdWString().c_str(),
                                GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr,
                                OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            setError_(errOut, "CreateFileW lock failed");
            return false;
        }
        OVERLAPPED overlapped;
        std::memset(&overlapped, 0, sizeof(overlapped));
        if (!::LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
                          MAXDWORD, MAXDWORD, &overlapped)) {
            setError_(errOut, "LockFileEx failed");
            ::CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            return false;
        }
        return true;
#else
        fd_ = ::open(path.toStdString().c_str(), O_RDWR | O_CREAT, 0644);
        if (fd_ < 0) {
            setError_(errOut, "open lock failed");
            return false;
        }
        struct flock fl;
        std::memset(&fl, 0, sizeof(fl));
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start = 0;
        fl.l_len = 0;
        if (::fcntl(fd_, F_SETLK, &fl) != 0) {
            setError_(errOut, "fcntl lock failed");
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        return true;
#endif
    }

    void unlock() {
#if defined(_WIN32)
        if (handle_ != INVALID_HANDLE_VALUE) {
            OVERLAPPED overlapped;
            std::memset(&overlapped, 0, sizeof(overlapped));
            (void)::UnlockFileEx(handle_, 0, MAXDWORD, MAXDWORD, &overlapped);
            ::CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (fd_ >= 0) {
            struct flock fl;
            std::memset(&fl, 0, sizeof(fl));
            fl.l_type = F_UNLCK;
            fl.l_whence = SEEK_SET;
            fl.l_start = 0;
            fl.l_len = 0;
            (void)::fcntl(fd_, F_SETLK, &fl);
            ::close(fd_);
            fd_ = -1;
        }
#endif
    }

    bool isLocked() const {
#if defined(_WIN32)
        return handle_ != INVALID_HANDLE_VALUE;
#else
        return fd_ >= 0;
#endif
    }

private:
    static void setError_(SwString* out, const char* prefix) {
        if (!out) {
            return;
        }
#if defined(_WIN32)
        *out = SwString(prefix) + ": " + SwString::number(static_cast<unsigned long long>(::GetLastError()));
#else
        *out = SwString(prefix) + ": " + SwString(std::strerror(errno));
#endif
    }

#if defined(_WIN32)
    HANDLE handle_;
#else
    int fd_;
#endif
};

class RandomAccessFile {
public:
    enum class OpenMode {
        ReadOnly,
        ReadWriteCreate,
        ReadWriteTruncate,
        AppendCreate,
        AppendCreateBuffered
    };

    RandomAccessFile() {
#if defined(_WIN32)
        handle_ = INVALID_HANDLE_VALUE;
#else
        fd_ = -1;
#endif
    }

    ~RandomAccessFile() {
        close();
    }

    bool open(const SwString& path, OpenMode mode, SwString* errOut = nullptr) {
        close();
        path_ = normalizePath(path);
        ensureDirectory(parentPath(path_));
#if defined(_WIN32)
        DWORD access = GENERIC_READ;
        DWORD disposition = OPEN_EXISTING;
        DWORD shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
        DWORD flags = FILE_ATTRIBUTE_NORMAL;
        if (mode != OpenMode::ReadOnly) {
            access |= GENERIC_WRITE;
        }
        if (mode == OpenMode::ReadWriteCreate ||
            mode == OpenMode::AppendCreate ||
            mode == OpenMode::AppendCreateBuffered) {
            disposition = OPEN_ALWAYS;
        } else if (mode == OpenMode::ReadWriteTruncate) {
            disposition = CREATE_ALWAYS;
        }
        writeThrough_ = (mode == OpenMode::AppendCreate);
        if (writeThrough_) {
            flags |= FILE_FLAG_WRITE_THROUGH;
        }
        handle_ = ::CreateFileW(path_.toStdWString().c_str(), access, shareMode, nullptr,
                                disposition, flags, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            setError_(errOut, "CreateFileW failed");
            return false;
        }
        if (mode == OpenMode::AppendCreate || mode == OpenMode::AppendCreateBuffered) {
            LARGE_INTEGER offset;
            offset.QuadPart = 0;
            (void)::SetFilePointerEx(handle_, offset, nullptr, FILE_END);
        }
#else
        int flags = O_RDONLY;
        mode_ = mode;
        writeThrough_ = (mode == OpenMode::AppendCreate);
        if (mode == OpenMode::ReadWriteCreate) {
            flags = O_RDWR | O_CREAT;
        } else if (mode == OpenMode::ReadWriteTruncate) {
            flags = O_RDWR | O_CREAT | O_TRUNC;
        } else if (mode == OpenMode::AppendCreate || mode == OpenMode::AppendCreateBuffered) {
            flags = O_RDWR | O_CREAT;
#if defined(O_DSYNC)
            if (mode == OpenMode::AppendCreate) {
                flags |= O_DSYNC;
            }
#elif defined(O_SYNC)
            if (mode == OpenMode::AppendCreate) {
                flags |= O_SYNC;
            }
#endif
        }
        fd_ = ::open(path_.toStdString().c_str(), flags, 0644);
        if (fd_ < 0) {
            setError_(errOut, "open failed");
            return false;
        }
        if (mode == OpenMode::AppendCreate || mode == OpenMode::AppendCreateBuffered) {
            (void)::lseek(fd_, 0, SEEK_END);
        }
#endif
        mode_ = mode;
        cachedSize_ = queryFileSize_();
        sizeKnown_ = true;
        return true;
    }

    void close() {
#if defined(_WIN32)
        if (handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
        path_.clear();
        cachedSize_ = 0;
        sizeKnown_ = false;
        writeThrough_ = false;
    }

    bool isOpen() const {
#if defined(_WIN32)
        return handle_ != INVALID_HANDLE_VALUE;
#else
        return fd_ >= 0;
#endif
    }

    unsigned long long size() const {
        if (!isOpen()) {
            return 0;
        }
        if (!sizeKnown_) {
            cachedSize_ = queryFileSize_();
            sizeKnown_ = true;
        }
        return cachedSize_;
    }

    bool readAt(unsigned long long offset, void* buffer, std::size_t bytes, SwString* errOut = nullptr) const {
        if (!isOpen()) {
            setError_(errOut, "readAt on closed file");
            return false;
        }
        char* out = static_cast<char*>(buffer);
        std::size_t remaining = bytes;
#if defined(_WIN32)
        while (remaining > 0) {
            OVERLAPPED overlapped;
            std::memset(&overlapped, 0, sizeof(overlapped));
            overlapped.Offset = static_cast<DWORD>(offset & 0xffffffffULL);
            overlapped.OffsetHigh = static_cast<DWORD>((offset >> 32) & 0xffffffffULL);
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, 1u << 20));
            DWORD readBytes = 0;
            if (!::ReadFile(handle_, out, chunk, &readBytes, &overlapped)) {
                setError_(errOut, "ReadFile failed");
                return false;
            }
            if (readBytes != chunk) {
                setError_(errOut, "short read");
                return false;
            }
            remaining -= readBytes;
            out += readBytes;
            offset += readBytes;
        }
#else
        while (remaining > 0) {
            const ssize_t ret = ::pread(fd_, out, remaining, static_cast<off_t>(offset));
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                setError_(errOut, "pread failed");
                return false;
            }
            if (ret == 0) {
                setError_(errOut, "short read");
                return false;
            }
            remaining -= static_cast<std::size_t>(ret);
            out += ret;
            offset += static_cast<unsigned long long>(ret);
        }
#endif
        return true;
    }

    bool writeAt(unsigned long long offset, const void* buffer, std::size_t bytes, SwString* errOut = nullptr) {
        if (!isOpen()) {
            setError_(errOut, "writeAt on closed file");
            return false;
        }
        const unsigned long long startOffset = offset;
        const char* in = static_cast<const char*>(buffer);
        std::size_t remaining = bytes;
#if defined(_WIN32)
        while (remaining > 0) {
            OVERLAPPED overlapped;
            std::memset(&overlapped, 0, sizeof(overlapped));
            overlapped.Offset = static_cast<DWORD>(offset & 0xffffffffULL);
            overlapped.OffsetHigh = static_cast<DWORD>((offset >> 32) & 0xffffffffULL);
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, 1u << 20));
            DWORD written = 0;
            if (!::WriteFile(handle_, in, chunk, &written, &overlapped)) {
                setError_(errOut, "WriteFile failed");
                return false;
            }
            if (written != chunk) {
                setError_(errOut, "short write");
                return false;
            }
            remaining -= written;
            in += written;
            offset += written;
        }
#else
        while (remaining > 0) {
            const ssize_t ret = ::pwrite(fd_, in, remaining, static_cast<off_t>(offset));
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                setError_(errOut, "pwrite failed");
                return false;
            }
            remaining -= static_cast<std::size_t>(ret);
            in += ret;
            offset += static_cast<unsigned long long>(ret);
        }
#endif
        cachedSize_ = std::max(cachedSize_, startOffset + static_cast<unsigned long long>(bytes));
        sizeKnown_ = true;
        return true;
    }

    bool append(const void* buffer,
                std::size_t bytes,
                unsigned long long* offsetOut = nullptr,
                SwString* errOut = nullptr) {
        const unsigned long long offset = size();
#if defined(_WIN32)
        if (mode_ == OpenMode::AppendCreate || mode_ == OpenMode::AppendCreateBuffered) {
            const char* in = static_cast<const char*>(buffer);
            std::size_t remaining = bytes;
            while (remaining > 0) {
                const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, 1u << 20));
                DWORD written = 0;
                if (!::WriteFile(handle_, in, chunk, &written, nullptr)) {
                    setError_(errOut, "WriteFile append failed");
                    return false;
                }
                if (written != chunk) {
                    setError_(errOut, "short append");
                    return false;
                }
                remaining -= written;
                in += written;
            }
            cachedSize_ = offset + static_cast<unsigned long long>(bytes);
            sizeKnown_ = true;
            if (offsetOut) {
                *offsetOut = offset;
            }
            return true;
        }
#else
        if (mode_ == OpenMode::AppendCreate || mode_ == OpenMode::AppendCreateBuffered) {
            const char* in = static_cast<const char*>(buffer);
            std::size_t remaining = bytes;
            while (remaining > 0) {
                const ssize_t ret = ::write(fd_, in, remaining);
                if (ret < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    setError_(errOut, "write append failed");
                    return false;
                }
                remaining -= static_cast<std::size_t>(ret);
                in += ret;
            }
            cachedSize_ = offset + static_cast<unsigned long long>(bytes);
            sizeKnown_ = true;
            if (offsetOut) {
                *offsetOut = offset;
            }
            return true;
        }
#endif
        if (!writeAt(offset, buffer, bytes, errOut)) {
            return false;
        }
        if (offsetOut) {
            *offsetOut = offset;
        }
        return true;
    }

    bool truncate(unsigned long long bytes, SwString* errOut = nullptr) {
        if (!isOpen()) {
            setError_(errOut, "truncate on closed file");
            return false;
        }
#if defined(_WIN32)
        LARGE_INTEGER offset;
        offset.QuadPart = static_cast<LONGLONG>(bytes);
        if (!::SetFilePointerEx(handle_, offset, nullptr, FILE_BEGIN) || !::SetEndOfFile(handle_)) {
            setError_(errOut, "truncate failed");
            return false;
        }
#else
        if (::ftruncate(fd_, static_cast<off_t>(bytes)) != 0) {
            setError_(errOut, "ftruncate failed");
            return false;
        }
#endif
        cachedSize_ = bytes;
        sizeKnown_ = true;
        return true;
    }

    bool reserveAllocation(unsigned long long bytes, SwString* errOut = nullptr) {
        if (!isOpen()) {
            setError_(errOut, "reserveAllocation on closed file");
            return false;
        }
#if defined(_WIN32)
        FILE_ALLOCATION_INFO allocationInfo;
        std::memset(&allocationInfo, 0, sizeof(allocationInfo));
        allocationInfo.AllocationSize.QuadPart = static_cast<LONGLONG>(bytes);
        if (!::SetFileInformationByHandle(handle_,
                                          FileAllocationInfo,
                                          &allocationInfo,
                                          static_cast<DWORD>(sizeof(allocationInfo)))) {
            setError_(errOut, "SetFileInformationByHandle(FileAllocationInfo) failed");
            return false;
        }
        return true;
#else
        // This helper must reserve physical space without changing logical EOF.
        // posix_fallocate() extends st_size, which breaks append-only WAL replay.
        (void)bytes;
        (void)errOut;
        return true;
#endif
    }

    bool sync(SwString* errOut = nullptr) {
        if (!isOpen()) {
            return false;
        }
        if (writeThrough_) {
            return true;
        }
#if defined(_WIN32)
        if (!::FlushFileBuffers(handle_)) {
            setError_(errOut, "FlushFileBuffers failed");
            return false;
        }
#else
        if (::fsync(fd_) != 0) {
            setError_(errOut, "fsync failed");
            return false;
        }
#endif
        return true;
    }

    const SwString& path() const {
        return path_;
    }

private:
    unsigned long long queryFileSize_() const {
        if (!isOpen()) {
            return 0;
        }
#if defined(_WIN32)
        LARGE_INTEGER fileSize;
        std::memset(&fileSize, 0, sizeof(fileSize));
        if (!::GetFileSizeEx(handle_, &fileSize)) {
            return 0;
        }
        return static_cast<unsigned long long>(fileSize.QuadPart);
#else
        struct stat st;
        if (::fstat(fd_, &st) != 0) {
            return 0;
        }
        return static_cast<unsigned long long>(st.st_size);
#endif
    }

    static void setError_(SwString* out, const char* prefix) {
        if (!out) {
            return;
        }
#if defined(_WIN32)
        *out = SwString(prefix) + ": " + SwString::number(static_cast<unsigned long long>(::GetLastError()));
#else
        *out = SwString(prefix) + ": " + SwString(std::strerror(errno));
#endif
    }

    OpenMode mode_{OpenMode::ReadOnly};
    SwString path_;
    mutable unsigned long long cachedSize_{0};
    mutable bool sizeKnown_{false};
    bool writeThrough_{false};
#if defined(_WIN32)
    HANDLE handle_;
#else
    int fd_;
#endif
};

class MappedFile {
public:
    MappedFile() = default;

    ~MappedFile() {
        close();
    }

    bool openReadOnly(const SwString& path, SwString* errOut = nullptr) {
        close();
        path_ = normalizePath(path);
#if defined(_WIN32)
        file_ = ::CreateFileW(path_.toStdWString().c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) {
            setError_(errOut, "CreateFileW mmap failed");
            return false;
        }
        LARGE_INTEGER fileSize;
        std::memset(&fileSize, 0, sizeof(fileSize));
        if (!::GetFileSizeEx(file_, &fileSize) || fileSize.QuadPart <= 0) {
            setError_(errOut, "GetFileSizeEx failed");
            close();
            return false;
        }
        size_ = static_cast<std::size_t>(fileSize.QuadPart);
        mapping_ = ::CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping_) {
            setError_(errOut, "CreateFileMappingW failed");
            close();
            return false;
        }
        data_ = static_cast<const char*>(::MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
        if (!data_) {
            setError_(errOut, "MapViewOfFile failed");
            close();
            return false;
        }
#else
        fd_ = ::open(path_.toStdString().c_str(), O_RDONLY);
        if (fd_ < 0) {
            setError_(errOut, "open mmap failed");
            return false;
        }
        struct stat st;
        if (::fstat(fd_, &st) != 0 || st.st_size <= 0) {
            setError_(errOut, "fstat failed");
            close();
            return false;
        }
        size_ = static_cast<std::size_t>(st.st_size);
        void* mem = ::mmap(nullptr, size_, PROT_READ, MAP_SHARED, fd_, 0);
        if (mem == MAP_FAILED) {
            setError_(errOut, "mmap failed");
            close();
            return false;
        }
        data_ = static_cast<const char*>(mem);
#endif
        return true;
    }

    void close() {
#if defined(_WIN32)
        if (data_) {
            ::UnmapViewOfFile(data_);
            data_ = nullptr;
        }
        if (mapping_) {
            ::CloseHandle(mapping_);
            mapping_ = nullptr;
        }
        if (file_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(file_);
            file_ = INVALID_HANDLE_VALUE;
        }
#else
        if (data_) {
            ::munmap(const_cast<char*>(data_), size_);
            data_ = nullptr;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
        size_ = 0;
        path_.clear();
    }

    bool isOpen() const {
        return data_ != nullptr && size_ > 0;
    }

    const char* data() const {
        return data_;
    }

    std::size_t size() const {
        return size_;
    }

    const SwString& path() const {
        return path_;
    }

private:
    static void setError_(SwString* out, const char* prefix) {
        if (!out) {
            return;
        }
#if defined(_WIN32)
        *out = SwString(prefix) + ": " + SwString::number(static_cast<unsigned long long>(::GetLastError()));
#else
        *out = SwString(prefix) + ": " + SwString(std::strerror(errno));
#endif
    }

    SwString path_;
    const char* data_{nullptr};
    std::size_t size_{0};
#if defined(_WIN32)
    HANDLE file_{INVALID_HANDLE_VALUE};
    HANDLE mapping_{nullptr};
#else
    int fd_{-1};
#endif
};

inline bool syncDirectory(const SwString& directory, SwString* errOut = nullptr) {
    const SwString normalized = normalizePath(directory);
    if (normalized.isEmpty()) {
        return true;
    }
#if defined(_WIN32)
    HANDLE handle = ::CreateFileW(normalized.toStdWString().c_str(),
                                  FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS,
                                  nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (errOut) {
            *errOut = SwString("syncDirectory open failed: ") + SwString::number(static_cast<unsigned long long>(::GetLastError()));
        }
        return false;
    }
    const bool ok = ::FlushFileBuffers(handle) != 0;
    if (!ok && errOut) {
        *errOut = SwString("FlushFileBuffers(dir) failed: ") + SwString::number(static_cast<unsigned long long>(::GetLastError()));
    }
    ::CloseHandle(handle);
    return ok;
#else
    const int fd = ::open(normalized.toStdString().c_str(), O_RDONLY);
    if (fd < 0) {
        if (errOut) {
            *errOut = SwString("open(dir) failed: ") + SwString(std::strerror(errno));
        }
        return false;
    }
    const bool ok = ::fsync(fd) == 0;
    if (!ok && errOut) {
        *errOut = SwString("fsync(dir) failed: ") + SwString(std::strerror(errno));
    }
    ::close(fd);
    return ok;
#endif
}

inline bool replaceFileAtomically(const SwString& sourcePath,
                                  const SwString& destinationPath,
                                  SwString* errOut = nullptr) {
    ensureDirectory(parentPath(destinationPath));
#if defined(_WIN32)
    if (::MoveFileExW(sourcePath.toStdWString().c_str(),
                      destinationPath.toStdWString().c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        (void)syncDirectory(parentPath(destinationPath), nullptr);
        return true;
    }
    if (errOut) {
        *errOut = SwString("MoveFileExW failed: ") + SwString::number(static_cast<unsigned long long>(::GetLastError()));
    }
    return false;
#else
    if (::rename(sourcePath.toStdString().c_str(), destinationPath.toStdString().c_str()) == 0) {
        (void)syncDirectory(parentPath(destinationPath), nullptr);
        return true;
    }
    if (errOut) {
        *errOut = SwString("rename failed: ") + SwString(std::strerror(errno));
    }
    return false;
#endif
}

inline bool writeWholeFileAtomically(const SwString& path,
                                     const std::string& bytes,
                                     SwString* errOut = nullptr) {
    const SwString directory = parentPath(path);
    if (!ensureDirectory(directory)) {
        if (errOut) {
            *errOut = SwString("failed to create directory: ") + directory;
        }
        return false;
    }
    const SwString tempPath = path + ".tmp";
    RandomAccessFile temp;
    if (!temp.open(tempPath, RandomAccessFile::OpenMode::ReadWriteTruncate, errOut)) {
        return false;
    }
    if (!bytes.empty() && !temp.writeAt(0, bytes.data(), bytes.size(), errOut)) {
        temp.close();
        (void)removeFile(tempPath);
        return false;
    }
    if (!temp.sync(errOut)) {
        temp.close();
        (void)removeFile(tempPath);
        return false;
    }
    temp.close();
    return replaceFileAtomically(tempPath, path, errOut);
}

inline bool readWholeFile(const SwString& path, std::string& out, SwString* errOut = nullptr) {
    out.clear();
    if (!fileExists(path)) {
        return false;
    }
    RandomAccessFile file;
    if (!file.open(path, RandomAccessFile::OpenMode::ReadOnly, errOut)) {
        return false;
    }
    const unsigned long long bytes = file.size();
    if (bytes == 0) {
        return true;
    }
    out.resize(static_cast<std::size_t>(bytes));
    if (!file.readAt(0, &out[0], out.size(), errOut)) {
        out.clear();
        return false;
    }
    return true;
}

} // namespace swDbPlatform
