#include "SwCoreApplication.h"

#include "SwApiApp.h"

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);
    SwApiApp api(app, argc, argv);
    return app.exec();
}
