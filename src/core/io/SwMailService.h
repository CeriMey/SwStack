#pragma once

#include "SwCoreApplication.h"
#include "SwDebug.h"
#include "SwObject.h"
#include "SwSslSocket.h"
#include "SwTcpServer.h"
#include "SwTimer.h"
#include "mail/SwMailCommon.h"
#include "mail/SwMailDkim.h"
#include "mail/SwMailDnsClient.h"
#include "mail/SwMailStore.h"

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

#if !defined(_WIN32)
#include <fcntl.h>
#include <netdb.h>
#endif

class SwMailService;

class SwMailSocketServer_ : public SwTcpServer {
    SW_OBJECT(SwMailSocketServer_, SwTcpServer)

public:
    explicit SwMailSocketServer_(SwObject* parent = nullptr)
        : SwTcpServer(parent) {
    }

    void setImplicitTls(bool enabled) {
        m_implicitTls = enabled;
    }

    bool implicitTls() const {
        return m_implicitTls;
    }

    bool setTlsContext(void* ctx) {
        m_tlsContext = ctx;
        return true;
    }

    void* tlsContext() const {
        return m_tlsContext;
    }

    SwSslSocket* nextPendingConnection() override {
        return static_cast<SwSslSocket*>(SwTcpServer::nextPendingConnection());
    }

protected:
    SwTcpSocket* createPendingSocket_() override {
        return new SwSslSocket();
    }

    bool finalizeAcceptedSocket_(SwTcpSocket* socket) override {
        SwSslSocket* sslSocket = static_cast<SwSslSocket*>(socket);
        if (!m_implicitTls) {
            queuePendingConnection_(sslSocket);
            return true;
        }
        if (!m_tlsContext) {
            sslSocket->close();
            sslSocket->deleteLater();
            return false;
        }

        std::shared_ptr<bool> completed(new bool(false));
        SwObject::connect(sslSocket, &SwSslSocket::encrypted, [this, sslSocket, completed]() {
            if (*completed) {
                return;
            }
            *completed = true;
            queuePendingConnection_(sslSocket);
        });
        auto fail = [completed, sslSocket]() {
            if (*completed) {
                return;
            }
            *completed = true;
            sslSocket->close();
            sslSocket->deleteLater();
        };
        SwObject::connect(sslSocket, &SwSslSocket::errorOccurred, [fail](int) { fail(); });
        SwObject::connect(sslSocket, &SwSslSocket::disconnected, fail);
        if (!sslSocket->startServerEncryption(m_tlsContext)) {
            fail();
            return false;
        }
        return true;
    }

private:
    bool m_implicitTls = false;
    void* m_tlsContext = nullptr;
};

class SwMailSmtpSession_;
class SwMailImapSession_;

class SwMailService : public SwObject {
    SW_OBJECT(SwMailService, SwObject)

public:
    explicit SwMailService(SwObject* parent = nullptr);
    ~SwMailService() override;

    void setConfig(const SwMailConfig& config);
    const SwMailConfig& config() const;

    void setDomainTlsConfig(const SwDomainTlsConfig& config);
    const SwDomainTlsConfig& domainTlsConfig() const;

    bool start();
    void stop();
    bool isStarted() const;

    bool reloadTlsCredentials(const SwString& certPath, const SwString& keyPath, SwString* outError = nullptr);
    void clearTlsCredentials();
    bool hasTlsCredentials() const;
    void* tlsServerContext() const;

    SwMailStore& store();
    const SwMailStore& store() const;
    SwMailMetrics metricsSnapshot() const;

    SwDbStatus createAccount(const SwString& address, const SwString& password, SwMailAccount* outAccount = nullptr);
    SwDbStatus setAccountPassword(const SwString& address, const SwString& password);
    SwDbStatus setAccountSuspended(const SwString& address, bool suspended);
    SwList<SwMailAccount> listAccounts();
    SwList<SwMailAlias> listAliases();
    SwDbStatus upsertAlias(const SwMailAlias& alias);
    SwList<SwMailMailbox> listMailboxes(const SwString& address);
    SwList<SwMailMessageEntry> listMessages(const SwString& address, const SwString& mailboxName);
    SwList<SwMailQueueItem> listQueueItems();
    SwList<SwMailDkimRecord> listDkimRecords();
    SwDbStatus getMessage(const SwString& address,
                          const SwString& mailboxName,
                          unsigned long long uid,
                          SwMailMessageEntry* outMessage);

    bool authenticate(const SwString& address,
                      const SwString& password,
                      SwMailAccount* outAccount = nullptr,
                      SwString* outError = nullptr);

    SwDbStatus deliverLocalMessage(const SwString& mailFrom,
                                   const SwList<SwString>& recipients,
                                   const SwByteArray& rawMessage,
                                   unsigned long long* deliveredCountOut = nullptr,
                                   SwString* outError = nullptr);
    SwDbStatus enqueueRemoteMessage(const SwString& mailFrom,
                                    const SwList<SwString>& recipients,
                                    const SwByteArray& rawMessage,
                                    SwString* outError = nullptr);
    SwDbStatus appendSentMessage(const SwString& accountAddress, const SwByteArray& rawMessage);
    SwDbStatus appendMailboxMessage(const SwString& accountAddress,
                                    const SwString& mailboxName,
                                    const SwByteArray& rawMessage,
                                    const SwList<SwString>& flags = SwList<SwString>(),
                                    const SwString& internalDate = SwString(),
                                    SwMailMessageEntry* createdOut = nullptr);
    SwDbStatus expungeMailbox(const SwString& accountAddress,
                              const SwString& mailboxName,
                              unsigned long long* removedCountOut = nullptr);
    SwDbStatus setMessageFlags(const SwString& accountAddress,
                               const SwString& mailboxName,
                               unsigned long long uid,
                               const SwList<SwString>& flags);
    SwDbStatus copyMessage(const SwString& accountAddress,
                           const SwString& sourceMailbox,
                           unsigned long long uid,
                           const SwString& targetMailbox);

    bool isAuthAllowed(const SwString& remoteAddress);
    void registerAuthResult(const SwString& remoteAddress, bool success);

private:
    friend class SwMailSmtpSession_;
    friend class SwMailImapSession_;

    struct AuthThrottle_ {
        long long windowStartMs = 0;
        int failures = 0;
    };

    mutable SwMutex m_mutex;
    SwMailConfig m_config;
    SwDomainTlsConfig m_tlsConfig;
    SwMailStore m_store;
    SwMailDnsClient m_dnsClient;
    SwMailMetrics m_metrics;

    SwMailSocketServer_* m_smtpServer = nullptr;
    SwMailSocketServer_* m_submissionServer = nullptr;
    SwMailSocketServer_* m_imapsServer = nullptr;

    void* m_tlsServerContext = nullptr;
    SwString m_tlsCertPath;
    SwString m_tlsKeyPath;

    std::thread m_queueThread;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    bool m_queueStop = false;
    bool m_started = false;

    SwMap<SwString, AuthThrottle_> m_authThrottle;

    bool rebuildTlsContext_(const SwString& certPath, const SwString& keyPath, SwString* outError);
    void freeTlsContext_();
    bool refreshTlsListeners_(SwString* outError);
    bool startServers_(SwString* outError);
    void stopServers_();
    void attachServerSignals_();
    void onNewSmtpConnection_();
    void onNewSubmissionConnection_();
    void onNewImapsConnection_();
    void spawnSmtpSession_(SwSslSocket* socket, bool submissionMode);
    void spawnImapSession_(SwSslSocket* socket);

    void queueLoop_();
    void wakeQueueWorker_();
    bool processQueueBatch_();
    bool deliverQueueItem_(SwMailQueueItem item, SwString& outError);
    bool deliverRemoteSmtp_(const SwString& relayHost,
                            uint16_t relayPort,
                            bool implicitTls,
                            bool startTls,
                            const SwString& relayUsername,
                            const SwString& relayPassword,
                            const SwString& trustedCaFile,
                            const SwMailQueueItem& item,
                            SwString& outError);
    void requeueOrBounce_(SwMailQueueItem& item, const SwString& errorMessage);
    void emitLocalDsn_(const SwMailQueueItem& item, const SwString& errorMessage);
    void incrementMetric_(unsigned long long SwMailMetrics::*field, unsigned long long delta = 1);
};

namespace swMailServiceDetail {

class BlockingSmtpSocket_ {
public:
    BlockingSmtpSocket_() = default;
    ~BlockingSmtpSocket_() {
        close();
    }

    bool connectToHost(const SwString& host, uint16_t port, int timeoutMs, SwString& outError) {
        outError.clear();
#if defined(_WIN32)
        static bool winsockReady = false;
        if (!winsockReady) {
            WSADATA wsaData {};
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                outError = "WSAStartup failed";
                return false;
            }
            winsockReady = true;
        }
#endif
        close();

        struct addrinfo hints {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        struct addrinfo* result = nullptr;
        const int rc = ::getaddrinfo(host.toStdString().c_str(),
                                     SwString::number(port).toStdString().c_str(),
                                     &hints,
                                     &result);
        if (rc != 0 || !result) {
            outError = "getaddrinfo failed for " + host;
            return false;
        }

        for (struct addrinfo* it = result; it != nullptr; it = it->ai_next) {
#if defined(_WIN32)
            m_socket = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
            if (m_socket == INVALID_SOCKET) {
                continue;
            }
            u_long mode = 1;
            ::ioctlsocket(m_socket, FIONBIO, &mode);
            const int connectRc = ::connect(m_socket, it->ai_addr, static_cast<int>(it->ai_addrlen));
            if (connectRc == SOCKET_ERROR) {
                const int err = WSAGetLastError();
                if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
                    close();
                    continue;
                }
            }
#else
            m_socket = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
            if (m_socket < 0) {
                continue;
            }
            const int flags = ::fcntl(m_socket, F_GETFL, 0);
            if (flags >= 0) {
                ::fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);
            }
            const int connectRc = ::connect(m_socket, it->ai_addr, it->ai_addrlen);
            if (connectRc != 0 && errno != EINPROGRESS) {
                close();
                continue;
            }
#endif
            if (waitWritable_(timeoutMs)) {
                int socketError = 0;
#if defined(_WIN32)
                int len = sizeof(socketError);
#else
                socklen_t len = sizeof(socketError);
#endif
                if (::getsockopt(m_socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &len) == 0 &&
                    socketError == 0) {
                    setBlocking_();
                    ::freeaddrinfo(result);
                    return true;
                }
            }
            close();
        }

