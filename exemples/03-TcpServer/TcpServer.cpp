
#include "SwCoreApplication.h"
#include "SwTcpServer.h"
#include "SwTcpSocket.h"

#include <iostream>

// Un handler qui va r�agir aux nouvelles connexions
class MyHandler : public SwObject {
    SW_OBJECT(MyHandler, SwObject)
public:
    MyHandler(SwObject* parent = nullptr) : SwObject(parent), m_server(nullptr) {}

    void setServer(SwTcpServer* server) {
        m_server = server;
    }

public slots:
    void onNewConnection() {
        if (!m_server) return;

        SwTcpSocket* client = m_server->nextPendingConnection();
        if (client) {
            std::cout << "Nouveau client connecté !" << std::endl;

            SwObject::connect(client, SIGNAL(disconnected), std::function<void()>([client]() {
                std::cout << "Client Déconnecté !" << std::endl;
                delete client;
            }));


            // Construire une r�ponse HTTP tr�s simple :
            // On envoie un en-t�te minimal et une page HTML
            std::string response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html; charset=UTF-8\r\n"
                "Connection: close\r\n"
                "\r\n" // Fin des en-t�tes
                "<!DOCTYPE html>\n"
                "<html lang=\"fr\">\n"
                "<head>\n"
                "<meta charset=\"UTF-8\">\n"
                "<title>Bienvenue sur le serveur</title>\n"
                "<style>\n"
                "    body {\n"
                "        background: #f0f0f0;\n"
                "        font-family: Arial, sans-serif;\n"
                "        text-align: center;\n"
                "        margin-top: 100px;\n"
                "    }\n"
                "    .welcome-container {\n"
                "        background: #fff;\n"
                "        display: inline-block;\n"
                "        padding: 50px;\n"
                "        border-radius: 10px;\n"
                "        box-shadow: 0 0 10px rgba(0,0,0,0.1);\n"
                "    }\n"
                "    h1 {\n"
                "        margin-top: 0;\n"
                "        font-size: 2em;\n"
                "        color: #333;\n"
                "    }\n"
                "    p {\n"
                "        font-size: 1.2em;\n"
                "        color: #666;\n"
                "    }\n"
                "    button {\n"
                "        background: #007BFF;\n"
                "        color: #fff;\n"
                "        border: none;\n"
                "        padding: 15px 30px;\n"
                "        border-radius: 5px;\n"
                "        font-size: 1em;\n"
                "        cursor: pointer;\n"
                "        margin-top: 20px;\n"
                "    }\n"
                "    button:hover {\n"
                "        background: #0056b3;\n"
                "    }\n"
                "</style>\n"
                "</head>\n"
                "<body>\n"
                "    <div class=\"welcome-container\">\n"
                "        <h1>Bienvenue sur le serveur</h1>\n"
                "        <p>Nous sommes heureux de vous accueillir !</p>\n"
                "        <button onclick=\"window.close();\">Fermer la connexion</button>\n"
                "    </div>\n"
                "</body>\n"
                "</html>\n";

            // Envoyer la r�ponse au client
            client->write(response.c_str());
            client->waitForBytesWritten();
            client->close();
        }
    }
private:
    SwTcpServer* m_server;
};


int main(int argc, char* argv[]) {
    SwCoreApplication app(argc, argv);

    SwTcpServer server;
    MyHandler handler;
    handler.setServer(&server);

    // Se connecter au signal newConnection
    SwObject::connect(&server, SIGNAL(newConnection), &handler, &MyHandler::onNewConnection);

    // �couter sur le port 12345
    if (!server.listen(12345)) {
        std::cerr << "�chec de listen sur le port 12345" << std::endl;
        return 1;
    }

    std::cout << "Serveur d�marr� sur le port 12345. En attente de connexions..." << std::endl;

    // Lancer la boucle d'�v�nements
    return app.exec();
}
