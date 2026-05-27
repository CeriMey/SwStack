$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$qtRoot = if ($env:QT_ROOT) { $env:QT_ROOT } else { 'C:\Qt\6.9.3\mingw_64' }
$mingwRoot = if ($env:MINGW_ROOT) { $env:MINGW_ROOT } else { 'C:\Qt\Tools\mingw1310_64' }

$qmake = Join-Path $qtRoot 'bin\qmake.exe'
$make = Join-Path $mingwRoot 'bin\mingw32-make.exe'

if (!(Test-Path $qmake)) {
    throw "qmake.exe not found. Set QT_ROOT to your Qt MinGW root."
}
if (!(Test-Path $make)) {
    throw "mingw32-make.exe not found. Set MINGW_ROOT to your MinGW root."
}

$env:PATH = (Join-Path $mingwRoot 'bin') + ';' + (Join-Path $qtRoot 'bin') + ';' + $env:PATH

Push-Location (Join-Path $root 'example')
try {
    & $qmake .\QtStaticVideoPlayer.pro
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $make -j1
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}