        ::freeaddrinfo(result);
        outError = "Unable to connect to remote SMTP host";
        return false;
    }

    bool startTls(const SwString& peerHost, const SwString& trustedCaFile, int timeoutMs, SwString& outError) {
        outError.clear();
        if (!isOpen_()) {
            outError = "Socket not open";
            return false;
        }
        if (peerHost.trimmed().isEmpty()) {
            outError = "TLS peer hostname is required";
            return false;
        }

        setNonBlocking_();
        m_sslBackend.reset(new SwBackendSsl());
        if (!m_sslBackend->init(peerHost.toStdString(),
                                static_cast<intptr_t>(m_socket),
                                trustedCaFile.toStdString())) {
            outError = "SMTP TLS init failed";
            const std::string detail = m_sslBackend->lastError();
            if (!detail.empty()) {
                outError += ": " + SwString(detail.c_str());
            }
            m_sslBackend.reset();
            return false;
        }

        while (true) {
            const SwBackendSsl::IoResult result = m_sslBackend->handshake();
            if (result == SwBackendSsl::IoResult::Ok) {
                m_tlsActive = true;
                m_ioTimeoutMs = timeoutMs;
                return true;
            }
            if (result == SwBackendSsl::IoResult::WantRead) {
                if (!waitReadable_(timeoutMs)) {
                    outError = "SMTP TLS handshake timeout";
                    m_sslBackend.reset();
                    return false;
                }
                continue;
            }
            if (result == SwBackendSsl::IoResult::WantWrite) {
                if (!waitWritable_(timeoutMs)) {
                    outError = "SMTP TLS handshake timeout";
                    m_sslBackend.reset();
                    return false;
                }
                continue;
            }

            outError = "SMTP TLS handshake failed";
            if (m_sslBackend) {
                const std::string detail = m_sslBackend->lastError();
                if (!detail.empty()) {
                    outError += ": " + SwString(detail.c_str());
                }
            }
            m_sslBackend.reset();
            return false;
        }
    }

    bool sendAll(const SwString& text, SwString& outError) {
        return sendAllBytes(SwByteArray(text.toUtf8()), outError);
    }

    bool sendAllBytes(const SwByteArray& bytes, SwString& outError) {
        outError.clear();
        if (!isOpen_()) {
            outError = "Socket not open";
            return false;
        }
        const char* data = bytes.constData();
        std::size_t remaining = bytes.size();
        while (remaining > 0) {
            if (m_tlsActive) {
                const int chunkSize =
                    remaining > static_cast<std::size_t>(32768) ? 32768 : static_cast<int>(remaining);
                int written = 0;
                const SwBackendSsl::IoResult result = m_sslBackend->write(data, chunkSize, written);
                if (result == SwBackendSsl::IoResult::Ok && written > 0) {
                    data += written;
                    remaining -= static_cast<std::size_t>(written);
                    continue;
                }
                if (result == SwBackendSsl::IoResult::WantRead) {
                    if (!waitReadable_(m_ioTimeoutMs)) {
                        outError = "SMTP TLS send timeout";
                        return false;
                    }
                    continue;
                }
                if (result == SwBackendSsl::IoResult::WantWrite) {
                    if (!waitWritable_(m_ioTimeoutMs)) {
                        outError = "SMTP TLS send timeout";
                        return false;
                    }
                    continue;
                }
                outError = "SMTP TLS send failed";
                if (m_sslBackend) {
                    const std::string detail = m_sslBackend->lastError();
                    if (!detail.empty()) {
                        outError += ": " + SwString(detail.c_str());
                    }
                }
                return false;
            }
#if defined(_WIN32)
            const int sent = ::send(m_socket, data, static_cast<int>(remaining), 0);
            if (sent <= 0) {
                outError = "SMTP send failed";
                return false;
            }
#else
            const ssize_t sent = ::send(m_socket, data, remaining, 0);
            if (sent <= 0) {
                outError = "SMTP send failed";
                return false;
            }
#endif
            data += sent;
            remaining -= static_cast<std::size_t>(sent);
        }
        return true;
    }

    bool readResponse(int timeoutMs, SwString& outResponse, int* outCode, SwString& outError) {
        outResponse.clear();
        if (outCode) {
            *outCode = 0;
        }
        outError.clear();
        if (!isOpen_()) {
            outError = "Socket not open";
            return false;
        }

        while (true) {
            SwString line;
            if (!readLine_(timeoutMs, line, outError)) {
                return false;
            }
            outResponse += line;
            outResponse += "\n";
            if (line.size() < 3) {
                continue;
            }
            const int code = std::atoi(line.left(3).toStdString().c_str());
            if (outCode && *outCode == 0) {
                *outCode = code;
            }
            if (line.size() >= 4 && line[3] == '-') {
                continue;
            }
            return true;
        }
    }

    void close() {
        m_tlsActive = false;
        if (m_sslBackend) {
            m_sslBackend->shutdown();
            m_sslBackend.reset();
        }
        if (!isOpen_()) {
            return;
        }
#if defined(_WIN32)
        ::closesocket(m_socket);
        m_socket = INVALID_SOCKET;
#else
        ::close(m_socket);
        m_socket = -1;
#endif
    }

private:
#if defined(_WIN32)
    SOCKET m_socket = INVALID_SOCKET;
#else
    int m_socket = -1;
#endif
    std::unique_ptr<SwBackendSsl> m_sslBackend;
    bool m_tlsActive = false;
    int m_ioTimeoutMs = 10000;

    bool isOpen_() const {
#if defined(_WIN32)
        return m_socket != INVALID_SOCKET;
#else
        return m_socket >= 0;
#endif
    }

    void setBlocking_() {
#if defined(_WIN32)
        u_long mode = 0;
        ::ioctlsocket(m_socket, FIONBIO, &mode);
#else
        const int flags = ::fcntl(m_socket, F_GETFL, 0);
        if (flags >= 0) {
            ::fcntl(m_socket, F_SETFL, flags & ~O_NONBLOCK);
        }
#endif
    }

    void setNonBlocking_() {
#if defined(_WIN32)
        u_long mode = 1;
        ::ioctlsocket(m_socket, FIONBIO, &mode);
#else
        const int flags = ::fcntl(m_socket, F_GETFL, 0);
        if (flags >= 0) {
            ::fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);
        }
#endif
    }

    bool waitReadable_(int timeoutMs) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(m_socket, &readSet);
        timeval timeout {};
        timeout.tv_sec = timeoutMs / 1000;
        timeout.tv_usec = (timeoutMs % 1000) * 1000;
        const int rc = ::select(static_cast<int>(m_socket) + 1, &readSet, nullptr, nullptr, &timeout);
        return rc > 0;
    }

    bool waitWritable_(int timeoutMs) {
        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(m_socket, &writeSet);
        timeval timeout {};
        timeout.tv_sec = timeoutMs / 1000;
        timeout.tv_usec = (timeoutMs % 1000) * 1000;
        const int rc = ::select(static_cast<int>(m_socket) + 1, nullptr, &writeSet, nullptr, &timeout);
        return rc > 0;
    }

    bool readLine_(int timeoutMs, SwString& outLine, SwString& outError) {
        outLine.clear();
        outError.clear();
        std::string buffer;
        while (true) {
            char ch = '\0';
            if (m_tlsActive) {
                int received = 0;
                while (true) {
                    const SwBackendSsl::IoResult result = m_sslBackend->read(&ch, 1, received);
                    if (result == SwBackendSsl::IoResult::Ok && received > 0) {
                        break;
                    }
                    if (result == SwBackendSsl::IoResult::WantRead) {
                        if (!waitReadable_(timeoutMs)) {
                            outError = "SMTP TLS read timeout";
                            return false;
                        }
                        continue;
                    }
                    if (result == SwBackendSsl::IoResult::WantWrite) {
                        if (!waitWritable_(timeoutMs)) {
                            outError = "SMTP TLS read timeout";
                            return false;
                        }
                        continue;
                    }
                    outError = "SMTP TLS read failed";
                    if (m_sslBackend) {
                        const std::string detail = m_sslBackend->lastError();
                        if (!detail.empty()) {
                            outError += ": " + SwString(detail.c_str());
                        }
                    }
                    return false;
                }
            } else {
                if (!waitReadable_(timeoutMs)) {
                    outError = "SMTP read timeout";
                    return false;
                }
#if defined(_WIN32)
                const int received = ::recv(m_socket, &ch, 1, 0);
#else
                const ssize_t received = ::recv(m_socket, &ch, 1, 0);
#endif
                if (received <= 0) {
                    outError = "SMTP read failed";
                    return false;
                }
            }
            buffer.push_back(ch);
            if (buffer.size() >= 2 && buffer[buffer.size() - 2] == '\r' && buffer[buffer.size() - 1] == '\n') {
                buffer.resize(buffer.size() - 2);
                outLine = SwString(buffer);
                return true;
            }
        }
    }
};

inline SwString buildDsnBody_(const SwMailQueueItem& item, const SwString& errorMessage) {
    SwString body;
    body += "From: MAILER-DAEMON <mailer-daemon@" + item.dkimDomain + ">\r\n";
    body += "To: <" + item.envelope.mailFrom + ">\r\n";
    body += "Subject: Delivery Status Notification (Failure)\r\n";
    body += "Date: " + swMailDetail::smtpDateNow() + "\r\n";
    body += "\r\n";
    body += "Your message could not be delivered.\r\n\r\n";
    body += "Error: " + errorMessage + "\r\n";
    return body;
}

inline SwMap<SwString, SwList<SwString>> groupRecipientsByDomain_(const SwList<SwString>& recipients) {
    SwMap<SwString, SwList<SwString>> groups;
    for (std::size_t i = 0; i < recipients.size(); ++i) {
        SwString localPart;
        SwString domain;
        if (!swMailDetail::splitAddress(recipients[i], localPart, domain)) {
            continue;
        }
        groups[domain].append(swMailDetail::canonicalAddress(recipients[i]));
    }
    return groups;
}

inline uint16_t outboundRelayPort_(const SwMailConfig::OutboundRelay& relay) {
    if (relay.port != 0) {
        return relay.port;
    }
    if (relay.implicitTls) {
        return 465;
    }
    if (relay.startTls) {
        return 587;
    }
    return 25;
}

inline bool smtpReadResponse_(BlockingSmtpSocket_& socket,
                              int timeoutMs,
                              int expectedClass,
                              SwString& response,
                              int* outCode,
                              SwString& outError) {
    int code = 0;
    if (!socket.readResponse(timeoutMs, response, &code, outError)) {
        return false;
    }
    if (outCode) {
        *outCode = code;
    }
    if (expectedClass > 0 && code / 100 != expectedClass) {
        if (outError.isEmpty()) {
            outError = "Unexpected SMTP response: " + SwString::number(code);
        }
        return false;
    }
    return true;
}

inline bool smtpEhlo_(BlockingSmtpSocket_& socket,
                      const SwString& heloHost,
                      SwString& response,
                      SwString& outError) {
    return socket.sendAll("EHLO " + heloHost + "\r\n", outError) &&
           smtpReadResponse_(socket, 10000, 2, response, nullptr, outError);
}

inline bool smtpAuthenticatePlain_(BlockingSmtpSocket_& socket,
                                   const SwString& username,
                                   const SwString& password,
                                   SwString& response,
                                   SwString& outError) {
    std::string raw;
    raw.push_back('\0');
    raw += username.toUtf8();
    raw.push_back('\0');
    raw += password.toUtf8();
    const SwString encoded(SwCrypto::base64Encode(raw));
    return socket.sendAll("AUTH PLAIN " + encoded + "\r\n", outError) &&
           smtpReadResponse_(socket, 10000, 2, response, nullptr, outError);
}

inline bool smtpAuthenticateLogin_(BlockingSmtpSocket_& socket,
                                   const SwString& username,
                                   const SwString& password,
                                   SwString& response,
                                   SwString& outError) {
    int code = 0;
    if (!socket.sendAll("AUTH LOGIN\r\n", outError) ||
        !smtpReadResponse_(socket, 10000, 3, response, &code, outError)) {
        return false;
    }
    if (!socket.sendAll(username.toBase64() + "\r\n", outError) ||
        !smtpReadResponse_(socket, 10000, 3, response, &code, outError)) {
        return false;
    }
    if (!socket.sendAll(password.toBase64() + "\r\n", outError) ||
        !smtpReadResponse_(socket, 10000, 2, response, &code, outError)) {
        return false;
    }
    return true;
}

} // namespace swMailServiceDetail

inline SwMailService::SwMailService(SwObject* parent)
    : SwObject(parent)
    , m_dnsClient(2500) {
    m_store.setConfig(m_config);
}

inline SwMailService::~SwMailService() {
    stop();
}

inline void SwMailService::setConfig(const SwMailConfig& config) {
    SwMutexLocker locker(&m_mutex);
    m_config = config;
    m_config.domain = swMailDetail::normalizeDomain(m_config.domain);
    m_config.mailHost = swMailDetail::normalizeMailHost(m_config.mailHost, m_config.domain);
    m_store.setConfig(m_config);
}

inline const SwMailConfig& SwMailService::config() const {
    return m_config;
}

inline void SwMailService::setDomainTlsConfig(const SwDomainTlsConfig& config) {
    SwMutexLocker locker(&m_mutex);
    m_tlsConfig = config;
    m_tlsConfig.domain = swMailDetail::normalizeDomain(m_tlsConfig.domain);
    m_tlsConfig.mailHost = swMailDetail::normalizeMailHost(m_tlsConfig.mailHost, m_tlsConfig.domain);
}

