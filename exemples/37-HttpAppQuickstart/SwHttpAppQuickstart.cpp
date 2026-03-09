#include "SwCoreApplication.h"
#include "SwHttpApp.h"
#include "http/SwHttpMiddlewarePack.h"
#include "SwTcpSocket.h"
#include "SwDir.h"
#include "SwFile.h"
#include "SwDateTime.h"
#include "SwJsonObject.h"
#include "SwJsonArray.h"
#include "SwDebug.h"
#include "SwEventLoop.h"
#include "SwMutex.h"
#include "platform/SwPlatformSelector.h"

#if defined(__has_include)
#if __has_include("src/core/runtime/SwThreadPool.h")
#include "src/core/runtime/SwThreadPool.h"
#else
#include "SwThreadPool.h"
#endif

#include <chrono>
#include <algorithm>
#else
#include "SwThreadPool.h"
#endif

static bool parseIntArg_(const SwString& value, int& outValue) {
    bool ok = false;
    const int parsed = value.toInt(&ok);
    if (!ok) {
        return false;
    }
    outValue = parsed;
    return true;
}

static SwString joinPath_(const SwString& root, const SwString& leaf) {
    SwString out = root;
    if (!out.endsWith("/")) {
        out += "/";
    }
    if (leaf.startsWith("/")) {
        out += leaf.mid(1);
    } else {
        out += leaf;
    }
    return out;
}

static SwString sanitizeFileName_(const SwString& rawName) {
    SwString name = rawName.trimmed();
    if (name.isEmpty()) {
        return "upload.bin";
    }

    SwString out;
    out.reserve(name.size());
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        const bool isLower = (c >= 'a' && c <= 'z');
        const bool isUpper = (c >= 'A' && c <= 'Z');
        const bool isDigit = (c >= '0' && c <= '9');
        const bool isSafePunct = (c == '.' || c == '_' || c == '-');
        if (isLower || isUpper || isDigit || isSafePunct) {
            out += SwString(c);
        } else {
            out += "_";
        }
    }

    while (out.startsWith(".")) {
        out = out.mid(1);
    }
    if (out.isEmpty()) {
        out = "upload.bin";
    }
    return out;
}

static bool ensureDirectory_(const SwString& path, SwString& absoluteOut) {
    absoluteOut = swDirPlatform().absolutePath(path);
    return SwDir::mkpathAbsolute(absoluteOut, true);
}

static bool ensureFileWithDefaultContent_(const SwString& filePath, const SwString& content) {
    if (swFilePlatform().isFile(filePath)) {
        return true;
    }

    SwFile file(filePath);
    if (!file.openBinary(SwFile::Write)) {
        return false;
    }
    const bool writeOk = file.write(SwByteArray(content.toStdString()));
    file.close();
    return writeOk;
}

static bool writeTextFile_(const SwString& filePath, const SwString& content) {
    SwFile file(filePath);
    if (!file.openBinary(SwFile::Write)) {
        return false;
    }
    const bool writeOk = file.write(SwByteArray(content.toStdString()));
    file.close();
    return writeOk;
}

static bool readTextFile_(const SwString& filePath, SwString& contentOut) {
    contentOut.clear();
    if (!swFilePlatform().isFile(filePath)) {
        return false;
    }
    SwFile file(filePath);
    if (!file.openBinary(SwFile::Read)) {
        return false;
    }
    contentOut = file.readAll();
    file.close();
    return !contentOut.isEmpty();
}

struct ProxyHeaderConfig {
    SwString key;
    SwString value;
};

struct ProxyUpstreamConfig {
    SwString id;
    SwString protocol = "http";
    SwString host = "127.0.0.1";
    int port = 8081;
    int connectTimeoutMs = 3000;
    int readTimeoutMs = 30000;
    bool active = true;
};

struct ProxySubdomainConfig {
    SwString host;
    SwString upstreamId;
    SwString pathPrefix = "/";
    bool tls = false;
    bool websocket = true;
    bool forceHttps = false;
    bool stripPrefix = false;
    int rateLimitRpm = 0;
    SwString accessPolicy = "public";
    SwList<ProxyHeaderConfig> headers;
};

struct ProxyGlobalConfig {
    SwString workerProcesses = "auto";
    int httpPort = 80;
    int httpsPort = 443;
    bool enableHttp2 = true;
    bool enableGzip = true;
    int clientMaxBodyMb = 100;
    bool proxyBuffering = true;
};

struct ProxyControlConfig {
    ProxyGlobalConfig global;
    SwList<ProxyUpstreamConfig> upstreams;
    SwList<ProxySubdomainConfig> subdomains;
};

struct ProxyControlState {
    SwMutex mutex;
    ProxyControlConfig config;
    SwString configFilePath;
    SwString generatedRuntimePath;
    SwString updatedAt;
    SwMap<SwString, int> routeWindowCounts;
    SwMap<SwString, long long> routeWindowStartMs;
};

static SwString toLowerTrimmed_(const SwString& text) {
    return text.trimmed().toLower();
}

static SwString normalizePathPrefix_(const SwString& rawPath) {
    SwString out = swHttpNormalizePath(rawPath.trimmed());
    if (!out.startsWith("/")) {
        out.prepend("/");
    }
    while (out.contains("//")) {
        out.replace("//", "/");
    }
    if (out.size() > 1 && out.endsWith("/")) {
        out.chop(1);
    }
    if (out.isEmpty()) {
        out = "/";
    }
    return out;
}

static SwString sanitizeProxyToken_(const SwString& rawToken) {
    SwString out;
    out.reserve(rawToken.size() + 8);
    for (std::size_t i = 0; i < rawToken.size(); ++i) {
        const char c = rawToken[i];
        const bool lower = (c >= 'a' && c <= 'z');
        const bool upper = (c >= 'A' && c <= 'Z');
        const bool digit = (c >= '0' && c <= '9');
        if (lower || upper || digit || c == '_') {
            out.append(c);
        } else {
            out.append('_');
        }
    }
    if (out.isEmpty()) {
        out = "route";
    }
    const char first = out[0];
    const bool firstLower = (first >= 'a' && first <= 'z');
    const bool firstUpper = (first >= 'A' && first <= 'Z');
    if (!firstLower && !firstUpper && first != '_') {
        out.prepend("r_");
    }
    return out;
}

