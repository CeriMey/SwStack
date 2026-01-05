#!/bin/bash

# Définir le mode par défaut à debug
BUILD_MODE="debug"

# Définir le chemin du répertoire livrable
LIVRABLE_DIR="$(dirname "$0")/build/bin"
DELIVERY_DIR="$(dirname "$0")/../../install"

# Vérifier si un argument est passé au script
if [ -z "$1" ]; then
    echo "Aucun mode spécifié, compilation en mode Debug par défaut."
else
    BUILD_MODE="$1"
    echo "Mode de compilation spécifié : $BUILD_MODE"
fi

# Aller au répertoire à la racine du projet
cd "$(dirname "$0")"

# Trouver le fichier .pro (suppose qu'il n'y a qu'un seul fichier .pro dans le sous-répertoire)
PRO_FILE=$(find . -name '*.pro' -print -quit)

# Afficher le chemin du fichier .pro trouvé
echo "Fichier .pro trouvé : $PRO_FILE"

# Créer les répertoires build et livrables s'ils n'existent pas
mkdir -p build "$LIVRABLE_DIR" "$DELIVERY_DIR"

# Aller dans le répertoire build
cd build

# Générer les fichiers de projet avec qmake pour le mode spécifié
/home/$USER/Qt/6.8.0/gcc_64/bin/qmake "../$PRO_FILE" -config $BUILD_MODE

# Compiler le projet avec make (remplacez par nmake ou jom si nécessaire sous Qt pour Windows)
make
cd ..
# Copier les bibliothèques partagées dans les répertoires livrables
if [ "$BUILD_MODE" == "debug" ]; then
    # Utiliser find pour obtenir tous les fichiers .so dans le répertoire bin et les copier un par un
    find build/bin/ -name '*.so' -exec cp -v {} "$LIVRABLE_DIR/" \;
    find build/bin/ -name '*.so' -exec cp -v {} "$DELIVERY_DIR/" \;
else
    # Même commande pour la configuration release
    find build/bin/ -name '*.so' -exec cp -v {} "$LIVRABLE_DIR/" \;
    find build/bin/ -name '*.so' -exec cp -v {} "$DELIVERY_DIR/" \;
fi


# Fin du script
echo "Compilation terminée."

