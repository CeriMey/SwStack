#include "SwCoreApplication.h"
#include "core/io/SwUdpSocket.h"
#include <iostream>

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);

    std::string ip = "0.0.0.0";
    uint16_t port = 50002;
    if (argc > 1 && argv[1]) {
        ip = argv[1];
    }
    if (argc > 2 && argv[2]) {
        port = static_cast<uint16_t>(std::atoi(argv[2]));
    }

    SwUdpSocket socket;
    if (!socket.bind(SwString(ip.c_str()), port)) {
        std::cerr << "[UdpProbe] Failed to bind " << ip << ":" << port << std::endl;
        return 1;
    }
    std::cerr << "[UdpProbe] Listening on " << ip << ":" << port << " (Ctrl+C to exit)\n";

    SwObject::connect(&socket, SIGNAL(readyRead), [&socket]() {
        while (socket.hasPendingDatagrams()) {
            SwString sender;
            uint16_t senderPort = 0;
            SwByteArray datagram = socket.receiveDatagram(&sender, &senderPort);
            if (!datagram.isEmpty()) {
                std::cerr << "[UdpProbe] Received " << datagram.size()
                          << " bytes from " << sender.toStdString()
                          << ":" << senderPort << std::endl;
            }
        }
    });

    return app.exec();
}
