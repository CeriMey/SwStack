#include <SwCoreApplication.h>
#include "SwInteractiveConsoleApplication.h"
#include <SwEventLoop.h>

int main(int argc, char *argv[]) {
    // Création de l'application principale
    SwCoreApplication app(argc, argv);

    // Initialisation d'un document JSON pour la configuration
    SwJsonDocument config;
    config.find("settings/display/brightness", true) = 80;
    config.find("settings/display/contrast", true) = 50;
    config.find("settings/network/wifi", true) = "Start wifi checkup ->";
    config.find("settings/network/ethernet", true) = "working...";

    // Création de l'application interactive avec la configuration JSON
    SwInteractiveConsoleApplication interactiveApp(config);

    // Ajouter des commentaires pour chaque chemin de configuration
    interactiveApp.addComment("settings", "Application settings:");
    interactiveApp.addComment("settings/display", "Display settings");
    interactiveApp.addComment("settings/display/brightness", "Screen brightness adjustment.");
    interactiveApp.addComment("settings/display/contrast", "Screen contrast adjustment.");
    interactiveApp.addComment("settings/network", "Network settings");
    interactiveApp.addComment("settings/network/wifi", "Wi-Fi checkup.");
    interactiveApp.addComment("settings/network/ethernet", "Ethernet status");

    // Enregistrer une commande pour modifier la luminosité
interactiveApp.registerCommand("settings/display/brightness", [&](const SwString &value) {
    std::cout << "Actual brightness: " << value.toStdString() << "\n";
    std::cout << "Enter new value ";
    std::string newVal = interactiveApp.waitForNewValue("settings/display/brightness", "quit");
    if (!newVal.empty()) {
        std::cout << "New brightness : " << newVal << "\n";
    } 
});
    // Enregistrer une commande pour afficher l'état du Wi-Fi
    interactiveApp.registerCommand("settings/network/wifi", [](const SwString &value) {
        const int maxDots = 7; // Nombre maximum de points
        const int repeatCount = 3; // Nombre de répétitions de l'animation

        for (int i = 0; i < repeatCount; ++i) {
            for (int dots = 0; dots <= maxDots; ++dots) {
                std::cout << "\rLe Wi-Fi setup ongoing" << std::string(dots, '.') << "   " << std::flush; 
                SwEventLoop::swsleep(300);
            }
        }

        std::cout << "\nSetup completed!" << "\n";
    });


    interactiveApp.setSingleLineMode(true);
    // Lancer la boucle principale de l'application
    return app.exec();
}
