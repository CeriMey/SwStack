#pragma once
/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include <memory>
#include <cstdlib>
#include <limits>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

/**
 * Minimal runtime loader/wrapper for OpenSSL (no static linkage).
 * Intended to be used by SwTcpSocket as a pluggable TLS backend.
 *
 * Usage:
 *   SwBackendSsl ssl;
 *   if (!ssl.init("host.name", socketFd)) { ... }
 *   auto hs = ssl.handshake();
 *   ...
 *   auto r = ssl.read(buf, len);
 *   auto w = ssl.write(buf, len);
 *   ssl.shutdown();
 */
class SwBackendSsl {
public:
    enum class IoResult {
        Ok,
        WantRead,
        WantWrite,
        Closed,
        Error
    };

    SwBackendSsl() = default;
    ~SwBackendSsl() {
        cleanup();
    }

    // Register an extra directory to search for libssl/libcrypto when loading dynamically.
    static void addSearchPath(const std::string& path) {
        if (!path.empty()) {
            extraSearchPaths().push_back(path);
        }
    }

    bool init(const std::string& host, intptr_t fd) {
        if (!ensureLoaded()) {
            m_lastError = "OpenSSL libraries not found";
            return false;
        }

        if (!m_loader || !m_loader->TLS_client_method) {
            m_lastError = "OpenSSL TLS_client_method missing";
            return false;
        }

        m_ctx = m_loader->SSL_CTX_new(m_loader->TLS_client_method());
        if (!m_ctx) {
            m_lastError = "SSL_CTX_new failed";
            return false;
        }

        // For now disable verification to unblock connectivity; add CA paths later if needed.
        m_loader->SSL_CTX_set_verify(m_ctx, 0 /*SSL_VERIFY_NONE*/, nullptr);
        m_loader->SSL_CTX_set_default_verify_paths(m_ctx);

        m_ssl = m_loader->SSL_new(m_ctx);
        if (!m_ssl) {
            m_lastError = "SSL_new failed";
            return false;
        }

        if (fd > std::numeric_limits<int>::max()) {
            m_lastError = "Socket handle out of range for SSL_set_fd";
            return false;
        }
        if (m_loader->SSL_set_fd(m_ssl, static_cast<int>(fd)) != 1) {
            m_lastError = "SSL_set_fd failed";
            return false;
        }

        if (m_loader->SSL_set1_host) {
            m_loader->SSL_set1_host(m_ssl, host.c_str());
        }
        if (m_loader->SSL_ctrl) {
            m_loader->SSL_ctrl(m_ssl, 55 /*SSL_CTRL_SET_TLSEXT_HOSTNAME*/, 0, (void*)host.c_str());
        }
        m_loader->SSL_set_connect_state(m_ssl);
        return true;
    }

    IoResult handshake() {
        if (!m_ssl || !m_loader) {
            return IoResult::Error;
        }
        int ret = m_loader->SSL_do_handshake(m_ssl);
        return mapResult(ret);
    }

    IoResult read(char* buf, int len, int& outBytes) {
        outBytes = 0;
        if (!m_ssl || !m_loader) {
            return IoResult::Error;
        }
        int ret = m_loader->SSL_read(m_ssl, buf, len);
        if (ret > 0) {
            outBytes = ret;
            return IoResult::Ok;
        }
        return mapResult(ret);
    }

    IoResult write(const char* buf, int len, int& outBytes) {
        outBytes = 0;
        if (!m_ssl || !m_loader) {
            return IoResult::Error;
        }
        int ret = m_loader->SSL_write(m_ssl, buf, len);
        if (ret > 0) {
            outBytes = ret;
            return IoResult::Ok;
        }
        return mapResult(ret);
    }

    void shutdown() {
        if (m_ssl && m_loader) {
            m_loader->SSL_shutdown(m_ssl);
        }
        cleanup();
    }

    std::string lastError() const {
        return m_lastError;
    }

private:
    struct Loader {
        using LibHandle =
#if defined(_WIN32)
            HMODULE
#else
            void*
#endif
            ;

        LibHandle ssl = nullptr;
        LibHandle crypto = nullptr;
        bool ready = false;