static bool isValidHostPattern_(const SwString& host) {
    const SwString value = host.trimmed().toLower();
    if (value.isEmpty()) {
        return false;
    }
    if (value.contains(" ") || value.contains("\t") || value.contains("..")) {
        return false;
    }
    if (value.startsWith(".") || value.endsWith(".")) {
        return false;
    }

    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        const bool lower = (c >= 'a' && c <= 'z');
        const bool digit = (c >= '0' && c <= '9');
        const bool allowed = lower || digit || c == '.' || c == '-' || c == '*';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

static SwString jsonStringOr_(const SwJsonObject& object, const SwString& key, const SwString& fallback) {
    if (!object.contains(key)) {
        return fallback;
    }
    const SwJsonValue value = object.value(key, SwJsonValue());
    if (value.isString()) {
        return SwString(value.toString()).trimmed();
    }
    if (value.isInt()) {
        return SwString::number(value.toLongLong());
    }
    if (value.isBool()) {
        return value.toBool() ? SwString("true") : SwString("false");
    }
    return fallback;
}

static int jsonIntOr_(const SwJsonObject& object, const SwString& key, int fallback) {
    if (!object.contains(key)) {
        return fallback;
    }
    const SwJsonValue value = object.value(key, SwJsonValue());
    if (value.isInt() || value.isDouble() || value.isBool()) {
        return static_cast<int>(value.toInteger(fallback));
    }
    if (value.isString()) {
        bool ok = false;
        const int parsed = SwString(value.toString()).toInt(&ok);
        if (ok) {
            return parsed;
        }
    }
    return fallback;
}

static bool jsonBoolOr_(const SwJsonObject& object, const SwString& key, bool fallback) {
    if (!object.contains(key)) {
        return fallback;
    }
    const SwJsonValue value = object.value(key, SwJsonValue());
    if (value.isBool()) {
        return value.toBool();
    }
    if (value.isInt() || value.isDouble()) {
        return value.toInteger(0) != 0;
    }
    if (value.isString()) {
        const SwString text = SwString(value.toString()).trimmed().toLower();
        if (text == "1" || text == "true" || text == "yes" || text == "on") {
            return true;
        }
        if (text == "0" || text == "false" || text == "no" || text == "off") {
            return false;
        }
    }
    return fallback;
}

static ProxyControlConfig defaultProxyControlConfig_() {
    ProxyControlConfig config;

    ProxyUpstreamConfig upstream;
    upstream.id = "app_main";
    upstream.protocol = "http";
    upstream.host = "127.0.0.1";
    upstream.port = 8081;
    upstream.connectTimeoutMs = 3000;
    upstream.readTimeoutMs = 30000;
    upstream.active = true;
    config.upstreams.append(upstream);

    ProxySubdomainConfig route;
    route.host = "app.local.test";
    route.upstreamId = upstream.id;
    route.pathPrefix = "/";
    route.tls = false;
    route.websocket = true;
    route.forceHttps = false;
    route.stripPrefix = false;
    route.rateLimitRpm = 0;
    route.accessPolicy = "public";
    config.subdomains.append(route);

    return config;
}

static SwJsonObject proxyConfigToJsonObject_(const ProxyControlConfig& config) {
    SwJsonObject root;

    SwJsonObject global;
    global["workerProcesses"] = config.global.workerProcesses.toStdString();
    global["httpPort"] = config.global.httpPort;
    global["httpsPort"] = config.global.httpsPort;
    global["enableHttp2"] = config.global.enableHttp2;
    global["enableGzip"] = config.global.enableGzip;
    global["clientMaxBodyMb"] = config.global.clientMaxBodyMb;
    global["proxyBuffering"] = config.global.proxyBuffering;
    root["global"] = global;

    SwJsonArray upstreams;
    for (std::size_t i = 0; i < config.upstreams.size(); ++i) {
        const ProxyUpstreamConfig& item = config.upstreams[i];
        SwJsonObject upstream;
        upstream["id"] = item.id.toStdString();
        upstream["protocol"] = item.protocol.toStdString();
        upstream["host"] = item.host.toStdString();
        upstream["port"] = item.port;
        upstream["connectTimeoutMs"] = item.connectTimeoutMs;
        upstream["readTimeoutMs"] = item.readTimeoutMs;
        upstream["active"] = item.active;
        upstreams.append(upstream);
    }
    root["upstreams"] = upstreams;

    SwJsonArray subdomains;
    for (std::size_t i = 0; i < config.subdomains.size(); ++i) {
        const ProxySubdomainConfig& item = config.subdomains[i];
        SwJsonObject route;
        route["host"] = item.host.toStdString();
        route["upstreamId"] = item.upstreamId.toStdString();
        route["pathPrefix"] = item.pathPrefix.toStdString();
        route["tls"] = item.tls;
        route["websocket"] = item.websocket;
        route["forceHttps"] = item.forceHttps;
        route["stripPrefix"] = item.stripPrefix;
        route["rateLimitRpm"] = item.rateLimitRpm;
        route["accessPolicy"] = item.accessPolicy.toStdString();

        SwJsonArray headers;
        for (std::size_t h = 0; h < item.headers.size(); ++h) {
            const ProxyHeaderConfig& header = item.headers[h];
            SwJsonObject headerObject;
            headerObject["key"] = header.key.toStdString();
            headerObject["value"] = header.value.toStdString();
            headers.append(headerObject);
        }
        route["headers"] = headers;
        subdomains.append(route);
    }
    root["subdomains"] = subdomains;

    return root;
}

static bool proxyConfigFromJsonObject_(const SwJsonObject& source, ProxyControlConfig& outConfig, SwString& outError) {
    outError.clear();
    ProxyControlConfig config = defaultProxyControlConfig_();

    if (source.contains("global")) {
        const SwJsonValue globalValue = source.value("global", SwJsonValue());
        if (globalValue.isObject()) {
            const SwJsonObject global = globalValue.toObject();
            config.global.workerProcesses = jsonStringOr_(global, "workerProcesses", config.global.workerProcesses);
            config.global.httpPort = jsonIntOr_(global, "httpPort", config.global.httpPort);
            config.global.httpsPort = jsonIntOr_(global, "httpsPort", config.global.httpsPort);
            config.global.enableHttp2 = jsonBoolOr_(global, "enableHttp2", config.global.enableHttp2);
            config.global.enableGzip = jsonBoolOr_(global, "enableGzip", config.global.enableGzip);
            config.global.clientMaxBodyMb = jsonIntOr_(global, "clientMaxBodyMb", config.global.clientMaxBodyMb);
            config.global.proxyBuffering = jsonBoolOr_(global, "proxyBuffering", config.global.proxyBuffering);
        }
    }

    if (source.contains("upstreams")) {
        const SwJsonValue upstreamsValue = source.value("upstreams", SwJsonValue());
        if (!upstreamsValue.isArray()) {
            outError = "Invalid upstreams array";
            return false;
        }

        config.upstreams.clear();
        const SwJsonArray upstreams = upstreamsValue.toArray();
        for (SwJsonArray::const_iterator it = upstreams.begin(); it != upstreams.end(); ++it) {
            const SwJsonValue& value = *it;
            if (!value.isObject()) {
                continue;
            }
            const SwJsonObject upstreamObject = value.toObject();
            ProxyUpstreamConfig upstream;
            upstream.id = jsonStringOr_(upstreamObject, "id", SwString());
            upstream.protocol = toLowerTrimmed_(jsonStringOr_(upstreamObject, "protocol", "http"));
            upstream.host = jsonStringOr_(upstreamObject, "host", "127.0.0.1");
            upstream.port = jsonIntOr_(upstreamObject, "port", 80);
            upstream.connectTimeoutMs = jsonIntOr_(upstreamObject, "connectTimeoutMs", 3000);
            upstream.readTimeoutMs = jsonIntOr_(upstreamObject, "readTimeoutMs", 30000);
            upstream.active = jsonBoolOr_(upstreamObject, "active", true);
            config.upstreams.append(upstream);
        }
    }

    if (source.contains("subdomains")) {
        const SwJsonValue subdomainsValue = source.value("subdomains", SwJsonValue());
        if (!subdomainsValue.isArray()) {
            outError = "Invalid subdomains array";
            return false;
        }

        config.subdomains.clear();
        const SwJsonArray subdomains = subdomainsValue.toArray();
        for (SwJsonArray::const_iterator it = subdomains.begin(); it != subdomains.end(); ++it) {
            const SwJsonValue& value = *it;
            if (!value.isObject()) {
                continue;
            }
            const SwJsonObject routeObject = value.toObject();
            ProxySubdomainConfig route;
            route.host = toLowerTrimmed_(jsonStringOr_(routeObject, "host", SwString()));
            route.upstreamId = jsonStringOr_(routeObject, "upstreamId", SwString());
            route.pathPrefix = normalizePathPrefix_(jsonStringOr_(routeObject, "pathPrefix", "/"));
            route.tls = jsonBoolOr_(routeObject, "tls", false);
            route.websocket = jsonBoolOr_(routeObject, "websocket", true);
            route.forceHttps = jsonBoolOr_(routeObject, "forceHttps", false);
            route.stripPrefix = jsonBoolOr_(routeObject, "stripPrefix", false);
            route.rateLimitRpm = jsonIntOr_(routeObject, "rateLimitRpm", 0);
            route.accessPolicy = toLowerTrimmed_(jsonStringOr_(routeObject, "accessPolicy", "public"));
            if (route.accessPolicy.isEmpty()) {
                route.accessPolicy = "public";
            }

            route.headers.clear();
            const SwJsonValue headersValue = routeObject.value("headers", SwJsonValue());
            if (headersValue.isArray()) {
                const SwJsonArray headers = headersValue.toArray();
                for (SwJsonArray::const_iterator hit = headers.begin(); hit != headers.end(); ++hit) {
                    const SwJsonValue& hv = *hit;
                    if (!hv.isObject()) {
                        continue;
                    }
                    const SwJsonObject headerObj = hv.toObject();
                    ProxyHeaderConfig header;
                    header.key = jsonStringOr_(headerObj, "key", SwString()).trimmed();
                    header.value = jsonStringOr_(headerObj, "value", SwString()).trimmed();
                    if (header.key.isEmpty()) {
                        continue;
                    }
                    route.headers.append(header);
                }
            }

            config.subdomains.append(route);
        }
    }

    if (config.global.httpPort <= 0 || config.global.httpPort > 65535) {
        outError = "global.httpPort must be between 1 and 65535";
        return false;
    }
    if (config.global.httpsPort <= 0 || config.global.httpsPort > 65535) {
        outError = "global.httpsPort must be between 1 and 65535";
        return false;
    }
    if (config.global.clientMaxBodyMb <= 0) {
        config.global.clientMaxBodyMb = 100;
    }
    if (config.global.workerProcesses.trimmed().isEmpty()) {
        config.global.workerProcesses = "auto";
    }

    if (config.upstreams.isEmpty()) {
        outError = "At least one upstream is required";
        return false;
    }

    SwMap<SwString, bool> upstreamIds;
    for (std::size_t i = 0; i < config.upstreams.size(); ++i) {
        ProxyUpstreamConfig& upstream = config.upstreams[i];
        upstream.id = sanitizeProxyToken_(upstream.id.trimmed());
        upstream.protocol = toLowerTrimmed_(upstream.protocol);
        if (upstream.protocol != "http" && upstream.protocol != "https") {
            outError = "upstream.protocol must be http or https";
            return false;
        }
        if (upstream.host.trimmed().isEmpty()) {
            outError = "upstream.host is required";
            return false;
        }
        if (upstream.port <= 0 || upstream.port > 65535) {
            outError = "upstream.port must be between 1 and 65535";
            return false;
        }
        if (upstream.connectTimeoutMs < 0) {
            upstream.connectTimeoutMs = 0;
        }
        if (upstream.readTimeoutMs < 0) {
            upstream.readTimeoutMs = 0;
        }
        if (upstreamIds.contains(upstream.id)) {
            outError = "Duplicate upstream id: " + upstream.id;
            return false;
        }
        upstreamIds[upstream.id] = true;
    }

    if (config.subdomains.isEmpty()) {
        outError = "At least one subdomain route is required";
        return false;
    }

    SwMap<SwString, bool> uniqueRoutes;
    for (std::size_t i = 0; i < config.subdomains.size(); ++i) {
        ProxySubdomainConfig& route = config.subdomains[i];
        route.host = toLowerTrimmed_(route.host);
        route.upstreamId = sanitizeProxyToken_(route.upstreamId.trimmed());
        route.pathPrefix = normalizePathPrefix_(route.pathPrefix);
        route.accessPolicy = toLowerTrimmed_(route.accessPolicy);
        if (route.accessPolicy.isEmpty()) {
            route.accessPolicy = "public";
        }

        if (!isValidHostPattern_(route.host)) {
            outError = "Invalid subdomain host: " + route.host;
            return false;
        }
        if (route.upstreamId.isEmpty() || !upstreamIds.contains(route.upstreamId)) {
            outError = "subdomain references unknown upstreamId: " + route.upstreamId;
            return false;
        }
        if (route.rateLimitRpm < 0) {
            route.rateLimitRpm = 0;
        }

        const SwString routeKey = route.host + "|" + route.pathPrefix;
        if (uniqueRoutes.contains(routeKey)) {
            outError = "Duplicate route host+path: " + routeKey;
            return false;
        }
        uniqueRoutes[routeKey] = true;
    }

    outConfig = config;
    return true;
}

static bool saveProxyConfig_(const SwString& filePath, const ProxyControlConfig& config, SwString& outError) {
    outError.clear();
    const SwJsonObject root = proxyConfigToJsonObject_(config);
    SwJsonDocument document(root);
    if (!writeTextFile_(filePath, document.toJson(SwJsonDocument::JsonFormat::Pretty))) {
        outError = "Unable to write proxy config file";
        return false;
    }
    return true;
}

static bool loadProxyConfig_(const SwString& filePath, ProxyControlConfig& configOut, SwString& outError) {
    outError.clear();
    SwString raw;
    if (!readTextFile_(filePath, raw)) {
        outError = "Proxy config file not found";
        return false;
    }

    SwString parseError;
    SwJsonDocument document = SwJsonDocument::fromJson(raw.toStdString(), parseError);
    if (!parseError.isEmpty()) {
        outError = "Invalid JSON in proxy config file";
        return false;
    }
    if (!document.isObject()) {
        outError = "Proxy config JSON root must be an object";
        return false;
    }

    ProxyControlConfig parsed;
    if (!proxyConfigFromJsonObject_(document.object(), parsed, outError)) {
        return false;
    }

    configOut = parsed;
    return true;
}

struct ProxyRouteSelection {
    bool found = false;
    ProxySubdomainConfig route;
    ProxyUpstreamConfig upstream;
};

struct ProxyHttpRawResponse {
    bool valid = false;
    int status = 0;
    SwString reason;
    SwMap<SwString, SwString> headers;
    SwByteArray body;
};

static long long proxyNowMs_() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static SwString proxyNormalizeHost_(const SwString& hostHeader) {
    SwString host = hostHeader.trimmed().toLower();
    if (host.isEmpty()) {
        return host;
    }

    if (host.startsWith("[")) {
        const int closing = host.indexOf("]");
        if (closing > 0) {
            return host.left(closing + 1);
        }
        return host;
    }

    const int colon = host.indexOf(":");
    if (colon > 0) {
        host = host.left(colon);
    }
    return host.trimmed();
}

static SwString firstForwardedIp_(const SwString& forwardedForHeader) {
    SwString value = forwardedForHeader.trimmed();
    if (value.isEmpty()) {
        return value;
    }
    const int comma = value.indexOf(",");
    if (comma >= 0) {
        value = value.left(comma).trimmed();
    }
    return value;
}

static bool proxyHostMatches_(const SwString& pattern, const SwString& host) {
    const SwString p = pattern.trimmed().toLower();
    const SwString h = host.trimmed().toLower();
    if (p.isEmpty() || h.isEmpty()) {
        return false;
    }
    if (p == h) {
        return true;
    }
    if (p.startsWith("*.")) {
        const SwString suffix = p.mid(1);
        if (!h.endsWith(suffix)) {
            return false;
        }
        return h.size() > suffix.size();
    }
    return false;
}

static bool proxyPathMatchesPrefix_(const SwString& path, const SwString& prefix) {
    const SwString normalizedPath = swHttpNormalizePath(path);
    const SwString normalizedPrefix = normalizePathPrefix_(prefix);
    if (normalizedPrefix == "/") {
        return true;
    }
    if (normalizedPath == normalizedPrefix) {
        return true;
    }
    if (!normalizedPath.startsWith(normalizedPrefix)) {
        return false;
    }
    return normalizedPath.size() > normalizedPrefix.size() &&
           normalizedPath[normalizedPrefix.size()] == '/';
}

static bool proxyIsHopByHopHeader_(const SwString& headerKeyLower) {
    const SwString key = headerKeyLower.toLower();
    return key == "connection" ||
           key == "keep-alive" ||
           key == "proxy-authenticate" ||
           key == "proxy-authorization" ||
           key == "te" ||
           key == "trailer" ||
           key == "transfer-encoding" ||
           key == "upgrade";
}

static bool proxySelectRoute_(const ProxyControlConfig& config,
                              const SwString& host,
                              const SwString& path,
                              ProxyRouteSelection& outSelection,
                              SwString& outReason) {
    outSelection = ProxyRouteSelection();
    outReason.clear();

    int bestScore = -1;
    int bestRouteIndex = -1;
    for (std::size_t i = 0; i < config.subdomains.size(); ++i) {
        const ProxySubdomainConfig& route = config.subdomains[i];
        if (!proxyHostMatches_(route.host, host)) {
            continue;
        }
        if (!proxyPathMatchesPrefix_(path, route.pathPrefix)) {
            continue;
        }

        int score = static_cast<int>(normalizePathPrefix_(route.pathPrefix).size());
        if (route.host.toLower() == host.toLower()) {
            score += 10000;
        } else {
            score += 5000;
        }
        if (score > bestScore) {
            bestScore = score;
            bestRouteIndex = static_cast<int>(i);
        }
    }

    if (bestRouteIndex < 0) {
        outReason = "No route matched host/path";
        return false;
    }

    const ProxySubdomainConfig selectedRoute = config.subdomains[static_cast<std::size_t>(bestRouteIndex)];
    for (std::size_t i = 0; i < config.upstreams.size(); ++i) {
        const ProxyUpstreamConfig& upstream = config.upstreams[i];
        if (upstream.id != selectedRoute.upstreamId) {
            continue;
        }
        if (!upstream.active) {
            outReason = "Route matched, but upstream is inactive";
            return false;
        }
        outSelection.found = true;
        outSelection.route = selectedRoute;
        outSelection.upstream = upstream;
        return true;
    }

    outReason = "Route matched, but upstream not found";
    return false;
}

static bool proxyConsumeRateLimit_(ProxyControlState& state,
                                   const ProxySubdomainConfig& route,
                                   const SwHttpRequest& request,
                                   int& outRetryAfterSeconds) {
    outRetryAfterSeconds = 0;
    if (route.rateLimitRpm <= 0) {
        return true;
    }

    SwString clientKey = firstForwardedIp_(request.headers.value("x-forwarded-for", SwString()));
    if (clientKey.isEmpty()) {
        clientKey = request.headers.value("x-real-ip", SwString()).trimmed();
    }
    if (clientKey.isEmpty()) {
        clientKey = "global";
    }

    const SwString routeKey = route.host + "|" + route.pathPrefix + "|" + clientKey;
    const long long nowMs = proxyNowMs_();
    const long long windowMs = 60 * 1000;

    SwMutexLocker locker(&state.mutex);
    long long windowStart = state.routeWindowStartMs.value(routeKey, 0LL);
    int counter = state.routeWindowCounts.value(routeKey, 0);
    if (windowStart <= 0 || (nowMs - windowStart) >= windowMs) {
        windowStart = nowMs;
        counter = 0;
    }

    if (counter >= route.rateLimitRpm) {
        const long long elapsed = nowMs - windowStart;
        long long remainingMs = windowMs - elapsed;
        if (remainingMs < 1) {
            remainingMs = 1;
        }
        outRetryAfterSeconds = static_cast<int>((remainingMs + 999) / 1000);
        state.routeWindowStartMs[routeKey] = windowStart;
        state.routeWindowCounts[routeKey] = counter;
        return false;
    }

    ++counter;
    state.routeWindowStartMs[routeKey] = windowStart;
    state.routeWindowCounts[routeKey] = counter;
    return true;
}

static bool proxyDecodeChunkedBody_(const SwByteArray& encoded,
                                    SwByteArray& decodedOut,
                                    SwString& outError) {
    outError.clear();
    decodedOut.clear();

    int pos = 0;
    while (true) {
        const int lineEnd = encoded.indexOf("\r\n", pos);
        if (lineEnd < 0) {
            outError = "Malformed chunked encoding";
            return false;
        }

        const SwString sizeLine = SwString(encoded.mid(pos, lineEnd - pos));
        std::size_t chunkSize = 0;
        if (!swHttpParseHexSize(sizeLine, chunkSize)) {
            outError = "Invalid chunk size";
            return false;
        }
        pos = lineEnd + 2;

        if (chunkSize == 0) {
            if (encoded.size() < static_cast<std::size_t>(pos + 2)) {
                outError = "Malformed chunked trailer";
                return false;
            }
            return true;
        }

        if (encoded.size() < static_cast<std::size_t>(pos) + chunkSize + 2) {
            outError = "Incomplete chunked body";
            return false;
        }
        decodedOut.append(encoded.constData() + pos, chunkSize);
        pos += static_cast<int>(chunkSize);
        if (encoded[pos] != '\r' || encoded[pos + 1] != '\n') {
            outError = "Missing CRLF after chunk";
            return false;
        }
        pos += 2;
    }
}

static bool proxyParseHttpResponse_(const SwByteArray& raw,
                                    ProxyHttpRawResponse& parsed,
                                    SwString& outError) {
    parsed = ProxyHttpRawResponse();
    outError.clear();

    const int boundary = raw.indexOf("\r\n\r\n");
    if (boundary < 0) {
        outError = "Malformed upstream response (missing headers)";
        return false;
    }

    const SwByteArray headersPart = raw.left(boundary);
    const SwByteArray bodyPart = raw.mid(boundary + 4);
    const SwString headerText(headersPart);
    const SwList<SwString> lines = headerText.split("\r\n");
    if (lines.isEmpty()) {
        outError = "Malformed upstream response";
        return false;
    }

    const SwString statusLine = lines[0].trimmed();
    if (!statusLine.startsWith("HTTP/1.")) {
        outError = "Unsupported upstream protocol";
        return false;
    }
    const SwList<SwString> statusTokens = statusLine.split(' ');
    if (statusTokens.size() < 2) {
        outError = "Invalid upstream status line";
        return false;
    }

    bool statusOk = false;
    const int status = statusTokens[1].toInt(&statusOk);
    if (!statusOk || status < 100 || status > 599) {
        outError = "Invalid upstream status code";
        return false;
    }

    parsed.status = status;
    if (statusTokens.size() >= 3) {
        parsed.reason = statusLine.mid(static_cast<int>(statusTokens[0].size() + statusTokens[1].size() + 2)).trimmed();
    }

    for (std::size_t i = 1; i < lines.size(); ++i) {
        const SwString line = lines[i];
        if (line.isEmpty()) {
            continue;
        }
        const int colon = line.indexOf(":");
        if (colon < 0) {
            continue;
        }
        const SwString key = line.left(colon).trimmed().toLower();
        const SwString value = line.mid(colon + 1).trimmed();
        parsed.headers[key] = value;
    }

    const SwString transferEncoding = parsed.headers.value("transfer-encoding", SwString()).toLower();
    if (transferEncoding.contains("chunked")) {
        SwString chunkError;
        if (!proxyDecodeChunkedBody_(bodyPart, parsed.body, chunkError)) {
            outError = chunkError;
            return false;
        }
    } else {
        parsed.body = bodyPart;
    }

    parsed.valid = true;
    return true;
}

static SwString proxyBuildUpstreamPath_(const SwHttpRequest& request, const ProxySubdomainConfig& route) {
    SwString upstreamPath = swHttpNormalizePath(request.path);
    const SwString routePrefix = normalizePathPrefix_(route.pathPrefix);
    if (route.stripPrefix && routePrefix != "/" && upstreamPath.startsWith(routePrefix)) {
        upstreamPath = upstreamPath.mid(static_cast<int>(routePrefix.size()));
        if (upstreamPath.isEmpty()) {
            upstreamPath = "/";
        }
        if (!upstreamPath.startsWith("/")) {
            upstreamPath.prepend("/");
        }
    }
    if (!request.queryString.isEmpty()) {
        upstreamPath += "?";
        upstreamPath += request.queryString;
    }
    return upstreamPath;
}

static SwString proxyBuildUpstreamHostHeader_(const ProxyUpstreamConfig& upstream) {
    const bool isDefaultPort = (upstream.protocol == "http" && upstream.port == 80) ||
                               (upstream.protocol == "https" && upstream.port == 443);
    SwString host = upstream.host;
    if (!isDefaultPort) {
        host += ":";
        host += SwString::number(upstream.port);
    }
    return host;
}

static SwMap<SwString, SwString> proxyBuildForwardHeaders_(const SwHttpRequest& request,
                                                           const ProxySubdomainConfig& route,
                                                           const ProxyUpstreamConfig& upstream,
                                                           bool websocketMode) {
    SwMap<SwString, SwString> forwardedHeaders;
    for (auto it = request.headers.begin(); it != request.headers.end(); ++it) {
        const SwString keyLower = it.key().toLower();
        if (keyLower == "host" || keyLower == "content-length") {
            continue;
        }
        if (!websocketMode && proxyIsHopByHopHeader_(keyLower)) {
            continue;
        }
        forwardedHeaders[keyLower] = it.value();
    }

    forwardedHeaders["host"] = proxyBuildUpstreamHostHeader_(upstream);

    SwString forwardedFor = request.headers.value("x-forwarded-for", SwString()).trimmed();
    if (forwardedFor.isEmpty()) {
        forwardedFor = "127.0.0.1";
    } else {
        forwardedFor += ", 127.0.0.1";
    }
    forwardedHeaders["x-forwarded-for"] = forwardedFor;
    forwardedHeaders["x-forwarded-host"] = request.headers.value("host", SwString());
    forwardedHeaders["x-forwarded-proto"] = route.tls ? "https" : "http";
    forwardedHeaders["x-forwarded-port"] = SwString::number(route.tls ? 443 : 80);

    for (std::size_t h = 0; h < route.headers.size(); ++h) {
        const ProxyHeaderConfig& extra = route.headers[h];
        if (extra.key.trimmed().isEmpty()) {
            continue;
        }
        forwardedHeaders[extra.key.toLower().trimmed()] = extra.value;
    }

    if (websocketMode) {
        forwardedHeaders["connection"] = "Upgrade";
        forwardedHeaders["upgrade"] = "websocket";
    } else {
        forwardedHeaders["connection"] = "close";
    }

    return forwardedHeaders;
}

static SwByteArray proxyBuildRequestPayload_(const SwString& method,
                                             const SwString& upstreamPath,
                                             const SwMap<SwString, SwString>& headers,
                                             const SwByteArray& body) {
    SwByteArray payload;
    payload.append((method + " " + upstreamPath + " HTTP/1.1\r\n").toStdString());
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        payload.append((it.key() + ": " + it.value() + "\r\n").toStdString());
    }
    payload.append("\r\n");
    if (!body.isEmpty()) {
        payload.append(body);
    }
    return payload;
}

