#pragma once

#include "SwDir.h"
#include "SwByteArray.h"
#include "SwString.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <cstdio>
#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#else
#include <fcntl.h>
#include <unistd.h>
#endif

/** Atomically replace a file without truncating the previous version first.
 *
 * A private, exclusively created temporary file lives beside the destination.
 * commit() synchronizes its contents before replacement. POSIX additionally
 * synchronizes the containing directory and its ancestors, including directory
 * entries created by open(); Windows uses FlushFileBuffers and a write-through
 * rename. A failed directory synchronization is reported even after replacement:
 * isCommitted() distinguishes that case from a failure leaving the old file intact.
 * Destroying an uncommitted writer removes its temporary file. No direct-write
 * fallback is permitted. Instances are owned by one thread.
 */
class SwSaveFile {
public:
    explicit SwSaveFile(const SwString& path) : destination_(path) {}
    ~SwSaveFile() { cancelWriting(); }
    SwSaveFile(const SwSaveFile&) = delete;
    SwSaveFile& operator=(const SwSaveFile&) = delete;

    bool open() {
        cancelWriting(); error_.clear(); committed_ = false; failed_ = false;
        if (destination_.isEmpty() || destination_.contains('\0'))
            return fail("Invalid save-file path");
        destination_ = SwDir::normalizePath(SwDir().absoluteFilePath(destination_));
        SwString portable = destination_; portable.replace("\\", "/");
        const auto slash = portable.lastIndexOf('/');
        directory_ = slash == static_cast<std::size_t>(-1) ? SwString(".") :
                     slash == 0 ? SwString("/") : portable.left(static_cast<int>(slash));
#if defined(_WIN32)
        if (directory_.size() == 2 && directory_[1] == ':') directory_ += "/";
#endif
        if (portable.endsWith("/") || !SwDir::mkpathAbsolute(directory_))
            return fail("Cannot create save-file directory: " + directory_);
        static std::atomic<uint64_t> sequence{0};
        for (unsigned attempt = 0; attempt < 128; ++attempt) {
#if defined(_WIN32)
            const auto pid = ::GetCurrentProcessId();
#else
            const auto pid = ::getpid();
#endif
            temporary_ = destination_ + ".tmp." + SwString::number(static_cast<unsigned long long>(pid)) +
                         "." + SwString::number(sequence.fetch_add(1));
#if defined(_WIN32)
            handle_ = ::CreateFileW(temporary_.toStdWString().c_str(), GENERIC_WRITE, 0, nullptr,
                                     CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle_ != INVALID_HANDLE_VALUE) return true;
            if (::GetLastError() != ERROR_FILE_EXISTS && ::GetLastError() != ERROR_ALREADY_EXISTS) break;
#else
            descriptor_ = ::open(temporary_.toStdString().c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
            if (descriptor_ >= 0) return true;
            if (errno != EEXIST) break;
#endif
        }
        // A path whose exclusive creation failed belongs to someone else.
        temporary_.clear();
        return systemFailure("Cannot create exclusive save-file temporary");
    }

    bool write(const SwString& bytes) { return write(bytes.data(), bytes.size()); }
    bool write(const SwByteArray& bytes) { return write(bytes.constData(), bytes.size()); }
    bool write(const char* bytes, std::size_t size) {
        if (!isOpen() || failed_ || (!bytes && size)) return fail("Cannot write unopened or failed save file");
        std::size_t offset = 0;
        while (offset < size) {
#if defined(_WIN32)
            DWORD written = 0;
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(size - offset, 1u << 20));
            if (!::WriteFile(handle_, bytes + offset, chunk, &written, nullptr))
                return systemFailure("Cannot write save file");
            if (!written) return fail("Save-file write made no progress");
#else
            const auto chunk = std::min<std::size_t>(size - offset, 1u << 20);
            const ssize_t written = ::write(descriptor_, bytes + offset, chunk);
            if (written < 0 && errno == EINTR) continue;
            if (written < 0) return systemFailure("Cannot write save file");
            if (!written) return fail("Save-file write made no progress");
#endif
            offset += static_cast<std::size_t>(written);
        }
        return true;
    }

    bool commit() {
        if (!isOpen() || failed_) return fail("Cannot commit unopened or failed save file");
#if defined(_WIN32)
        if (!::FlushFileBuffers(handle_)) return systemFailure("Cannot synchronize save file");
        if (!::CloseHandle(handle_)) { handle_ = INVALID_HANDLE_VALUE; return systemFailure("Cannot close save file"); }
        handle_ = INVALID_HANDLE_VALUE;
        if (!::MoveFileExW(temporary_.toStdWString().c_str(), destination_.toStdWString().c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return systemFailure("Cannot replace save file");
#else
        if (!syncDescriptor(descriptor_)) return systemFailure("Cannot synchronize save file");
        const int file = descriptor_; descriptor_ = -1;
        if (::close(file) != 0) return systemFailure("Cannot close save file");
        if (::rename(temporary_.toStdString().c_str(), destination_.toStdString().c_str()) != 0)
            return systemFailure("Cannot replace save file");
#endif
        committed_ = true;
        temporary_.clear();
#if !defined(_WIN32)
        SwString directory = SwDir(directory_).absolutePath();
        while (!directory.isEmpty()) {
            const int fd = ::open(directory.toStdString().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (fd < 0) return systemFailure("File replaced, but cannot open directory for synchronization");
            const bool synced = syncDescriptor(fd);
            const int savedError = errno;
            ::close(fd); errno = savedError;
            if (!synced) return systemFailure("File replaced, but directory synchronization failed");
            if (directory == "/") break;
            const auto slash = directory.lastIndexOf('/');
            directory = slash == 0 ? SwString("/") : directory.left(static_cast<int>(slash));
        }
#endif
        return true;
    }

    void cancelWriting() noexcept {
#if defined(_WIN32)
        if (handle_ != INVALID_HANDLE_VALUE) { ::CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; }
        if (!temporary_.isEmpty()) ::DeleteFileW(temporary_.toStdWString().c_str());
#else
        if (descriptor_ >= 0) { ::close(descriptor_); descriptor_ = -1; }
        if (!temporary_.isEmpty()) ::unlink(temporary_.toStdString().c_str());
#endif
        temporary_.clear();
    }
    bool isOpen() const noexcept {
#if defined(_WIN32)
        return handle_ != INVALID_HANDLE_VALUE;
#else
        return descriptor_ >= 0;
#endif
    }
    bool isCommitted() const noexcept { return committed_; }
    const SwString& errorString() const noexcept { return error_; }

    static bool writeAll(const SwString& path, const SwString& bytes, SwString* error = nullptr) {
        SwSaveFile file(path);
        const bool ok = file.open() && file.write(bytes) && file.commit();
        if (error) *error = file.errorString();
        return ok;
    }

private:
    bool fail(const SwString& message) { if (!failed_) error_ = message; failed_ = true; return false; }
    bool systemFailure(const SwString& operation) {
#if defined(_WIN32)
        return fail(operation + ": " + SwString::number(static_cast<unsigned long long>(::GetLastError())));
#else
        return fail(operation + ": " + SwString(std::strerror(errno)));
#endif
    }
#if !defined(_WIN32)
    static bool syncDescriptor(int fd) {
        int result;
        do { result = ::fsync(fd); } while (result != 0 && errno == EINTR);
        return result == 0;
    }
    int descriptor_{-1};
#else
    HANDLE handle_{INVALID_HANDLE_VALUE};
#endif
    SwString destination_, directory_, temporary_, error_;
    bool committed_{false}, failed_{false};
};
