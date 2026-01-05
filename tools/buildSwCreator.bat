@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem -----------------------------------------------------------------------------
rem Build SwCreator (Windows) and copy to <repo>/bin
rem -----------------------------------------------------------------------------

for %%I in ("%~dp0..") do set "ROOT_DIR=%%~fI"

if not defined BUILD_DIR (
    set "BUILD_DIR=%ROOT_DIR%\\build-win"
)
if not defined BUILD_TYPE (
    set "BUILD_TYPE=Release"
)
if not defined CMAKE_BIN (
    set "CMAKE_BIN=cmake"
)

rem -----------------------------------------------------------------------------
rem Locate CMake
rem -----------------------------------------------------------------------------
where "%CMAKE_BIN%" >nul 2>&1
if errorlevel 1 (
    if exist "%ProgramFiles%\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe" (
        set "CMAKE_BIN=%ProgramFiles%\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe"
    ) else if exist "%ProgramFiles(x86)%\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe" (
        set "CMAKE_BIN=%ProgramFiles(x86)%\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe"
    ) else (
        echo [Erreur] Impossible de trouver cmake.exe. CMAKE_BIN=%CMAKE_BIN%
        exit /b 1
    )
)

if not exist "%BUILD_DIR%" (
    mkdir "%BUILD_DIR%" >nul 2>&1
)

echo [Info] Root: "%ROOT_DIR%"
echo [Info] Build: "%BUILD_DIR%" (%BUILD_TYPE%)

echo [CMake] Configure...
"%CMAKE_BIN%" -S "%ROOT_DIR%" -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if errorlevel 1 exit /b 1

echo [CMake] Build target: SwCreator
"%CMAKE_BIN%" --build "%BUILD_DIR%" --config %BUILD_TYPE% --target SwCreator
if errorlevel 1 exit /b 1

rem -----------------------------------------------------------------------------
rem Find executable
rem -----------------------------------------------------------------------------
set "CAND1=%BUILD_DIR%\\SwCreator\\%BUILD_TYPE%\\SwCreator.exe"
set "CAND2=%BUILD_DIR%\\bin\\%BUILD_TYPE%\\SwCreator.exe"
set "CAND3=%BUILD_DIR%\\%BUILD_TYPE%\\SwCreator.exe"
set "SWCREATOR_EXE="

if exist "%CAND1%" set "SWCREATOR_EXE=%CAND1%"
if not defined SWCREATOR_EXE if exist "%CAND2%" set "SWCREATOR_EXE=%CAND2%"
if not defined SWCREATOR_EXE if exist "%CAND3%" set "SWCREATOR_EXE=%CAND3%"

if not defined SWCREATOR_EXE (
    for /f "delims=" %%F in ('dir /b /s "%BUILD_DIR%\\SwCreator.exe" 2^>nul') do (
        set "SWCREATOR_EXE=%%F"
        goto :found
    )
)
:found

if not defined SWCREATOR_EXE (
    echo [Erreur] SwCreator.exe introuvable dans "%BUILD_DIR%".
    exit /b 1
)

rem -----------------------------------------------------------------------------
rem Copy to /bin
rem -----------------------------------------------------------------------------
set "BIN_DIR=%ROOT_DIR%\\bin"
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%" >nul 2>&1

copy /y "%SWCREATOR_EXE%" "%BIN_DIR%\\SwCreator.exe" >nul
if errorlevel 1 exit /b 1

echo [OK] Copie: "%BIN_DIR%\\SwCreator.exe"
exit /b 0

