#pragma once

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

namespace sw { namespace ipc { namespace detail {
// ftruncate alone creates sparse tmpfs pages: mmap succeeds even when /dev/shm
// is full, then the first write kills the process with SIGBUS. Reserve storage
// before exposing a writable mapping so callers can report allocation failure.
inline int reserveSharedMemory_(int fd, off_t bytes) {
    if (::ftruncate(fd, bytes) != 0) return -1;
#if defined(__linux__)
    int error;
    do { error = ::posix_fallocate(fd, 0, bytes); } while (error == EINTR);
    if (error) { errno = error; return -1; }
#endif
    return 0;
}
}}}
#endif