static SwHttpResponse proxyBuildTextResponse_(const SwHttpRequest& request, int status, const SwString& text) {
    SwHttpResponse response = swHttpTextResponse(status, text);
    response.closeConnection = !request.keepAlive;
    return response;
}

static SwHttpResponse proxyBuildJsonResponse_(const SwHttpRequest& request,
                                              int status,
                                              const SwJsonObject& payload) {
    SwHttpResponse response;
    response.status = status;
    response.reason = swHttpStatusReason(status);
    response.headers["content-type"] = "application/json; charset=utf-8";
    response.body = SwByteArray(
        SwJsonDocument(payload).toJson(SwJsonDocument::JsonFormat::Compact).toStdString());
    response.closeConnection = !request.keepAlive;
    return response;
}

static SwHttpResponse proxyBuildRedirectResponse_(const SwHttpRequest& request,
                                                  const SwString& location,
                                                  int status = 308) {
    SwHttpResponse response;
    response.status = status;
    response.reason = swHttpStatusReason(status);
    response.headers["location"] = location;
    response.body.clear();
    response.closeConnection = !request.keepAlive;
    return response;
}

static SwHttpResponse proxyBuildResponseFromParsed_(const SwHttpRequest& request,
                                                    const ProxyHttpRawResponse& parsed) {
    SwHttpResponse outResponse;
    outResponse.status = parsed.status;
    outResponse.reason = parsed.reason.isEmpty() ? swHttpStatusReason(parsed.status) : parsed.reason;
    outResponse.body = parsed.body;
    for (auto it = parsed.headers.begin(); it != parsed.headers.end(); ++it) {
        const SwString key = it.key().toLower();
        if (proxyIsHopByHopHeader_(key) || key == "content-length" || key == "transfer-encoding") {
            continue;
        }
        outResponse.headers[key] = it.value();
    }
    if (request.method.toUpper() == "HEAD") {
        outResponse.body.clear();
        outResponse.headOnly = true;
    }
    outResponse.closeConnection = !request.keepAlive;
    return outResponse;
}

static bool proxyIsWebSocketUpgradeRequest_(const SwHttpRequest& request) {
    const SwString method = request.method.toUpper();
    if (method != "GET") {
        return false;
    }

    const SwString connection = request.headers.value("connection", SwString()).toLower();
    const SwString upgrade = request.headers.value("upgrade", SwString()).toLower();
    return connection.contains("upgrade") && upgrade == "websocket";
}

static bool proxyParseStatusAndHeaders_(const SwByteArray& headersRaw,
                                        int& statusOut,
                                        SwString& reasonOut,
                                        SwMap<SwString, SwString>& headersOut,
                                        SwString& outError) {
    statusOut = 0;
    reasonOut.clear();
    headersOut.clear();
    outError.clear();

    const SwString headerText(headersRaw);
    const SwList<SwString> lines = headerText.split("\r\n");
    if (lines.isEmpty()) {
        outError = "Malformed upstream response";
        return false;
    }

    const SwString statusLine = lines[0].trimmed();
    if (!statusLine.startsWith("HTTP/1.")) {
        outError = "Unsupported upstream protocol";
        return false;
    }

    const SwList<SwString> statusTokens = statusLine.split(' ');
    if (statusTokens.size() < 2) {
        outError = "Invalid upstream status line";
        return false;
    }

    bool statusOk = false;
    const int parsedStatus = statusTokens[1].toInt(&statusOk);
    if (!statusOk || parsedStatus < 100 || parsedStatus > 599) {
        outError = "Invalid upstream status code";
        return false;
    }

    statusOut = parsedStatus;
    if (statusTokens.size() >= 3) {
        reasonOut = statusLine.mid(static_cast<int>(statusTokens[0].size() + statusTokens[1].size() + 2)).trimmed();
    }

    for (std::size_t i = 1; i < lines.size(); ++i) {
        const SwString line = lines[i].trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const int colon = line.indexOf(":");
        if (colon < 0) {
            continue;
        }
        const SwString key = line.left(colon).trimmed().toLower();
        const SwString value = line.mid(colon + 1).trimmed();
        if (key.isEmpty()) {
            continue;
        }
        headersOut[key] = value;
    }
    return true;
}

class ProxyHttpForwardJob : public SwObject {
    SW_OBJECT(ProxyHttpForwardJob, SwObject)

public:
    using DoneCallback = std::function<void(const SwHttpResponse&, const SwString&)>;

    ProxyHttpForwardJob(const SwHttpRequest& request,
                        const ProxySubdomainConfig& route,
                        const ProxyUpstreamConfig& upstream,
                        const SwHttpLimits& limits,
                        const DoneCallback& done,
                        SwObject* parent = nullptr)
        : SwObject(parent),
          m_request(request),
          m_route(route),
          m_upstream(upstream),
          m_limits(limits),
          m_done(done) {
        m_connectTimeoutMs = (m_upstream.connectTimeoutMs > 0) ? m_upstream.connectTimeoutMs : 5000;
        m_readTimeoutMs = (m_upstream.readTimeoutMs > 0) ? m_upstream.readTimeoutMs : 30000;
        m_maxResponseBytes = (m_limits.maxBodyBytes > 0) ? (m_limits.maxBodyBytes + 128 * 1024) : (64 * 1024 * 1024);
        m_startedAtMs = proxyNowMs_();
        m_lastProgressMs = m_startedAtMs;
    }

    void start() {
        if (m_finished) {
            return;
        }

        SwString upstreamPath = proxyBuildUpstreamPath_(m_request, m_route);
        SwMap<SwString, SwString> headers = proxyBuildForwardHeaders_(m_request, m_route, m_upstream, false);
        headers["content-length"] = SwString::number(static_cast<long long>(m_request.body.size()));
        m_payload = proxyBuildRequestPayload_(m_request.method, upstreamPath, headers, m_request.body);

        m_socket = new SwTcpSocket(this);
        m_socket->useSsl(m_upstream.protocol == "https", m_upstream.host);
        connect(m_socket, SIGNAL(connected), this, &ProxyHttpForwardJob::onConnected_);
        connect(m_socket, SIGNAL(readyRead), this, &ProxyHttpForwardJob::onReadyRead_);
        connect(m_socket, SIGNAL(disconnected), this, &ProxyHttpForwardJob::onDisconnected_);
        connect(m_socket, SIGNAL(errorOccurred), this, &ProxyHttpForwardJob::onError_);

        m_timeoutWatch = new SwTimer(100, this);
        connect(m_timeoutWatch, SIGNAL(timeout), this, &ProxyHttpForwardJob::onTimeout_);
        m_timeoutWatch->start();

        if (!m_socket->connectToHost(m_upstream.host, static_cast<uint16_t>(m_upstream.port))) {
            fail_("Unable to connect to upstream");
            return;
        }
    }

private slots:
    void onConnected_() {
        if (m_finished || !m_socket) {
            return;
        }
        m_connected = true;
        m_lastProgressMs = proxyNowMs_();
        if (!m_socket->write(SwString(m_payload))) {
            fail_("Unable to write upstream request");
        }
    }

