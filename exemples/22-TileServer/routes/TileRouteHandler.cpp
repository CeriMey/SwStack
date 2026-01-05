#include "TileRouteHandler.h"
#include "SwCoreApplication.h"
#include "../MapDatabase.h"
#include "../HttpSession.h"
#include "SwDebug.h"
#include <memory>
#include <chrono>
#include <cstdlib>
#include <sstream>

std::atomic<bool> TileRouteHandler::s_offlineMode{false};

static bool isNumeric(const SwString& s) {
    if (s.isEmpty()) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    return true;
}

void TileRouteHandler::handle(const HttpRequest& req, SwTcpSocket* socket) {
    if (!socket) return;
    swDebug() << "[TileRouteHandler] handling request path=" << req.path.toStdString();
    TileRequestContext ctx;
    if (!parsePath(req.path, ctx)) {
        swWarning() << "[TileRouteHandler] parsePath failed";
        sendError(socket, 400, SwString("Malformed path"), !req.keepAlive);
        return;
    }

    auto startTime = std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now());
    auto logElapsed = [startTime](const SwString& phase) {
        if (!startTime) return;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - *startTime).count();
        swDebug() << "[TileRouteHandler] " << phase.toStdString() << " after " << elapsed << " ms";
    };
    swDebug() << "[TileRouteHandler] request start path=" << req.path.toStdString();

    SwString relPath = SwString("/") + SwString::number(ctx.z) + "/" + SwString::number(ctx.x) + "/" + SwString::number(ctx.y) + ".png";
    SwString cachePath = buildCacheFilePath(ctx, relPath);
    swDebug() << "[TileRouteHandler] cache target resolved to " << cachePath.toStdString();
    SwString cached;
    if (readCacheFile(cachePath, cached) && !cached.isEmpty()) {
        logElapsed(SwString("cache hit"));
        sendTile(socket, cached, SwString("cache"), !req.keepAlive);
        return;
    }
    logElapsed(SwString("cache miss"));

    if (!ctx.mapKnown) {
        swWarning() << "[TileRouteHandler] map not found: " << ctx.mapName.toStdString();
        sendError(socket, 404, SwString("Map Not Found"), !req.keepAlive);
        return;
    }

    SwString url = buildUpstreamUrl(ctx, relPath);
    swDebug() << "[TileRouteHandler] fetching upstream url=" << url.toStdString();
    SwNetworkAccessManager* fetcher = new SwNetworkAccessManager(nullptr);
    auto socketAlive = std::make_shared<bool>(true);
    auto respondDirect = std::make_shared<bool>(true);
    if (auto* session = dynamic_cast<HttpSession*>(socket->parent())) {
        session->addCleanupHook([socketAlive]() {
            *socketAlive = false;
        });
    }
    connect(socket, SIGNAL(destroyed), [socketAlive, fetcher]() {
        *socketAlive = false;
        fetcher->abort();
        fetcher->deleteLater();
    });
    connect(socket, SIGNAL(disconnected), [socketAlive, fetcher]() {
        *socketAlive = false;
        fetcher->abort();
        fetcher->deleteLater();
    });
    fetcher->setRawHeader("User-Agent", "SwTileServer/1.0");

    const bool keepAlive = req.keepAlive;
    SwTimer* fallbackTimer = new SwTimer(fetcher);
    fallbackTimer->setSingleShot(true);
    fallbackTimer->start(1000);
    swDebug() << "[TileRouteHandler] upstream watchdog started (1000ms window)";
    connect(fallbackTimer, SIGNAL(timeout), [this, socket, socketAlive, respondDirect, keepAlive, fallbackTimer, logElapsed]() {
        if (fallbackTimer) {
            fallbackTimer->stop();
        }
        if (!socket || !(*socketAlive) || !(*respondDirect)) {
            return;
        }
        *respondDirect = false;
        logElapsed(SwString("upstream pending >100ms, returning queued response"));
        sendError(socket, 504, SwString("Tile queued"), true);
    });

    if (s_offlineMode.load(std::memory_order_relaxed)) {
        swWarning() << "[TileRouteHandler] offline mode active, serving stale response while probing upstream";
        *respondDirect = false;
        sendError(socket, 503, SwString("Offline mode"), !keepAlive);
    }

    connect(fetcher, SIGNAL(finished), [this, socket, cachePath, socketAlive, fetcher, keepAlive, fallbackTimer, respondDirect, logElapsed](const SwByteArray& payload) {
        if (fallbackTimer) {
            fallbackTimer->stop();
            fallbackTimer->deleteLater();
        }
        SwString payloadString(payload);
        SwString cleaned = stripHttpEnvelope(payloadString);
        logElapsed(SwString("upstream fetch finished, size=").append(SwString::number(static_cast<int>(cleaned.size()))));
        s_offlineMode.store(false, std::memory_order_relaxed);
        if (!cachePath.isEmpty() && ensureCacheDirectory(cachePath)) {
            if (writeCacheFile(cachePath, cleaned)) {
                swDebug() << "[TileRouteHandler] cached tile to " << cachePath.toStdString();
            } else {
                swWarning() << "[TileRouteHandler] failed to write cache " << cachePath.toStdString();
            }
        } else {
            swWarning() << "[TileRouteHandler] ensureCacheDirectory failed for " << cachePath.toStdString();
        }
        bool shouldRespond = (*respondDirect && socket && *socketAlive);
        if (shouldRespond) {
            *respondDirect = false;
            sendTile(socket, cleaned, SwString("upstream"), !keepAlive);
        } else {
            swDebug() << "[TileRouteHandler] upstream fetch completed asynchronously; tile ready in cache";
        }
        fetcher->abort();
        fetcher->deleteLater();
    });
    connect(fetcher, SIGNAL(errorOccurred), [this, socket, socketAlive, fetcher, keepAlive, fallbackTimer, respondDirect, logElapsed](int err) {
        if (fallbackTimer) {
            fallbackTimer->stop();
            fallbackTimer->deleteLater();
        }
        logElapsed(SwString("upstream fetch error err=").append(SwString::number(static_cast<int>(err))));
        if (err <= 0) {
            s_offlineMode.store(true, std::memory_order_relaxed);
        }
        bool shouldRespond = (*respondDirect && socket && *socketAlive);
        if (shouldRespond) {
            *respondDirect = false;
            sendError(socket, 502, SwString("Bad Gateway"), !keepAlive);
        } else {
            swWarning() << "[TileRouteHandler] async fetch error occurred after fallback response";
        }
        fetcher->abort();
        fetcher->deleteLater();
    });

    logElapsed(SwString("upstream fetch start"));
    fetcher->get(url);
}

