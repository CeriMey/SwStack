$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$qtRoot = if ($env:QT_ROOT) { $env:QT_ROOT } else { 'C:\Qt\6.9.3\mingw_64' }
$mingwRoot = if ($env:MINGW_ROOT) { $env:MINGW_ROOT } else { 'C:\Qt\Tools\mingw1310_64' }
$exe = Join-Path $root 'bin\QtStaticVideoPlayer.exe'
$windeployqt = Join-Path $qtRoot 'bin\windeployqt.exe'

if (!(Test-Path $exe)) {
    throw "Build the example first: .\build.ps1"
}
if (!(Test-Path $windeployqt)) {
    throw "windeployqt.exe not found. Set QT_ROOT to your Qt MinGW root."
}

$env:PATH = (Join-Path $mingwRoot 'bin') + ';' + (Join-Path $qtRoot 'bin') + ';' + $env:PATH

& $windeployqt --release --compiler-runtime --dir (Join-Path $root 'bin') $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