    void onReadyRead_() {
        if (m_finished || !m_socket) {
            return;
        }

        bool gotData = false;
        while (true) {
            SwString chunk = m_socket->read();
            if (chunk.isEmpty()) {
                break;
            }
            gotData = true;
            m_rawResponse.append(chunk.data(), chunk.size());
            if (m_maxResponseBytes > 0 && m_rawResponse.size() > m_maxResponseBytes) {
                fail_("Upstream response too large");
                return;
            }
        }

        if (gotData) {
            m_lastProgressMs = proxyNowMs_();
        }
    }

    void onDisconnected_() {
        if (m_finished) {
            return;
        }

        ProxyHttpRawResponse parsed;
        SwString parseError;
        if (!proxyParseHttpResponse_(m_rawResponse, parsed, parseError)) {
            fail_(parseError);
            return;
        }

        SwHttpResponse response = proxyBuildResponseFromParsed_(m_request, parsed);
        finish_(response);
    }

    void onError_(int) {
        if (m_finished) {
            return;
        }
        fail_("Upstream socket error");
    }

    void onTimeout_() {
        if (m_finished) {
            return;
        }

        const long long nowMs = proxyNowMs_();
        if (!m_connected) {
            if (m_connectTimeoutMs > 0 && (nowMs - m_startedAtMs) > m_connectTimeoutMs) {
                fail_("Upstream connection timeout");
            }
            return;
        }

        if (m_readTimeoutMs > 0 && (nowMs - m_lastProgressMs) > m_readTimeoutMs) {
            fail_("Upstream read timeout");
        }
    }

private:
    void finish_(const SwHttpResponse& response) {
        if (m_finished) {
            return;
        }
        m_finished = true;
        if (m_timeoutWatch) {
            m_timeoutWatch->stop();
        }
        if (m_done) {
            m_done(response, SwString());
        }
        if (m_socket) {
            m_socket->disconnectAllSlots();
            m_socket->close();
        }
        deleteLater();
    }

    void fail_(const SwString& error) {
        if (m_finished) {
            return;
        }
        m_finished = true;
        if (m_timeoutWatch) {
            m_timeoutWatch->stop();
        }
        if (m_done) {
            m_done(SwHttpResponse(), error);
        }
        if (m_socket) {
            m_socket->disconnectAllSlots();
            m_socket->close();
        }
        deleteLater();
    }

    SwHttpRequest m_request;
    ProxySubdomainConfig m_route;
    ProxyUpstreamConfig m_upstream;
    SwHttpLimits m_limits;
    DoneCallback m_done;

    SwTcpSocket* m_socket = nullptr;
    SwTimer* m_timeoutWatch = nullptr;
    SwByteArray m_payload;
    SwByteArray m_rawResponse;

    bool m_finished = false;
    bool m_connected = false;
    int m_connectTimeoutMs = 5000;
    int m_readTimeoutMs = 30000;
    std::size_t m_maxResponseBytes = 0;
    long long m_startedAtMs = 0;
    long long m_lastProgressMs = 0;
};

class ProxyWebSocketTunnelBridge : public SwObject {
    SW_OBJECT(ProxyWebSocketTunnelBridge, SwObject)

public:
    using DoneCallback = std::function<void(const SwHttpResponse&, const SwString&)>;

    ProxyWebSocketTunnelBridge(const SwHttpRequest& request,
                               const ProxySubdomainConfig& route,
                               const ProxyUpstreamConfig& upstream,
                               const SwHttpLimits& limits,
                               const DoneCallback& done,
                               SwObject* parent = nullptr)
        : SwObject(parent),
          m_request(request),
          m_route(route),
          m_upstream(upstream),
          m_limits(limits),
          m_done(done) {
        m_connectTimeoutMs = (m_upstream.connectTimeoutMs > 0) ? m_upstream.connectTimeoutMs : 5000;
        m_readTimeoutMs = (m_upstream.readTimeoutMs > 0) ? m_upstream.readTimeoutMs : 30000;
        m_startedAtMs = proxyNowMs_();
        m_lastProgressMs = m_startedAtMs;
    }

    void start() {
        if (m_finished) {
            return;
        }

        SwString upstreamPath = proxyBuildUpstreamPath_(m_request, m_route);
        SwMap<SwString, SwString> headers = proxyBuildForwardHeaders_(m_request, m_route, m_upstream, true);
        headers.remove("content-length");
        headers["content-length"] = "0";
        m_handshakePayload = proxyBuildRequestPayload_("GET", upstreamPath, headers, SwByteArray());

        m_upstreamSocket = new SwTcpSocket(this);
        m_upstreamSocket->useSsl(m_upstream.protocol == "https", m_upstream.host);
        connect(m_upstreamSocket, SIGNAL(connected), this, &ProxyWebSocketTunnelBridge::onUpstreamConnected_);
        connect(m_upstreamSocket, SIGNAL(readyRead), this, &ProxyWebSocketTunnelBridge::onUpstreamReadyRead_);
        connect(m_upstreamSocket, SIGNAL(disconnected), this, &ProxyWebSocketTunnelBridge::onUpstreamDisconnected_);
        connect(m_upstreamSocket, SIGNAL(errorOccurred), this, &ProxyWebSocketTunnelBridge::onUpstreamError_);

        m_timeoutWatch = new SwTimer(100, this);
        connect(m_timeoutWatch, SIGNAL(timeout), this, &ProxyWebSocketTunnelBridge::onTimeout_);
        m_timeoutWatch->start();

        if (!m_upstreamSocket->connectToHost(m_upstream.host, static_cast<uint16_t>(m_upstream.port))) {
            fail_("Unable to connect to websocket upstream");
        }
    }

private slots:
    void onUpstreamConnected_() {
        if (m_finished || !m_upstreamSocket) {
            return;
        }
        m_upstreamConnected = true;
        m_lastProgressMs = proxyNowMs_();
        if (!m_upstreamSocket->write(SwString(m_handshakePayload))) {
            fail_("Unable to write websocket handshake to upstream");
        }
    }

    void onUpstreamReadyRead_() {
        if (m_finished || !m_upstreamSocket) {
            return;
        }

        if (!m_upstreamHandshakeDone) {
            while (true) {
                SwString chunk = m_upstreamSocket->read();
                if (chunk.isEmpty()) {
                    break;
                }
                m_upstreamHandshakeBuffer.append(chunk.data(), chunk.size());
                m_lastProgressMs = proxyNowMs_();
            }

            const int boundary = m_upstreamHandshakeBuffer.indexOf("\r\n\r\n");
            if (boundary < 0) {
                return;
            }

            const SwByteArray headerBytes = m_upstreamHandshakeBuffer.left(boundary);
            const SwByteArray tailBytes = m_upstreamHandshakeBuffer.mid(boundary + 4);
            m_upstreamHandshakeBuffer.clear();

            int status = 0;
            SwString reason;
            SwMap<SwString, SwString> headers;
            SwString parseError;
            if (!proxyParseStatusAndHeaders_(headerBytes, status, reason, headers, parseError)) {
                fail_(parseError);
                return;
            }

            if (status != 101) {
                fail_("WebSocket upstream rejected upgrade");
                return;
            }

            m_upstreamHandshakeDone = true;
            m_upstreamHandshakeHeaders = headers;
            m_upstreamHandshakeReason = reason;
            m_pendingUpstreamBytesAfterHandshake = tailBytes;

            SwHttpResponse response;
            response.status = 101;
            response.reason = m_upstreamHandshakeReason.isEmpty() ? "Switching Protocols" : m_upstreamHandshakeReason;
            for (auto it = m_upstreamHandshakeHeaders.begin(); it != m_upstreamHandshakeHeaders.end(); ++it) {
                const SwString key = it.key().toLower();
                if (key == "content-length" || key == "transfer-encoding") {
                    continue;
                }
                response.headers[key] = it.value();
            }
            response.headers["x-proxy-upstream"] =
                m_upstream.protocol + "://" + m_upstream.host + ":" + SwString::number(m_upstream.port);
            response.headers["x-proxy-route"] = m_route.host + m_route.pathPrefix;
            response.switchToRawSocket = true;
            response.onSwitchToRawSocket = [this](SwTcpSocket* clientSocket) {
                onClientSocketHandover_(clientSocket);
            };

            m_responseSent = true;
            m_waitingClientHandover = true;
            if (m_done) {
                m_done(response, SwString());
            }
            return;
        }

        pumpSocket_(m_upstreamSocket, m_clientSocket);
    }

    void onUpstreamDisconnected_() {
        if (m_finished) {
            return;
        }
        if (!m_upstreamHandshakeDone) {
            fail_("WebSocket upstream disconnected during handshake");
            return;
        }
        closeTunnel_();
    }

    void onUpstreamError_(int) {
        if (m_finished) {
            return;
        }
        fail_("WebSocket upstream socket error");
    }

    void onClientReadyRead_() {
        if (m_finished) {
            return;
        }
        pumpSocket_(m_clientSocket, m_upstreamSocket);
    }

    void onClientDisconnected_() {
        if (m_finished) {
            return;
        }
        closeTunnel_();
    }

    void onClientError_(int) {
        if (m_finished) {
            return;
        }
        closeTunnel_();
    }

    void onTimeout_() {
        if (m_finished) {
            return;
        }

        const long long nowMs = proxyNowMs_();
        if (!m_upstreamConnected) {
            if (m_connectTimeoutMs > 0 && (nowMs - m_startedAtMs) > m_connectTimeoutMs) {
                fail_("WebSocket upstream connection timeout");
            }
            return;
        }

        if (m_readTimeoutMs > 0 && (nowMs - m_lastProgressMs) > m_readTimeoutMs) {
            closeTunnel_();
        }
    }

private:
    void onClientSocketHandover_(SwTcpSocket* clientSocket) {
        m_waitingClientHandover = false;
        if (m_finished || !clientSocket) {
            if (clientSocket) {
                clientSocket->close();
                clientSocket->deleteLater();
            }
            deleteLater();
            return;
        }

        m_clientSocket = clientSocket;
        m_clientSocket->setParent(this);
        connect(m_clientSocket, SIGNAL(readyRead), this, &ProxyWebSocketTunnelBridge::onClientReadyRead_);
        connect(m_clientSocket, SIGNAL(disconnected), this, &ProxyWebSocketTunnelBridge::onClientDisconnected_);
        connect(m_clientSocket, SIGNAL(errorOccurred), this, &ProxyWebSocketTunnelBridge::onClientError_);

        if (!m_pendingUpstreamBytesAfterHandshake.isEmpty()) {
            m_clientSocket->write(SwString(m_pendingUpstreamBytesAfterHandshake));
            m_pendingUpstreamBytesAfterHandshake.clear();
        }

        if (m_closeWhenClientAttached) {
            closeTunnel_();
        }
    }

    void pumpSocket_(SwTcpSocket* src, SwTcpSocket* dst) {
        if (m_finished || !src || !dst) {
            return;
        }

        bool progressed = false;
        while (true) {
            SwString chunk = src->read();
            if (chunk.isEmpty()) {
                break;
            }
            progressed = true;
            dst->write(chunk);
        }

        if (progressed) {
            m_lastProgressMs = proxyNowMs_();
        }
    }

    void closeTunnel_() {
        if (m_finished) {
            return;
        }
        m_finished = true;
        if (m_timeoutWatch) {
            m_timeoutWatch->stop();
        }
        if (m_clientSocket) {
            m_clientSocket->disconnectAllSlots();
            m_clientSocket->close();
            m_clientSocket->deleteLater();
            m_clientSocket = nullptr;
        }
        if (m_upstreamSocket) {
            m_upstreamSocket->disconnectAllSlots();
            m_upstreamSocket->close();
            m_upstreamSocket = nullptr;
        }
        if (m_waitingClientHandover) {
            m_closeWhenClientAttached = true;
            return;
        }
        deleteLater();
    }

    void fail_(const SwString& error) {
        if (m_finished) {
            return;
        }
        m_finished = true;
        if (m_timeoutWatch) {
            m_timeoutWatch->stop();
        }
        if (!m_responseSent && m_done) {
            m_done(SwHttpResponse(), error);
        }
        if (m_clientSocket) {
            m_clientSocket->disconnectAllSlots();
            m_clientSocket->close();
            m_clientSocket->deleteLater();
            m_clientSocket = nullptr;
        }
        if (m_upstreamSocket) {
            m_upstreamSocket->disconnectAllSlots();
            m_upstreamSocket->close();
            m_upstreamSocket = nullptr;
        }
        if (m_waitingClientHandover) {
            m_closeWhenClientAttached = true;
            return;
        }
        deleteLater();
    }

    SwHttpRequest m_request;
    ProxySubdomainConfig m_route;
    ProxyUpstreamConfig m_upstream;
    SwHttpLimits m_limits;
    DoneCallback m_done;

