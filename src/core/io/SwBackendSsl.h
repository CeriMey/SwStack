#pragma once

/**
 * @file src/core/io/SwBackendSsl.h
 * @ingroup core_io
 * @brief Declares the public interface exposed by SwBackendSsl in the CoreSw IO layer.
 *
 * This header belongs to the CoreSw IO layer. It defines files, sockets, servers, descriptors,
 * processes, and network helpers that sit directly at operating-system boundaries.
 *
 * Within that layer, this file focuses on the backend SSL interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwBackendSsl.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * IO-facing declarations here usually manage handles, readiness state, buffering, and error
 * propagation while presenting a portable framework API.
 *
 */

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
#include <map>
#include <functional>
#include <iostream>
#include <memory>
#include <cstdlib>
#include <limits>
#include <array>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#if defined(_MSC_VER)
#pragma comment(lib, "crypt32.lib")
#endif
#else
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
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
    struct Loader;
    struct ServerContextBundle {
        Loader* loader = nullptr;
        std::atomic<int> refCount{0};
        void* defaultCtx = nullptr;
        std::vector<void*> contexts;
        std::map<std::string, void*> namedContexts;
    };

public:
    enum class IoResult {
        Ok,
        WantRead,
        WantWrite,
        Closed,
        Error
    };

    /**
     * @brief Constructs a `SwBackendSsl` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwBackendSsl() = default;
    /**
     * @brief Destroys the `SwBackendSsl` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwBackendSsl() {
        cleanup();
    }

    // Register an extra directory to search for libssl/libcrypto when loading dynamically.
    /**
     * @brief Adds the specified search Path.
     * @param path Path used by the operation.
     * @return The requested search Path.
     */
    static void addSearchPath(const std::string& path) {
        if (!path.empty()) {
            extraSearchPaths().push_back(path);
        }
    }

    static void setFallbackTrustedCaFile(const std::string& path) {
        fallbackTrustedCaFile_() = path;
    }

    static void setFallbackTrustedCaDirectory(const std::string& path) {
        fallbackTrustedCaDirectory_() = path;
    }

    struct ServerCertificateConfig {
        std::string host;
        std::string certPath;
        std::string keyPath;
        bool isDefault = false;
    };

    /**
     * @brief Creates a shared SSL_CTX configured for server-side TLS.
     * @param certPath Path to the PEM certificate file.
     * @param keyPath Path to the PEM private key file.
     * @param outError Receives the error description on failure.
     * @return An opaque pointer to the SSL_CTX, or nullptr on failure.
     */
    static void* createServerContext(const std::string& certPath,
                                     const std::string& keyPath,
                                     std::string& outError) {
        std::vector<ServerCertificateConfig> configs;
        ServerCertificateConfig config;
        config.certPath = certPath;
        config.keyPath = keyPath;
        config.isDefault = true;
        configs.push_back(config);
        return createServerContextSet(configs, outError);
    }

    static void* createServerContextSet(const std::vector<ServerCertificateConfig>& configs,
                                        std::string& outError) {
        if (configs.empty()) {
            outError = "No TLS certificate configured";
            return nullptr;
        }

        SwBackendSsl tmp;
        if (!tmp.ensureLoaded()) {
            outError = "OpenSSL libraries not found";
            return nullptr;
        }
        Loader* loader = tmp.m_loader;
        if (!loader->TLS_server_method) {
            outError = "TLS_server_method not available";
            return nullptr;
        }

        std::unique_ptr<ServerContextBundle> bundle(new ServerContextBundle());
        bundle->loader = loader;
        bundle->refCount.store(1);

        for (std::size_t i = 0; i < configs.size(); ++i) {
            const ServerCertificateConfig& config = configs[i];
            if (config.certPath.empty() || config.keyPath.empty()) {
                continue;
            }

            void* ctx = createServerSslContext_(loader, config.certPath, config.keyPath, outError);
            if (!ctx) {
                releaseServerContextBundle_(bundle.release());
                return nullptr;
            }

            bundle->contexts.push_back(ctx);
            const std::string host = normalizeServerName_(config.host);
            if (!host.empty()) {
                bundle->namedContexts[host] = ctx;
            }
            if (!bundle->defaultCtx || config.isDefault) {
                bundle->defaultCtx = ctx;
            }
        }

        if (!bundle->defaultCtx && !bundle->contexts.empty()) {
            bundle->defaultCtx = bundle->contexts.front();
        }
        if (!bundle->defaultCtx) {
            outError = "No valid TLS certificate configured";
            releaseServerContextBundle_(bundle.release());
            return nullptr;
        }

        if (!bundle->namedContexts.empty()) {
            if (!loader->SSL_CTX_callback_ctrl || !loader->SSL_get_servername || !loader->SSL_set_SSL_CTX) {
                outError = "OpenSSL SNI symbols are not available";
                releaseServerContextBundle_(bundle.release());
                return nullptr;
            }
            if (!loader->SSL_CTX_callback_ctrl(bundle->defaultCtx,
                                               kSslCtrlSetTlsextServernameCallback,
                                               reinterpret_cast<void (*)()>(serverNameCallback_))) {
                outError = "Unable to install TLS SNI callback";
                releaseServerContextBundle_(bundle.release());
                return nullptr;
            }
            if (!loader->SSL_CTX_ctrl(bundle->defaultCtx,
                                      kSslCtrlSetTlsextServernameArg,
                                      0,
                                      bundle.get())) {
                outError = "Unable to install TLS SNI callback context";
                releaseServerContextBundle_(bundle.release());
                return nullptr;
            }
        }

        return bundle.release();
    }

    /**
     * @brief Frees a server SSL_CTX previously created by createServerContext().
     * @param ctx The context to free.
     */
    static void freeServerContext(void* ctx) {
        releaseServerContextBundle_(static_cast<ServerContextBundle*>(ctx));
    }

    /**
     * @brief Initializes a server-side TLS session from a shared SSL_CTX.
     * @param sharedCtx An SSL_CTX pointer created by createServerContext().
     * @param fd The accepted socket file descriptor.
     * @return true on success.
     */
    bool initServer(void* sharedCtx, intptr_t fd) {
        ServerContextBundle* bundle = static_cast<ServerContextBundle*>(sharedCtx);
        if (!bundle || !bundle->defaultCtx) {
            m_lastError = "Null server SSL_CTX";
            return false;
        }
        if (!ensureLoaded()) {
            return false;
        }
        if (!m_loader->SSL_set_accept_state) {
            m_lastError = "SSL_set_accept_state not available";
            return false;
        }

        // We do NOT own the CTX — it is shared. Store nullptr so cleanup() won't free it.
        retainServerContextBundle_(bundle);
        m_serverContextBundle = bundle;
        if (bundle->loader) {
            m_loader = bundle->loader;
        }

        m_ctx = nullptr;
        m_ssl = m_loader->SSL_new(bundle->defaultCtx);
        if (!m_ssl) {
            m_lastError = "SSL_new (server) failed";
            releaseServerContextBundle_(m_serverContextBundle);
            m_serverContextBundle = nullptr;
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
        m_loader->SSL_set_accept_state(m_ssl);
        return true;
    }

    /**
     * @brief Initializes a client-side TLS session.
     * @param host Value passed to the method.
     * @param fd Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool init(const std::string& host, intptr_t fd, const std::string& caFilePath = std::string()) {
        if (!ensureLoaded()) {
            m_lastError = "OpenSSL libraries not found";
            return false;
        }

        if (!m_loader || !m_loader->TLS_client_method) {
            m_lastError = "OpenSSL TLS_client_method missing";
            return false;
        }
        if (host.empty()) {
            m_lastError = "TLS peer hostname is required";
            return false;
        }
        if (!m_loader->SSL_CTX_set_verify) {
            m_lastError = "SSL_CTX_set_verify missing";
            return false;
        }
        if (!m_loader->SSL_set1_host) {
            m_lastError = "SSL_set1_host missing";
            return false;
        }

        m_ctx = m_loader->SSL_CTX_new(m_loader->TLS_client_method());
        if (!m_ctx) {
            m_lastError = "SSL_CTX_new failed";
            return false;
        }

        m_loader->SSL_CTX_set_verify(m_ctx, SSL_VERIFY_PEER, nullptr);
        if (!configureClientTrust_(caFilePath)) {
            return false;
        }

        // Enable partial writes for non-blocking sockets.
        if (m_loader->SSL_CTX_ctrl) {
            m_loader->SSL_CTX_ctrl(m_ctx, 33 /*SSL_CTRL_MODE*/, 0x01 | 0x02, nullptr);
        }

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

    /**
     * @brief Returns the current handshake.
     * @return The current handshake.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    IoResult handshake() {
        if (!m_ssl || !m_loader) {
            return IoResult::Error;
        }
        int ret = m_loader->SSL_do_handshake(m_ssl);
        return mapResult(ret);
    }

    /**
     * @brief Performs the `read` operation on the associated resource.
     * @param buf Value passed to the method.
     * @param len Value passed to the method.
     * @param outBytes Output value filled by the method.
     * @return The resulting read.
     */
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

    /**
     * @brief Performs the `write` operation on the associated resource.
     * @param buf Value passed to the method.
     * @param len Value passed to the method.
     * @param outBytes Output value filled by the method.
     * @return The requested write.
     */
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

    /**
     * @brief Performs the `shutdown` operation.
     */
    void shutdown() {
        if (m_ssl && m_loader) {
            m_loader->SSL_shutdown(m_ssl);
        }
        cleanup();
    }

    /**
     * @brief Returns the current last Error.
     * @return The current last Error.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::string lastError() const {
        return m_lastError;
    }

private:
    bool configureClientTrust_(const std::string& caFilePath) {
        if (!caFilePath.empty()) {
            if (!pathIsFile_(caFilePath)) {
                m_lastError = "Failed to load CA file: " + caFilePath + " (file not found)";
                return false;
            }
            if (!m_loader->SSL_CTX_load_verify_locations ||
                m_loader->SSL_CTX_load_verify_locations(m_ctx, caFilePath.c_str(), nullptr) != 1) {
                m_lastError = "Failed to load CA file: " + caFilePath;
                const std::string detail = describeOpenSslFailure_();
                if (!detail.empty()) {
                    m_lastError += " " + detail;
                }
                if (m_loader->ERR_clear_error) {
                    m_loader->ERR_clear_error();
                }
                return false;
            }
            return true;
        }

        std::string trustSummary;
        bool trustLoaded = false;

        trustLoaded = loadDefaultVerifyPaths_(trustSummary) || trustLoaded;
#if defined(_WIN32)
        trustLoaded = loadWindowsSystemStores_(trustSummary) || trustLoaded;
#else
        trustLoaded = loadPosixSystemTrust_(trustSummary) || trustLoaded;
#endif
        trustLoaded = loadFallbackTrustSources_(trustSummary) || trustLoaded;

        if (!trustLoaded) {
            m_lastError = "No trusted CA certificates available";
            if (!trustSummary.empty()) {
                m_lastError += " (" + trustSummary + ")";
            }
            return false;
        }
        return true;
    }

    static std::string normalizeServerName_(const std::string& host) {
        std::string out = host;
        while (!out.empty() &&
               (out.back() == '.' || out.back() == ' ' || out.back() == '\t' || out.back() == '\r' || out.back() == '\n')) {
            out.pop_back();
        }
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return out;
    }

    static void retainServerContextBundle_(ServerContextBundle* bundle) {
        if (!bundle) {
            return;
        }
        ++bundle->refCount;
    }

    static void releaseServerContextBundle_(ServerContextBundle* bundle) {
        if (!bundle) {
            return;
        }
        if (--bundle->refCount > 0) {
            return;
        }
        Loader* loader = bundle->loader;
        if (loader && loader->SSL_CTX_free) {
            for (std::size_t i = 0; i < bundle->contexts.size(); ++i) {
                if (bundle->contexts[i]) {
                    loader->SSL_CTX_free(bundle->contexts[i]);
                }
            }
        }
        delete bundle;
    }

    static void* createServerSslContext_(Loader* loader,
                                         const std::string& certPath,
                                         const std::string& keyPath,
                                         std::string& outError) {
        if (!loader || !loader->TLS_server_method) {
            outError = "TLS_server_method not available";
            return nullptr;
        }

        void* ctx = loader->SSL_CTX_new(loader->TLS_server_method());
        if (!ctx) {
            outError = "SSL_CTX_new (server) failed";
            return nullptr;
        }

        bool certificateLoaded = false;
        if (loader->SSL_CTX_use_certificate_chain_file) {
            certificateLoaded = (loader->SSL_CTX_use_certificate_chain_file(ctx, certPath.c_str()) == 1);
            if (!certificateLoaded && loader->ERR_clear_error) {
                loader->ERR_clear_error();
            }
        }
        if (!certificateLoaded) {
            if (!loader->SSL_CTX_use_certificate_file ||
                loader->SSL_CTX_use_certificate_file(ctx, certPath.c_str(), 1) != 1) {
                outError = "Failed to load certificate: " + certPath;
                loader->SSL_CTX_free(ctx);
                return nullptr;
            }
        }
        if (!loader->SSL_CTX_use_PrivateKey_file ||
            loader->SSL_CTX_use_PrivateKey_file(ctx, keyPath.c_str(), 1) != 1) {
            outError = "Failed to load private key: " + keyPath;
            loader->SSL_CTX_free(ctx);
            return nullptr;
        }
        if (loader->SSL_CTX_check_private_key &&
            loader->SSL_CTX_check_private_key(ctx) != 1) {
            outError = "Private key does not match certificate";
            loader->SSL_CTX_free(ctx);
            return nullptr;
        }
        if (loader->SSL_CTX_ctrl) {
            loader->SSL_CTX_ctrl(ctx,
                                 kSslCtrlMode,
                                 kSslModeEnablePartialWrite | kSslModeAcceptMovingWriteBuffer,
                                 nullptr);
        }
        return ctx;
    }

    static int serverNameCallback_(void* ssl, int*, void* arg) {
        ServerContextBundle* bundle = static_cast<ServerContextBundle*>(arg);
        if (!bundle || !bundle->loader || !bundle->loader->SSL_get_servername || !bundle->loader->SSL_set_SSL_CTX) {
            return kSslTlsextErrOk;
        }

        const char* requestedName = bundle->loader->SSL_get_servername(ssl, kTlsExtNameTypeHostName);
        if (!requestedName || !*requestedName) {
            return kSslTlsextErrOk;
        }

        const std::string normalizedName = normalizeServerName_(requestedName);
        std::map<std::string, void*>::const_iterator it = bundle->namedContexts.find(normalizedName);
        if (it != bundle->namedContexts.end() && it->second) {
            bundle->loader->SSL_set_SSL_CTX(ssl, it->second);
        }
        return kSslTlsextErrOk;
    }

    bool loadDefaultVerifyPaths_(std::string& trustSummary) {
        if (!m_loader->SSL_CTX_set_default_verify_paths) {
            appendTrustSummary_(trustSummary, "openssl-defaults unavailable");
            return false;
        }

        if (m_loader->SSL_CTX_set_default_verify_paths(m_ctx) == 1) {
            appendTrustSummary_(trustSummary, "openssl-defaults");
            return true;
        }

        if (m_loader->ERR_clear_error) {
            m_loader->ERR_clear_error();
        }
        appendTrustSummary_(trustSummary, "openssl-defaults failed");
        return false;
    }

    bool loadVerifyLocation_(const std::string& filePath,
                             const std::string& dirPath,
                             const std::string& label,
                             std::string& trustSummary) {
        if ((!filePath.empty() && !pathIsFile_(filePath)) ||
            (!dirPath.empty() && !pathIsDirectory_(dirPath))) {
            return false;
        }
        if (!m_loader->SSL_CTX_load_verify_locations) {
            appendTrustSummary_(trustSummary, label + " unavailable");
            return false;
        }

        const char* fileArg = filePath.empty() ? nullptr : filePath.c_str();
        const char* dirArg = dirPath.empty() ? nullptr : dirPath.c_str();
        if (m_loader->SSL_CTX_load_verify_locations(m_ctx, fileArg, dirArg) == 1) {
            appendTrustSummary_(trustSummary, label);
            return true;
        }

        if (m_loader->ERR_clear_error) {
            m_loader->ERR_clear_error();
        }
        appendTrustSummary_(trustSummary, label + " failed");
        return false;
    }

#if !defined(_WIN32)
    bool loadPosixSystemTrust_(std::string& trustSummary) {
        bool loaded = false;

        const std::string envFile = environmentValue_("SSL_CERT_FILE");
        if (!envFile.empty()) {
            loaded = loadVerifyLocation_(envFile, std::string(), "env SSL_CERT_FILE", trustSummary) || loaded;
        }

        const std::string envDir = environmentValue_("SSL_CERT_DIR");
        if (!envDir.empty()) {
            loaded = loadVerifyLocation_(std::string(), envDir, "env SSL_CERT_DIR", trustSummary) || loaded;
        }

        static const std::array<const char*, 7> kSystemCaFiles = {
            "/etc/ssl/certs/ca-certificates.crt",
            "/etc/pki/tls/certs/ca-bundle.crt",
            "/etc/ssl/ca-bundle.pem",
            "/etc/pki/tls/cacert.pem",
            "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
            "/etc/ssl/cert.pem",
            "/etc/openssl/certs/ca-certificates.crt"
        };
        for (const char* file : kSystemCaFiles) {
            loaded = loadVerifyLocation_(file, std::string(), std::string("system-ca-file:") + file, trustSummary) || loaded;
        }

        static const std::array<const char*, 4> kSystemCaDirs = {
            "/etc/ssl/certs",
            "/etc/pki/tls/certs",
            "/etc/ca-certificates/extracted/cadir",
            "/etc/openssl/certs"
        };
        for (const char* dir : kSystemCaDirs) {
            loaded = loadVerifyLocation_(std::string(), dir, std::string("system-ca-dir:") + dir, trustSummary) || loaded;
        }

        return loaded;
    }
#endif

    bool loadFallbackTrustSources_(std::string& trustSummary) {
        bool loaded = false;

        const std::string configuredFile = fallbackTrustedCaFile_();
        if (!configuredFile.empty()) {
            loaded = loadVerifyLocation_(configuredFile, std::string(), "fallback-ca-file", trustSummary) || loaded;
        }

        const std::string configuredDir = fallbackTrustedCaDirectory_();
        if (!configuredDir.empty()) {
            loaded = loadVerifyLocation_(std::string(), configuredDir, "fallback-ca-dir", trustSummary) || loaded;
        }

        const std::string envFile = environmentValue_("SW_SSL_CA_FILE");
        if (!envFile.empty()) {
            loaded = loadVerifyLocation_(envFile, std::string(), "env SW_SSL_CA_FILE", trustSummary) || loaded;
        }

        const std::string envDir = environmentValue_("SW_SSL_CA_DIR");
        if (!envDir.empty()) {
            loaded = loadVerifyLocation_(std::string(), envDir, "env SW_SSL_CA_DIR", trustSummary) || loaded;
        }

        for (const std::string& candidate : bundledTrustFileCandidates_()) {
            loaded = loadVerifyLocation_(candidate, std::string(), std::string("bundle:") + candidate, trustSummary) || loaded;
        }

        for (const std::string& candidate : bundledTrustDirectoryCandidates_()) {
            loaded = loadVerifyLocation_(std::string(), candidate, std::string("bundle-dir:") + candidate, trustSummary) || loaded;
        }

        return loaded;
    }

    static void appendTrustSummary_(std::string& trustSummary, const std::string& detail) {
        if (detail.empty()) {
            return;
        }
        if (!trustSummary.empty()) {
            trustSummary += "; ";
        }
        trustSummary += detail;
    }

    static bool pathIsFile_(const std::string& path) {
        if (path.empty()) {
            return false;
        }
#if defined(_WIN32)
        const DWORD attrs = GetFileAttributesA(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
        struct stat st;
        return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
    }

    static bool pathIsDirectory_(const std::string& path) {
        if (path.empty()) {
            return false;
        }
#if defined(_WIN32)
        const DWORD attrs = GetFileAttributesA(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
        struct stat st;
        return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
    }

    static std::string normalizePathSeparators_(std::string path) {
        for (char& c : path) {
            if (c == '\\') {
                c = '/';
            }
        }
        return path;
    }

    static std::string parentDirectory_(const std::string& path) {
        if (path.empty()) {
            return {};
        }
        const std::string normalized = normalizePathSeparators_(path);
        const std::size_t pos = normalized.find_last_of('/');
        if (pos == std::string::npos) {
            return {};
        }
        return normalized.substr(0, pos);
    }

    static std::string joinPath_(const std::string& base, const std::string& leaf) {
        if (base.empty()) {
            return normalizePathSeparators_(leaf);
        }
        std::string result = normalizePathSeparators_(base);
        if (!result.empty() && result.back() != '/') {
            result.push_back('/');
        }
        result += normalizePathSeparators_(leaf);
        return result;
    }

    static std::string currentExecutableDirectory_() {
#if defined(_WIN32)
        std::vector<char> buffer(static_cast<size_t>(MAX_PATH), '\0');
        DWORD len = ::GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        while (len > 0 && len >= buffer.size() - 1) {
            buffer.resize(buffer.size() * 2, '\0');
            len = ::GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        }
        if (len == 0) {
            return {};
        }
        return parentDirectory_(std::string(buffer.data(), len));
#else
        std::array<char, 4096> buffer{};
        const ssize_t len = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
        if (len <= 0) {
            return {};
        }
        buffer[static_cast<std::size_t>(len)] = '\0';
        return parentDirectory_(std::string(buffer.data(), static_cast<std::size_t>(len)));
#endif
    }

    static std::string currentWorkingDirectory_() {
#if defined(_WIN32)
        DWORD len = ::GetCurrentDirectoryA(0, nullptr);
        if (len == 0) {
            return {};
        }
        std::vector<char> buffer(static_cast<std::size_t>(len) + 1, '\0');
        len = ::GetCurrentDirectoryA(static_cast<DWORD>(buffer.size()), buffer.data());
        if (len == 0) {
            return {};
        }
        return normalizePathSeparators_(std::string(buffer.data(), len));
#else
        std::array<char, 4096> buffer{};
        if (!::getcwd(buffer.data(), buffer.size())) {
            return {};
        }
        return normalizePathSeparators_(std::string(buffer.data()));
#endif
    }

    static std::vector<std::string> bundledTrustFileCandidates_() {
        static const std::array<const char*, 9> kRelativeFiles = {
            "certs/ca-bundle.pem",
            "certs/ca-bundle.crt",
            "certs/cacert.pem",
            "certs/cert.pem",
            "ssl/cert.pem",
            "ca-bundle.pem",
            "ca-bundle.crt",
            "cacert.pem",
            "cert.pem"
        };

        std::vector<std::string> candidates;
        const std::string exeDir = currentExecutableDirectory_();
        const std::string cwd = currentWorkingDirectory_();
        for (const char* rel : kRelativeFiles) {
            if (!exeDir.empty()) {
                candidates.push_back(joinPath_(exeDir, rel));
            }
            if (!cwd.empty() && cwd != exeDir) {
                candidates.push_back(joinPath_(cwd, rel));
            }
        }
        return candidates;
    }

    static std::vector<std::string> bundledTrustDirectoryCandidates_() {
        static const std::array<const char*, 3> kRelativeDirs = {
            "certs",
            "ssl/certs",
            "ca"
        };

        std::vector<std::string> candidates;
        const std::string exeDir = currentExecutableDirectory_();
        const std::string cwd = currentWorkingDirectory_();
        for (const char* rel : kRelativeDirs) {
            if (!exeDir.empty()) {
                candidates.push_back(joinPath_(exeDir, rel));
            }
            if (!cwd.empty() && cwd != exeDir) {
                candidates.push_back(joinPath_(cwd, rel));
            }
        }
        return candidates;
    }

    static std::string environmentValue_(const char* key) {
        if (!key || !*key) {
            return {};
        }
#if defined(_MSC_VER)
        char* value = nullptr;
        size_t valueLen = 0;
        if (_dupenv_s(&value, &valueLen, key) != 0 || !value) {
            return {};
        }
        std::string out(value);
        std::free(value);
        return out;
#else
        const char* value = std::getenv(key);
        return value ? std::string(value) : std::string();
#endif
    }

    static std::string& fallbackTrustedCaFile_() {
        static std::string path;
        return path;
    }

    static std::string& fallbackTrustedCaDirectory_() {
        static std::string path;
        return path;
    }

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
        using FnCTXLoadVerifyLocations = int (*)(void*, const char*, const char*);
        using FnNew = void* (*)(void*);
        using FnFree = void (*)(void*);
        using FnSetFd = int (*)(void*, int);
        using FnSet1Host = int (*)(void*, const char*);
        using FnCtrl = long (*)(void*, int, long, void*);
        using FnCTXCallbackCtrl = long (*)(void*, int, void (*)());
        using FnSetConnectState = void (*)(void*);
        using FnSetAcceptState = void (*)(void*);
        using FnDoHandshake = int (*)(void*);
        using FnGetError = int (*)(const void*, int);
        using FnRead = int (*)(void*, void*, int);
        using FnWrite = int (*)(void*, const void*, int);
        using FnShutdown = int (*)(void*);
        using FnTLSServerMethod = const void* (*)();
        using FnCTXUseCertFile = int (*)(void*, const char*, int);
        using FnCTXUseCertChainFile = int (*)(void*, const char*);
        using FnCTXUseKeyFile = int (*)(void*, const char*, int);
        using FnCTXCheckPrivateKey = int (*)(const void*);
        using FnSSLGetVerifyResult = long (*)(const void*);
        using FnErrGetError = unsigned long (*)();
        using FnErrClearError = void (*)();
        using FnErrErrorStringN = void (*)(unsigned long, char*, size_t);
        using FnX509VerifyCertErrorString = const char* (*)(long);
        using FnCTXGetCertStore = void* (*)(void*);
        using FnD2IX509 = void* (*)(void**, const unsigned char**, long);
        using FnX509StoreAddCert = int (*)(void*, void*);
        using FnX509Free = void (*)(void*);
        using FnSSLGetServername = const char* (*)(const void*, int);
        using FnSSLSetSSLCTX = void* (*)(void*, void*);

        FnOpenSSLInit OPENSSL_init_ssl = nullptr;
        FnTLSClientMethod TLS_client_method = nullptr;
        FnCTXNew SSL_CTX_new = nullptr;
        FnCTXFree SSL_CTX_free = nullptr;
        FnCTXSetVerify SSL_CTX_set_verify = nullptr;
        FnCTXSetDefaultVerifyPaths SSL_CTX_set_default_verify_paths = nullptr;
        FnCTXLoadVerifyLocations SSL_CTX_load_verify_locations = nullptr;
        FnNew SSL_new = nullptr;
        FnFree SSL_free = nullptr;
        FnSetFd SSL_set_fd = nullptr;
        FnSet1Host SSL_set1_host = nullptr;
        FnCtrl SSL_ctrl = nullptr;
        FnCTXCallbackCtrl SSL_CTX_callback_ctrl = nullptr;
        FnSetConnectState SSL_set_connect_state = nullptr;
        FnSetAcceptState SSL_set_accept_state = nullptr;
        FnDoHandshake SSL_do_handshake = nullptr;
        FnGetError SSL_get_error = nullptr;
        FnRead SSL_read = nullptr;
        FnWrite SSL_write = nullptr;
        FnShutdown SSL_shutdown = nullptr;
        FnTLSServerMethod TLS_server_method = nullptr;
        FnCTXUseCertFile SSL_CTX_use_certificate_file = nullptr;
        FnCTXUseCertChainFile SSL_CTX_use_certificate_chain_file = nullptr;
        FnCTXUseKeyFile SSL_CTX_use_PrivateKey_file = nullptr;
        FnCTXCheckPrivateKey SSL_CTX_check_private_key = nullptr;
        using FnCTXCtrl = long (*)(void*, int, long, void*);
        FnCTXCtrl SSL_CTX_ctrl = nullptr;
        FnSSLGetVerifyResult SSL_get_verify_result = nullptr;
        FnErrGetError ERR_get_error = nullptr;
        FnErrClearError ERR_clear_error = nullptr;
        FnErrErrorStringN ERR_error_string_n = nullptr;
        FnX509VerifyCertErrorString X509_verify_cert_error_string = nullptr;
        FnCTXGetCertStore SSL_CTX_get_cert_store = nullptr;
        FnD2IX509 d2i_X509 = nullptr;
        FnX509StoreAddCert X509_STORE_add_cert = nullptr;
        FnX509Free X509_free = nullptr;
        FnSSLGetServername SSL_get_servername = nullptr;
        FnSSLSetSSLCTX SSL_set_SSL_CTX = nullptr;

        /**
         * @brief Performs the `load` operation on the associated resource.
         * @param outError Output value filled by the method.
         * @return `true` on success; otherwise `false`.
         */
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
            SSL_CTX_load_verify_locations = (FnCTXLoadVerifyLocations)sym(ssl, "SSL_CTX_load_verify_locations");
            SSL_new = (FnNew)sym(ssl, "SSL_new");
            SSL_free = (FnFree)sym(ssl, "SSL_free");
            SSL_set_fd = (FnSetFd)sym(ssl, "SSL_set_fd");
            SSL_set1_host = (FnSet1Host)sym(ssl, "SSL_set1_host");
            SSL_ctrl = (FnCtrl)sym(ssl, "SSL_ctrl");
            SSL_CTX_callback_ctrl = (FnCTXCallbackCtrl)sym(ssl, "SSL_CTX_callback_ctrl");
            SSL_set_connect_state = (FnSetConnectState)sym(ssl, "SSL_set_connect_state");
            SSL_set_accept_state = (FnSetAcceptState)sym(ssl, "SSL_set_accept_state");
            SSL_do_handshake = (FnDoHandshake)sym(ssl, "SSL_do_handshake");
            SSL_get_error = (FnGetError)sym(ssl, "SSL_get_error");
            SSL_read = (FnRead)sym(ssl, "SSL_read");
            SSL_write = (FnWrite)sym(ssl, "SSL_write");
            SSL_shutdown = (FnShutdown)sym(ssl, "SSL_shutdown");
            TLS_server_method = (FnTLSServerMethod)sym(ssl, "TLS_server_method");
            SSL_CTX_use_certificate_file = (FnCTXUseCertFile)sym(ssl, "SSL_CTX_use_certificate_file");
            SSL_CTX_use_certificate_chain_file =
                (FnCTXUseCertChainFile)sym(ssl, "SSL_CTX_use_certificate_chain_file");
            SSL_CTX_use_PrivateKey_file = (FnCTXUseKeyFile)sym(ssl, "SSL_CTX_use_PrivateKey_file");
            SSL_CTX_check_private_key = (FnCTXCheckPrivateKey)sym(ssl, "SSL_CTX_check_private_key");
            SSL_CTX_ctrl = (FnCTXCtrl)sym(ssl, "SSL_CTX_ctrl");
            SSL_get_verify_result = (FnSSLGetVerifyResult)sym(ssl, "SSL_get_verify_result");
            ERR_get_error = (FnErrGetError)sym(crypto, "ERR_get_error");
            ERR_clear_error = (FnErrClearError)sym(crypto, "ERR_clear_error");
            ERR_error_string_n = (FnErrErrorStringN)sym(crypto, "ERR_error_string_n");
            X509_verify_cert_error_string =
                (FnX509VerifyCertErrorString)sym(crypto, "X509_verify_cert_error_string");
            SSL_CTX_get_cert_store = (FnCTXGetCertStore)sym(ssl, "SSL_CTX_get_cert_store");
            d2i_X509 = (FnD2IX509)sym(crypto, "d2i_X509");
            X509_STORE_add_cert = (FnX509StoreAddCert)sym(crypto, "X509_STORE_add_cert");
            X509_free = (FnX509Free)sym(crypto, "X509_free");
            SSL_get_servername = (FnSSLGetServername)sym(ssl, "SSL_get_servername");
            SSL_set_SSL_CTX = (FnSSLSetSSLCTX)sym(ssl, "SSL_set_SSL_CTX");

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
            std::string detail = describeOpenSslFailure_();
            m_lastError = "SSL error code " + std::to_string(err) + " sys=" + std::to_string(sys);
            if (!detail.empty()) {
                m_lastError += " " + detail;
            }
            return IoResult::Error;
        }
    }

    std::string describeOpenSslFailure_() const {
        if (!m_loader) {
            return {};
        }

        std::string detail;
        if (m_loader->SSL_get_verify_result && m_ssl) {
            const long verifyResult = m_loader->SSL_get_verify_result(m_ssl);
            if (verifyResult != X509_V_OK) {
                detail = "verify=" + std::to_string(verifyResult);
                if (m_loader->X509_verify_cert_error_string) {
                    const char* verifyText = m_loader->X509_verify_cert_error_string(verifyResult);
                    if (verifyText && *verifyText) {
                        detail += " (" + std::string(verifyText) + ")";
                    }
                }
            }
        }

        if (m_loader->ERR_get_error) {
            const unsigned long errCode = m_loader->ERR_get_error();
            if (errCode != 0) {
                char buffer[256] = {};
                if (m_loader->ERR_error_string_n) {
                    m_loader->ERR_error_string_n(errCode, buffer, sizeof(buffer));
                }
                if (!detail.empty()) {
                    detail += " ";
                }
                detail += "openssl=" + std::to_string(errCode);
                if (buffer[0] != '\0') {
                    detail += " (" + std::string(buffer) + ")";
                }
            }
        }

        return detail;
    }

    void cleanup() {
        if (m_ssl && m_loader && m_loader->SSL_free) {
            m_loader->SSL_free(m_ssl);
        }
        if (m_ctx && m_loader && m_loader->SSL_CTX_free) {
            m_loader->SSL_CTX_free(m_ctx);
        }
        if (m_serverContextBundle) {
            releaseServerContextBundle_(m_serverContextBundle);
        }
        m_ssl = nullptr;
        m_ctx = nullptr;
        m_serverContextBundle = nullptr;
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
    static constexpr long X509_V_OK = 0;
    static constexpr int kSslCtrlMode = 33;
    static constexpr long kSslModeEnablePartialWrite = 0x01;
    static constexpr long kSslModeAcceptMovingWriteBuffer = 0x02;
    static constexpr int kSslCtrlSetTlsextServernameCallback = 53;
    static constexpr int kSslCtrlSetTlsextServernameArg = 54;
    static constexpr int kTlsExtNameTypeHostName = 0;
    static constexpr int kSslTlsextErrOk = 0;

#if defined(_WIN32)
    bool loadWindowsSystemStores_(std::string& trustSummary) {
        bool loaded = false;
        loaded = loadWindowsSystemStore_(L"ROOT", CERT_SYSTEM_STORE_CURRENT_USER) || loaded;
        loaded = loadWindowsSystemStore_(L"CA", CERT_SYSTEM_STORE_CURRENT_USER) || loaded;
        loaded = loadWindowsSystemStore_(L"ROOT", CERT_SYSTEM_STORE_LOCAL_MACHINE) || loaded;
        loaded = loadWindowsSystemStore_(L"CA", CERT_SYSTEM_STORE_LOCAL_MACHINE) || loaded;
        if (loaded) {
            appendTrustSummary_(trustSummary, "windows-system-store");
        } else {
            appendTrustSummary_(trustSummary, "windows-system-store unavailable");
        }
        return loaded;
    }

    bool loadWindowsSystemStore_(const wchar_t* storeName, DWORD locationFlag) {
        if (!m_ctx || !m_loader || !m_loader->SSL_CTX_get_cert_store || !m_loader->d2i_X509 ||
            !m_loader->X509_STORE_add_cert || !m_loader->X509_free) {
            return false;
        }

        HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W,
                                         0,
                                         0,
                                         locationFlag | CERT_STORE_OPEN_EXISTING_FLAG | CERT_STORE_READONLY_FLAG,
                                         storeName);
        if (!store) {
            return false;
        }

        void* x509Store = m_loader->SSL_CTX_get_cert_store(m_ctx);
        if (!x509Store) {
            CertCloseStore(store, 0);
            return false;
        }

        bool loaded = false;
        PCCERT_CONTEXT certContext = nullptr;
        while ((certContext = CertEnumCertificatesInStore(store, certContext)) != nullptr) {
            const unsigned char* encoded =
                reinterpret_cast<const unsigned char*>(certContext->pbCertEncoded);
            void* x509 = m_loader->d2i_X509(nullptr, &encoded, static_cast<long>(certContext->cbCertEncoded));
            if (!x509) {
                if (m_loader->ERR_clear_error) {
                    m_loader->ERR_clear_error();
                }
                continue;
            }

            if (m_loader->X509_STORE_add_cert(x509Store, x509) == 1) {
                loaded = true;
            } else if (m_loader->ERR_clear_error) {
                m_loader->ERR_clear_error();
            }

            m_loader->X509_free(x509);
        }

        CertCloseStore(store, 0);
        return loaded;
    }
#endif

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
    ServerContextBundle* m_serverContextBundle = nullptr;
    std::string m_lastError;
};
