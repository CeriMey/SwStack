#include "SwCoreApplication.h"

#include "SwBridgeApp.h"

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);
    SwBridgeApp mainObj(argc, argv);
    return app.exec();
}
