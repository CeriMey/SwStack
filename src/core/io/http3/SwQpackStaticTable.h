#ifndef SWQPACKSTATICTABLE_H
#define SWQPACKSTATICTABLE_H

#include "SwByteArray.h"

#include <cstddef>
#include <string>

//--------------------------------------------------------------------------------------------------
// SwQpackStaticTable
//
// The QPACK static table defined in RFC 9204 Appendix A. It holds 99 predefined name/value pairs
// addressed by absolute index 0..98. Unlike HPACK, the QPACK static table is decoupled from the
// dynamic table, so these indices are stable and always available to both peers.
//
// nameValueAt() returns the pair at a given index. findExact() locates an entry matching both name
// and value (used to emit an Indexed Field Line). findName() locates the first entry with a given
// name (used to emit a Literal Field Line With Name Reference).
//--------------------------------------------------------------------------------------------------

class SwQpackStaticTable {
public:
    static std::size_t entryCount() {
        return 99;
    }

    static bool nameValueAt(std::size_t index, SwByteArray& name, SwByteArray& value) {
        if (index >= entryCount()) {
            return false;
        }
        const Entry& entry = table_()[index];
        name = SwByteArray(std::string(entry.name));
        value = SwByteArray(std::string(entry.value));
        return true;
    }

    static bool findExact(const SwByteArray& name, const SwByteArray& value, std::size_t& index) {
        for (std::size_t i = 0; i < entryCount(); ++i) {
            const Entry& entry = table_()[i];
            if (matches_(name, entry.name) && matches_(value, entry.value)) {
                index = i;
                return true;
            }
        }
        return false;
    }

    static bool findName(const SwByteArray& name, std::size_t& index) {
        for (std::size_t i = 0; i < entryCount(); ++i) {
            if (matches_(name, table_()[i].name)) {
                index = i;
                return true;
            }
        }
        return false;
    }

private:
    struct Entry {
        const char* name;
        const char* value;
    };

    static bool matches_(const SwByteArray& value, const char* literal) {
        return value == SwByteArray(std::string(literal));
    }

    static const Entry* table_() {
        static const Entry table[99] = {
            {":authority", ""},
            {":path", "/"},
            {"age", "0"},
            {"content-disposition", ""},
            {"content-length", "0"},
            {"cookie", ""},
            {"date", ""},
            {"etag", ""},
            {"if-modified-since", ""},
            {"if-none-match", ""},
            {"last-modified", ""},
            {"link", ""},
            {"location", ""},
            {"referer", ""},
            {"set-cookie", ""},
            {":method", "CONNECT"},
            {":method", "DELETE"},
            {":method", "GET"},
            {":method", "HEAD"},
            {":method", "OPTIONS"},
            {":method", "POST"},
            {":method", "PUT"},
            {":scheme", "http"},
            {":scheme", "https"},
            {":status", "103"},
            {":status", "200"},
            {":status", "304"},
            {":status", "404"},
            {":status", "503"},
            {"accept", "*/*"},
            {"accept", "application/dns-message"},
            {"accept-encoding", "gzip, deflate, br"},
            {"accept-ranges", "bytes"},
            {"access-control-allow-headers", "cache-control"},
            {"access-control-allow-headers", "content-type"},
            {"access-control-allow-origin", "*"},
            {"cache-control", "max-age=0"},
            {"cache-control", "max-age=2592000"},
            {"cache-control", "max-age=604800"},
            {"cache-control", "no-cache"},
            {"cache-control", "no-store"},
            {"cache-control", "public, max-age=31536000"},
            {"content-encoding", "br"},
            {"content-encoding", "gzip"},
            {"content-type", "application/dns-message"},
            {"content-type", "application/javascript"},
            {"content-type", "application/json"},
            {"content-type", "application/x-www-form-urlencoded"},
            {"content-type", "image/gif"},
            {"content-type", "image/jpeg"},
            {"content-type", "image/png"},
            {"content-type", "text/css"},
            {"content-type", "text/html; charset=utf-8"},
            {"content-type", "text/plain"},
            {"content-type", "text/plain;charset=utf-8"},
            {"range", "bytes=0-"},
            {"strict-transport-security", "max-age=31536000"},
            {"strict-transport-security", "max-age=31536000; includesubdomains"},
            {"strict-transport-security", "max-age=31536000; includesubdomains; preload"},
            {"vary", "accept-encoding"},
            {"vary", "origin"},
            {"x-content-type-options", "nosniff"},
            {"x-xss-protection", "1; mode=block"},
            {":status", "100"},
            {":status", "204"},
            {":status", "206"},
            {":status", "302"},
            {":status", "400"},
            {":status", "403"},
            {":status", "421"},
            {":status", "425"},
            {":status", "500"},
            {"accept-language", ""},
            {"access-control-allow-credentials", "FALSE"},
            {"access-control-allow-credentials", "TRUE"},
            {"access-control-allow-headers", "*"},
            {"access-control-allow-methods", "get"},
            {"access-control-allow-methods", "get, post, options"},
            {"access-control-allow-methods", "options"},
            {"access-control-expose-headers", "content-length"},
            {"access-control-request-headers", "content-type"},
            {"access-control-request-method", "get"},
            {"access-control-request-method", "post"},
            {"alt-svc", "clear"},
            {"authorization", ""},
            {"content-security-policy",
             "script-src 'none'; object-src 'none'; base-uri 'none'"},
            {"early-data", "1"},
            {"expect-ct", ""},
            {"forwarded", ""},
            {"if-range", ""},
            {"origin", ""},
            {"purpose", "prefetch"},
            {"server", ""},
            {"timing-allow-origin", "*"},
            {"upgrade-insecure-requests", "1"},
            {"user-agent", ""},
            {"x-forwarded-for", ""},
            {"x-frame-options", "deny"},
            {"x-frame-options", "sameorigin"}
        };
        return table;
    }
};

#endif
