@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem -----------------------------------------------------------------------------
rem Configuration initiale
rem -----------------------------------------------------------------------------
set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"

if not defined BUILD_DIR (
    set "BUILD_DIR=%ROOT_DIR%\build-win"
) else (
    set "BUILD_DIR=%BUILD_DIR%"
)

if not defined BUILD_TYPE (
    set "BUILD_TYPE=Release"
)

rem Optionnel: construire un seul target CMake (ex: set BUILD_TARGET=ConfigurableObjectDemo)
if not defined BUILD_TARGET (
    set "BUILD_TARGET="
)

if not defined CMAKE_BIN (
    set "CMAKE_BIN=cmake"
)

rem -----------------------------------------------------------------------------
rem Localisation de CMake
rem -----------------------------------------------------------------------------
where "%CMAKE_BIN%" >nul 2>&1
if errorlevel 1 (
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
        set "CMAKE_BIN=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    ) else if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
        set "CMAKE_BIN=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    ) else (
        echo [Erreur] Impossible de trouver l'executable CMake "%CMAKE_BIN%".
        goto :end
    )
)

if not exist "%BUILD_DIR%" (
    mkdir "%BUILD_DIR%" >nul 2>&1
)

echo [Info] Build directory: "%BUILD_DIR%"
echo [CMake] Configuration...
"%CMAKE_BIN%" -S "%ROOT_DIR%" -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if errorlevel 1 goto :cmake_error

echo [CMake] Compilation %BUILD_TYPE%...
if defined BUILD_TARGET (
    echo [CMake] Target: %BUILD_TARGET%
    "%CMAKE_BIN%" --build "%BUILD_DIR%" --config %BUILD_TYPE% --target %BUILD_TARGET%
) else (
    "%CMAKE_BIN%" --build "%BUILD_DIR%" --config %BUILD_TYPE%
)
if errorlevel 1 goto :build_error

rem -----------------------------------------------------------------------------
rem Recherche des executables
rem -----------------------------------------------------------------------------
set "EXE_COUNT=0"
set "EXAMPLES_DIR=%BUILD_DIR%\exemples"
set "SWNODE_DIR=%BUILD_DIR%\SwNode"

call :scan_examples "%EXAMPLES_DIR%" "%BUILD_TYPE%"
call :scan_examples "%SWNODE_DIR%" "%BUILD_TYPE%"
if /I not "%BUILD_TYPE%"=="Release" (
    call :scan_examples "%EXAMPLES_DIR%" "Release"
    call :scan_examples "%SWNODE_DIR%" "Release"
)
call :scan_examples "%EXAMPLES_DIR%" ""
call :scan_examples "%SWNODE_DIR%" ""

if %EXE_COUNT% EQU 0 (
    echo Aucun executable trouve dans "%EXAMPLES_DIR%" ni "%SWNODE_DIR%".
    goto :end
)

rem -----------------------------------------------------------------------------
rem Menu interactif
rem -----------------------------------------------------------------------------
:menu
echo ==============================================
echo   Executables disponibles (%BUILD_TYPE%)
echo ==============================================
for /L %%I in (1,1,%EXE_COUNT%) do (
    call echo   %%I^) %%EXEC_NAME_%%I%%  [%%EXEC_PATH_%%I%%]
)
echo   q^) Quitter
set /p "CHOIX=Selectionner un executable (1-%EXE_COUNT% ou q) : "
if "%CHOIX%"=="" goto :menu
if /I "%CHOIX%"=="Q" goto :end

for /f "delims=0123456789" %%A in ("%CHOIX%") do goto :menu
if %CHOIX% LSS 1 goto :menu
if %CHOIX% GTR %EXE_COUNT% goto :menu

call set "TARGET=%%EXEC_PATH_%CHOIX%%%"
echo.
echo [RUN] !TARGET!
"!TARGET!"
echo.
pause
goto :menu

rem -----------------------------------------------------------------------------
rem Fonctions auxiliaires
rem -----------------------------------------------------------------------------
:scan_examples
set "BASE=%~1"
set "SUBDIR=%~2"
if not exist "%BASE%" goto :eof
for /d %%E in ("%BASE%\*") do (
    if "%SUBDIR%"=="" (
        call :collect_execs "%%~fE"
    ) else (
        call :collect_execs "%%~fE\%SUBDIR%"
    )
)
goto :eof

:collect_execs
set "CAND_DIR=%~1"
if not exist "%CAND_DIR%" goto :eof
for /f "delims=" %%F in ('dir /b /a:-d "%CAND_DIR%\*.exe" 2^>nul ^| sort') do (
    call :add_exec "%CAND_DIR%\%%F"
)
goto :eof

:add_exec
set "NEW_EXEC=%~f1"
call :is_duplicate "%NEW_EXEC%"
if "!DUP_FOUND!"=="1" goto :eof
set /a EXE_COUNT+=1
set "EXEC_PATH_!EXE_COUNT!=%NEW_EXEC%"
for %%N in ("%NEW_EXEC%") do set "EXEC_NAME_!EXE_COUNT!=%%~nxN"
goto :eof

:is_duplicate
set "DUP_FOUND=0"
for /L %%I in (1,1,%EXE_COUNT%) do (
    if /I "!EXEC_PATH_%%I!"=="%~f1" (
        set "DUP_FOUND=1"
        goto :dup_done
    )
)
:dup_done
goto :eof

rem -----------------------------------------------------------------------------
rem Gestion des erreurs
rem -----------------------------------------------------------------------------
:cmake_error
echo [Erreur] CMake configuration a echoue.
goto :end

:build_error
echo [Erreur] Compilation CMake echouee.
goto :end

:end
endlocal
