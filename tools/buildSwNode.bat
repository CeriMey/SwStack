@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem -----------------------------------------------------------------------------
rem Build SwNode utilities (Windows) and copy to <repo>/bin
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
    ) else if exist "%ProgramFiles%\\Microsoft Visual Studio\\2022\\BuildTools\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe" (
        set "CMAKE_BIN=%ProgramFiles%\\Microsoft Visual Studio\\2022\\BuildTools\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe"
    ) else if exist "%ProgramFiles(x86)%\\Microsoft Visual Studio\\2022\\BuildTools\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe" (
        set "CMAKE_BIN=%ProgramFiles(x86)%\\Microsoft Visual Studio\\2022\\BuildTools\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe"
    ) else if exist "%ProgramFiles%\\CMake\\bin\\cmake.exe" (
        set "CMAKE_BIN=%ProgramFiles%\\CMake\\bin\\cmake.exe"
    ) else if exist "%ProgramFiles(x86)%\\CMake\\bin\\cmake.exe" (
        set "CMAKE_BIN=%ProgramFiles(x86)%\\CMake\\bin\\cmake.exe"
    ) else (
        echo [Erreur] Impossible de trouver cmake.exe. Ajustez CMAKE_BIN.
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

for %%T in (SwBridge swapi SwLaunch SwComponentContainer SwBuild) do (
    echo [CMake] Build target: %%T
    "%CMAKE_BIN%" --build "%BUILD_DIR%" --config %BUILD_TYPE% --target %%T
    if errorlevel 1 exit /b 1
)

set "BIN_DIR=%ROOT_DIR%\\bin"
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%" >nul 2>&1

call :copy_exec "SwBridge" "SwNode\\SwAPI\\SwBridge" "SwBridge.exe"
if errorlevel 1 exit /b 1
call :copy_exec "swapi" "SwNode\\SwAPI\\SwApi" "swapi.exe"
if errorlevel 1 exit /b 1
call :copy_exec "SwLaunch" "SwNode\\SwLaunch" "SwLaunch.exe"
if errorlevel 1 exit /b 1
call :copy_exec "SwComponentContainer" "SwNode\\SwComponentContainer" "SwComponentContainer.exe"
if errorlevel 1 exit /b 1
call :copy_exec "SwBuild" "SwNode\\SwBuild" "SwBuild.exe"
if errorlevel 1 exit /b 1

echo [OK] Termine.
exit /b 0

:copy_exec
set "TARGET_LABEL=%~1"
set "REL_DIR=%~2"
set "EXE_NAME=%~3"
set "EXE_PATH="

set "CAND1=%BUILD_DIR%\\%REL_DIR%\\%BUILD_TYPE%\\%EXE_NAME%"
set "CAND2=%BUILD_DIR%\\%REL_DIR%\\%EXE_NAME%"
set "CAND3=%BUILD_DIR%\\bin\\%BUILD_TYPE%\\%EXE_NAME%"
set "CAND4=%BUILD_DIR%\\bin\\%EXE_NAME%"
set "CAND5=%BUILD_DIR%\\%BUILD_TYPE%\\%EXE_NAME%"
set "CAND6=%BUILD_DIR%\\%EXE_NAME%"

if exist "%CAND1%" set "EXE_PATH=%CAND1%"
if not defined EXE_PATH if exist "%CAND2%" set "EXE_PATH=%CAND2%"
if not defined EXE_PATH if exist "%CAND3%" set "EXE_PATH=%CAND3%"
if not defined EXE_PATH if exist "%CAND4%" set "EXE_PATH=%CAND4%"
if not defined EXE_PATH if exist "%CAND5%" set "EXE_PATH=%CAND5%"
if not defined EXE_PATH if exist "%CAND6%" set "EXE_PATH=%CAND6%"

if not defined EXE_PATH (
    for /f "delims=" %%F in ('dir /b /s "%BUILD_DIR%\\%REL_DIR%\\%EXE_NAME%" 2^>nul') do (
        set "EXE_PATH=%%F"
        goto :found_exec
    )
)

if not defined EXE_PATH (
    for /f "delims=" %%F in ('dir /b /s "%BUILD_DIR%\\%EXE_NAME%" 2^>nul') do (
        set "EXE_PATH=%%F"
        goto :found_exec
    )
)

:found_exec
if not defined EXE_PATH (
    echo [Erreur] %EXE_NAME% introuvable dans "%BUILD_DIR%".
    exit /b 1
)

copy /y "%EXE_PATH%" "%BIN_DIR%\\%EXE_NAME%" >nul
if errorlevel 1 exit /b 1
echo [OK] Copie: "%BIN_DIR%\\%EXE_NAME%"

if /I "%TARGET_LABEL%"=="SwBridge" (
    call :copy_swbridge_assets "%EXE_PATH%"
    if errorlevel 1 exit /b 1
)

exit /b 0

:copy_swbridge_assets
set "BRIDGE_EXE=%~1"
for %%D in ("%BRIDGE_EXE%") do set "BRIDGE_DIR=%%~dpD"

if exist "%BRIDGE_DIR%index.html" (
    copy /y "%BRIDGE_DIR%index.html" "%BIN_DIR%\\index.html" >nul
    if errorlevel 1 exit /b 1
)

if exist "%BRIDGE_DIR%css\\style.css" (
    if not exist "%BIN_DIR%\\css" mkdir "%BIN_DIR%\\css" >nul 2>&1
    copy /y "%BRIDGE_DIR%css\\style.css" "%BIN_DIR%\\css\\style.css" >nul
    if errorlevel 1 exit /b 1
)

if exist "%BRIDGE_DIR%js\\app.js" (
    if not exist "%BIN_DIR%\\js" mkdir "%BIN_DIR%\\js" >nul 2>&1
    copy /y "%BRIDGE_DIR%js\\app.js" "%BIN_DIR%\\js\\app.js" >nul
    if errorlevel 1 exit /b 1
)

exit /b 0

