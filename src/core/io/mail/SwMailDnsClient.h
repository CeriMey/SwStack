#pragma once

#include "SwDebug.h"
#include "SwFile.h"
#include "SwList.h"
#include "SwMailCommon.h"
#include "SwString.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#include <iphlpapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

class SwMailDnsClient {
public:
    explicit SwMailDnsClient(int timeoutMs = 2500)
        : m_timeoutMs(timeoutMs) {
    }

    SwList<SwMailMxRecord> resolveMx(const SwString& domain, SwString* outError = nullptr) const {
        SwList<SwMailMxRecord> out;
        queryMx_(swMailDetail::normalizeDomain(domain), out, outError);
        std::sort(out.begin(), out.end(), [](const SwMailMxRecord& a, const SwMailMxRecord& b) {
            if (a.preference == b.preference) {
                return a.exchange < b.exchange;
            }
            return a.preference < b.preference;
        });
        return out;
    }

    SwList<SwString> resolveA(const SwString& host, SwString* outError = nullptr) const {
        SwList<SwString> out;
        queryHostAddresses_(swMailDetail::normalizeDomain(host), 1, out, outError);
        return out;
    }

    SwList<SwString> resolveAaaa(const SwString& host, SwString* outError = nullptr) const {
        SwList<SwString> out;
        queryHostAddresses_(swMailDetail::normalizeDomain(host), 28, out, outError);
        return out;
    }

    SwList<SwMailDnsTxtRecord> resolveTxt(const SwString& host, SwString* outError = nullptr) const {
        SwList<SwMailDnsTxtRecord> out;
        queryTxt_(swMailDetail::normalizeDomain(host), out, outError);
        return out;
    }

    SwList<SwString> defaultMailTargetsForDomain(const SwString& domain, SwString* outError = nullptr) const {
        SwList<SwString> targets;
        const SwList<SwMailMxRecord> mxRecords = resolveMx(domain, outError);
        for (std::size_t i = 0; i < mxRecords.size(); ++i) {
            if (!mxRecords[i].exchange.isEmpty()) {
                targets.append(mxRecords[i].exchange);
            }
        }
        if (!targets.isEmpty()) {
            return targets;
        }
        const SwList<SwString> aRecords = resolveA(domain, outError);
        for (std::size_t i = 0; i < aRecords.size(); ++i) {
            targets.append(aRecords[i]);
        }
        const SwList<SwString> aaaaRecords = resolveAaaa(domain, outError);
        for (std::size_t i = 0; i < aaaaRecords.size(); ++i) {
            targets.append(aaaaRecords[i]);
        }
        return targets;
    }

private:
    int m_timeoutMs = 2500;

    struct DnsHeader_ {
        uint16_t id = 0;
        uint16_t flags = 0;
        uint16_t qdCount = 0;
        uint16_t anCount = 0;
        uint16_t nsCount = 0;
        uint16_t arCount = 0;
    };

    static uint16_t readU16_(const unsigned char* bytes, std::size_t offset) {
        return static_cast<uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
    }

