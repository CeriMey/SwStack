#include "SwGuiApplication.h"

#include "IpcRingBufferPlayer.h"

#include <iostream>

int main(int argc, char** argv) {
#if !defined(_WIN32)
    std::cerr << "IpcRingBufferPlayer is only supported on Windows.\n";
    (void)argc;
    (void)argv;
    return 0;
#else
    SwGuiApplication app;
    IpcRingBufferPlayer player(argc, argv);
    player.show();
    return app.exec();
#endif
}