    SwTcpSocket* m_upstreamSocket = nullptr;
    SwTcpSocket* m_clientSocket = nullptr;
    SwTimer* m_timeoutWatch = nullptr;

    SwByteArray m_handshakePayload;
    SwByteArray m_upstreamHandshakeBuffer;
    SwByteArray m_pendingUpstreamBytesAfterHandshake;

    SwMap<SwString, SwString> m_upstreamHandshakeHeaders;
    SwString m_upstreamHandshakeReason;

    bool m_finished = false;
    bool m_responseSent = false;
    bool m_waitingClientHandover = false;
    bool m_closeWhenClientAttached = false;
    bool m_upstreamConnected = false;
    bool m_upstreamHandshakeDone = false;

    int m_connectTimeoutMs = 5000;
    int m_readTimeoutMs = 30000;
    long long m_startedAtMs = 0;
    long long m_lastProgressMs = 0;
};

static SwString buildProxyRuntimePlan_(const ProxyControlConfig& config) {
    SwString out;
    out += "# Proxy Runtime Plan\n";
    out += "workerProcesses=" + config.global.workerProcesses + "\n";
    out += "httpPort=" + SwString::number(config.global.httpPort) + "\n";
    out += "httpsPort=" + SwString::number(config.global.httpsPort) + "\n";
    out += "http2=" + SwString(config.global.enableHttp2 ? "true" : "false") + "\n";
    out += "gzip=" + SwString(config.global.enableGzip ? "true" : "false") + "\n";
    out += "proxyBuffering=" + SwString(config.global.proxyBuffering ? "true" : "false") + "\n";
    out += "clientMaxBodyMb=" + SwString::number(config.global.clientMaxBodyMb) + "\n\n";

    out += "[UPSTREAMS]\n";
    for (std::size_t i = 0; i < config.upstreams.size(); ++i) {
        const ProxyUpstreamConfig& u = config.upstreams[i];
        out += "- id=" + u.id +
               " target=" + u.protocol + "://" + u.host + ":" + SwString::number(u.port) +
               " active=" + SwString(u.active ? "true" : "false") +
               " connectMs=" + SwString::number(u.connectTimeoutMs) +
               " readMs=" + SwString::number(u.readTimeoutMs) + "\n";
    }

    out += "\n[ROUTES]\n";
    for (std::size_t i = 0; i < config.subdomains.size(); ++i) {
        const ProxySubdomainConfig& r = config.subdomains[i];
        out += "- host=" + r.host +
               " prefix=" + r.pathPrefix +
               " -> upstream=" + r.upstreamId +
               " tls=" + SwString(r.tls ? "true" : "false") +
               " ws=" + SwString(r.websocket ? "true" : "false") +
               " forceHttps=" + SwString(r.forceHttps ? "true" : "false") +
               " stripPrefix=" + SwString(r.stripPrefix ? "true" : "false") +
               " rateLimitRpm=" + SwString::number(r.rateLimitRpm) +
               " policy=" + r.accessPolicy + "\n";
    }
    return out;
}

static void printUsage_() {
    swDebug() << "[HttpAppQuickstart] Usage:";
    swDebug() << "  HttpAppQuickstart --port 8080 --www ./http_app_quickstart_www --uploads ./http_app_quickstart_uploads --workers 4";
    swDebug() << "  Options:";
    swDebug() << "    --port <n>       HTTP listening port (default: 8080)";
    swDebug() << "    --www <path>     Static public directory (default: ./http_app_quickstart_www)";
    swDebug() << "    --uploads <path> Upload storage directory (default: ./http_app_quickstart_uploads)";
    swDebug() << "    --workers <n>    SwThreadPool worker count (default: CPU count from global pool)";
    swDebug() << "    --no-threadpool  Keep inline request dispatch";
    swDebug() << "    --help / -h      Show this help";
}