inline const SwDomainTlsConfig& SwMailService::domainTlsConfig() const {
    return m_tlsConfig;
}

inline bool SwMailService::start() {
    SwString error;
    {
        SwMutexLocker locker(&m_mutex);
        if (m_started) {
            return true;
        }
        m_store.setConfig(m_config);
    }

    const SwDbStatus storeStatus = m_store.open();
    if (!storeStatus.ok()) {
        swCError(kSwLogCategory_SwMail) << "[SwMailService] store open failed: "
                                        << storeStatus.message().toStdString();
        return false;
    }

    if (!m_tlsCertPath.isEmpty() && !m_tlsKeyPath.isEmpty()) {
        if (!reloadTlsCredentials(m_tlsCertPath, m_tlsKeyPath, &error)) {
            swCError(kSwLogCategory_SwMail) << "[SwMailService] TLS reload failed: " << error.toStdString();
        }
    } else if (m_tlsConfig.mode == SwDomainTlsConfig::Manual &&
               !m_tlsConfig.certPath.isEmpty() &&
               !m_tlsConfig.keyPath.isEmpty()) {
        if (!reloadTlsCredentials(m_tlsConfig.certPath, m_tlsConfig.keyPath, &error)) {
            swCError(kSwLogCategory_SwMail) << "[SwMailService] manual TLS reload failed: " << error.toStdString();
        }
    }

    if (!startServers_(&error)) {
        swCError(kSwLogCategory_SwMail) << "[SwMailService] server startup failed: " << error.toStdString();
        stop();
        return false;
    }

    {
        std::lock_guard<std::mutex> queueLocker(m_queueMutex);
        m_queueStop = false;
    }
    m_queueThread = std::thread([this]() { queueLoop_(); });

    SwMutexLocker locker(&m_mutex);
    m_started = true;
    return true;
}

inline void SwMailService::stop() {
    {
        SwMutexLocker locker(&m_mutex);
        if (!m_started && !m_smtpServer && !m_submissionServer && !m_imapsServer && !m_queueThread.joinable()) {
            m_store.close();
            freeTlsContext_();
            return;
        }
        m_started = false;
    }

    {
        std::lock_guard<std::mutex> locker(m_queueMutex);
        m_queueStop = true;
    }
    m_queueCv.notify_all();
    if (m_queueThread.joinable()) {
        m_queueThread.join();
    }

    stopServers_();
    freeTlsContext_();
    m_store.close();
}

inline bool SwMailService::isStarted() const {
    return m_started;
}

inline bool SwMailService::reloadTlsCredentials(const SwString& certPath,
                                                const SwString& keyPath,
                                                SwString* outError) {
    if (!rebuildTlsContext_(certPath, keyPath, outError)) {
        return false;
    }
    if (m_started && !refreshTlsListeners_(outError)) {
        return false;
    }
    return true;
}

inline void SwMailService::clearTlsCredentials() {
    freeTlsContext_();
    m_tlsCertPath.clear();
    m_tlsKeyPath.clear();
}

inline bool SwMailService::hasTlsCredentials() const {
    return m_tlsServerContext != nullptr;
}

inline void* SwMailService::tlsServerContext() const {
    return m_tlsServerContext;
}

inline SwMailStore& SwMailService::store() {
    return m_store;
}

inline const SwMailStore& SwMailService::store() const {
    return m_store;
}

inline SwMailMetrics SwMailService::metricsSnapshot() const {
    SwMutexLocker locker(&m_mutex);
    return m_metrics;
}

inline SwDbStatus SwMailService::createAccount(const SwString& address,
                                               const SwString& password,
                                               SwMailAccount* outAccount) {
    return m_store.createAccount(address, password, outAccount);
}

inline SwDbStatus SwMailService::setAccountPassword(const SwString& address, const SwString& password) {
    return m_store.setAccountPassword(address, password);
}

inline SwDbStatus SwMailService::setAccountSuspended(const SwString& address, bool suspended) {
    return m_store.setAccountSuspended(address, suspended);
}

inline SwList<SwMailAccount> SwMailService::listAccounts() {
    return m_store.listAccounts();
}

inline SwList<SwMailAlias> SwMailService::listAliases() {
    return m_store.listAliases();
}

inline SwDbStatus SwMailService::upsertAlias(const SwMailAlias& alias) {
    return m_store.upsertAlias(alias);
}

inline SwList<SwMailMailbox> SwMailService::listMailboxes(const SwString& address) {
    return m_store.listMailboxes(address);
}

inline SwList<SwMailMessageEntry> SwMailService::listMessages(const SwString& address, const SwString& mailboxName) {
    return m_store.listMessages(address, mailboxName);
}

inline SwList<SwMailQueueItem> SwMailService::listQueueItems() {
    return m_store.listQueueItems();
}

inline SwList<SwMailDkimRecord> SwMailService::listDkimRecords() {
    return m_store.listDkimRecords();
}

inline SwDbStatus SwMailService::getMessage(const SwString& address,
                                            const SwString& mailboxName,
                                            unsigned long long uid,
                                            SwMailMessageEntry* outMessage) {
    return m_store.getMessage(address, mailboxName, uid, outMessage);
}

inline bool SwMailService::authenticate(const SwString& address,
                                        const SwString& password,
                                        SwMailAccount* outAccount,
                                        SwString* outError) {
    return m_store.authenticate(address, password, outAccount, outError);
}

inline SwDbStatus SwMailService::deliverLocalMessage(const SwString& mailFrom,
                                                     const SwList<SwString>& recipients,
                                                     const SwByteArray& rawMessage,
                                                     unsigned long long* deliveredCountOut,
                                                     SwString* outError) {
    if (outError) {
        outError->clear();
    }
    SwList<SwString> expandedTargets;
    const SwDbStatus resolveStatus = m_store.resolveLocalRecipients(recipients, &expandedTargets);
    if (!resolveStatus.ok()) {
        if (outError) {
            *outError = resolveStatus.message();
        }
        return resolveStatus;
    }

    const SwByteArray prepared = swMailDetail::ensureMessageEnvelopeHeaders(m_config, rawMessage, mailFrom, recipients);
    unsigned long long delivered = 0;
    for (std::size_t i = 0; i < expandedTargets.size(); ++i) {
        const SwDbStatus appendStatus =
            m_store.appendMessage(expandedTargets[i], "INBOX", prepared, SwList<SwString>(), SwString(), nullptr);
        if (!appendStatus.ok()) {
            if (outError) {
                *outError = appendStatus.message();
            }
            return appendStatus;
        }
        ++delivered;
    }
    incrementMetric_(&SwMailMetrics::localDeliveries, delivered);
    incrementMetric_(&SwMailMetrics::inboundAccepted, delivered);
    if (deliveredCountOut) {
        *deliveredCountOut = delivered;
    }
    return SwDbStatus::success();
}

inline SwDbStatus SwMailService::enqueueRemoteMessage(const SwString& mailFrom,
                                                      const SwList<SwString>& recipients,
                                                      const SwByteArray& rawMessage,
                                                      SwString* outError) {
    if (outError) {
        outError->clear();
    }
    const SwMap<SwString, SwList<SwString>> grouped = swMailServiceDetail::groupRecipientsByDomain_(recipients);
    for (SwMap<SwString, SwList<SwString>>::const_iterator it = grouped.begin(); it != grouped.end(); ++it) {
        if (it.key() == m_config.domain) {
            continue;
        }
        SwMailQueueItem item;
        item.id = swMailDetail::generateId("queue");
        item.envelope.mailFrom = swMailDetail::canonicalAddress(mailFrom);
        item.envelope.rcptTo = swMailDetail::normalizeRecipients(it.value());
        item.rawMessage = rawMessage;
        item.createdAtMs = swMailDetail::currentEpochMs();
        item.updatedAtMs = item.createdAtMs;
        item.nextAttemptAtMs = item.createdAtMs;
        item.expireAtMs = item.createdAtMs + static_cast<long long>(m_config.queueMaxAgeMs);
        item.dkimDomain = m_config.domain;
        item.dkimSelector = "swstack";
        const SwDbStatus status = m_store.storeQueueItem(item);
        if (!status.ok()) {
            if (outError) {
                *outError = status.message();
            }
            return status;
        }
        incrementMetric_(&SwMailMetrics::outboundQueued, 1);
    }
    wakeQueueWorker_();
    return SwDbStatus::success();
}

inline SwDbStatus SwMailService::appendSentMessage(const SwString& accountAddress, const SwByteArray& rawMessage) {
    SwList<SwString> flags;
    flags.append("\\Seen");
    return m_store.appendMessage(accountAddress, "Sent", rawMessage, flags, SwString(), nullptr);
}

inline SwDbStatus SwMailService::appendMailboxMessage(const SwString& accountAddress,
                                                      const SwString& mailboxName,
                                                      const SwByteArray& rawMessage,
                                                      const SwList<SwString>& flags,
                                                      const SwString& internalDate,
                                                      SwMailMessageEntry* createdOut) {
    return m_store.appendMessage(accountAddress, mailboxName, rawMessage, flags, internalDate, createdOut);
}

inline SwDbStatus SwMailService::expungeMailbox(const SwString& accountAddress,
                                                const SwString& mailboxName,
                                                unsigned long long* removedCountOut) {
    return m_store.expungeMailbox(accountAddress, mailboxName, removedCountOut);
}

inline SwDbStatus SwMailService::setMessageFlags(const SwString& accountAddress,
                                                 const SwString& mailboxName,
                                                 unsigned long long uid,
                                                 const SwList<SwString>& flags) {
    return m_store.setMessageFlags(accountAddress, mailboxName, uid, flags);
}

inline SwDbStatus SwMailService::copyMessage(const SwString& accountAddress,
                                             const SwString& sourceMailbox,
                                             unsigned long long uid,
                                             const SwString& targetMailbox) {
    return m_store.copyMessage(accountAddress, sourceMailbox, uid, targetMailbox);
}

inline bool SwMailService::isAuthAllowed(const SwString& remoteAddress) {
    SwMutexLocker locker(&m_mutex);
    AuthThrottle_ state = m_authThrottle.value(remoteAddress);
    const long long nowMs = swMailDetail::currentEpochMs();
    if (state.windowStartMs <= 0 || nowMs - state.windowStartMs > m_config.authThrottleWindowMs) {
        return true;
    }
    return state.failures < m_config.authThrottleMaxAttempts;
}

inline void SwMailService::registerAuthResult(const SwString& remoteAddress, bool success) {
    SwMutexLocker locker(&m_mutex);
    const long long nowMs = swMailDetail::currentEpochMs();
    AuthThrottle_ state = m_authThrottle.value(remoteAddress);
    if (state.windowStartMs <= 0 || nowMs - state.windowStartMs > m_config.authThrottleWindowMs) {
        state.windowStartMs = nowMs;
        state.failures = 0;
    }
    if (success) {
        state.failures = 0;
        state.windowStartMs = nowMs;
    } else {
        ++state.failures;
        ++m_metrics.authFailures;
    }
    m_authThrottle[remoteAddress] = state;
}

inline bool SwMailService::rebuildTlsContext_(const SwString& certPath,
                                              const SwString& keyPath,
                                              SwString* outError) {
    if (outError) {
        outError->clear();
    }
    std::string error;
    void* context = SwBackendSsl::createServerContext(certPath.toStdString(), keyPath.toStdString(), error);
    if (!context) {
        if (outError) {
            *outError = SwString(error);
        }
        return false;
    }

    freeTlsContext_();
    m_tlsServerContext = context;
    m_tlsCertPath = certPath;
    m_tlsKeyPath = keyPath;

    if (m_smtpServer) {
        m_smtpServer->setTlsContext(m_tlsServerContext);
    }
    if (m_submissionServer) {
        m_submissionServer->setTlsContext(m_tlsServerContext);
    }
    if (m_imapsServer) {
        m_imapsServer->setTlsContext(m_tlsServerContext);
    }
    return true;
}

