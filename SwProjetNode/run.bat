@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem -----------------------------------------------------------------------------
rem SwProjetNode - build puis launch
rem Usage:
rem   run.bat                 -> build incremental puis launch (duree depuis JSON)
rem   run.bat -c|--clean      -> build --clean puis launch
rem   run.bat 0               -> build incremental puis launch (override: run infini)
rem   run.bat -c|--clean 0    -> build --clean puis launch (override: run infini)
rem
rem Legacy (no-op / kept for compatibility):
rem   run.bat noclean         -> equivalent to default (incremental)
rem -----------------------------------------------------------------------------

set "THIS_DIR=%~dp0"
if "%THIS_DIR:~-1%"=="\" set "THIS_DIR=%THIS_DIR:~0,-1%"

pushd "%THIS_DIR%" >nul

set "SWBUILD_EXE=%THIS_DIR%\..\build-win\tools\SwNode\SwBuild\Release\SwBuild.exe"
set "SWLAUNCH_EXE=%THIS_DIR%\..\build-win\tools\SwNode\SwLaunch\Release\SwLaunch.exe"
set "LAUNCH_JSON=%THIS_DIR%\SwProjetNode.launch.json"

if not exist "%SWBUILD_EXE%" (
  echo [Erreur] SwBuild introuvable: "%SWBUILD_EXE%"
  echo         Lance d'abord le build du repo parent: "..\\build.bat"
  goto :fail
)
if not exist "%SWLAUNCH_EXE%" (
  echo [Erreur] SwLaunch introuvable: "%SWLAUNCH_EXE%"
  echo         Lance d'abord le build du repo parent: "..\\build.bat"
  goto :fail
)
if not exist "%LAUNCH_JSON%" (
  echo [Erreur] Launch JSON introuvable: "%LAUNCH_JSON%"
  goto :fail
)

set "BUILD_CLEAN="
set "BUILD_VERBOSE=--verbose"
set "LAUNCH_DURATION="

:parse
if "%~1"=="" goto :parse_done

if /I "%~1"=="noclean" (
  shift
  goto :parse
)

if /I "%~1"=="-c" (
  set "BUILD_CLEAN=--clean"
  shift
  goto :parse
)

if /I "%~1"=="--clean" (
  set "BUILD_CLEAN=--clean"
  shift
  goto :parse
)

if "%LAUNCH_DURATION%"=="" (
  set "LAUNCH_DURATION=--duration_ms=%~1"
  shift
  goto :parse
)

echo [Erreur] argument inconnu: "%~1"
goto :fail

:parse_done

echo [SwBuild] build SwProjetNode...
"%SWBUILD_EXE%" --root "%THIS_DIR%" --scan src %BUILD_CLEAN% %BUILD_VERBOSE%
if errorlevel 1 goto :fail

echo [SwLaunch] launch...
"%SWLAUNCH_EXE%" --config_file "%LAUNCH_JSON%" %LAUNCH_DURATION%
set "EXITCODE=%ERRORLEVEL%"

popd >nul
exit /b %EXITCODE%

:fail
set "EXITCODE=%ERRORLEVEL%"
if "%EXITCODE%"=="0" set "EXITCODE=1"
popd >nul
exit /b %EXITCODE%