int main(int argc, char* argv[]) {
    SwCoreApplication app(argc, argv);

    if (app.hasArgument("help") || app.hasArgument("h")) {
        printUsage_();
        return 0;
    }

    uint16_t port = 8080;
    {
        SwString portArg = app.getArgument("port", app.getArgument("p", SwString()));
        if (!portArg.isEmpty()) {
            int parsedPort = 0;
            if (!parseIntArg_(portArg, parsedPort) || parsedPort <= 0 || parsedPort > 65535) {
                swError() << "[HttpAppQuickstart] Invalid --port value: " << portArg;
                return 1;
            }
            port = static_cast<uint16_t>(parsedPort);
        }
    }

    const SwString wwwArg = app.getArgument("www", "http_app_quickstart_www");
    const SwString uploadsArg = app.getArgument("uploads", "http_app_quickstart_uploads");
    const bool useThreadPool = !app.hasArgument("no-threadpool");

    int workersArg = -1;
    {
        const SwString workersText = app.getArgument("workers", SwString());
        if (!workersText.isEmpty()) {
            if (!parseIntArg_(workersText, workersArg) || workersArg <= 0) {
                swError() << "[HttpAppQuickstart] Invalid --workers value: " << workersText;
                return 1;
            }
        }
    }

    SwString absPublicRoot;
    if (!ensureDirectory_(wwwArg, absPublicRoot)) {
        swError() << "[HttpAppQuickstart] Failed to create public directory: " << wwwArg;
        return 1;
    }

    SwString absUploadRoot;
    if (!ensureDirectory_(uploadsArg, absUploadRoot)) {
        swError() << "[HttpAppQuickstart] Failed to create uploads directory: " << uploadsArg;
        return 1;
    }

    SwString absMultipartTempRoot;
    if (!ensureDirectory_(joinPath_(absUploadRoot, ".tmp_multipart"), absMultipartTempRoot)) {
        swError() << "[HttpAppQuickstart] Failed to create multipart temp directory";
        return 1;
    }

    SwString absProxyControlRoot;
    if (!ensureDirectory_(joinPath_(absUploadRoot, ".proxy_control"), absProxyControlRoot)) {
        swError() << "[HttpAppQuickstart] Failed to create proxy control directory";
        return 1;
    }

    const SwString indexHtml = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>SwHttpApp Quickstart</title>
  <style>
    body { font-family: "Space Grotesk", "Trebuchet MS", sans-serif; margin: 2rem; line-height: 1.5; background: radial-gradient(circle at 10% 10%, #f4f8ff 0%, #fef8ec 45%, #f6f6f6 100%); }
    h1 { margin-bottom: 0.3rem; }
    code { background: #f0f3f8; padding: 0.2rem 0.35rem; border-radius: 4px; }
    pre { background: #101522; color: #d6e1ff; padding: 1rem; border-radius: 10px; overflow: auto; }
    .box { max-width: 960px; background: rgba(255,255,255,0.85); border: 1px solid #e6e9f0; border-radius: 14px; padding: 1rem 1.2rem; box-shadow: 0 20px 60px rgba(16,21,34,0.08); }
    .hero-link { display: inline-block; margin: 0.7rem 0 1rem; padding: 0.65rem 0.95rem; border-radius: 10px; text-decoration: none; color: #fff; background: linear-gradient(120deg, #0f7a6e, #2b5ba7); font-weight: 700; }
  </style>
</head>
<body>
  <div class="box">
    <h1>SwHttpApp Quickstart</h1>
    <p>Server is running. Use the proxy control panel to manage subdomains and reverse-proxy settings.</p>
    <a class="hero-link" href="/public/proxy-admin.html">Open Proxy Control Plane</a>
    <ul>
      <li><code>GET /api/v1/health</code></li>
      <li><code>GET /api/v1/users/42?verbose=1</code></li>
      <li><code>POST /api/v1/echo</code> (raw body)</li>
      <li><code>POST /api/v1/upload</code> (multipart/form-data)</li>
      <li><code>GET /api/v1/chunked</code></li>
      <li><code>GET /api/v1/routes</code> (reverse URL)</li>
      <li><code>GET /api/v1/slow</code> (soft timeout demo)</li>
      <li><code>GET /api/v1/admin/metrics</code> (auth required)</li>
    </ul>
    <pre>curl -i http://127.0.0.1:8080/api/v1/health
curl -i -X POST http://127.0.0.1:8080/api/v1/echo --data "hello"
curl -i -H "Authorization: Bearer dev-admin-token" http://127.0.0.1:8080/api/v1/admin/metrics
curl -i -H "Authorization: Bearer dev-admin-token" http://127.0.0.1:8080/api/v1/admin/proxy/runtime.txt</pre>
  </div>
</body>
</html>
)HTML";

    const SwString proxyDashboardHtml = R"HTML(
<!doctype html>
<html lang="fr">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Proxy Control Plane</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Space+Grotesk:wght@400;500;600;700&family=IBM+Plex+Mono:wght@400;500&display=swap" rel="stylesheet">
  <style>
    :root {
      --bg-0: #090c12;
      --bg-1: #111a29;
      --panel: rgba(16, 24, 39, 0.78);
      --panel-2: rgba(15, 23, 42, 0.9);
      --line: rgba(148, 163, 184, 0.27);
      --text: #e8eefb;
      --muted: #9bb0d6;
      --teal: #31d0b9;
      --amber: #f6b349;
      --danger: #ff667d;
      --ok: #5ef4a1;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: "Space Grotesk", "Segoe UI", sans-serif;
      color: var(--text);
      background:
        radial-gradient(1200px 700px at 10% -20%, #2f7a92 0%, rgba(47, 122, 146, 0) 60%),
        radial-gradient(1000px 700px at 95% 0%, #78478f 0%, rgba(120, 71, 143, 0) 60%),
        linear-gradient(160deg, var(--bg-0), var(--bg-1) 62%, #121822);
      min-height: 100vh;
    }
    .gridfx::before {
      content: "";
      position: fixed;
      inset: 0;
      pointer-events: none;
      background-image: linear-gradient(to right, rgba(255,255,255,0.03) 1px, transparent 1px), linear-gradient(to bottom, rgba(255,255,255,0.03) 1px, transparent 1px);
      background-size: 34px 34px;
      opacity: 0.32;
    }
    .wrap {
      width: min(1320px, calc(100% - 2rem));
      margin: 1rem auto 2rem;
      position: relative;
      z-index: 1;
    }
    .hero {
      border: 1px solid var(--line);
      border-radius: 16px;
      padding: 1rem 1.1rem;
      background: linear-gradient(120deg, rgba(21,31,53,0.85), rgba(15,23,42,0.78));
      box-shadow: 0 26px 60px rgba(0, 0, 0, 0.32);
      animation: rise 560ms ease both;
    }
    .hero h1 {
      margin: 0;
      font-size: clamp(1.45rem, 2.5vw, 2rem);
      letter-spacing: 0.02em;
    }
    .hero p {
      margin: 0.45rem 0 0;
      color: var(--muted);
    }
    .toolbar {
      display: flex;
      flex-wrap: wrap;
      gap: 0.65rem;
      margin-top: 0.95rem;
      align-items: center;
    }
    .token {
      display: flex;
      align-items: center;
      gap: 0.45rem;
      border: 1px solid var(--line);
      background: rgba(15,23,42,0.7);
      border-radius: 999px;
      padding: 0.38rem 0.8rem;
      color: var(--muted);
    }
    .token input {
      width: 280px;
      border: none;
      outline: none;
      background: transparent;
      color: var(--text);
      font: inherit;
    }
    .btn {
      border: 1px solid var(--line);
      background: linear-gradient(120deg, #202c42, #111827);
      color: var(--text);
      font: inherit;
      padding: 0.5rem 0.8rem;
      border-radius: 10px;
      cursor: pointer;
      transition: transform 160ms ease, border-color 160ms ease, box-shadow 160ms ease;
    }
    .btn:hover { transform: translateY(-1px); border-color: #9cc8ff; box-shadow: 0 12px 24px rgba(8,14,25,0.35); }
    .btn.primary { border-color: #2ab89f; background: linear-gradient(125deg, #0f8f7e, #2760b5); }
    .btn.warn { border-color: #f09b34; background: linear-gradient(125deg, #5c3714, #8e4e10); }
    .btn.danger { border-color: #f06a80; background: linear-gradient(125deg, #5a1b30, #7f233f); }
    .layout {
      display: grid;
      grid-template-columns: 1.2fr 1fr;
      gap: 1rem;
      margin-top: 1rem;
    }
    .card {
      border: 1px solid var(--line);
      border-radius: 14px;
      padding: 0.9rem;
      background: var(--panel);
      backdrop-filter: blur(6px);
      animation: rise 680ms ease both;
    }
    .card h2 {
      margin: 0 0 0.55rem;
      font-size: 1.05rem;
      letter-spacing: 0.02em;
    }
    .muted { color: var(--muted); font-size: 0.9rem; }
    .form-grid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 0.6rem;
    }
    label { display: flex; flex-direction: column; gap: 0.28rem; font-size: 0.82rem; color: var(--muted); }
    input, select, textarea {
      width: 100%;
      border: 1px solid var(--line);
      border-radius: 10px;
      background: rgba(15, 23, 42, 0.88);
      color: var(--text);
      font: inherit;
      padding: 0.45rem 0.55rem;
    }
    textarea { min-height: 74px; resize: vertical; font-family: "IBM Plex Mono", monospace; }
    .switches {
      display: flex;
      flex-wrap: wrap;
      gap: 0.55rem;
      margin-top: 0.5rem;
    }
    .switch {
      border: 1px solid var(--line);
      border-radius: 999px;
      padding: 0.28rem 0.58rem;
      display: inline-flex;
      gap: 0.4rem;
      align-items: center;
      color: var(--muted);
      background: rgba(15,23,42,0.62);
    }
    .list {
      display: grid;
      gap: 0.65rem;
      margin-top: 0.65rem;
    }
    .item {
      border: 1px solid var(--line);
      border-radius: 12px;
      background: var(--panel-2);
      padding: 0.72rem;
      animation: rise 520ms ease both;
    }
    .item-head {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 0.45rem;
    }
    .item-head b { font-size: 0.9rem; }
    .item .grid { display: grid; grid-template-columns: repeat(3, minmax(0,1fr)); gap: 0.5rem; }
    .item .grid.two { grid-template-columns: repeat(2, minmax(0,1fr)); }
    .badge {
      display: inline-flex;
      align-items: center;
      gap: 0.35rem;
      padding: 0.28rem 0.54rem;
      border-radius: 999px;
      font-size: 0.74rem;
      border: 1px solid var(--line);
      background: rgba(25, 37, 63, 0.5);
      color: #cde1ff;
    }
    .status {
      margin-left: auto;
      font-size: 0.83rem;
      color: var(--muted);
    }
    .status.ok { color: var(--ok); }
    .status.err { color: var(--danger); }
    .preview {
      height: min(75vh, 900px);
      border: 1px solid var(--line);
      border-radius: 12px;
      padding: 0.7rem;
      background: rgba(8, 11, 19, 0.85);
      color: #d7e3ff;
      font: 0.8rem/1.5 "IBM Plex Mono", ui-monospace, monospace;
      overflow: auto;
      white-space: pre;
    }
    @media (max-width: 980px) {
      .layout { grid-template-columns: 1fr; }
      .item .grid, .item .grid.two, .form-grid { grid-template-columns: 1fr; }
      .token input { width: 210px; }
    }
    @keyframes rise {
      from { opacity: 0; transform: translateY(9px); }
      to { opacity: 1; transform: translateY(0); }
    }
  </style>
</head>
<body class="gridfx">
  <div class="wrap">
    <section class="hero">
      <div style="display:flex;align-items:center;gap:.7rem;flex-wrap:wrap;">
        <h1>Proxy Control Plane</h1>
        <span class="badge">Native Proxy Builder</span>
        <span class="badge">Subdomain Router</span>
        <span id="saveStatus" class="status">Ready</span>
      </div>
      <p>Configure un reverse proxy natif via interface web: upstreams, subdomaines, policies, headers et timeouts.</p>
      <div class="toolbar">
        <label class="token">
          <span>Bearer</span>
          <input id="tokenInput" value="dev-admin-token" />
        </label>
        <button id="reloadBtn" class="btn">Reload</button>
        <button id="saveBtn" class="btn primary">Save Config</button>
        <button id="resetBtn" class="btn warn">Reset Defaults</button>
        <button id="downloadBtn" class="btn">Download runtime plan</button>
      </div>
    </section>

    <section class="layout">
      <div style="display:grid;gap:1rem;">
        <article class="card">
          <h2>Global Settings</h2>
          <div class="form-grid">
            <label>Worker Processes <input id="g_workerProcesses" /></label>
            <label>HTTP Port <input id="g_httpPort" type="number" min="1" max="65535" /></label>
            <label>HTTPS Port <input id="g_httpsPort" type="number" min="1" max="65535" /></label>
            <label>Client Max Body (MB) <input id="g_clientMaxBodyMb" type="number" min="1" max="8192" /></label>
          </div>
          <div class="switches">
            <label class="switch"><input id="g_enableHttp2" type="checkbox" />HTTP/2</label>
            <label class="switch"><input id="g_enableGzip" type="checkbox" />Gzip</label>
            <label class="switch"><input id="g_proxyBuffering" type="checkbox" />Proxy Buffering</label>
          </div>
        </article>

        <article class="card">
          <div style="display:flex;justify-content:space-between;align-items:center;">
            <h2 style="margin:0;">Upstreams</h2>
            <button id="addUpstreamBtn" class="btn">+ Add Upstream</button>
          </div>
          <p class="muted">Un upstream = une cible applicative (host/port/protocol).</p>
          <div id="upstreamsList" class="list"></div>
        </article>

        <article class="card">
          <div style="display:flex;justify-content:space-between;align-items:center;">
            <h2 style="margin:0;">Subdomains & Routes</h2>
            <button id="addSubdomainBtn" class="btn">+ Add Route</button>
          </div>
          <p class="muted">Associe un host (ex: <code>api.example.com</code>) + path prefix vers un upstream.</p>
          <div id="subdomainsList" class="list"></div>
        </article>
      </div>

      <aside class="card">
        <h2>Runtime Preview</h2>
        <p class="muted">Preview auto genere cote serveur apres chaque reload/save.</p>
        <div id="runtimePreview" class="preview"></div>
      </aside>
    </section>
  </div>
)HTML"
R"HTML(
  <script>
    const state = {
      config: { global: {}, upstreams: [], subdomains: [] },
      runtimePlan: ""
    };

    const el = (id) => document.getElementById(id);
    const authHeader = () => ({ "Authorization": "Bearer " + (el("tokenInput").value || "").trim() });

    const setStatus = (text, ok = true) => {
      const node = el("saveStatus");
      node.textContent = text;
      node.className = "status " + (ok ? "ok" : "err");
    };

    const safeNum = (value, fallback) => {
      const n = Number(value);
      return Number.isFinite(n) ? n : fallback;
    };

    const parseHeadersText = (raw) => {
      const lines = (raw || "").split(/\r?\n/);
      const headers = [];
      for (const line of lines) {
        const trimmed = line.trim();
        if (!trimmed) continue;
        const idx = trimmed.indexOf(":");
        if (idx <= 0) continue;
        headers.push({ key: trimmed.slice(0, idx).trim(), value: trimmed.slice(idx + 1).trim() });
      }
      return headers;
    };

    const headersToText = (headers) => (headers || []).map(h => `${h.key}: ${h.value}`).join("\n");

    const getCurrentConfigFromForm = () => {
      const cfg = structuredClone(state.config);
      cfg.global.workerProcesses = el("g_workerProcesses").value.trim() || "auto";
      cfg.global.httpPort = safeNum(el("g_httpPort").value, 80);
      cfg.global.httpsPort = safeNum(el("g_httpsPort").value, 443);
      cfg.global.clientMaxBodyMb = safeNum(el("g_clientMaxBodyMb").value, 100);
      cfg.global.enableHttp2 = el("g_enableHttp2").checked;
      cfg.global.enableGzip = el("g_enableGzip").checked;
      cfg.global.proxyBuffering = el("g_proxyBuffering").checked;

      cfg.upstreams = Array.from(document.querySelectorAll("[data-upstream-item]")).map((node, idx) => ({
        id: node.querySelector("[data-field='id']").value.trim() || `upstream_${idx + 1}`,
        protocol: node.querySelector("[data-field='protocol']").value,
        host: node.querySelector("[data-field='host']").value.trim() || "127.0.0.1",
        port: safeNum(node.querySelector("[data-field='port']").value, 80),
        connectTimeoutMs: safeNum(node.querySelector("[data-field='connectTimeoutMs']").value, 3000),
        readTimeoutMs: safeNum(node.querySelector("[data-field='readTimeoutMs']").value, 30000),
        active: node.querySelector("[data-field='active']").checked
      }));

      cfg.subdomains = Array.from(document.querySelectorAll("[data-subdomain-item]")).map(node => ({
        host: node.querySelector("[data-field='host']").value.trim().toLowerCase(),
        upstreamId: node.querySelector("[data-field='upstreamId']").value.trim(),
        pathPrefix: node.querySelector("[data-field='pathPrefix']").value.trim() || "/",
        tls: node.querySelector("[data-field='tls']").checked,
        websocket: node.querySelector("[data-field='websocket']").checked,
        forceHttps: node.querySelector("[data-field='forceHttps']").checked,
        stripPrefix: node.querySelector("[data-field='stripPrefix']").checked,
        rateLimitRpm: safeNum(node.querySelector("[data-field='rateLimitRpm']").value, 0),
        accessPolicy: node.querySelector("[data-field='accessPolicy']").value,
        headers: parseHeadersText(node.querySelector("[data-field='headers']").value)
      }));

      return cfg;
    };

    const fillGlobalForm = (cfg) => {
      const g = cfg.global || {};
      el("g_workerProcesses").value = g.workerProcesses ?? "auto";
      el("g_httpPort").value = g.httpPort ?? 80;
      el("g_httpsPort").value = g.httpsPort ?? 443;
      el("g_clientMaxBodyMb").value = g.clientMaxBodyMb ?? 100;
      el("g_enableHttp2").checked = !!g.enableHttp2;
      el("g_enableGzip").checked = !!g.enableGzip;
      el("g_proxyBuffering").checked = !!g.proxyBuffering;
    };

    const upstreamOptionsHtml = (selected = "") => {
      return state.config.upstreams.map(up => `<option value="${up.id}" ${up.id === selected ? "selected" : ""}>${up.id}</option>`).join("");
    };

    const renderUpstreams = () => {
      const root = el("upstreamsList");
      root.innerHTML = "";
      state.config.upstreams.forEach((upstream, idx) => {
        const item = document.createElement("div");
        item.className = "item";
        item.dataset.upstreamItem = "1";
        item.innerHTML = `
          <div class="item-head">
            <b>Upstream #${idx + 1}</b>
            <button class="btn danger" data-remove-upstream="${idx}">Delete</button>
          </div>
          <div class="grid">
            <label>ID <input data-field="id" value="${upstream.id ?? ""}" /></label>
            <label>Protocol
              <select data-field="protocol">
                <option value="http" ${(upstream.protocol ?? "http") === "http" ? "selected" : ""}>http</option>
                <option value="https" ${(upstream.protocol ?? "http") === "https" ? "selected" : ""}>https</option>
              </select>
            </label>
            <label>Host <input data-field="host" value="${upstream.host ?? "127.0.0.1"}" /></label>
            <label>Port <input data-field="port" type="number" value="${upstream.port ?? 80}" /></label>
            <label>Connect Timeout (ms) <input data-field="connectTimeoutMs" type="number" value="${upstream.connectTimeoutMs ?? 3000}" /></label>
            <label>Read Timeout (ms) <input data-field="readTimeoutMs" type="number" value="${upstream.readTimeoutMs ?? 30000}" /></label>
          </div>
          <div class="switches"><label class="switch"><input data-field="active" type="checkbox" ${upstream.active !== false ? "checked" : ""} />Active</label></div>
        `;
        root.appendChild(item);
      });

      root.querySelectorAll("[data-remove-upstream]").forEach(btn => {
        btn.addEventListener("click", () => {
          const idx = Number(btn.dataset.removeUpstream);
          state.config.upstreams.splice(idx, 1);
          renderAll();
        });
      });
    };

    const renderSubdomains = () => {
      const root = el("subdomainsList");
      root.innerHTML = "";
      state.config.subdomains.forEach((route, idx) => {
        const item = document.createElement("div");
        item.className = "item";
        item.dataset.subdomainItem = "1";
        item.innerHTML = `
          <div class="item-head">
            <b>Route #${idx + 1}</b>
            <button class="btn danger" data-remove-subdomain="${idx}">Delete</button>
          </div>
          <div class="grid two">
            <label>Host/Subdomain <input data-field="host" value="${route.host ?? ""}" placeholder="api.example.com" /></label>
            <label>Upstream
              <select data-field="upstreamId">${upstreamOptionsHtml(route.upstreamId ?? "")}</select>
            </label>
          </div>
          <div class="grid">
            <label>Path Prefix <input data-field="pathPrefix" value="${route.pathPrefix ?? "/"}" /></label>
            <label>Rate Limit (req/min) <input data-field="rateLimitRpm" type="number" min="0" value="${route.rateLimitRpm ?? 0}" /></label>
            <label>Access Policy
              <select data-field="accessPolicy">
                <option value="public" ${(route.accessPolicy ?? "public") === "public" ? "selected" : ""}>public</option>
                <option value="basic" ${(route.accessPolicy ?? "public") === "basic" ? "selected" : ""}>basic</option>
                <option value="jwt" ${(route.accessPolicy ?? "public") === "jwt" ? "selected" : ""}>jwt (placeholder)</option>
              </select>
            </label>
          </div>
          <div class="switches">
            <label class="switch"><input data-field="tls" type="checkbox" ${route.tls ? "checked" : ""} />TLS</label>
            <label class="switch"><input data-field="forceHttps" type="checkbox" ${route.forceHttps ? "checked" : ""} />Force HTTPS</label>
            <label class="switch"><input data-field="websocket" type="checkbox" ${route.websocket !== false ? "checked" : ""} />WebSocket</label>
            <label class="switch"><input data-field="stripPrefix" type="checkbox" ${route.stripPrefix ? "checked" : ""} />Strip Prefix</label>
          </div>
          <label style="margin-top:.5rem;">Extra Proxy Headers (one per line: <code>Header: value</code>)
            <textarea data-field="headers" placeholder="X-Request-Origin: proxy-control">${headersToText(route.headers)}</textarea>
          </label>
        `;
        root.appendChild(item);
      });

      root.querySelectorAll("[data-remove-subdomain]").forEach(btn => {
        btn.addEventListener("click", () => {
          const idx = Number(btn.dataset.removeSubdomain);
          state.config.subdomains.splice(idx, 1);
          renderAll();
        });
      });
    };

    const renderPreview = () => {
      el("runtimePreview").textContent = state.runtimePlan || "# runtime preview unavailable";
    };

    const renderAll = () => {
      fillGlobalForm(state.config);
      renderUpstreams();
      renderSubdomains();
      renderPreview();
    };

    const fetchConfig = async () => {
      setStatus("Loading...", true);
      const response = await fetch("/api/v1/admin/proxy/config", { headers: authHeader() });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const payload = await response.json();
      if (!payload.ok) throw new Error(payload.error || "Invalid server response");
      state.config = payload.config;
      state.runtimePlan = payload.runtimePlan || "";
      renderAll();
      setStatus("Loaded", true);
    };

    const saveConfig = async () => {
      try {
        setStatus("Saving...", true);
        const config = getCurrentConfigFromForm();
        const response = await fetch("/api/v1/admin/proxy/config", {
          method: "POST",
          headers: { "Content-Type": "application/json", ...authHeader() },
          body: JSON.stringify({ config })
        });
        const payload = await response.json();
        if (!response.ok || !payload.ok) {
          throw new Error(payload.error || `HTTP ${response.status}`);
        }
        state.config = payload.config;
        state.runtimePlan = payload.runtimePlan || "";
        renderAll();
        setStatus("Saved", true);
      } catch (error) {
        setStatus(`Save failed: ${error.message}`, false);
      }
    };

    const resetConfig = async () => {
      try {
        setStatus("Resetting...", true);
        const response = await fetch("/api/v1/admin/proxy/reset", {
          method: "POST",
          headers: authHeader()
        });
        const payload = await response.json();
        if (!response.ok || !payload.ok) {
          throw new Error(payload.error || `HTTP ${response.status}`);
        }
        state.config = payload.config;
        state.runtimePlan = payload.runtimePlan || "";
        renderAll();
        setStatus("Defaults restored", true);
      } catch (error) {
        setStatus(`Reset failed: ${error.message}`, false);
      }
    };

    const downloadRuntimePlan = async () => {
      try {
        const response = await fetch("/api/v1/admin/proxy/runtime.txt", { headers: authHeader() });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const text = await response.text();
        const blob = new Blob([text], { type: "text/plain;charset=utf-8" });
        const href = URL.createObjectURL(blob);
        const a = document.createElement("a");
        a.href = href;
        a.download = "proxy.runtime.txt";
        document.body.appendChild(a);
        a.click();
        a.remove();
        URL.revokeObjectURL(href);
      } catch (error) {
        setStatus(`Download failed: ${error.message}`, false);
      }
    };

    el("addUpstreamBtn").addEventListener("click", () => {
      state.config.upstreams.push({
        id: `upstream_${state.config.upstreams.length + 1}`,
        protocol: "http",
        host: "127.0.0.1",
        port: 8080,
        connectTimeoutMs: 3000,
        readTimeoutMs: 30000,
        active: true
      });
      renderAll();
    });

    el("addSubdomainBtn").addEventListener("click", () => {
      const firstUpstream = state.config.upstreams[0]?.id || "";
      state.config.subdomains.push({
        host: "",
        upstreamId: firstUpstream,
        pathPrefix: "/",
        tls: false,
        websocket: true,
        forceHttps: false,
        stripPrefix: false,
        rateLimitRpm: 0,
        accessPolicy: "public",
        headers: []
      });
      renderAll();
    });

    el("reloadBtn").addEventListener("click", () => fetchConfig().catch(err => setStatus(`Reload failed: ${err.message}`, false)));
    el("saveBtn").addEventListener("click", saveConfig);
    el("resetBtn").addEventListener("click", resetConfig);
    el("downloadBtn").addEventListener("click", downloadRuntimePlan);

    fetchConfig().catch(err => setStatus(`Initial load failed: ${err.message}`, false));
  </script>
</body>
</html>
)HTML";

    const SwString indexPath = joinPath_(absPublicRoot, "index.html");
    if (!ensureFileWithDefaultContent_(indexPath, indexHtml)) {
        swError() << "[HttpAppQuickstart] Failed to initialize index.html at: " << indexPath;
        return 1;
    }

    const SwString proxyDashboardPath = joinPath_(absPublicRoot, "proxy-admin.html");
    if (!ensureFileWithDefaultContent_(proxyDashboardPath, proxyDashboardHtml)) {
        swError() << "[HttpAppQuickstart] Failed to initialize proxy-admin.html at: " << proxyDashboardPath;
        return 1;
    }

    SwHttpApp httpApp(nullptr);

    ProxyControlState proxyState;
    proxyState.config = defaultProxyControlConfig_();
    proxyState.configFilePath = joinPath_(absProxyControlRoot, "proxy-config.json");
    proxyState.generatedRuntimePath = joinPath_(absProxyControlRoot, "proxy.runtime.txt");
    proxyState.updatedAt = SwDateTime().toString();

    {
        ProxyControlConfig loadedConfig;
        SwString loadError;
        if (loadProxyConfig_(proxyState.configFilePath, loadedConfig, loadError)) {
            proxyState.config = loadedConfig;
            swDebug() << "[HttpAppQuickstart] Proxy config loaded from: " << proxyState.configFilePath;
        } else {
            swDebug() << "[HttpAppQuickstart] Proxy config init with defaults: " << loadError;
            SwString saveError;
            if (!saveProxyConfig_(proxyState.configFilePath, proxyState.config, saveError)) {
                swDebug() << "[HttpAppQuickstart] Proxy default config save failed: " << saveError;
            }
        }

        const SwString initialRuntimePlan = buildProxyRuntimePlan_(proxyState.config);
        if (!writeTextFile_(proxyState.generatedRuntimePath, initialRuntimePlan)) {
            swDebug() << "[HttpAppQuickstart] Failed to write initial proxy runtime plan: " << proxyState.generatedRuntimePath;
        }
    }

    SwHttpLimits limits;
    limits.maxBodyBytes = 64 * 1024 * 1024;
    limits.maxChunkSize = 8 * 1024 * 1024;
    limits.maxConnections = 4096;
    limits.maxInFlightRequests = 2048;
    limits.maxThreadPoolQueuedDispatches = 4096;
    limits.maxMultipartParts = 128;
    limits.maxMultipartPartHeadersBytes = 16 * 1024;
    limits.maxMultipartFieldBytes = 2 * 1024 * 1024;
    limits.enableMultipartFileStreaming = true;
    limits.multipartTempDirectory = absMultipartTempRoot;
    httpApp.setLimits(limits);

    SwThreadPool requestPool;
    if (workersArg > 0) {
        requestPool.setMaxThreadCount(workersArg);
    }
    if (useThreadPool) {
        httpApp.setThreadPool(&requestPool);
        httpApp.setDispatchMode(SwHttpServer::DispatchMode::ThreadPool);
    }
    httpApp.setTrailingSlashPolicy(SwHttpRouter::TrailingSlashPolicy::RedirectToNoSlash);

    SwHttpRateLimiter apiRateLimiter;
    SwHttpMetricsCollector middlewareMetrics;
    SwHttpMiddlewarePack::InstallOptions middlewareOptions;
    middlewareOptions.enableAuth = true;
    middlewareOptions.enableRecoveryHandler = true;
    middlewareOptions.rateLimit.maxRequests = 240;
    middlewareOptions.rateLimit.windowMs = 60 * 1000;
    middlewareOptions.rateLimit.clientKeyHeader = "x-forwarded-for";
    middlewareOptions.cors.allowCredentials = false;
    middlewareOptions.auth.authorize = [](SwHttpContext& context) {
        const SwString path = swHttpNormalizePath(context.path());
        if (!path.startsWith("/api/v1/admin")) {
            return true;
        }
        const SwString auth = context.headerValue("authorization", SwString()).trimmed();
        return auth == "Bearer dev-admin-token";
    };
    middlewareOptions.auth.onReject = [](SwHttpContext& context) {
        context.text("Unauthorized (admin token required)", 401);
    };
    SwHttpMiddlewarePack::install(httpApp, middlewareOptions, &apiRateLimiter, &middlewareMetrics);

    httpApp.use("/api", [](SwHttpContext& context, const SwHttpApp::SwHttpNext& next) {
        context.response().headers["x-server"] = "SwHttpAppQuickstart";
        if (next) {
            next();
        }
    });

    httpApp.get("/", [](SwHttpContext& context) {
        context.redirect("/public/index.html");
    });

    httpApp.group("/api/v1", [absUploadRoot](SwHttpApp& api) {
        SwHttpApp* appRef = &api;

        SwHttpApp::SwHttpRouteOptions healthRoute;
        healthRoute.name = "api.health";
        api.get("/health", [](SwHttpContext& context) {
            SwJsonObject payload;
            payload["ok"] = true;
            payload["service"] = "SwHttpAppQuickstart";
            payload["time"] = SwDateTime().toString();
            context.json(SwJsonDocument(payload), 200);
        }, healthRoute);

        SwHttpApp::SwHttpRouteOptions userRoute;
        userRoute.name = "api.user.show";
        api.get("/users/:id(int)", [](SwHttpContext& context) {
            const SwString userId = context.pathValue("id", "0");
            const SwString verbose = context.queryValue("verbose", "0");

            SwJsonObject payload;
            payload["id"] = userId.toStdString();
            payload["verbose"] = (verbose == "1");
            payload["hint"] = "Use POST /api/v1/echo or POST /api/v1/upload";
            context.json(SwJsonDocument(payload), 200);
        }, userRoute);

        SwHttpApp::SwHttpRouteOptions routesRoute;
        routesRoute.name = "api.routes";
        api.get("/routes", [appRef](SwHttpContext& context) {
            SwMap<SwString, SwString> userParams;
            userParams["id"] = "42";
            SwMap<SwString, SwString> userQuery;
            userQuery["verbose"] = "1";

            const SwString healthUrl = appRef->urlFor("api.health");
            const SwString userUrl = appRef->urlFor("api.user.show", userParams, userQuery);

            SwJsonObject payload;
            payload["ok"] = true;
            payload["healthUrl"] = healthUrl.toStdString();
            payload["userUrl"] = userUrl.toStdString();
            payload["metricsRouteRegistered"] = appRef->hasRouteName("api.admin.metrics");
            context.json(SwJsonDocument(payload), 200);
        }, routesRoute);

        SwHttpApp::SwHttpRouteOptions echoRoute;
        echoRoute.name = "api.echo";
        api.post("/echo", [](SwHttpContext& context) {
            const SwString bodyText = context.bodyText();
            context.send(bodyText, "text/plain; charset=utf-8", 200);
        }, echoRoute);

        SwHttpApp::SwHttpRouteOptions chunkedRoute;
        chunkedRoute.name = "api.chunked";
        api.get("/chunked", [](SwHttpContext& context) {
            SwHttpResponse& response = context.response();
            response.status = 200;
            response.reason = swHttpStatusReason(200);
            response.headers["content-type"] = "text/plain; charset=utf-8";
            response.useChunkedTransfer = true;
            response.chunkedParts.append(SwByteArray("chunk-1\n"));
            response.chunkedParts.append(SwByteArray("chunk-2\n"));
            response.chunkedParts.append(SwByteArray("chunk-3\n"));
            context.setHandled(true);
        }, chunkedRoute);

        SwHttpApp::SwHttpRouteOptions slowRoute;
        slowRoute.name = "api.slow";
        slowRoute.softTimeoutMs = 250;
        slowRoute.timeoutOverridesResponse = true;
        api.get("/slow", [](SwHttpContext& context) {
            int delayMs = 350;
            bool ok = false;
            const int fromQuery = context.queryValue("ms", "350").toInt(&ok);
            if (ok && fromQuery > 0 && fromQuery < 10000) {
                delayMs = fromQuery;
            }
            SwEventLoop::swsleep(delayMs);
            context.text("slow-handler-done:" + SwString::number(delayMs), 200);
        }, slowRoute);

        SwHttpApp::SwHttpRouteOptions uploadRoute;
        uploadRoute.name = "api.upload";
        api.post("/upload", [absUploadRoot](SwHttpContext& context) {
            const SwHttpRequest& request = context.request();
            if (!request.isMultipartFormData) {
                context.text("Expected multipart/form-data", 400);
                return;
            }

            int storedCount = 0;
            long long totalBytes = 0;
            SwString storedNames;
            const SwString meta = request.formFields.value("meta", SwString());
            const long long stamp = static_cast<long long>(SwDateTime().toTimeT());

            for (std::size_t i = 0; i < request.multipartParts.size(); ++i) {
                const SwHttpRequest::MultipartPart& part = request.multipartParts[i];
                if (!part.isFile) {
                    continue;
                }

                SwString baseName = sanitizeFileName_(part.fileName);
                if (baseName.isEmpty()) {
                    baseName = "upload.bin";
                }

                const SwString uniqueName =
                    SwString::number(stamp) + "_" +
                    SwString::number(static_cast<long long>(storedCount)) + "_" +
                    baseName;

                const SwString absolutePath = joinPath_(absUploadRoot, uniqueName);
                if (part.storedOnDisk && !part.tempFilePath.isEmpty()) {
                    if (!swFilePlatform().copy(part.tempFilePath, absolutePath, true)) {
                        context.text("Unable to persist streamed upload file", 500);
                        return;
                    }
                    totalBytes += static_cast<long long>(part.sizeBytes);
                } else {
                    SwFile outFile(absolutePath);
                    if (!outFile.openBinary(SwFile::Write)) {
                        context.text("Unable to open upload file on disk", 500);
                        return;
                    }
                    if (!outFile.write(part.data)) {
                        outFile.close();
                        context.text("Unable to write upload file on disk", 500);
                        return;
                    }
                    outFile.close();
                    totalBytes += static_cast<long long>(part.data.size());
                }

                ++storedCount;
                if (!storedNames.isEmpty()) {
                    storedNames += ",";
                }
                storedNames += uniqueName;
            }

            if (storedCount == 0) {
                context.text("No file part found in multipart payload", 400);
                return;
            }

            SwJsonObject payload;
            payload["ok"] = true;
            payload["files"] = storedCount;
            payload["bytes"] = totalBytes;
            payload["meta"] = meta.toStdString();
            payload["stored"] = storedNames.toStdString();
            payload["downloadPrefix"] = "/uploads/";
            context.json(SwJsonDocument(payload), 200);
        }, uploadRoute);
    });

    SwHttpApp::SwHttpRouteOptions adminMetricsRoute;
    adminMetricsRoute.name = "api.admin.metrics";
    httpApp.get("/api/v1/admin/metrics", [&httpApp, &middlewareMetrics](SwHttpContext& context) {
        const SwHttpServerMetrics serverMetrics = httpApp.server().metricsSnapshot();
        const SwHttpMiddlewareMetrics middlewareSnapshot = middlewareMetrics.snapshot();

        SwJsonObject payload;
        payload["ok"] = true;
        payload["inFlight"] = serverMetrics.inFlightRequests;
        payload["totalRequests"] = serverMetrics.totalRequests;
        payload["totalResponses"] = serverMetrics.totalResponses;
        payload["totalLatencyMs"] = serverMetrics.totalLatencyMs;
        payload["maxLatencyMs"] = serverMetrics.maxLatencyMs;
        payload["rejectedConnections"] = serverMetrics.rejectedConnections;
        payload["rejectedInFlight"] = serverMetrics.rejectedInFlight;
        payload["rejectedThreadPoolSaturation"] = serverMetrics.rejectedThreadPoolSaturation;
        payload["middlewareRequests"] = middlewareSnapshot.totalRequests;
        payload["middlewareLatencyMs"] = middlewareSnapshot.totalLatencyMs;
        payload["middlewareMaxLatencyMs"] = middlewareSnapshot.maxLatencyMs;
        payload["middlewareRequestBytes"] = middlewareSnapshot.totalRequestBodyBytes;
        payload["middlewareResponseBytes"] = middlewareSnapshot.totalResponseBodyBytes;
        context.json(SwJsonDocument(payload), 200);
    }, adminMetricsRoute);

    SwHttpApp::SwHttpRouteOptions proxyConfigGetRoute;
    proxyConfigGetRoute.name = "api.admin.proxy.config.get";
    httpApp.get("/api/v1/admin/proxy/config", [&proxyState](SwHttpContext& context) {
        ProxyControlConfig snapshot;
        SwString updatedAt;
        {
            SwMutexLocker locker(&proxyState.mutex);
            snapshot = proxyState.config;
            updatedAt = proxyState.updatedAt;
        }

        const SwString runtimePlan = buildProxyRuntimePlan_(snapshot);
        SwJsonObject payload;
        payload["ok"] = true;
        payload["updatedAt"] = updatedAt.toStdString();
        payload["config"] = proxyConfigToJsonObject_(snapshot);
        payload["runtimePlan"] = runtimePlan.toStdString();
        context.json(SwJsonDocument(payload), 200);
    }, proxyConfigGetRoute);

    SwHttpApp::SwHttpRouteOptions proxyConfigPostRoute;
    proxyConfigPostRoute.name = "api.admin.proxy.config.update";
    httpApp.post("/api/v1/admin/proxy/config", [&proxyState](SwHttpContext& context) {
        SwJsonDocument requestDocument;
        SwString parseError;
        if (!context.parseJsonBody(requestDocument, parseError)) {
            context.text("Invalid JSON payload", 400);
            return;
        }
        if (!requestDocument.isObject()) {
            context.text("JSON root must be an object", 400);
            return;
        }

        SwJsonObject root = requestDocument.object();
        if (root.contains("config")) {
            const SwJsonValue configValue = root.value("config", SwJsonValue());
            if (!configValue.isObject()) {
                context.text("config must be an object", 400);
                return;
            }
            root = configValue.toObject();
        }

        ProxyControlConfig incomingConfig;
        SwString configError;
        if (!proxyConfigFromJsonObject_(root, incomingConfig, configError)) {
            context.text("Invalid proxy config: " + configError, 400);
            return;
        }

        const SwString runtimePlan = buildProxyRuntimePlan_(incomingConfig);
        SwString saveError;
        if (!saveProxyConfig_(proxyState.configFilePath, incomingConfig, saveError)) {
            context.text("Failed to save proxy config: " + saveError, 500);
            return;
        }
        if (!writeTextFile_(proxyState.generatedRuntimePath, runtimePlan)) {
            context.text("Failed to write generated runtime plan", 500);
            return;
        }

        const SwString updatedAt = SwDateTime().toString();
        {
            SwMutexLocker locker(&proxyState.mutex);
            proxyState.config = incomingConfig;
            proxyState.updatedAt = updatedAt;
        }

        SwJsonObject payload;
        payload["ok"] = true;
        payload["updatedAt"] = updatedAt.toStdString();
        payload["config"] = proxyConfigToJsonObject_(incomingConfig);
        payload["runtimePlan"] = runtimePlan.toStdString();
        context.json(SwJsonDocument(payload), 200);
    }, proxyConfigPostRoute);

    SwHttpApp::SwHttpRouteOptions proxyResetRoute;
    proxyResetRoute.name = "api.admin.proxy.reset";
    httpApp.post("/api/v1/admin/proxy/reset", [&proxyState](SwHttpContext& context) {
        ProxyControlConfig defaults = defaultProxyControlConfig_();
        const SwString runtimePlan = buildProxyRuntimePlan_(defaults);

        SwString saveError;
        if (!saveProxyConfig_(proxyState.configFilePath, defaults, saveError)) {
            context.text("Failed to save default proxy config: " + saveError, 500);
            return;
        }
        if (!writeTextFile_(proxyState.generatedRuntimePath, runtimePlan)) {
            context.text("Failed to write generated runtime plan", 500);
            return;
        }

        const SwString updatedAt = SwDateTime().toString();
        {
            SwMutexLocker locker(&proxyState.mutex);
            proxyState.config = defaults;
            proxyState.updatedAt = updatedAt;
        }

        SwJsonObject payload;
        payload["ok"] = true;
        payload["updatedAt"] = updatedAt.toStdString();
        payload["config"] = proxyConfigToJsonObject_(defaults);
        payload["runtimePlan"] = runtimePlan.toStdString();
        context.json(SwJsonDocument(payload), 200);
    }, proxyResetRoute);

    SwHttpApp::SwHttpRouteOptions proxyRuntimeRoute;
    proxyRuntimeRoute.name = "api.admin.proxy.runtime";
    httpApp.get("/api/v1/admin/proxy/runtime.txt", [&proxyState](SwHttpContext& context) {
        ProxyControlConfig snapshot;
        {
            SwMutexLocker locker(&proxyState.mutex);
            snapshot = proxyState.config;
        }

        const SwString runtimePlan = buildProxyRuntimePlan_(snapshot);
        context.send(runtimePlan, "text/plain; charset=utf-8", 200);
        context.setHeader("content-disposition", "attachment; filename=\"proxy.runtime.txt\"");
    }, proxyRuntimeRoute);

    SwHttpStaticOptions staticOptions;
    staticOptions.enableRange = true;
    staticOptions.ioChunkBytes = 64 * 1024;
    staticOptions.cacheControl = "public, max-age=60";
    httpApp.mountStatic("/public", absPublicRoot, staticOptions);

    SwHttpStaticOptions uploadStaticOptions;
    uploadStaticOptions.enableRange = true;
    uploadStaticOptions.ioChunkBytes = 64 * 1024;
    uploadStaticOptions.cacheControl = "no-store";
    httpApp.mountStatic("/uploads", absUploadRoot, uploadStaticOptions);

    httpApp.server().addNamedRouteAsync("proxy.runtime.catchall", "*", "/*",
                                        [&proxyState, limits, &httpApp](const SwHttpRequest& request,
                                                                        const SwHttpRouteResponder& done) {
        const SwString requestHost = proxyNormalizeHost_(request.headers.value("host", SwString()));
        const SwString requestPath = swHttpNormalizePath(request.path);

        ProxyControlConfig snapshot;
        {
            SwMutexLocker locker(&proxyState.mutex);
            snapshot = proxyState.config;
        }

        ProxyRouteSelection selection;
        SwString selectionReason;
        if (!proxySelectRoute_(snapshot, requestHost, requestPath, selection, selectionReason)) {
            SwJsonObject payload;
            payload["ok"] = false;
            payload["error"] = "proxy-route-not-found";
            payload["reason"] = selectionReason.toStdString();
            payload["host"] = requestHost.toStdString();
            payload["path"] = requestPath.toStdString();
            done(proxyBuildJsonResponse_(request, 404, payload));
            return;
        }

        if (selection.route.forceHttps) {
            const SwString forwardedProto = request.headers.value("x-forwarded-proto", "http").toLower();
            if (forwardedProto != "https") {
                const SwString location = "https://" + requestHost + request.target;
                done(proxyBuildRedirectResponse_(request, location, 308));
                return;
            }
        }

        int retryAfterSeconds = 0;
        if (!proxyConsumeRateLimit_(proxyState, selection.route, request, retryAfterSeconds)) {
            SwHttpResponse limited = proxyBuildTextResponse_(request, 429, "Proxy route rate limit exceeded");
            limited.headers["retry-after"] = SwString::number(retryAfterSeconds);
            done(limited);
            return;
        }

        const bool websocketUpgrade = proxyIsWebSocketUpgradeRequest_(request);
        if (websocketUpgrade) {
            if (!selection.route.websocket) {
                done(proxyBuildTextResponse_(request, 400, "WebSocket disabled for this route"));
                return;
            }

            ProxyWebSocketTunnelBridge* bridge = new ProxyWebSocketTunnelBridge(
                request,
                selection.route,
                selection.upstream,
                limits,
                [done, request](const SwHttpResponse& response, const SwString& error) {
                    if (!error.isEmpty()) {
                        done(proxyBuildTextResponse_(request, 502, "Bad gateway: " + error));
                        return;
                    }
                    done(response);
                },
                &httpApp.server());
            bridge->start();
            return;
        }

        ProxyHttpForwardJob* job = new ProxyHttpForwardJob(
            request,
            selection.route,
            selection.upstream,
            limits,
            [done, selection, request](const SwHttpResponse& response, const SwString& error) {
                if (!error.isEmpty()) {
                    done(proxyBuildTextResponse_(request, 502, "Bad gateway: " + error));
                    return;
                }
                SwHttpResponse proxied = response;
                proxied.headers["x-proxy-upstream"] =
                    selection.upstream.protocol + "://" + selection.upstream.host + ":" + SwString::number(selection.upstream.port);
                proxied.headers["x-proxy-route"] = selection.route.host + selection.route.pathPrefix;
                done(proxied);
            },
            &httpApp.server());
        job->start();
    });

    httpApp.setNotFoundHandler([](SwHttpContext& context) {
        SwJsonObject payload;
        payload["ok"] = false;
        payload["error"] = "not-found";
        payload["path"] = context.path().toStdString();
        context.json(SwJsonDocument(payload), 404);
    });

    if (!httpApp.listen(port)) {
        swError() << "[HttpAppQuickstart] Failed to listen on port " << static_cast<int>(port);
        return 1;
    }

    swDebug() << "[HttpAppQuickstart] Listening on 0.0.0.0:" << static_cast<int>(port);
    swDebug() << "[HttpAppQuickstart] Public root:  " << absPublicRoot;
    swDebug() << "[HttpAppQuickstart] Upload root:  " << absUploadRoot;
    swDebug() << "[HttpAppQuickstart] Proxy control root: " << absProxyControlRoot;
    swDebug() << "[HttpAppQuickstart] Proxy config file: " << proxyState.configFilePath;
    swDebug() << "[HttpAppQuickstart] Proxy runtime file: " << proxyState.generatedRuntimePath;
    swDebug() << "[HttpAppQuickstart] Dispatch mode: "
              << (useThreadPool ? SwString("ThreadPool") : SwString("Inline"));
    swDebug() << "[HttpAppQuickstart] Trailing slash policy: RedirectToNoSlash";
    swDebug() << "[HttpAppQuickstart] Admin token: Bearer dev-admin-token";
    if (useThreadPool) {
        swDebug() << "[HttpAppQuickstart] ThreadPool workers: " << requestPool.maxThreadCount();
    }
    swDebug() << "[HttpAppQuickstart] Try:";
    swDebug() << "  http://127.0.0.1:" << static_cast<int>(port) << "/public/index.html";
    swDebug() << "  http://127.0.0.1:" << static_cast<int>(port) << "/public/proxy-admin.html";
    swDebug() << "  http://127.0.0.1:" << static_cast<int>(port) << "/api/v1/health";
    swDebug() << "  http://127.0.0.1:" << static_cast<int>(port) << "/api/v1/routes";
    swDebug() << "  http://127.0.0.1:" << static_cast<int>(port) << "/api/v1/admin/metrics";
    swDebug() << "  http://127.0.0.1:" << static_cast<int>(port) << "/api/v1/admin/proxy/config";
    swDebug() << "  http://127.0.0.1:" << static_cast<int>(port) << "/api/v1/admin/proxy/runtime.txt";

    return app.exec();
}