    static uint32_t readU32_(const unsigned char* bytes, std::size_t offset) {
        return (static_cast<uint32_t>(bytes[offset]) << 24) |
               (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
               (static_cast<uint32_t>(bytes[offset + 2]) << 8) |
               static_cast<uint32_t>(bytes[offset + 3]);
    }

    static void writeU16_(std::vector<unsigned char>& buffer, uint16_t value) {
        buffer.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
        buffer.push_back(static_cast<unsigned char>(value & 0xff));
    }

    static std::vector<unsigned char> buildQueryPacket_(const SwString& host, uint16_t type, uint16_t id) {
        std::vector<unsigned char> packet;
        packet.reserve(512);
        writeU16_(packet, id);
        writeU16_(packet, 0x0100);
        writeU16_(packet, 1);
        writeU16_(packet, 0);
        writeU16_(packet, 0);
        writeU16_(packet, 0);

        const std::string ascii = swMailDetail::normalizeDomain(host).toStdString();
        std::size_t labelStart = 0;
        while (labelStart < ascii.size()) {
            std::size_t dotPos = ascii.find('.', labelStart);
            if (dotPos == std::string::npos) {
                dotPos = ascii.size();
            }
            const std::size_t labelLen = dotPos - labelStart;
            packet.push_back(static_cast<unsigned char>(labelLen));
            for (std::size_t i = labelStart; i < dotPos; ++i) {
                packet.push_back(static_cast<unsigned char>(ascii[i]));
            }
            labelStart = dotPos + 1;
        }
        packet.push_back(0);
        writeU16_(packet, type);
        writeU16_(packet, 1);
        return packet;
    }

    static bool parseName_(const std::vector<unsigned char>& packet,
                           std::size_t& offset,
                           SwString& outName) {
        outName.clear();
        if (offset >= packet.size()) {
            return false;
        }

        std::size_t cursor = offset;
        bool jumped = false;
        int guard = 0;
        std::string name;

        while (cursor < packet.size() && guard++ < 256) {
            const unsigned char length = packet[cursor];
            if (length == 0) {
                if (!jumped) {
                    offset = cursor + 1;
                }
                outName = SwString(name);
                return true;
            }

            if ((length & 0xC0) == 0xC0) {
                if (cursor + 1 >= packet.size()) {
                    return false;
                }
                const uint16_t pointer =
                    static_cast<uint16_t>(((length & 0x3F) << 8) | packet[cursor + 1]);
                if (!jumped) {
                    offset = cursor + 2;
                }
                cursor = pointer;
                jumped = true;
                continue;
            }

            ++cursor;
            if (cursor + length > packet.size()) {
                return false;
            }
            if (!name.empty()) {
                name.push_back('.');
            }
            for (unsigned char i = 0; i < length; ++i) {
                name.push_back(static_cast<char>(packet[cursor + i]));
            }
            cursor += length;
        }
        return false;
    }

    SwList<SwString> resolverAddresses_() const {
        SwList<SwString> resolvers;
#if defined(_WIN32)
        ULONG bufferBytes = 0;
        if (GetNetworkParams(nullptr, &bufferBytes) == ERROR_BUFFER_OVERFLOW && bufferBytes > 0) {
            std::vector<unsigned char> buffer(bufferBytes, 0);
            FIXED_INFO* info = reinterpret_cast<FIXED_INFO*>(buffer.data());
            if (GetNetworkParams(info, &bufferBytes) == NO_ERROR) {
                IP_ADDR_STRING* current = &info->DnsServerList;
                while (current) {
                    if (current->IpAddress.String[0] != '\0') {
                        resolvers.append(SwString(current->IpAddress.String));
                    }
                    current = current->Next;
                }
            }
        }
#else
        SwFile file("/etc/resolv.conf");
        if (file.open(SwIODevice::Read)) {
            const SwString content = file.read();
            file.close();
            const std::string text = content.toStdString();
            std::size_t pos = 0;
            while (pos < text.size()) {
                std::size_t end = text.find('\n', pos);
                if (end == std::string::npos) {
                    end = text.size();
                }
                const std::string line = text.substr(pos, end - pos);
                pos = end + 1;
                if (line.find("nameserver") != 0) {
                    continue;
                }
                std::string ip = line.substr(std::strlen("nameserver"));
                ip.erase(std::remove_if(ip.begin(), ip.end(), [](unsigned char c) { return std::isspace(c) != 0; }),
                         ip.end());
                if (!ip.empty()) {
                    resolvers.append(SwString(ip));
                }
            }
        }
#endif
        if (resolvers.isEmpty()) {
            resolvers.append("1.1.1.1");
            resolvers.append("8.8.8.8");
        }
        return resolvers;
    }

    bool sendUdpQuery_(const SwString& resolverIp,
                       const std::vector<unsigned char>& query,
                       std::vector<unsigned char>& response,
                       SwString* outError) const {
#if defined(_WIN32)
        static bool winsockReady = false;
        if (!winsockReady) {
            WSADATA wsaData {};
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                if (outError) {
                    *outError = "WSAStartup failed";
                }
                return false;
            }
            winsockReady = true;
        }
#endif

        response.clear();

#if defined(_WIN32)
        SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) {
            if (outError) {
                *outError = "DNS socket creation failed";
            }
            return false;
        }
#else
        int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            if (outError) {
                *outError = "DNS socket creation failed";
            }
            return false;
        }