inline void SwMailService::freeTlsContext_() {
    if (!m_tlsServerContext) {
        return;
    }
    SwBackendSsl::freeServerContext(m_tlsServerContext);
    m_tlsServerContext = nullptr;
}

inline bool SwMailService::refreshTlsListeners_(SwString* outError) {
    if (outError) {
        outError->clear();
    }
    if (!hasTlsCredentials()) {
        return true;
    }
    if (m_submissionServer && m_config.submissionPort != 0 && !m_submissionServer->isListening()) {
        if (!m_submissionServer->listen(m_config.submissionPort)) {
            if (outError) {
                *outError = "Unable to listen Submission port " + SwString::number(m_config.submissionPort);
            }
            return false;
        }
    }
    if (m_imapsServer && m_config.imapsPort != 0 && !m_imapsServer->isListening()) {
        if (!m_imapsServer->listen(m_config.imapsPort)) {
            if (outError) {
                *outError = "Unable to listen IMAPS port " + SwString::number(m_config.imapsPort);
            }
            return false;
        }
    }
    return true;
}

inline bool SwMailService::startServers_(SwString* outError) {
    if (!m_smtpServer) {
        m_smtpServer = new SwMailSocketServer_(this);
        m_smtpServer->setImplicitTls(false);
        SwObject::connect(m_smtpServer, &SwTcpServer::newConnection, this, &SwMailService::onNewSmtpConnection_);
    }
    if (!m_submissionServer) {
        m_submissionServer = new SwMailSocketServer_(this);
        m_submissionServer->setImplicitTls(false);
        SwObject::connect(m_submissionServer, &SwTcpServer::newConnection, this, &SwMailService::onNewSubmissionConnection_);
    }
    if (!m_imapsServer) {
        m_imapsServer = new SwMailSocketServer_(this);
        m_imapsServer->setImplicitTls(true);
        SwObject::connect(m_imapsServer, &SwTcpServer::newConnection, this, &SwMailService::onNewImapsConnection_);
    }

    m_smtpServer->setTlsContext(m_tlsServerContext);
    m_submissionServer->setTlsContext(m_tlsServerContext);
    m_imapsServer->setTlsContext(m_tlsServerContext);

    if (m_config.smtpPort != 0 && !m_smtpServer->listen(m_config.smtpPort)) {
        if (outError) {
            *outError = "Unable to listen SMTP port " + SwString::number(m_config.smtpPort);
        }
        return false;
    }
    if (m_config.submissionPort != 0 && hasTlsCredentials() && !m_submissionServer->listen(m_config.submissionPort)) {
        if (outError) {
            *outError = "Unable to listen Submission port " + SwString::number(m_config.submissionPort);
        }
        return false;
    }
    if (m_config.imapsPort != 0 && hasTlsCredentials() && !m_imapsServer->listen(m_config.imapsPort)) {
        if (outError) {
            *outError = "Unable to listen IMAPS port " + SwString::number(m_config.imapsPort);
        }
        return false;
    }
    return true;
}

inline void SwMailService::stopServers_() {
    if (m_smtpServer) {
        m_smtpServer->close();
    }
    if (m_submissionServer) {
        m_submissionServer->close();
    }
    if (m_imapsServer) {
        m_imapsServer->close();
    }
}

inline void SwMailService::attachServerSignals_() {
    // Signals are connected once at server creation time.
}

class SwMailSmtpSession_ : public SwObject {
    SW_OBJECT(SwMailSmtpSession_, SwObject)

public:
    SwMailSmtpSession_(SwMailService* service, SwSslSocket* socket, bool submissionMode)
        : SwObject(service)
        , m_service(service)
        , m_socket(socket)
        , m_submissionMode(submissionMode)
        , m_idleTimer(this) {
        if (m_socket) {
            m_socket->setParent(this);
        }
        m_idleTimer.setSingleShot(true);
        SwObject::connect(&m_idleTimer, &SwTimer::timeout, [this]() {
            sendLine_("421 Session timeout");
            close_();
        });
        if (m_socket) {
            SwObject::connect(m_socket, &SwIODevice::readyRead, this, &SwMailSmtpSession_::onReadyRead_);
            SwObject::connect(m_socket, &SwSslSocket::encrypted, this, &SwMailSmtpSession_::onEncrypted_);
            SwObject::connect(m_socket, &SwAbstractSocket::writeFinished, [this]() { onWriteFinished_(); });
            SwObject::connect(m_socket, &SwAbstractSocket::disconnected, [this]() { deleteLater(); });
            SwObject::connect(m_socket, &SwAbstractSocket::errorOccurred, [this](int) { abort_(); });
        }
        m_remoteAddress = m_socket ? m_socket->peerAddress() : SwString();
        resetTransaction_();
    }

    void start() {
        resetIdle_();
        sendLine_("220 " + m_service->config().mailHost + " ESMTP SwMail");
        if (m_submissionMode) {
            m_service->incrementMetric_(&SwMailMetrics::submissionSessions, 1);
        } else {
            m_service->incrementMetric_(&SwMailMetrics::smtpSessions, 1);
        }
    }

private:
    enum AuthState_ {
        NoAuthState,
        PlainContinuationState,
        LoginUserState,
        LoginPassState
    };

    SwMailService* m_service = nullptr;
    SwSslSocket* m_socket = nullptr;
    bool m_submissionMode = false;
    SwString m_remoteAddress;
    SwTimer m_idleTimer;
    std::string m_inputBuffer;
    std::string m_dataBuffer;
    bool m_dataMode = false;
    bool m_closeRequested = false;
    bool m_closeAfterFlush = false;
    AuthState_ m_authState = NoAuthState;
    SwString m_authenticatedAddress;
    SwString m_pendingAuthUser;
    SwString m_mailFrom;
    SwList<SwString> m_recipients;
    bool m_haveMailFrom = false;

    void onReadyRead_() {
        resetIdle_();
        if (!m_socket) {
            return;
        }
        m_inputBuffer += m_socket->read().toStdString();
        processInput_();
    }

    void onEncrypted_() {
        m_inputBuffer.clear();
        m_authState = NoAuthState;
        resetTransaction_();
        resetIdle_();
    }

    void processInput_() {
        while (true) {
            const std::size_t eol = m_inputBuffer.find("\r\n");
            if (eol == std::string::npos) {
                break;
            }
            const std::string line = m_inputBuffer.substr(0, eol);
            m_inputBuffer.erase(0, eol + 2);
            if (m_dataMode) {
                if (line == ".") {
                    finalizeData_();
                    m_dataMode = false;
                    m_dataBuffer.clear();
                    continue;
                }
                if (!line.empty() && line[0] == '.') {
                    m_dataBuffer.append(line.substr(1));
                } else {
                    m_dataBuffer.append(line);
                }
                m_dataBuffer.append("\r\n");
                if (static_cast<unsigned long long>(m_dataBuffer.size()) > m_service->config().maxMessageBytes) {
                    sendLine_("552 Message too large");
                    resetTransaction_();
                    m_dataMode = false;
                    m_dataBuffer.clear();
                }
                continue;
            }
            processCommand_(SwString(line));
        }
    }

    void processCommand_(const SwString& line) {
        if (m_authState == PlainContinuationState) {
            handleAuthPlain_(line.trimmed());
            return;
        }
        if (m_authState == LoginUserState) {
            handleAuthLoginUser_(line.trimmed());
            return;
        }
        if (m_authState == LoginPassState) {
            handleAuthLoginPass_(line.trimmed());
            return;
        }

        const int space = line.indexOf(" ");
        const SwString command = (space >= 0 ? line.left(space) : line).trimmed().toUpper();
        const SwString args = (space >= 0 ? line.mid(space + 1) : SwString()).trimmed();

        if (command == "EHLO") {
            handleEhlo_(args);
        } else if (command == "HELO") {
            sendLine_("250 " + m_service->config().mailHost);
        } else if (command == "NOOP") {
            sendLine_("250 OK");
        } else if (command == "RSET") {
            resetTransaction_();
            sendLine_("250 OK");
        } else if (command == "QUIT") {
            sendLine_("221 Bye");
            close_();
        } else if (command == "VRFY") {
            sendLine_("252 Cannot VRFY user, but will accept message");
        } else if (command == "STARTTLS") {
            handleStartTls_();
        } else if (command == "AUTH") {
            handleAuth_(args);
        } else if (command == "MAIL") {
            handleMailFrom_(args);
        } else if (command == "RCPT") {
            handleRcptTo_(args);
        } else if (command == "DATA") {
            handleData_();
        } else {
            sendLine_("502 Command not implemented");
        }
    }

    void handleEhlo_(const SwString&) {
        SwList<SwString> lines;
        lines.append("250-" + m_service->config().mailHost);
        lines.append("250-PIPELINING");
        lines.append("250-8BITMIME");
        lines.append("250-SIZE " + SwString::number(static_cast<long long>(m_service->config().maxMessageBytes)));
        if (m_service->hasTlsCredentials() && !m_socket->isEncrypted()) {
            lines.append("250-STARTTLS");
        }
        if (m_submissionMode && m_socket->isEncrypted()) {
            lines.append("250-AUTH PLAIN LOGIN");
        }
        lines.append("250 HELP");
        sendLines_(lines);
    }

    void handleStartTls_() {
        if (!m_service->hasTlsCredentials()) {
            sendLine_("454 TLS not available");
            return;
        }
        if (m_socket->isEncrypted()) {
            sendLine_("503 TLS already active");
            return;
        }
        sendLine_("220 Ready to start TLS");
        resetTransaction_();
        m_authState = NoAuthState;
        if (!m_socket->startServerEncryption(m_service->tlsServerContext())) {
            close_();
        }
    }

    void handleAuth_(const SwString& args) {
        if (!m_submissionMode) {
            sendLine_("503 AUTH not supported on this listener");
            return;
        }
        if (!m_service->isAuthAllowed(m_remoteAddress)) {
            sendLine_("454 Too many authentication failures");
            return;
        }
        if (!m_socket->isEncrypted()) {
            sendLine_("530 Must issue STARTTLS first");
            return;
        }
        const SwString upper = args.toUpper();
        if (upper.startsWith("PLAIN")) {
            SwString token = args.mid(5).trimmed();
            if (token.isEmpty()) {
                m_authState = PlainContinuationState;
                sendLine_("334 ");
            } else {
                handleAuthPlain_(token);
            }
            return;
        }
        if (upper.startsWith("LOGIN")) {
            SwString token = args.mid(5).trimmed();
            if (token.isEmpty()) {
                m_authState = LoginUserState;
                sendLine_("334 " + SwString(SwCrypto::base64Encode("Username:")));
            } else {
                handleAuthLoginUser_(token);
            }
            return;
        }
        sendLine_("504 Unsupported authentication mechanism");
    }

    void handleAuthPlain_(const SwString& token) {
        m_authState = NoAuthState;
        const SwByteArray decoded = SwByteArray::fromBase64(SwByteArray(token.toUtf8()));
        const std::string raw = decoded.toStdString();
        std::size_t firstNull = raw.find('\0');
        std::size_t secondNull = (firstNull == std::string::npos) ? std::string::npos : raw.find('\0', firstNull + 1);
        if (secondNull == std::string::npos) {
            m_service->registerAuthResult(m_remoteAddress, false);
            sendLine_("535 Authentication failed");
            return;
        }
        SwString username = SwString(raw.substr(firstNull + 1, secondNull - firstNull - 1));
        SwString password = SwString(raw.substr(secondNull + 1));
        authenticateUser_(username, password);
    }

    void handleAuthLoginUser_(const SwString& token) {
        const SwByteArray decoded = SwByteArray::fromBase64(SwByteArray(token.toUtf8()));
        m_pendingAuthUser = SwString(decoded.toStdString()).trimmed();
        m_authState = LoginPassState;
        sendLine_("334 " + SwString(SwCrypto::base64Encode("Password:")));
    }

