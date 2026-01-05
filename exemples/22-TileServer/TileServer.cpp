#include "SwCoreApplication.h"
#include "SwTcpServer.h"
#include "SwTcpSocket.h"
#include "SwObject.h"
#include "SwString.h"

#include "http/HttpServer.h"
#include "routes/StaticRouteHandler.h"
#include "routes/TileRouteHandler.h"
#include "routes/MapApiRouteHandler.h"

#include "SwDebug.h"

int main(int argc, char* argv[]) {
    SwCoreApplication app(argc, argv);

    HttpServer server(nullptr);
    server.addHandler(new TileRouteHandler(&server));
    server.addHandler(new StaticRouteHandler(SwString("www")));
    server.addHandler(new MapApiRouteHandler());

    const uint16_t port = 8085;
    if (!server.listen(port)) {
        return 1;
    }

    swDebug() << "[TileServer] HTTP router ready on http://localhost:" << port << "/{z}/{x}/{y}.png";

    return app.exec();
}
