#pragma once

/**
 * @file src/media/SwSrtLibrary.h
 * @ingroup media
 * @brief Runtime loader for libsrt (Secure Reliable Transport), loaded with dlopen.
 *
 * No build-time or link-time dependency: the minimal, ABI-stable subset of the SRT C API
 * used by `SwSrtVideoSource` is declared locally and resolved at runtime — the same
 * pattern as `SwAlsaAudioSink` (libasound) and `SwOpusAudioDecoder` (libopus). When the
 * library is absent, `SwSrtLibrary::instance().available()` reports `false` and callers
 * degrade gracefully.
 *
 * The socket-option and error constants below are copied from srt.h, where they are
 * documented as frozen ABI values (stable since libsrt 1.3).
 */

#include "SwDebug.h"

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#else
#include <dlfcn.h>
#endif

#include <cstddef>

static constexpr const char* kSwLogCategory_SwSrtLibrary = "sw.media.swsrtlibrary";

class SwSrtLibrary {
public:
    // ABI-frozen values from srt.h.
    static const int kInvalidSocket = -1; // SRT_INVALID_SOCK
    static const int kError = -1;         // SRT_ERROR

    static const int kOptSndSyn = 1;      // SRTO_SNDSYN
    static const int kOptRcvSyn = 2;      // SRTO_RCVSYN
    static const int kOptSndTimeout = 13; // SRTO_SNDTIMEO (ms)
    static const int kOptRcvTimeout = 14; // SRTO_RCVTIMEO (ms)
    static const int kOptPassphrase = 26; // SRTO_PASSPHRASE
    static const int kOptConnTimeout = 36; // SRTO_CONNTIMEO (ms)
    static const int kOptRcvLatency = 43; // SRTO_RCVLATENCY (ms)
    static const int kOptStreamId = 46;   // SRTO_STREAMID

    // srt_getlasterror() codes are major*1000+minor; major 6 (MJ_AGAIN) covers the
    // retryable non-blocking/timeout family (EASYNCSND=6001, EASYNCRCV=6002,
    // ETIMEOUT=6003, ECONGEST=6004).
    static bool isRetryableError(int errorCode) {
        return errorCode / 1000 == 6;
    }

    using StartupFn = int (*)();
    using CleanupFn = int (*)();
    using CreateSocketFn = int (*)();
    using BindFn = int (*)(int socket, const void* addr, int addrLen);
    using ConnectFn = int (*)(int socket, const void* addr, int addrLen);
    using ListenFn = int (*)(int socket, int backlog);
    using AcceptFn = int (*)(int socket, void* addr, int* addrLen);
    using CloseFn = int (*)(int socket);
    using RecvMsgFn = int (*)(int socket, char* buffer, int length);
    using SendMsgFn = int (*)(int socket, const char* buffer, int length, int ttl, int inOrder);
    using SetSockFlagFn = int (*)(int socket, int option, const void* value, int valueLen);
    using GetLastErrorFn = int (*)(int* systemErrno);
    using GetLastErrorStrFn = const char* (*)();

    StartupFn startup{nullptr};
    CleanupFn cleanup{nullptr};
    CreateSocketFn createSocket{nullptr};
    BindFn bind{nullptr};
    ConnectFn connect{nullptr};
    ListenFn listen{nullptr};
    AcceptFn accept{nullptr};
    CloseFn close{nullptr};
    RecvMsgFn recvMsg{nullptr};
    SendMsgFn sendMsg{nullptr};
    SetSockFlagFn setSockFlag{nullptr};
    GetLastErrorFn getLastError{nullptr};
    GetLastErrorStrFn getLastErrorStr{nullptr};

    static SwSrtLibrary& instance() {
        static SwSrtLibrary g_library;
        return g_library;
    }

    bool available() const { return m_available; }

    /**
     * @brief Ensures srt_startup() ran once for the process. Safe to call repeatedly.
     */
    bool ensureStarted() {
        if (!m_available) {
            return false;
        }
        static const bool g_started = (instance().startup() >= 0);
        return g_started;
    }

    int lastErrorCode() const {
        if (!getLastError) {
            return 0;
        }
        int systemErrno = 0;
        return getLastError(&systemErrno);
    }

private:
    SwSrtLibrary() {
#if defined(_WIN32)
        static const char* kCandidates[] = {"srt.dll", "libsrt.dll"};
        for (std::size_t i = 0; i < sizeof(kCandidates) / sizeof(kCandidates[0]); ++i) {
            m_handle = ::LoadLibraryA(kCandidates[i]);
            if (m_handle) {
                break;
            }
        }
        if (!m_handle) {
            return;
        }
        struct Resolver {
            HMODULE handle;
            void* operator()(const char* name) const {
                return reinterpret_cast<void*>(::GetProcAddress(handle, name));
            }
        };
        Resolver resolve{static_cast<HMODULE>(m_handle)};
#else
        static const char* kCandidates[] = {
#if defined(__APPLE__)
            "libsrt.1.5.dylib", "libsrt.dylib",
#endif
            "libsrt.so.1.5", "libsrt.so.1.4", "libsrt.so.1", "libsrt.so"
        };
        for (std::size_t i = 0; i < sizeof(kCandidates) / sizeof(kCandidates[0]); ++i) {
            m_handle = ::dlopen(kCandidates[i], RTLD_NOW | RTLD_LOCAL);
            if (m_handle) {
                break;
            }
        }
        if (!m_handle) {
            return;
        }
        struct Resolver {
            void* handle;
            void* operator()(const char* name) const { return ::dlsym(handle, name); }
        };
        Resolver resolve{m_handle};
#endif
        startup = reinterpret_cast<StartupFn>(resolve("srt_startup"));
        cleanup = reinterpret_cast<CleanupFn>(resolve("srt_cleanup"));
        createSocket = reinterpret_cast<CreateSocketFn>(resolve("srt_create_socket"));
        bind = reinterpret_cast<BindFn>(resolve("srt_bind"));
        connect = reinterpret_cast<ConnectFn>(resolve("srt_connect"));
        listen = reinterpret_cast<ListenFn>(resolve("srt_listen"));
        accept = reinterpret_cast<AcceptFn>(resolve("srt_accept"));
        close = reinterpret_cast<CloseFn>(resolve("srt_close"));
        recvMsg = reinterpret_cast<RecvMsgFn>(resolve("srt_recvmsg"));
        sendMsg = reinterpret_cast<SendMsgFn>(resolve("srt_sendmsg"));
        setSockFlag = reinterpret_cast<SetSockFlagFn>(resolve("srt_setsockflag"));
        getLastError = reinterpret_cast<GetLastErrorFn>(resolve("srt_getlasterror"));
        getLastErrorStr = reinterpret_cast<GetLastErrorStrFn>(resolve("srt_getlasterror_str"));
        m_available = startup && createSocket && bind && connect && listen && accept &&
                      close && recvMsg && sendMsg && setSockFlag && getLastError;
        if (!m_available) {
            swCWarning(kSwLogCategory_SwSrtLibrary)
                << "[SwSrtLibrary] libsrt loaded but required symbols are missing.";
        }
    }

#if defined(_WIN32)
    void* m_handle{nullptr};
#else
    void* m_handle{nullptr};
#endif
    bool m_available{false};
};