    void handleAuthLoginPass_(const SwString& token) {
        m_authState = NoAuthState;
        const SwByteArray decoded = SwByteArray::fromBase64(SwByteArray(token.toUtf8()));
        authenticateUser_(m_pendingAuthUser, SwString(decoded.toStdString()));
        m_pendingAuthUser.clear();
    }

    void authenticateUser_(SwString username, const SwString& password) {
        username = username.trimmed();
        if (!username.contains("@")) {
            username += "@" + m_service->config().domain;
        }
        SwMailAccount account;
        SwString error;
        const bool ok = m_service->authenticate(username, password, &account, &error);
        m_service->registerAuthResult(m_remoteAddress, ok);
        if (!ok) {
            sendLine_("535 Authentication failed");
            return;
        }
        m_authenticatedAddress = account.address;
        sendLine_("235 Authentication successful");
    }

    void handleMailFrom_(const SwString& args) {
        if (m_submissionMode) {
            if (!m_socket->isEncrypted()) {
                sendLine_("530 Must issue STARTTLS first");
                return;
            }
            if (m_authenticatedAddress.isEmpty()) {
                sendLine_("530 Authentication required");
                return;
            }
        }
        SwString address;
        if (!extractPathArgument_(args, "FROM:", address, true)) {
            sendLine_("501 Syntax: MAIL FROM:<address>");
            return;
        }
        m_mailFrom = swMailDetail::canonicalAddress(address);
        m_haveMailFrom = true;
        m_recipients.clear();
        sendLine_("250 OK");
    }

    void handleRcptTo_(const SwString& args) {
        if (!m_haveMailFrom) {
            sendLine_("503 Need MAIL FROM first");
            return;
        }
        if (m_submissionMode && m_authenticatedAddress.isEmpty()) {
            sendLine_("530 Authentication required");
            return;
        }
        SwString recipient;
        if (!extractPathArgument_(args, "TO:", recipient, false)) {
            sendLine_("501 Syntax: RCPT TO:<address>");
            return;
        }
        SwString localPart;
        SwString domain;
        if (!swMailDetail::splitAddress(recipient, localPart, domain)) {
            sendLine_("501 Invalid recipient");
            return;
        }

        if (!m_submissionMode) {
            SwList<SwString> single;
            single.append(recipient);
            SwList<SwString> resolved;
            const SwDbStatus status = m_service->store().resolveLocalRecipients(single, &resolved);
            if (!status.ok()) {
                sendLine_("550 No such local recipient");
                return;
            }
        }

        bool exists = false;
        for (std::size_t i = 0; i < m_recipients.size(); ++i) {
            if (m_recipients[i] == swMailDetail::canonicalAddress(recipient)) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            m_recipients.append(swMailDetail::canonicalAddress(recipient));
        }
        sendLine_("250 Accepted");
    }

    void handleData_() {
        if (!m_haveMailFrom || m_recipients.isEmpty()) {
            sendLine_("503 Need MAIL FROM and RCPT TO first");
            return;
        }
        m_dataMode = true;
        m_dataBuffer.clear();
        sendLine_("354 End data with <CR><LF>.<CR><LF>");
    }

    void finalizeData_() {
        SwByteArray message(m_dataBuffer);
        message = swMailDetail::ensureMessageEnvelopeHeaders(m_service->config(), message, m_mailFrom, m_recipients);
        if (m_submissionMode && !m_authenticatedAddress.isEmpty() && m_service->config().enableDkimSigning) {
            SwString selector;
            SwString signError;
            SwByteArray signedMessage = message;
            if (SwMailDkimSigner::signMessage(m_service->config(), m_service->store(), signedMessage, selector, signError)) {
                message = signedMessage;
            }
        }

        SwList<SwString> localRecipients;
        SwList<SwString> remoteRecipients;
        for (std::size_t i = 0; i < m_recipients.size(); ++i) {
            SwString localPart;
            SwString domain;
            if (!swMailDetail::splitAddress(m_recipients[i], localPart, domain)) {
                continue;
            }
            if (domain == m_service->config().domain) {
                localRecipients.append(m_recipients[i]);
            } else {
                remoteRecipients.append(m_recipients[i]);
            }
        }

        SwString error;
        unsigned long long deliveredLocal = 0;
        if (!localRecipients.isEmpty()) {
            const SwDbStatus status =
                m_service->deliverLocalMessage(m_mailFrom, localRecipients, message, &deliveredLocal, &error);
            if (!status.ok()) {
                sendLine_("550 Local delivery failed");
                resetTransaction_();
                return;
            }
        }
        if (m_submissionMode && !m_authenticatedAddress.isEmpty()) {
            (void)m_service->appendSentMessage(m_authenticatedAddress, message);
        }
        if (!remoteRecipients.isEmpty()) {
            const SwDbStatus status = m_service->enqueueRemoteMessage(m_mailFrom, remoteRecipients, message, &error);
            if (!status.ok()) {
                sendLine_("451 Unable to queue outbound message");
                resetTransaction_();
                return;
            }
        }
        sendLine_("250 Message accepted for delivery");
        resetTransaction_();
    }

    static bool extractPathArgument_(const SwString& args,
                                     const SwString& keyword,
                                     SwString& outValue,
                                     bool allowEmpty) {
        const SwString trimmed = args.trimmed();
        if (!trimmed.toUpper().startsWith(keyword.toUpper())) {
            return false;
        }
        SwString value = trimmed.mid(keyword.size()).trimmed();
        if (value.startsWith("<")) {
            const int closing = value.indexOf(">");
            if (closing < 0) {
                return false;
            }
            outValue = value.mid(1, closing - 1);
            return allowEmpty || !outValue.trimmed().isEmpty();
        }
        const int space = value.indexOf(" ");
        if (space >= 0) {
            value = value.left(space);
        }
        outValue = value;
        return allowEmpty || !outValue.trimmed().isEmpty();
    }

    void sendLine_(const SwString& line) {
        if (m_socket) {
            m_socket->write(line + "\r\n");
        }
    }

    void sendLines_(const SwList<SwString>& lines) {
        for (std::size_t i = 0; i < lines.size(); ++i) {
            sendLine_(lines[i]);
        }
    }

    void resetTransaction_() {
        m_mailFrom = SwString();
        m_recipients.clear();
        m_dataMode = false;
        m_dataBuffer.clear();
        m_haveMailFrom = false;
    }

    void resetIdle_() {
        m_idleTimer.start(static_cast<int>(m_service->config().sessionIdleTimeoutMs));
    }

    void close_() {
        if (m_closeRequested) {
            return;
        }
        m_closeRequested = true;
        m_idleTimer.stop();
        if (!m_socket) {
            deleteLater();
            return;
        }
        if (!m_socket->hasPendingWrites()) {
            m_socket->close();
            return;
        }
        m_closeAfterFlush = true;
    }

    void abort_() {
        if (m_socket) {
            m_socket->close();
        } else {
            deleteLater();
        }
    }

    void onWriteFinished_() {
        if (!m_closeAfterFlush || !m_socket) {
            return;
        }
        m_closeAfterFlush = false;
        m_socket->close();
    }
};

class SwMailImapSession_ : public SwObject {
    SW_OBJECT(SwMailImapSession_, SwObject)

public:
    SwMailImapSession_(SwMailService* service, SwSslSocket* socket)
        : SwObject(service)
        , m_service(service)
        , m_socket(socket)
        , m_idleTimer(this) {
        if (m_socket) {
            m_socket->setParent(this);
        }
        m_idleTimer.setSingleShot(true);
        SwObject::connect(&m_idleTimer, &SwTimer::timeout, [this]() {
            sendRaw_("* BYE Session timeout\r\n");
            close_();
        });
        if (m_socket) {
            SwObject::connect(m_socket, &SwIODevice::readyRead, this, &SwMailImapSession_::onReadyRead_);
            SwObject::connect(m_socket, &SwAbstractSocket::writeFinished, [this]() { onWriteFinished_(); });
            SwObject::connect(m_socket, &SwAbstractSocket::disconnected, [this]() { deleteLater(); });
            SwObject::connect(m_socket, &SwAbstractSocket::errorOccurred, [this](int) { abort_(); });
        }
    }

    void start() {
        resetIdle_();
        m_service->incrementMetric_(&SwMailMetrics::imapSessions, 1);
        sendRaw_("* OK SwMail IMAP4rev1 service ready\r\n");
    }

private:
    enum AuthState_ {
        NoAuthState,
        PlainContinuationState
    };

    struct PendingAppend_ {
        bool active = false;
        SwString tag;
        SwString mailbox;
        SwList<SwString> flags;
        SwString internalDate;
        std::size_t literalSize = 0;
    };

    SwMailService* m_service = nullptr;
    SwSslSocket* m_socket = nullptr;
    SwTimer m_idleTimer;
    std::string m_inputBuffer;
    SwString m_authenticatedAddress;
    SwString m_selectedMailbox = "INBOX";
    bool m_selectedReadOnly = false;
    bool m_idleMode = false;
    bool m_closeRequested = false;
    bool m_closeAfterFlush = false;
    AuthState_ m_authState = NoAuthState;
    SwString m_pendingAuthTag;
    PendingAppend_ m_pendingAppend;

    void onReadyRead_() {
        resetIdle_();
        if (!m_socket) {
            return;
        }
        m_inputBuffer += m_socket->read().toStdString();
        processInput_();
    }

    void processInput_() {
        while (true) {
            if (m_pendingAppend.active) {
                if (!processAppendLiteral_()) {
                    break;
                }
                continue;
            }
            const std::size_t eol = m_inputBuffer.find("\r\n");
            if (eol == std::string::npos) {
                break;
            }
            const std::string line = m_inputBuffer.substr(0, eol);
            m_inputBuffer.erase(0, eol + 2);
            processLine_(SwString(line));
        }
    }

    void processLine_(const SwString& line) {
        if (m_idleMode && line.trimmed().toUpper() == "DONE") {
            m_idleMode = false;
            sendTagged_(m_pendingAuthTag, "OK IDLE completed");
            return;
        }

        if (m_authState == PlainContinuationState) {
            handleAuthPlain_(m_pendingAuthTag, line.trimmed());
            return;
        }

        SwList<SwString> parts = parseAtoms_(line);
        if (parts.size() < 2) {
            sendRaw_("* BAD Invalid IMAP command\r\n");
            return;
        }

        const SwString tag = parts[0];
        const SwString command = parts[1].toUpper();
        if (command == "CAPABILITY") {
            sendRaw_("* CAPABILITY IMAP4rev1 AUTH=PLAIN UIDPLUS IDLE LITERAL+\r\n");
            sendTagged_(tag, "OK CAPABILITY completed");
        } else if (command == "NOOP") {
            sendTagged_(tag, "OK NOOP completed");
        } else if (command == "LOGOUT") {
            sendRaw_("* BYE Logging out\r\n");
            sendTagged_(tag, "OK LOGOUT completed");
            close_();
        } else if (command == "LOGIN") {
            handleLogin_(tag, parts);
        } else if (command == "AUTHENTICATE") {
            handleAuthenticate_(tag, parts);
        } else if (command == "LIST") {
            handleList_(tag);
        } else if (command == "SELECT" || command == "EXAMINE") {
            handleSelect_(tag, parts, command == "EXAMINE");
        } else if (command == "UID") {
            handleUid_(tag, parts);
        } else if (command == "EXPUNGE") {
            handleExpunge_(tag);
        } else if (command == "CLOSE") {
            handleClose_(tag);
        } else if (command == "IDLE") {
            m_idleMode = true;
            m_pendingAuthTag = tag;
            sendRaw_("+ idling\r\n");
        } else if (command == "APPEND") {
            handleAppend_(tag, line);
        } else {
            sendTagged_(tag, "BAD Unsupported IMAP command");
        }
    }

