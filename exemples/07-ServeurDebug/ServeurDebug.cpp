
#include "SwCoreApplication.h"
#include "SwTcpServer.h"
#include "SwTcpSocket.h"
#include "SwObject.h"
#include <iostream>

// Handler qui gère la lecture des données pour une socket cliente donnée
class MyReaderHandler : public SwObject {
    SW_OBJECT(MyReaderHandler, SwObject)
public:
    MyReaderHandler(SwTcpSocket* client, SwObject* parent = nullptr)
        : SwObject(parent),
        m_client(client)
    {
        if (m_client) {
            // Connexion des signaux de la socket aux slots du handler
            connect(m_client, SIGNAL(readyRead), this, &MyReaderHandler::onReadyRead);
            connect(m_client, SIGNAL(disconnected), this, &MyReaderHandler::onDisconnected);
            connect(m_client, SIGNAL(errorOccurred), this, &MyReaderHandler::onError);
        }
    }

private slots:
    void onReadyRead() {
        if (!m_client) return;

        // Lire toutes les données disponibles
        SwString data = m_client->read();
        if (!data.isEmpty()) {
            // Afficher les données reçues dans la console
            std::cout << "Données reçues du client : " << data.toStdString() << std::endl;
        }
    }

    void onDisconnected() {
        std::cout << "Client déconnecté." << std::endl;
        cleanup();
    }

    void onError(int err) {
        std::cerr << "Erreur sur la socket du client: " << err << std::endl;
        cleanup();
    }

private:
    void cleanup() {
        if (m_client) {
            m_client->disconnectAllSlots();
            m_client->deleteLater();
            m_client = nullptr;
        }

        // L'objet MyReaderHandler s'autodétruit pour éviter les fuites mémoire
        this->deleteLater();
    }

    SwTcpSocket* m_client;
};

// Handler principal pour les nouvelles connexions
class MyServerHandler : public SwObject {
    SW_OBJECT(MyServerHandler, SwObject)
public:
    MyServerHandler(SwTcpServer* server, SwObject* parent = nullptr)
        : SwObject(parent), m_server(server) {}

public slots:
    void onNewConnection() {
        if (!m_server) return;

        // On récupère la connexion en attente
        SwTcpSocket* client = m_server->nextPendingConnection();
        if (client) {
            std::cout << "Nouveau client connecté !" << std::endl;

            // Créer un handler spécifique pour ce client
            new MyReaderHandler(client, this);
            // Note : pas besoin de conserver un pointeur dessus, MyReaderHandler s'occupe
            // de se détruire lorsque la socket sera déconnectée.
        }
    }

private:
    SwTcpServer* m_server;
};

int main(int argc, char* argv[]) {
    SwCoreApplication app(argc, argv);

    SwTcpServer server;
    MyServerHandler handler(&server);

    // Se connecter au signal newConnection
    SwObject::connect(&server, SIGNAL(newConnection), &handler, &MyServerHandler::onNewConnection);

    // Écouter sur le port 12345
    if (!server.listen(12345)) {
        std::cerr << "Échec de listen sur le port 12345" << std::endl;
        return 1;
    }

    std::cout << "Serveur démarré sur le port 12345. En attente de connexions..." << std::endl;

    return app.exec();
}
