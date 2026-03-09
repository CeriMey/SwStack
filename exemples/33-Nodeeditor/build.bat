@echo off
setlocal

set BUILD_MODE=%~1
if "%BUILD_MODE%"=="" set BUILD_MODE=Release

set SCRIPT_DIR=%~dp0
set ROOT_DIR=%SCRIPT_DIR%..\..\
set BUILD_DIR=%SCRIPT_DIR%build

echo Configuring NodeEditorExample (%BUILD_MODE%) from %ROOT_DIR%
cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -DSW_BUILD_exemples_33_Nodeeditor=ON
if errorlevel 1 exit /b 1

echo Building NodeEditorExample
cmake --build "%BUILD_DIR%" --config %BUILD_MODE% --target NodeEditorExample
if errorlevel 1 exit /b 1

echo Build completed.
endlocal