        // Function pointers we need
        using FnOpenSSLInit = int (*)(uint64_t, const void*, const void*);
        using FnTLSClientMethod = const void* (*)();
        using FnCTXNew = void* (*)(const void*);
        using FnCTXFree = void (*)(void*);
        using FnCTXSetVerify = void (*)(void*, int, int (*)(int, void*));
        using FnCTXSetDefaultVerifyPaths = int (*)(void*);
        using FnNew = void* (*)(void*);
        using FnFree = void (*)(void*);
        using FnSetFd = int (*)(void*, int);
        using FnSet1Host = int (*)(void*, const char*);
        using FnCtrl = long (*)(void*, int, long, void*);
        using FnSetConnectState = void (*)(void*);
        using FnDoHandshake = int (*)(void*);
        using FnGetError = int (*)(const void*, int);
        using FnRead = int (*)(void*, void*, int);
        using FnWrite = int (*)(void*, const void*, int);
        using FnShutdown = int (*)(void*);

        FnOpenSSLInit OPENSSL_init_ssl = nullptr;
        FnTLSClientMethod TLS_client_method = nullptr;
        FnCTXNew SSL_CTX_new = nullptr;
        FnCTXFree SSL_CTX_free = nullptr;
        FnCTXSetVerify SSL_CTX_set_verify = nullptr;
        FnCTXSetDefaultVerifyPaths SSL_CTX_set_default_verify_paths = nullptr;
        FnNew SSL_new = nullptr;
        FnFree SSL_free = nullptr;
        FnSetFd SSL_set_fd = nullptr;
        FnSet1Host SSL_set1_host = nullptr;
        FnCtrl SSL_ctrl = nullptr;
        FnSetConnectState SSL_set_connect_state = nullptr;
        FnDoHandshake SSL_do_handshake = nullptr;
        FnGetError SSL_get_error = nullptr;
        FnRead SSL_read = nullptr;
        FnWrite SSL_write = nullptr;
        FnShutdown SSL_shutdown = nullptr;

        bool load(std::string& outError) {
            if (ready) {
                return true;
            }
            std::vector<std::string> sslNames = {
#if defined(_WIN32)
                "libssl-3-x64.dll", "libssl-3.dll", "libssl-1_1.dll", "libssl.dll"
#else
                "libssl.so.3", "libssl.so.1.1", "libssl.so"
#endif
            };
            std::vector<std::string> cryptoNames = {
#if defined(_WIN32)
                "libcrypto-3-x64.dll", "libcrypto-3.dll", "libcrypto-1_1.dll", "libcrypto.dll"
#else
                "libcrypto.so.3", "libcrypto.so.1.1", "libcrypto.so"
#endif
            };

            ssl = tryLoad(sslNames);
            crypto = tryLoad(cryptoNames);
            if (!ssl || !crypto) {
                outError = "Unable to load OpenSSL (searched PATH and fallbacks)";
                return false;
            }

            auto sym = [&](LibHandle h, const char* n) -> void* {
                return getSymbol(h, n);
            };

            OPENSSL_init_ssl = (FnOpenSSLInit)sym(ssl, "OPENSSL_init_ssl");
            TLS_client_method = (FnTLSClientMethod)sym(ssl, "TLS_client_method");
            SSL_CTX_new = (FnCTXNew)sym(ssl, "SSL_CTX_new");
            SSL_CTX_free = (FnCTXFree)sym(ssl, "SSL_CTX_free");
            SSL_CTX_set_verify = (FnCTXSetVerify)sym(ssl, "SSL_CTX_set_verify");
            SSL_CTX_set_default_verify_paths = (FnCTXSetDefaultVerifyPaths)sym(ssl, "SSL_CTX_set_default_verify_paths");
            SSL_new = (FnNew)sym(ssl, "SSL_new");
            SSL_free = (FnFree)sym(ssl, "SSL_free");
            SSL_set_fd = (FnSetFd)sym(ssl, "SSL_set_fd");
            SSL_set1_host = (FnSet1Host)sym(ssl, "SSL_set1_host");
            SSL_ctrl = (FnCtrl)sym(ssl, "SSL_ctrl");
            SSL_set_connect_state = (FnSetConnectState)sym(ssl, "SSL_set_connect_state");
            SSL_do_handshake = (FnDoHandshake)sym(ssl, "SSL_do_handshake");
            SSL_get_error = (FnGetError)sym(ssl, "SSL_get_error");
            SSL_read = (FnRead)sym(ssl, "SSL_read");
            SSL_write = (FnWrite)sym(ssl, "SSL_write");
            SSL_shutdown = (FnShutdown)sym(ssl, "SSL_shutdown");

            if (!TLS_client_method || !SSL_CTX_new || !SSL_new || !SSL_set_fd || !SSL_do_handshake ||
                !SSL_get_error || !SSL_read || !SSL_write || !SSL_shutdown) {
                outError = "Missing required OpenSSL symbols";
                return false;
            }

            if (OPENSSL_init_ssl) {
                OPENSSL_init_ssl(0, nullptr, nullptr);
            }

            ready = true;
            return true;
        }

