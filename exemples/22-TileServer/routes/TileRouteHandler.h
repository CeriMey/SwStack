#pragma once
#include "http/HttpRouteHandler.h"
#include "SwNetworkAccessManager.h"
#include "SwDir.h"
#include "SwTimer.h"
#include "SwByteArray.h"
#include <fstream>
#include <sstream>
#include <atomic>

struct TileRequestContext {
    int z = -1;
    int x = -1;
    int y = -1;
    SwString mapName = "standard";
    SwString serverBase;
    SwString cacheDirectory;
    bool mapKnown = true;
};

class TileRouteHandler : public HttpRouteHandler, public SwObject {
    SW_OBJECT(TileRouteHandler, SwObject)
public:
    TileRouteHandler(SwObject* parent = nullptr)
        : SwObject(parent) {}

    bool canHandle(const HttpRequest& req) const override {
        if (!req.path.toLower().startsWith("/map/")) return false;
        if (!req.path.toLower().endsWith(".png")) return false;
        SwList<SwString> seg = req.path.split('/');
        size_t count = 0;
        for (size_t i = 0; i < seg.size(); ++i) { if (!seg[i].isEmpty()) ++count; }
        // expect /map/z/x/y.png or /map/name/z/x/y.png => at least 4 tokens ("map", z, x, y)
        return count >= 4;
    }

    void handle(const HttpRequest& req, SwTcpSocket* socket) override;

private:
    bool parsePath(const SwString& path, TileRequestContext& outCtx) const;
    SwString buildUpstreamUrl(const TileRequestContext& ctx, const SwString& rel) const;
    SwString buildCacheFilePath(const TileRequestContext& ctx, const SwString& rel) const;
    bool ensureCacheDirectory(const SwString& filePath) const;
    bool writeCacheFile(const SwString& path, const SwString& data);
    bool readCacheFile(const SwString& path, SwString& outData) const;

    SwString cacheStyleSegment(const TileRequestContext& ctx) const;
    SwString stripHttpEnvelope(const SwString& data) const;

    void sendTile(SwTcpSocket* socket, const SwString& body, const SwString& source, bool closeAfter) const;
    void sendError(SwTcpSocket* socket, int status, const SwString& message, bool closeAfter) const;

    static std::atomic<bool> s_offlineMode;
};