bool TileRouteHandler::parsePath(const SwString& path, TileRequestContext& outCtx) const {
    SwString clean = path;
    int q = clean.indexOf("?");
    if (q >= 0) clean = clean.left(q);
    while (clean.contains("//")) clean.replace("//", "/");
    if (clean.startsWith("/")) clean = clean.mid(1);
    if (!clean.startsWith("map/")) return false;
    clean = clean.mid(4); // remove "map/"

    SwList<SwString> parts = clean.split('/');
    SwList<SwString> segments;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (!parts[i].isEmpty()) segments.append(parts[i]);
    }
    swDebug() << "[TileRouteHandler] parsed segments count=" << segments.size();
    size_t idx = 0;
    if (segments.size() >= 4 && !isNumeric(segments[0])) {
        outCtx.mapName = segments[0].toLower();
        idx = 1;
    } else {
        outCtx.mapName = "standard";
    }
    if (segments.size() < idx + 3) return false;
    if (!isNumeric(segments[idx]) || !isNumeric(segments[idx + 1])) return false;
    SwString yTok = segments[idx + 2];
    if (!yTok.endsWith(".png")) return false;
    SwString yDigits = yTok.left(static_cast<int>(yTok.size()) - 4);
    if (!isNumeric(yDigits)) return false;

    outCtx.z = segments[idx].toInt();
    outCtx.x = segments[idx + 1].toInt();
    outCtx.y = yDigits.toInt();
    SwString mapped = MapDatabase::instance()->urlFor(outCtx.mapName);
    if (mapped.isEmpty()) {
        outCtx.mapKnown = false;
    } else {
        outCtx.serverBase = mapped;
        outCtx.cacheDirectory = SwString("maps");
    }
    return true;
}

SwString TileRouteHandler::buildUpstreamUrl(const TileRequestContext& ctx, const SwString& rel) const {
    SwString base = ctx.serverBase;
    // Handle placeholder template %1/%2/%3
    if (base.contains("%1") || base.contains("%2") || base.contains("%3")) {
        base.replace("%1", SwString::number(ctx.z));
        base.replace("%2", SwString::number(ctx.x));
        base.replace("%3", SwString::number(ctx.y));
        return base;
    }
    // Handle placeholder template {z}/{x}/{y}
    if (base.contains("{z}") || base.contains("{x}") || base.contains("{y}")) {
        base.replace("{z}", SwString::number(ctx.z));
        base.replace("{x}", SwString::number(ctx.x));
        base.replace("{y}", SwString::number(ctx.y));
        return base;
    }
    if (base.endsWith("/")) {
        base.chop(1);
    }
    return base + rel;
}

SwString TileRouteHandler::buildCacheFilePath(const TileRequestContext& ctx, const SwString& rel) const {
    SwString dir = ctx.cacheDirectory;
    SwString styleSegment = cacheStyleSegment(ctx);
    if (!dir.isEmpty() && !dir.endsWith("/") && !dir.endsWith("\\")) {
        dir.append("/");
    }
    SwString relClean = rel;
    if (relClean.startsWith("/")) relClean = relClean.mid(1);
    SwString path = dir;
    if (!styleSegment.isEmpty()) {
        path += styleSegment;
        if (!path.endsWith("/")) path.append("/");
    }
    path += relClean;
    return path;
}