    private:
        LibHandle tryLoad(const std::vector<std::string>& names) {
            std::vector<std::string> searchPaths = {""}; // honor PATH/LD_LIBRARY_PATH first
            auto& custom = extraSearchPaths();
            searchPaths.insert(searchPaths.end(), custom.begin(), custom.end());
#if defined(_WIN32)
            searchPaths.push_back("C:\\\\emsdk\\\\python\\\\3.9.2-nuget_64bit\\\\DLLs\\\\");
            searchPaths.push_back("C:\\\\Libs\\\\gstreamer\\\\1.0\\\\msvc_x86_64\\\\bin\\\\");
#else
            searchPaths.push_back("/usr/local/lib/");
            searchPaths.push_back("/usr/lib/");
#endif
            for (const auto& name : names) {
                LibHandle h = nullptr;
                for (const auto& prefix : searchPaths) {
                    std::string full = prefix + name;
                    h =
#if defined(_WIN32)
                        LoadLibraryA(full.c_str());
#else
                        dlopen(full.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
                    if (h) {
                        break;
                    }
                }
                if (h) {
                    return h;
                }
            }
            return nullptr;
        }

        void* getSymbol(LibHandle h, const char* name) {
            if (!h) {
                return nullptr;
            }
#if defined(_WIN32)
            return reinterpret_cast<void*>(GetProcAddress(h, name));
#else
            return dlsym(h, name);
#endif
        }
    };

    IoResult mapResult(int ret) {
        if (!m_loader || !m_ssl) {
            return IoResult::Error;
        }
        int err = m_loader->SSL_get_error(m_ssl, ret);
        switch (err) {
        case kSSL_ERROR_NONE:
            return IoResult::Ok;
        case kSSL_ERROR_WANT_READ:
            return IoResult::WantRead;
        case kSSL_ERROR_WANT_WRITE:
            return IoResult::WantWrite;
        case kSSL_ERROR_ZERO_RETURN:
            return IoResult::Closed;
        default:
            long sys = 0;
#if defined(_WIN32)
            sys = WSAGetLastError();
#else
            sys = errno;
#endif
            m_lastError = "SSL error code " + std::to_string(err) + " sys=" + std::to_string(sys);
            return IoResult::Error;
        }
    }

    void cleanup() {
        if (m_ssl && m_loader && m_loader->SSL_free) {
            m_loader->SSL_free(m_ssl);
        }
        if (m_ctx && m_loader && m_loader->SSL_CTX_free) {
            m_loader->SSL_CTX_free(m_ctx);
        }
        m_ssl = nullptr;
        m_ctx = nullptr;
    }

    bool ensureLoaded() {
        static Loader s_loader;
        std::string err;
        if (!s_loader.load(err)) {
            m_lastError = err.empty() ? "OpenSSL libraries not found" : err;
            return false;
        }
        m_loader = &s_loader;
        return true;
    }

    static constexpr int kSSL_ERROR_NONE = 0;
    static constexpr int kSSL_ERROR_WANT_READ = 2;
    static constexpr int kSSL_ERROR_WANT_WRITE = 3;
    static constexpr int kSSL_ERROR_ZERO_RETURN = 6;
    static constexpr int SSL_VERIFY_PEER = 0x01;

    static std::vector<std::string>& extraSearchPaths() {
        static std::vector<std::string> paths;
        if (paths.empty()) {
#if defined(_MSC_VER)
            char* value = nullptr;
            size_t valueLen = 0;
            if (_dupenv_s(&value, &valueLen, "SW_SSL_PATH") == 0 && value) {
                paths.push_back(std::string(value));
                std::free(value);
            }
#else
            const char* env = std::getenv("SW_SSL_PATH");
            if (env) {
                paths.push_back(std::string(env));
            }
#endif
        }
        return paths;
    }

    Loader* m_loader = nullptr;
    void* m_ctx = nullptr;
    void* m_ssl = nullptr;
    std::string m_lastError;
};