    void handleLogin_(const SwString& tag, const SwList<SwString>& parts) {
        if (parts.size() < 4) {
            sendTagged_(tag, "BAD LOGIN requires username and password");
            return;
        }
        authenticate_(tag, unquote_(parts[2]), unquote_(parts[3]));
    }

    void handleAuthenticate_(const SwString& tag, const SwList<SwString>& parts) {
        if (parts.size() < 3 || parts[2].toUpper() != "PLAIN") {
            sendTagged_(tag, "NO Unsupported authentication mechanism");
            return;
        }
        if (parts.size() >= 4) {
            handleAuthPlain_(tag, parts[3]);
            return;
        }
        m_authState = PlainContinuationState;
        m_pendingAuthTag = tag;
        sendRaw_("+ \r\n");
    }

    void handleAuthPlain_(const SwString& tag, const SwString& token) {
        m_authState = NoAuthState;
        const SwByteArray decoded = SwByteArray::fromBase64(SwByteArray(token.toUtf8()));
        const std::string raw = decoded.toStdString();
        std::size_t firstNull = raw.find('\0');
        std::size_t secondNull = (firstNull == std::string::npos) ? std::string::npos : raw.find('\0', firstNull + 1);
        if (secondNull == std::string::npos) {
            sendTagged_(tag, "NO Authentication failed");
            return;
        }
        authenticate_(tag,
                      SwString(raw.substr(firstNull + 1, secondNull - firstNull - 1)),
                      SwString(raw.substr(secondNull + 1)));
    }

    void authenticate_(const SwString& tag, SwString username, const SwString& password) {
        if (!username.contains("@")) {
            username += "@" + m_service->config().domain;
        }
        SwMailAccount account;
        if (!m_service->authenticate(username, password, &account, nullptr)) {
            sendTagged_(tag, "NO Authentication failed");
            return;
        }
        m_authenticatedAddress = account.address;
        sendTagged_(tag, "OK LOGIN completed");
    }

    void handleList_(const SwString& tag) {
        if (!ensureAuthenticated_(tag)) {
            return;
        }
        const SwList<SwMailMailbox> mailboxes = m_service->listMailboxes(m_authenticatedAddress);
        for (std::size_t i = 0; i < mailboxes.size(); ++i) {
            sendRaw_("* LIST (\\HasNoChildren) \"/\" \"" + mailboxes[i].name + "\"\r\n");
        }
        sendTagged_(tag, "OK LIST completed");
    }

    void handleSelect_(const SwString& tag, const SwList<SwString>& parts, bool readOnly) {
        if (!ensureAuthenticated_(tag)) {
            return;
        }
        if (parts.size() < 3) {
            sendTagged_(tag, "BAD Missing mailbox name");
            return;
        }
        const SwString mailbox = unquote_(parts[2]);
        const SwList<SwMailMailbox> mailboxes = m_service->listMailboxes(m_authenticatedAddress);
        bool found = false;
        SwMailMailbox selected;
        for (std::size_t i = 0; i < mailboxes.size(); ++i) {
            if (mailboxes[i].name.toUpper() == swMailDetail::normalizeMailboxName(mailbox).toUpper()) {
                selected = mailboxes[i];
                found = true;
                break;
            }
        }
        if (!found) {
            sendTagged_(tag, "NO No such mailbox");
            return;
        }
        m_selectedMailbox = selected.name;
        m_selectedReadOnly = readOnly;
        sendRaw_("* FLAGS (\\Seen \\Answered \\Flagged \\Deleted \\Draft)\r\n");
        sendRaw_("* " + SwString::number(static_cast<long long>(selected.totalCount)) + " EXISTS\r\n");
        sendRaw_("* " + SwString::number(static_cast<long long>(selected.unseenCount)) + " RECENT\r\n");
        sendRaw_("* OK [UIDNEXT " + SwString::number(static_cast<long long>(selected.uidNext)) + "] Predicted next UID\r\n");
        sendTagged_(tag, readOnly ? "OK [READ-ONLY] EXAMINE completed" : "OK [READ-WRITE] SELECT completed");
    }

    void handleUid_(const SwString& tag, const SwList<SwString>& parts) {
        if (!ensureAuthenticated_(tag)) {
            return;
        }
        if (parts.size() < 3) {
            sendTagged_(tag, "BAD Missing UID subcommand");
            return;
        }
        const SwString subcommand = parts[2].toUpper();
        if (subcommand == "SEARCH") {
            handleUidSearch_(tag, parts);
        } else if (subcommand == "FETCH") {
            handleUidFetch_(tag, parts);
        } else if (subcommand == "STORE") {
            handleUidStore_(tag, parts);
        } else if (subcommand == "COPY") {
            handleUidCopy_(tag, parts);
        } else {
            sendTagged_(tag, "BAD Unsupported UID subcommand");
        }
    }

    void handleUidSearch_(const SwString& tag, const SwList<SwString>& parts) {
        const SwList<SwMailMessageEntry> messages = m_service->listMessages(m_authenticatedAddress, m_selectedMailbox);
        SwString result = "* SEARCH";
        const bool unseenOnly = (parts.size() >= 4 && parts[3].toUpper() == "UNSEEN");
        for (std::size_t i = 0; i < messages.size(); ++i) {
            if (unseenOnly) {
                bool seen = false;
                for (std::size_t j = 0; j < messages[i].flags.size(); ++j) {
                    if (messages[i].flags[j].toLower() == "\\seen") {
                        seen = true;
                        break;
                    }
                }
                if (seen) {
                    continue;
                }
            }
            result += " " + SwString::number(static_cast<long long>(messages[i].uid));
        }
        sendRaw_(result + "\r\n");
        sendTagged_(tag, "OK UID SEARCH completed");
    }

    void handleUidFetch_(const SwString& tag, const SwList<SwString>& parts) {
        if (parts.size() < 5) {
            sendTagged_(tag, "BAD UID FETCH requires sequence and data items");
            return;
        }
        const SwString sequenceSet = parts[3];
        const SwString items = joinTail_(parts, 4).toUpper();
        const SwList<SwMailMessageEntry> messages = m_service->listMessages(m_authenticatedAddress, m_selectedMailbox);
        for (std::size_t i = 0; i < messages.size(); ++i) {
            if (!matchesUidSet_(messages[i].uid, sequenceSet, messages)) {
                continue;
            }
            SwString flagsText = "(";
            for (std::size_t j = 0; j < messages[i].flags.size(); ++j) {
                if (j > 0) {
                    flagsText += " ";
                }
                flagsText += messages[i].flags[j];
            }
            flagsText += ")";

            SwString fetchLine = "* " + SwString::number(static_cast<long long>(i + 1)) + " FETCH (UID " +
                                 SwString::number(static_cast<long long>(messages[i].uid)) + " FLAGS " + flagsText +
                                 " RFC822.SIZE " + SwString::number(static_cast<long long>(messages[i].sizeBytes));
            const bool wantsBody = items.contains("BODY[]") || items.contains("BODY.PEEK[]") || items.contains("RFC822");
            if (wantsBody) {
                fetchLine += " BODY[] {" + SwString::number(static_cast<long long>(messages[i].rawMessage.size())) + "}\r\n";
                sendRaw_(fetchLine);
                sendRaw_(SwString(messages[i].rawMessage.toStdString()));
                sendRaw_("\r\n)\r\n");
            } else {
                fetchLine += ")\r\n";
                sendRaw_(fetchLine);
            }
        }
        sendTagged_(tag, "OK UID FETCH completed");
    }

    void handleUidStore_(const SwString& tag, const SwList<SwString>& parts) {
        if (m_selectedReadOnly) {
            sendTagged_(tag, "NO Mailbox is read-only");
            return;
        }
        if (parts.size() < 6) {
            sendTagged_(tag, "BAD UID STORE requires sequence, action and flags");
            return;
        }
        const SwString sequenceSet = parts[3];
        const SwString action = parts[4].toUpper();
        const SwList<SwString> targetFlags = parseFlagList_(joinTail_(parts, 5));
        const SwList<SwMailMessageEntry> messages = m_service->listMessages(m_authenticatedAddress, m_selectedMailbox);
        for (std::size_t i = 0; i < messages.size(); ++i) {
            if (!matchesUidSet_(messages[i].uid, sequenceSet, messages)) {
                continue;
            }
            SwList<SwString> nextFlags = messages[i].flags;
            if (action == "FLAGS") {
                nextFlags = targetFlags;
            } else if (action == "+FLAGS" || action == "+FLAGS.SILENT") {
                for (std::size_t j = 0; j < targetFlags.size(); ++j) {
                    bool present = false;
                    for (std::size_t k = 0; k < nextFlags.size(); ++k) {
                        if (nextFlags[k] == targetFlags[j]) {
                            present = true;
                            break;
                        }
                    }
                    if (!present) {
                        nextFlags.append(targetFlags[j]);
                    }
                }
            } else if (action == "-FLAGS" || action == "-FLAGS.SILENT") {
                SwList<SwString> filtered;
                for (std::size_t j = 0; j < nextFlags.size(); ++j) {
                    bool remove = false;
                    for (std::size_t k = 0; k < targetFlags.size(); ++k) {
                        if (nextFlags[j] == targetFlags[k]) {
                            remove = true;
                            break;
                        }
                    }
                    if (!remove) {
                        filtered.append(nextFlags[j]);
                    }
                }
                nextFlags = filtered;
            }
            (void)m_service->setMessageFlags(m_authenticatedAddress, m_selectedMailbox, messages[i].uid, nextFlags);
        }
        sendTagged_(tag, "OK UID STORE completed");
    }

    void handleUidCopy_(const SwString& tag, const SwList<SwString>& parts) {
        if (parts.size() < 5) {
            sendTagged_(tag, "BAD UID COPY requires sequence and destination mailbox");
            return;
        }
        const SwString sequenceSet = parts[3];
        const SwString destinationMailbox = unquote_(parts[4]);
        const SwList<SwMailMessageEntry> messages = m_service->listMessages(m_authenticatedAddress, m_selectedMailbox);
        for (std::size_t i = 0; i < messages.size(); ++i) {
            if (!matchesUidSet_(messages[i].uid, sequenceSet, messages)) {
                continue;
            }
            (void)m_service->copyMessage(m_authenticatedAddress, m_selectedMailbox, messages[i].uid, destinationMailbox);
        }
        sendTagged_(tag, "OK UID COPY completed");
    }

    void handleExpunge_(const SwString& tag) {
        if (m_selectedReadOnly) {
            sendTagged_(tag, "NO Mailbox is read-only");
            return;
        }
        unsigned long long removed = 0;
        (void)m_service->expungeMailbox(m_authenticatedAddress, m_selectedMailbox, &removed);
        sendTagged_(tag, "OK EXPUNGE completed");
    }

    void handleClose_(const SwString& tag) {
        if (!m_selectedReadOnly) {
            unsigned long long removed = 0;
            (void)m_service->expungeMailbox(m_authenticatedAddress, m_selectedMailbox, &removed);
        }
        sendTagged_(tag, "OK CLOSE completed");
    }

    void handleAppend_(const SwString& tag, const SwString& line) {
        if (!ensureAuthenticated_(tag)) {
            return;
        }

        PendingAppend_ append;
        bool nonSynchronizingLiteral = false;
        SwString error;
        if (!parseAppendCommand_(line, append, nonSynchronizingLiteral, error)) {
            sendTagged_(tag, "BAD " + error);
            return;
        }
        if (append.literalSize == 0) {
            sendTagged_(tag, "NO Message body is empty");
            return;
        }
        if (static_cast<unsigned long long>(append.literalSize) > m_service->config().maxMessageBytes) {
            sendTagged_(tag, "NO Message exceeds maximum size");
            return;
        }

        append.active = true;
        append.tag = tag;
        m_pendingAppend = append;

        if (!nonSynchronizingLiteral) {
            sendRaw_("+ Ready for literal data\r\n");
        }
    }

