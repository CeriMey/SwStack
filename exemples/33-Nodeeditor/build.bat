@echo off
setlocal

REM Définir le mode par défaut à debug
set BUILD_MODE=release

REM Vérifier si un argument a été passé
if "%~1"=="" (
    echo Aucun argument passé, utilisation du mode par défaut : %BUILD_MODE%
) else (
    set BUILD_MODE=%~1
    echo Mode de construction défini à : %BUILD_MODE%
)

REM Définir le chemin du répertoire livrable
set LIVRABLE_DIR=%~dp0build\bin
set DELIVERY_DIR=%~dp0..\..\install



REM Vérifier si un argument est passé au script
if "%~1"=="" (
    echo Aucun mode spécifié, compilation en mode Debug par défaut.
) else (
    set BUILD_MODE=%~1
    echo Mode de compilation spécifié : %BUILD_MODE%
)

REM Aller au répertoire à la racine du projet
cd /d "%~dp0"

REM Trouver le fichier .pro (suppose qu'il n'y a qu'un seul fichier .pro)
for /R %%i in (*.pro) do set PRO_FILE=%%i

REM Afficher le chemin du fichier .pro trouvé
echo Fichier .pro trouvé : %PRO_FILE%

REM Créer le répertoire build s'il n'existe pas
if not exist build mkdir build
if not exist "%LIVRABLE_DIR%" mkdir "%LIVRABLE_DIR%"
if not exist "%DELIVERY_DIR%" mkdir "%DELIVERY_DIR%"
REM Aller dans le répertoire build
cd build

REM Générer les fichiers de projet avec qmake pour le mode spécifié
qmake "%PRO_FILE%" -config %BUILD_MODE%

REM Compiler le projet. Remplacez mingw32-make par nmake ou jom si nécessaire
mingw32-make

  if "%BUILD_MODE%"=="debug" (
        copy /Y debug\*.dll "%LIVRABLE_DIR%"
        copy /Y debug\*.dll "%DELIVERY_DIR%"
    ) else (
        copy /Y release\*.dll "%LIVRABLE_DIR%"
        copy /Y release\*.dll "%DELIVERY_DIR%"
    )
	
REM Fin du script
endlocal
echo Compilation terminée.