bool TileRouteHandler::ensureCacheDirectory(const SwString& filePath) const {
    int pos = -1;
    auto p1 = filePath.lastIndexOf("/");
    auto p2 = filePath.lastIndexOf("\\");
    size_t invalid = static_cast<size_t>(-1);
    size_t best = (p1 != invalid) ? p1 : p2;
    if (p2 != invalid && (p1 == invalid || p2 > p1)) best = p2;
    if (best == invalid) return false;
    pos = static_cast<int>(best);
    SwString dir = filePath.left(pos);
    if (dir.isEmpty()) {
        swWarning() << "[TileRouteHandler] ensureCacheDirectory: empty directory for " << filePath.toStdString();
        return false;
    }
    SwString abs = swDirPlatform().absolutePath(dir);
    if (abs.isEmpty()) {
        abs = dir;
    }
    if (SwDir::exists(abs)) {
        return true;
    }
    bool ok = SwDir::mkpathAbsolute(abs, false);
    if (!ok) {
        // Retry with normalization; if it still fails, use a simple platform-specific mkdir -p.
        ok = SwDir::mkpathAbsolute(abs, true);
    }
    if (!ok) {
#if defined(_WIN32)
        SwString cmd = SwString("cmd /C mkdir \"") + abs + "\"";
        ok = std::system(cmd.toStdString().c_str()) == 0;
#else
        SwString cmd = SwString("mkdir -p \"") + abs + "\"";
        ok = std::system(cmd.toStdString().c_str()) == 0;
#endif
        if (!ok) {
            swWarning() << "[TileRouteHandler] Failed to create cache directory: " << abs.toStdString();
        }
    }
    return ok;
}

bool TileRouteHandler::writeCacheFile(const SwString& path, const SwString& data) {
    std::ofstream output(path.toStdString().c_str(), std::ios::binary | std::ios::trunc);
    if (!output) return false;
    SwByteArray bytes;
    bytes.reserve(data.size());
    for (size_t i = 0; i < data.size(); ++i) bytes.append(data[i]);
    output.write(bytes.constData(), static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

bool TileRouteHandler::readCacheFile(const SwString& path, SwString& outData) const {
    std::ifstream input(path.toStdString().c_str(), std::ios::binary);
    if (!input) return false;
    SwByteArray bytes;
    char buffer[4096];
    while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0) {
        bytes.append(buffer, static_cast<size_t>(input.gcount()));
    }
    outData = SwString(bytes);
    return !outData.isEmpty();
}

SwString TileRouteHandler::cacheStyleSegment(const TileRequestContext& ctx) const {
    if (ctx.mapName.isEmpty()) return SwString("map0");
    return ctx.mapName;
}

SwString TileRouteHandler::stripHttpEnvelope(const SwString& data) const {
    if (data.isEmpty()) {
        return data;
    }
    if (!data.startsWith("HTTP/1.") && !data.startsWith("HTTP/2.")) {
        return data;
    }

    auto stripWithMarker = [&](const SwString& marker, SwString& out) -> bool {
        int idx = data.indexOf(marker);
        if (idx >= 0) {
            out = data.mid(idx + static_cast<int>(marker.size()));
            return true;
        }
        return false;
    };

    SwString result;
    if (stripWithMarker("\r\n\r\n", result)) {
        return result;
    }

    if (stripWithMarker("\r\r\n\r\r\n", result)) {
        return result;
    }

    // Header not fully present? return original payload.
    return data;
}

void TileRouteHandler::sendTile(SwTcpSocket* socket, const SwString& body, const SwString& source, bool) const {
    if (!socket) {
        swWarning() << "[TileRouteHandler] sendTile aborted (socket null) source=" << source.toStdString();
        return;
    }
    swDebug() << "[TileRouteHandler] sendTile start socket=" << socket << " source=" << source.toStdString()
              << " size=" << body.size();
    swDebug() << "[TileRouteHandler] sending tile from " << source.toStdString() << " size=" << body.size() << " [close]";
    std::ostringstream oss;
    oss << "HTTP/1.1 200 OK\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Content-Type: image/png\r\n";
    oss << "X-Tile-Source: " << source.toStdString() << "\r\n";
    oss << "Connection: close\r\n\r\n";
    socket->write(SwString(oss.str()));
    socket->write(body);

    socket->waitForBytesWritten(5000);
    socket->shutdownWrite();

    if (auto* session = dynamic_cast<HttpSession*>(socket->parent())) {
        session->closeSession();
    } else {
        socket->close();
    }
}

void TileRouteHandler::sendError(SwTcpSocket* socket, int status, const SwString& message, bool) const {
    if (!socket) return;
    swWarning() << "[TileRouteHandler] sending error status=" << status << " msg=" << message.toStdString() << " [close]";
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " " << message.toStdString() << "\r\n";
    oss << "Content-Length: 0\r\n";
    oss << "Connection: close\r\n\r\n";
    socket->write(SwString(oss.str()));

    socket->waitForBytesWritten(5000);
    socket->shutdownWrite();

    if (auto* session = dynamic_cast<HttpSession*>(socket->parent())) {
        session->closeSession();
    } else {
        socket->close();
    }
}
