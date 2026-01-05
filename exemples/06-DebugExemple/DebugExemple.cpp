
#include <SwCoreApplication.h>
#include <SwDebug.h>
#include <SwTimer.h>

int main(int argc, char *argv[]) {
    // Création de l'application principale
    SwCoreApplication app(argc, argv);

    if (argc >= 3) {
        SwString host = argv[1];
        SwString portStr = argv[2];
        bool ok = false;
        int port = portStr.toInt(&ok);
        if (ok && port > 0 && port <= 65535) {
            SwDebug::setRemoteEndpoint(host, static_cast<uint16_t>(port));
            swDebug() << "Remote debug endpoint set to " << host << ":" << port;
        }
    }

    SwDebug::setAppName("MySuperApp");
    SwDebug::setVersion("1.2.3");

    // Tentative de connexion sur le host et port
    swDebug() << "Ceci est un message de debug avec valeur: " << 42;
    swWarning() << "Attention, quelque chose n'est pas optimal";
    swError() << "Erreur critique: valeur invalide.";

    // Création d'un timer
    SwTimer timer;
    timer.setInterval(2000); // Intervalle de 2000 ms (2 secondes)

    // Connexion du signal timeout à une lambda pour afficher des messages de debug
    SwObject::connect(&timer, SIGNAL(timeout), &app, [&]() {
        static int counter = 0;
        swDebug() << "Message périodique numéro: " << ++counter;
    });

    // Démarrage du timer
    timer.start();

    return app.exec();
}