    bool processAppendLiteral_() {
        if (!m_pendingAppend.active) {
            return false;
        }
        if (m_inputBuffer.size() < m_pendingAppend.literalSize) {
            return false;
        }

        const PendingAppend_ append = m_pendingAppend;
        const SwByteArray literal(m_inputBuffer.data(), append.literalSize);
        m_inputBuffer.erase(0, append.literalSize);
        m_pendingAppend = PendingAppend_();

        SwMailMessageEntry created;
        const SwDbStatus status = m_service->appendMailboxMessage(m_authenticatedAddress,
                                                                  append.mailbox,
                                                                  literal,
                                                                  append.flags,
                                                                  append.internalDate,
                                                                  &created);
        if (!status.ok()) {
            sendTagged_(append.tag, "NO " + status.message());
            return true;
        }

        if (swMailDetail::normalizeMailboxName(append.mailbox).toUpper() == m_selectedMailbox.toUpper()) {
            const SwList<SwMailMailbox> mailboxes = m_service->listMailboxes(m_authenticatedAddress);
            for (std::size_t i = 0; i < mailboxes.size(); ++i) {
                if (mailboxes[i].name.toUpper() == m_selectedMailbox.toUpper()) {
                    sendRaw_("* " + SwString::number(static_cast<long long>(mailboxes[i].totalCount)) + " EXISTS\r\n");
                    break;
                }
            }
        }

        sendTagged_(append.tag, "OK APPEND completed");
        return true;
    }

    bool ensureAuthenticated_(const SwString& tag) {
        if (m_authenticatedAddress.isEmpty()) {
            sendTagged_(tag, "NO Authentication required");
            return false;
        }
        return true;
    }

    static SwList<SwString> parseAtoms_(const SwString& line) {
        SwList<SwString> parts;
        std::string input = line.toStdString();
        std::string current;
        bool inQuotes = false;
        for (std::size_t i = 0; i < input.size(); ++i) {
            const char c = input[i];
            if (c == '"') {
                inQuotes = !inQuotes;
                current.push_back(c);
                continue;
            }
            if (!inQuotes && std::isspace(static_cast<unsigned char>(c))) {
                if (!current.empty()) {
                    parts.append(SwString(current));
                    current.clear();
                }
                continue;
            }
            current.push_back(c);
        }
        if (!current.empty()) {
            parts.append(SwString(current));
        }
        return parts;
    }

    static SwString joinTail_(const SwList<SwString>& parts, std::size_t index) {
        SwString out;
        for (std::size_t i = index; i < parts.size(); ++i) {
            if (!out.isEmpty()) {
                out += " ";
            }
            out += parts[i];
        }
        return out;
    }

    static SwString unquote_(const SwString& value) {
        SwString out = value.trimmed();
        if (out.startsWith("\"") && out.endsWith("\"") && out.size() >= 2) {
            out = out.mid(1, out.size() - 2);
        }
        return out;
    }

    static SwList<SwString> parseFlagList_(const SwString& raw) {
        SwString text = raw.trimmed();
        if (text.startsWith("(") && text.endsWith(")") && text.size() >= 2) {
            text = text.mid(1, text.size() - 2);
        }
        SwList<SwString> flags;
        SwList<SwString> atoms = parseAtoms_(text);
        for (std::size_t i = 0; i < atoms.size(); ++i) {
            flags.append(atoms[i]);
        }
        return flags;
    }

    static std::size_t skipSpaces_(const std::string& input, std::size_t offset) {
        while (offset < input.size() && std::isspace(static_cast<unsigned char>(input[offset]))) {
            ++offset;
        }
        return offset;
    }

    static bool readQuotedToken_(const std::string& input, std::size_t& offset, SwString& outToken) {
        outToken.clear();
        if (offset >= input.size() || input[offset] != '"') {
            return false;
        }
        ++offset;
        std::string value;
        while (offset < input.size()) {
            const char c = input[offset++];
            if (c == '\\' && offset < input.size()) {
                value.push_back(input[offset++]);
                continue;
            }
            if (c == '"') {
                outToken = SwString(value);
                return true;
            }
            value.push_back(c);
        }
        return false;
    }

    static bool readAtomToken_(const std::string& input, std::size_t& offset, SwString& outToken) {
        outToken.clear();
        const std::size_t start = offset;
        while (offset < input.size() && !std::isspace(static_cast<unsigned char>(input[offset]))) {
            ++offset;
        }
        if (offset <= start) {
            return false;
        }
        outToken = SwString(input.substr(start, offset - start));
        return true;
    }

    static bool readMailboxToken_(const std::string& input, std::size_t& offset, SwString& outMailbox) {
        outMailbox.clear();
        if (offset < input.size() && input[offset] == '"') {
            return readQuotedToken_(input, offset, outMailbox);
        }
        return readAtomToken_(input, offset, outMailbox);
    }

    static bool readParenthesizedToken_(const std::string& input, std::size_t& offset, SwString& outToken) {
        outToken.clear();
        if (offset >= input.size() || input[offset] != '(') {
            return false;
        }
        const std::size_t start = offset;
        int depth = 0;
        bool inQuotes = false;
        while (offset < input.size()) {
            const char c = input[offset++];
            if (c == '"' && (offset < 2 || input[offset - 2] != '\\')) {
                inQuotes = !inQuotes;
            }
            if (inQuotes) {
                continue;
            }
            if (c == '(') {
                ++depth;
            } else if (c == ')') {
                --depth;
                if (depth == 0) {
                    outToken = SwString(input.substr(start, offset - start));
                    return true;
                }
            }
        }
        return false;
    }

    static bool parseLiteralMarker_(const std::string& input,
                                    std::size_t& offset,
                                    std::size_t& outLiteralSize,
                                    bool& outNonSynchronizingLiteral) {
        outLiteralSize = 0;
        outNonSynchronizingLiteral = false;
        if (offset >= input.size() || input[offset] != '{') {
            return false;
        }
        ++offset;

        std::string digits;
        while (offset < input.size() && std::isdigit(static_cast<unsigned char>(input[offset]))) {
            digits.push_back(input[offset++]);
        }
        if (digits.empty()) {
            return false;
        }

        if (offset < input.size() && input[offset] == '+') {
            outNonSynchronizingLiteral = true;
            ++offset;
        }
        if (offset >= input.size() || input[offset] != '}') {
            return false;
        }
        ++offset;

        outLiteralSize = static_cast<std::size_t>(std::strtoull(digits.c_str(), nullptr, 10));
        return true;
    }

    static bool parseAppendCommand_(const SwString& line,
                                    PendingAppend_& outAppend,
                                    bool& outNonSynchronizingLiteral,
                                    SwString& outError) {
        outAppend = PendingAppend_();
        outNonSynchronizingLiteral = false;
        outError.clear();

        const std::string input = line.toStdString();
        std::size_t offset = 0;
        SwString token;
        if (!readAtomToken_(input, offset, token)) {
            outError = "Invalid APPEND tag";
            return false;
        }
        offset = skipSpaces_(input, offset);
        if (!readAtomToken_(input, offset, token) || token.toUpper() != "APPEND") {
            outError = "Invalid APPEND command";
            return false;
        }
        offset = skipSpaces_(input, offset);
        if (!readMailboxToken_(input, offset, outAppend.mailbox)) {
            outError = "APPEND requires a mailbox name";
            return false;
        }
        outAppend.mailbox = swMailDetail::normalizeMailboxName(outAppend.mailbox);

        offset = skipSpaces_(input, offset);
        if (offset < input.size() && input[offset] == '(') {
            SwString flagsToken;
            if (!readParenthesizedToken_(input, offset, flagsToken)) {
                outError = "Invalid APPEND flag list";
                return false;
            }
            outAppend.flags = parseFlagList_(flagsToken);
            offset = skipSpaces_(input, offset);
        }

        if (offset < input.size() && input[offset] == '"') {
            SwString rawInternalDate;
            if (!readQuotedToken_(input, offset, rawInternalDate) ||
                !swMailDetail::parseImapInternalDate(rawInternalDate, &outAppend.internalDate)) {
                outError = "Invalid APPEND date-time";
                return false;
            }
            offset = skipSpaces_(input, offset);
        } else if (offset + 3 <= input.size() &&
                   SwString(input.substr(offset, 3)).toUpper() == "NIL" &&
                   (offset + 3 == input.size() || std::isspace(static_cast<unsigned char>(input[offset + 3])) ||
                    input[offset + 3] == '{')) {
            offset += 3;
            offset = skipSpaces_(input, offset);
        }

        if (!parseLiteralMarker_(input, offset, outAppend.literalSize, outNonSynchronizingLiteral)) {
            outError = "APPEND requires literal message data";
            return false;
        }
        offset = skipSpaces_(input, offset);
        if (offset != input.size()) {
            outError = "Unexpected APPEND arguments after literal marker";
            return false;
        }
        return true;
    }

    static bool matchesUidSet_(unsigned long long uid,
                               const SwString& sequenceSet,
                               const SwList<SwMailMessageEntry>& messages) {
        const SwString trimmed = sequenceSet.trimmed();
        if (trimmed == "*" && !messages.isEmpty()) {
            return uid == messages.last().uid;
        }
        const int colon = trimmed.indexOf(":");
        if (colon >= 0) {
            unsigned long long start = parseUidToken_(trimmed.left(colon), messages);
            unsigned long long end = parseUidToken_(trimmed.mid(colon + 1), messages);
            if (start > end) {
                std::swap(start, end);
            }
            return uid >= start && uid <= end;
        }
        return uid == parseUidToken_(trimmed, messages);
    }

    static unsigned long long parseUidToken_(const SwString& token, const SwList<SwMailMessageEntry>& messages) {
        if (token.trimmed() == "*" && !messages.isEmpty()) {
            return messages.last().uid;
        }
        return static_cast<unsigned long long>(std::strtoull(token.toStdString().c_str(), nullptr, 10));
    }

    void sendTagged_(const SwString& tag, const SwString& status) {
        sendRaw_(tag + " " + status + "\r\n");
    }

    void sendRaw_(const SwString& payload) {
        if (m_socket) {
            m_socket->write(payload);
        }
    }

    void resetIdle_() {
        m_idleTimer.start(static_cast<int>(m_service->config().sessionIdleTimeoutMs));
    }

    void close_() {
        if (m_closeRequested) {
            return;
        }
        m_closeRequested = true;
        m_idleTimer.stop();
        if (!m_socket) {
            deleteLater();
            return;
        }
        if (!m_socket->hasPendingWrites()) {
            m_socket->close();
            return;
        }
        m_closeAfterFlush = true;
    }

    void abort_() {
        if (m_socket) {
            m_socket->close();
        } else {
            deleteLater();
        }
    }

    void onWriteFinished_() {
        if (!m_closeAfterFlush || !m_socket) {
            return;
        }
        m_closeAfterFlush = false;
        m_socket->close();
    }
};

inline void SwMailService::onNewSmtpConnection_() {
    while (m_smtpServer) {
        SwSslSocket* socket = m_smtpServer->nextPendingConnection();
        if (!socket) {
            break;
        }
        spawnSmtpSession_(socket, false);
    }
}

inline void SwMailService::onNewSubmissionConnection_() {
    while (m_submissionServer) {
        SwSslSocket* socket = m_submissionServer->nextPendingConnection();
        if (!socket) {
            break;
        }
        spawnSmtpSession_(socket, true);
    }
}

inline void SwMailService::onNewImapsConnection_() {
    while (m_imapsServer) {
        SwSslSocket* socket = m_imapsServer->nextPendingConnection();
        if (!socket) {
            break;
        }
        spawnImapSession_(socket);
    }
}

inline void SwMailService::spawnSmtpSession_(SwSslSocket* socket, bool submissionMode) {
    if (!socket) {
        return;
    }
    SwMailSmtpSession_* session = new SwMailSmtpSession_(this, socket, submissionMode);
    session->start();
}

inline void SwMailService::spawnImapSession_(SwSslSocket* socket) {
    if (!socket) {
        return;
    }
    SwMailImapSession_* session = new SwMailImapSession_(this, socket);
    session->start();
}