#endif

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(53);
        if (::inet_pton(AF_INET, resolverIp.toStdString().c_str(), &addr.sin_addr) != 1) {
            if (outError) {
                *outError = "Invalid resolver address: " + resolverIp;
            }
            closeSocket_(sock);
            return false;
        }

        const int sendBytes = static_cast<int>(query.size());
        const int sent = ::sendto(sock,
                                  reinterpret_cast<const char*>(query.data()),
                                  sendBytes,
                                  0,
                                  reinterpret_cast<const sockaddr*>(&addr),
                                  sizeof(addr));
        if (sent != sendBytes) {
            if (outError) {
                *outError = "DNS send failed";
            }
            closeSocket_(sock);
            return false;
        }

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(sock, &readSet);
        timeval timeout {};
        timeout.tv_sec = m_timeoutMs / 1000;
        timeout.tv_usec = (m_timeoutMs % 1000) * 1000;
        const int ready = ::select(static_cast<int>(sock) + 1, &readSet, nullptr, nullptr, &timeout);
        if (ready <= 0) {
            if (outError) {
                *outError = "DNS query timeout";
            }
            closeSocket_(sock);
            return false;
        }

        unsigned char buffer[2048];
        sockaddr_in from {};
#if defined(_WIN32)
        int fromLen = sizeof(from);
        const int received = ::recvfrom(sock,
                                        reinterpret_cast<char*>(buffer),
                                        sizeof(buffer),
                                        0,
                                        reinterpret_cast<sockaddr*>(&from),
                                        &fromLen);
#else
        socklen_t fromLen = sizeof(from);
        const int received = static_cast<int>(::recvfrom(sock,
                                                         buffer,
                                                         sizeof(buffer),
                                                         0,
                                                         reinterpret_cast<sockaddr*>(&from),
                                                         &fromLen));
#endif
        closeSocket_(sock);
        if (received <= 0) {
            if (outError) {
                *outError = "DNS receive failed";
            }
            return false;
        }
        response.assign(buffer, buffer + received);
        return true;
    }

    static void closeSocket_(
#if defined(_WIN32)
        SOCKET
#else
        int
#endif
            sock) {
#if defined(_WIN32)
        ::closesocket(sock);
#else
        ::close(sock);
#endif
    }

    bool skipQuestion_(const std::vector<unsigned char>& packet, std::size_t& offset) const {
        SwString questionName;
        if (!parseName_(packet, offset, questionName)) {
            return false;
        }
        if (offset + 4 > packet.size()) {
            return false;
        }
        offset += 4;
        return true;
    }

    bool queryMx_(const SwString& domain,
                  SwList<SwMailMxRecord>& out,
                  SwString* outError) const {
        const std::vector<unsigned char> response = performQuery_(domain, 15, outError);
        if (response.empty()) {
            return false;
        }
        return parseMxAnswers_(response, out, outError);
    }

    bool queryTxt_(const SwString& host,
                   SwList<SwMailDnsTxtRecord>& out,
                   SwString* outError) const {
        const std::vector<unsigned char> response = performQuery_(host, 16, outError);
        if (response.empty()) {
            return false;
        }
        return parseTxtAnswers_(response, out, outError);
    }

    bool queryHostAddresses_(const SwString& host,
                             uint16_t qtype,
                             SwList<SwString>& out,
                             SwString* outError) const {
        const std::vector<unsigned char> response = performQuery_(host, qtype, outError);
        if (response.empty()) {
            return false;
        }
        return parseAddressAnswers_(response, qtype, out, outError);
    }

    std::vector<unsigned char> performQuery_(const SwString& host,
                                             uint16_t type,
                                             SwString* outError) const {
        if (outError) {
            outError->clear();
        }
        const uint16_t queryId = static_cast<uint16_t>(swMailDetail::currentEpochMs() & 0xFFFF);
        const std::vector<unsigned char> query = buildQueryPacket_(host, type, queryId);
        const SwList<SwString> resolvers = resolverAddresses_();
        for (std::size_t i = 0; i < resolvers.size(); ++i) {
            std::vector<unsigned char> response;
            SwString error;
            if (!sendUdpQuery_(resolvers[i], query, response, &error)) {
                if (outError) {
                    *outError = error;
                }
                continue;
            }
            if (response.size() < 12) {
                if (outError) {
                    *outError = "DNS response too short";
                }
                continue;
            }
            if (readU16_(response.data(), 0) != queryId) {
                continue;
            }
            const uint16_t flags = readU16_(response.data(), 2);
            const uint16_t rcode = flags & 0x000F;
            if (rcode != 0) {
                if (outError) {
                    *outError = "DNS server returned error " + SwString::number(rcode);
                }
                continue;
            }
            return response;
        }
        return std::vector<unsigned char>();
    }

    bool parseMxAnswers_(const std::vector<unsigned char>& packet,
                         SwList<SwMailMxRecord>& out,
                         SwString* outError) const {
        std::size_t offset = 12;
        const uint16_t qdCount = readU16_(packet.data(), 4);
        const uint16_t anCount = readU16_(packet.data(), 6);
        for (uint16_t i = 0; i < qdCount; ++i) {
            if (!skipQuestion_(packet, offset)) {
                if (outError) {
                    *outError = "Failed to parse DNS question";
                }
                return false;
            }
        }
        for (uint16_t i = 0; i < anCount; ++i) {
            SwString name;
            if (!parseName_(packet, offset, name) || offset + 10 > packet.size()) {
                return false;
            }
            const uint16_t type = readU16_(packet.data(), offset);
            const uint16_t rdLength = readU16_(packet.data(), offset + 8);
            offset += 10;
            if (offset + rdLength > packet.size()) {
                return false;
            }
            if (type == 15 && rdLength >= 3) {
                SwMailMxRecord record;
                record.preference = static_cast<int>(readU16_(packet.data(), offset));
                std::size_t exchangeOffset = offset + 2;
                SwString exchange;
                if (parseName_(packet, exchangeOffset, exchange)) {
                    record.exchange = exchange.toLower();
                    out.append(record);
                }
            }
            offset += rdLength;
        }
        return true;
    }

    bool parseTxtAnswers_(const std::vector<unsigned char>& packet,
                          SwList<SwMailDnsTxtRecord>& out,
                          SwString* outError) const {
        std::size_t offset = 12;
        const uint16_t qdCount = readU16_(packet.data(), 4);
        const uint16_t anCount = readU16_(packet.data(), 6);
        for (uint16_t i = 0; i < qdCount; ++i) {
            if (!skipQuestion_(packet, offset)) {
                if (outError) {
                    *outError = "Failed to parse DNS question";
                }
                return false;
            }
        }
        for (uint16_t i = 0; i < anCount; ++i) {
            SwString name;
            if (!parseName_(packet, offset, name) || offset + 10 > packet.size()) {
                return false;
            }
            const uint16_t type = readU16_(packet.data(), offset);
            const uint16_t rdLength = readU16_(packet.data(), offset + 8);
            offset += 10;
            if (offset + rdLength > packet.size()) {
                return false;
            }
            if (type == 16) {
                std::size_t cursor = offset;
                std::string combined;
                while (cursor < offset + rdLength) {
                    const unsigned char txtLen = packet[cursor++];
                    if (cursor + txtLen > offset + rdLength) {
                        break;
                    }
                    combined.append(reinterpret_cast<const char*>(&packet[cursor]), txtLen);
                    cursor += txtLen;
                }
                SwMailDnsTxtRecord record;
                record.value = SwString(combined);
                out.append(record);
            }
            offset += rdLength;
        }
        return true;
    }

    bool parseAddressAnswers_(const std::vector<unsigned char>& packet,
                              uint16_t qtype,
                              SwList<SwString>& out,
                              SwString* outError) const {
        std::size_t offset = 12;
        const uint16_t qdCount = readU16_(packet.data(), 4);
        const uint16_t anCount = readU16_(packet.data(), 6);
        for (uint16_t i = 0; i < qdCount; ++i) {
            if (!skipQuestion_(packet, offset)) {
                if (outError) {
                    *outError = "Failed to parse DNS question";
                }
                return false;
            }
        }
        for (uint16_t i = 0; i < anCount; ++i) {
            SwString name;
            if (!parseName_(packet, offset, name) || offset + 10 > packet.size()) {
                return false;
            }
            const uint16_t type = readU16_(packet.data(), offset);
            const uint16_t rdLength = readU16_(packet.data(), offset + 8);
            offset += 10;
            if (offset + rdLength > packet.size()) {
                return false;
            }
            char buffer[INET6_ADDRSTRLEN] = {0};
            if (type == qtype) {
                if (qtype == 1 && rdLength == 4) {
                    ::inet_ntop(AF_INET, packet.data() + offset, buffer, sizeof(buffer));
                    out.append(SwString(buffer));
                } else if (qtype == 28 && rdLength == 16) {
                    ::inet_ntop(AF_INET6, packet.data() + offset, buffer, sizeof(buffer));
                    out.append(SwString(buffer));
                }
            }
            offset += rdLength;
        }
        return true;
    }
};