inline void SwMailService::queueLoop_() {
    while (true) {
        {
            std::unique_lock<std::mutex> locker(m_queueMutex);
            m_queueCv.wait_for(locker, std::chrono::seconds(2), [this]() { return m_queueStop; });
            if (m_queueStop) {
                break;
            }
        }
        (void)processQueueBatch_();
    }
}

inline void SwMailService::wakeQueueWorker_() {
    m_queueCv.notify_all();
}

inline bool SwMailService::processQueueBatch_() {
    const SwList<SwMailQueueItem> items = m_store.listDueQueueItems(swMailDetail::currentEpochMs(), 16);
    for (std::size_t i = 0; i < items.size(); ++i) {
        SwString error;
        SwMailQueueItem item = items[i];
        swCDebug(kSwLogCategory_SwMail) << "[SwMailService] processing queue item id="
                                        << item.id.toStdString()
                                        << " attempt=" << item.attemptCount + 1
                                        << " rcptCount=" << item.envelope.rcptTo.size();
        if (deliverQueueItem_(item, error)) {
            (void)m_store.removeQueueItem(item.id);
            swCDebug(kSwLogCategory_SwMail) << "[SwMailService] queue item delivered id="
                                            << item.id.toStdString();
            incrementMetric_(&SwMailMetrics::outboundDelivered, 1);
            continue;
        }
        swCWarning(kSwLogCategory_SwMail) << "[SwMailService] queue item deferred id="
                                          << item.id.toStdString()
                                          << " error=" << error.toStdString();
        requeueOrBounce_(item, error);
    }
    return !items.isEmpty();
}

inline bool SwMailService::deliverQueueItem_(SwMailQueueItem item, SwString& outError) {
    outError.clear();
    if (item.envelope.rcptTo.isEmpty()) {
        outError = "Queue item has no recipients";
        return false;
    }

    if (!m_config.outboundRelay.host.trimmed().isEmpty()) {
        const uint16_t relayPort = swMailServiceDetail::outboundRelayPort_(m_config.outboundRelay);
        if (relayPort == 0) {
            outError = "Invalid outbound relay port";
            return false;
        }
        swCDebug(kSwLogCategory_SwMail) << "[SwMailService] using outbound relay host="
                                        << m_config.outboundRelay.host.toStdString()
                                        << " port=" << relayPort
                                        << " implicitTls=" << m_config.outboundRelay.implicitTls
                                        << " startTls=" << m_config.outboundRelay.startTls
                                        << " auth=" << (!m_config.outboundRelay.username.isEmpty());
        return deliverRemoteSmtp_(m_config.outboundRelay.host.trimmed(),
                                  relayPort,
                                  m_config.outboundRelay.implicitTls,
                                  m_config.outboundRelay.startTls,
                                  m_config.outboundRelay.username,
                                  m_config.outboundRelay.password,
                                  m_config.outboundRelay.trustedCaFile,
                                  item,
                                  outError);
    }

    SwString localPart;
    SwString recipientDomain;
    if (!swMailDetail::splitAddress(item.envelope.rcptTo.first(), localPart, recipientDomain)) {
        outError = "Invalid queued recipient";
        return false;
    }

    SwString dnsError;
    SwList<SwString> targets = m_dnsClient.defaultMailTargetsForDomain(recipientDomain, &dnsError);
    if (targets.isEmpty()) {
        outError = dnsError.isEmpty() ? SwString("No MX target found") : dnsError;
        return false;
    }
    swCDebug(kSwLogCategory_SwMail) << "[SwMailService] MX delivery domain="
                                    << recipientDomain.toStdString()
                                    << " targetCount=" << targets.size();
    for (std::size_t i = 0; i < targets.size(); ++i) {
        swCDebug(kSwLogCategory_SwMail) << "[SwMailService] trying MX target="
                                        << targets[i].toStdString();
        if (deliverRemoteSmtp_(targets[i], 25, false, false, SwString(), SwString(), SwString(), item, outError)) {
            return true;
        }
    }
    return false;
}

inline bool SwMailService::deliverRemoteSmtp_(const SwString& relayHost,
                                              uint16_t relayPort,
                                              bool implicitTls,
                                              bool startTls,
                                              const SwString& relayUsername,
                                              const SwString& relayPassword,
                                              const SwString& trustedCaFile,
                                              const SwMailQueueItem& item,
                                              SwString& outError) {
    swMailServiceDetail::BlockingSmtpSocket_ socket;
    swCDebug(kSwLogCategory_SwMail) << "[SwMailService] connect SMTP host="
                                    << relayHost.toStdString()
                                    << " port=" << relayPort;
    if (!socket.connectToHost(relayHost, relayPort, 10000, outError)) {
        swCWarning(kSwLogCategory_SwMail) << "[SwMailService] connect failed host="
                                          << relayHost.toStdString()
                                          << " port=" << relayPort
                                          << " error=" << outError.toStdString();
        return false;
    }

    if (implicitTls && !socket.startTls(relayHost, trustedCaFile, 10000, outError)) {
        swCWarning(kSwLogCategory_SwMail) << "[SwMailService] implicit TLS failed host="
                                          << relayHost.toStdString()
                                          << " error=" << outError.toStdString();
        return false;
    }

    SwString response;
    int code = 0;
    if (!swMailServiceDetail::smtpReadResponse_(socket, 10000, 2, response, &code, outError)) {
        if (outError.isEmpty()) {
            outError = "SMTP greeting rejected";
        }
        swCWarning(kSwLogCategory_SwMail) << "[SwMailService] greeting failed host="
                                          << relayHost.toStdString()
                                          << " code=" << code
                                          << " error=" << outError.toStdString();
        return false;
    }
    if (!swMailServiceDetail::smtpEhlo_(socket, m_config.mailHost, response, outError)) {
        swCWarning(kSwLogCategory_SwMail) << "[SwMailService] EHLO failed host="
                                          << relayHost.toStdString()
                                          << " error=" << outError.toStdString();
        return false;
    }

    if (!implicitTls && startTls) {
        if (!socket.sendAll("STARTTLS\r\n", outError) ||
            !swMailServiceDetail::smtpReadResponse_(socket, 10000, 2, response, &code, outError)) {
            if (outError.isEmpty()) {
                outError = "STARTTLS rejected";
            }
            swCWarning(kSwLogCategory_SwMail) << "[SwMailService] STARTTLS rejected host="
                                              << relayHost.toStdString()
                                              << " code=" << code
                                              << " error=" << outError.toStdString();
            return false;
        }
        if (!socket.startTls(relayHost, trustedCaFile, 10000, outError)) {
            swCWarning(kSwLogCategory_SwMail) << "[SwMailService] STARTTLS handshake failed host="
                                              << relayHost.toStdString()
                                              << " error=" << outError.toStdString();
            return false;
        }
        if (!swMailServiceDetail::smtpEhlo_(socket, m_config.mailHost, response, outError)) {
            swCWarning(kSwLogCategory_SwMail) << "[SwMailService] post-STARTTLS EHLO failed host="
                                              << relayHost.toStdString()
                                              << " error=" << outError.toStdString();
            return false;
        }
    }

    if (!relayUsername.isEmpty()) {
        if (!swMailServiceDetail::smtpAuthenticatePlain_(socket, relayUsername, relayPassword, response, outError) &&
            !swMailServiceDetail::smtpAuthenticateLogin_(socket, relayUsername, relayPassword, response, outError)) {
            if (outError.isEmpty()) {
                outError = "SMTP relay authentication failed";
            }
            swCWarning(kSwLogCategory_SwMail) << "[SwMailService] relay auth failed host="
                                              << relayHost.toStdString()
                                              << " user=" << relayUsername.toStdString()
                                              << " error=" << outError.toStdString();
            return false;
        }
        swCDebug(kSwLogCategory_SwMail) << "[SwMailService] relay auth success host="
                                        << relayHost.toStdString()
                                        << " user=" << relayUsername.toStdString();
    }

    if (!socket.sendAll("MAIL FROM:<" + item.envelope.mailFrom + ">\r\n", outError) ||
        !swMailServiceDetail::smtpReadResponse_(socket, 10000, 2, response, &code, outError)) {
        swCWarning(kSwLogCategory_SwMail) << "[SwMailService] MAIL FROM failed host="
                                          << relayHost.toStdString()
                                          << " from=" << item.envelope.mailFrom.toStdString()
                                          << " code=" << code
                                          << " error=" << outError.toStdString();
        return false;
    }
    for (std::size_t i = 0; i < item.envelope.rcptTo.size(); ++i) {
        if (!socket.sendAll("RCPT TO:<" + item.envelope.rcptTo[i] + ">\r\n", outError) ||
            !swMailServiceDetail::smtpReadResponse_(socket, 10000, 2, response, &code, outError)) {
            swCWarning(kSwLogCategory_SwMail) << "[SwMailService] RCPT TO failed host="
                                              << relayHost.toStdString()
                                              << " rcpt=" << item.envelope.rcptTo[i].toStdString()
                                              << " code=" << code
                                              << " error=" << outError.toStdString();
            return false;
        }
    }
    if (!socket.sendAll("DATA\r\n", outError) ||
        !socket.readResponse(10000, response, &code, outError) || code != 354) {
        swCWarning(kSwLogCategory_SwMail) << "[SwMailService] DATA failed host="
                                          << relayHost.toStdString()
                                          << " code=" << code
                                          << " error=" << outError.toStdString();
        return false;
    }
    if (!socket.sendAll(swMailDetail::dotStuffMessage(item.rawMessage), outError) ||
        !swMailServiceDetail::smtpReadResponse_(socket, 10000, 2, response, &code, outError)) {
        swCWarning(kSwLogCategory_SwMail) << "[SwMailService] DATA body failed host="
                                          << relayHost.toStdString()
                                          << " code=" << code
                                          << " error=" << outError.toStdString();
        return false;
    }
    swCDebug(kSwLogCategory_SwMail) << "[SwMailService] SMTP delivery success host="
                                    << relayHost.toStdString()
                                    << " rcptCount=" << item.envelope.rcptTo.size();
    (void)socket.sendAll("QUIT\r\n", outError);
    return true;
}

inline void SwMailService::requeueOrBounce_(SwMailQueueItem& item, const SwString& errorMessage) {
    item.attemptCount += 1;
    item.updatedAtMs = swMailDetail::currentEpochMs();
    item.lastError = errorMessage;
    if (item.updatedAtMs >= item.expireAtMs) {
        (void)m_store.removeQueueItem(item.id);
        emitLocalDsn_(item, errorMessage);
        incrementMetric_(&SwMailMetrics::outboundFailed, 1);
        return;
    }
    const long long delayMs = static_cast<long long>(m_config.queueRetryBaseMs) *
                              static_cast<long long>(std::max(1, item.attemptCount));
    item.nextAttemptAtMs = item.updatedAtMs + delayMs;
    (void)m_store.storeQueueItem(item);
    incrementMetric_(&SwMailMetrics::outboundDeferred, 1);
}

inline void SwMailService::emitLocalDsn_(const SwMailQueueItem& item, const SwString& errorMessage) {
    if (item.envelope.mailFrom.isEmpty()) {
        return;
    }
    SwMailAccount account;
    if (m_store.getAccount(item.envelope.mailFrom, &account).ok()) {
        SwList<SwString> recipient;
        recipient.append(account.address);
        const SwString dsnBody = swMailServiceDetail::buildDsnBody_(item, errorMessage);
        (void)deliverLocalMessage("mailer-daemon@" + m_config.domain,
                                  recipient,
                                  SwByteArray(dsnBody.toUtf8()),
                                  nullptr,
                                  nullptr);
    }
}

inline void SwMailService::incrementMetric_(unsigned long long SwMailMetrics::*field, unsigned long long delta) {
    SwMutexLocker locker(&m_mutex);
    m_metrics.*field += delta;
}
