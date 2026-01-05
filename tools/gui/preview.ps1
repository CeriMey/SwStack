param(
    [string]$Target = "TabWidgetSnapshot",
    [string]$Configuration = "Release",
    [string]$BuildDir = "",
    [string]$OutFile = "",
    [string]$Position = "",
    [string]$Style = "",
    [string[]]$ExeArgs = @(),
    [switch]$NoBuild,
    [switch]$Show
)

$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
    $here = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $here "..\\..")).Path
}

function Find-CMake {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $candidates = @(
        "$env:ProgramFiles\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe",
        "$env:ProgramFiles(x86)\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }
    throw "cmake.exe not found (install CMake or Visual Studio CMake component)."
}

function Find-TargetExe([string]$BuildRoot, [string]$TargetName, [string]$Config) {
    $exeName = "$TargetName.exe"
    $direct = Join-Path $BuildRoot (Join-Path $Config $exeName)
    if (Test-Path $direct) { return (Resolve-Path $direct).Path }

    $bin = Join-Path $BuildRoot (Join-Path (Join-Path "bin" $Config) $exeName)
    if (Test-Path $bin) { return (Resolve-Path $bin).Path }

    $configDirs = Get-ChildItem -Path $BuildRoot -Directory -Recurse -Filter $Config -ErrorAction SilentlyContinue
    foreach ($dir in $configDirs) {
        $candidate = Join-Path $dir.FullName $exeName
        if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }
    }

    $fallback = Get-ChildItem -Path $BuildRoot -Recurse -Filter $exeName -File -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($fallback) { return $fallback.FullName }
    return $null
}

function Get-ExeCachePath([string]$BuildRoot, [string]$TargetName, [string]$Config) {
    $cacheDir = Join-Path $BuildRoot ".codex"
    if (-not (Test-Path $cacheDir)) {
        New-Item -ItemType Directory -Force -Path $cacheDir | Out-Null
    }
    return Join-Path $cacheDir ("preview-exe-{0}-{1}.txt" -f $TargetName, $Config)
}

function Read-ExeCache([string]$CachePath) {
    if (-not (Test-Path $CachePath)) { return $null }
    $path = (Get-Content -Path $CachePath -Raw -ErrorAction SilentlyContinue)
    if ([string]::IsNullOrWhiteSpace($path)) { return $null }
    $path = $path.Trim()
    if (-not (Test-Path $path)) { return $null }
    return $path
}

function Write-ExeCache([string]$CachePath, [string]$ExePath) {
    if ([string]::IsNullOrWhiteSpace($ExePath)) { return }
    try { Set-Content -Path $CachePath -Value $ExePath -NoNewline } catch {}
}

function Parse-ExeFromBuildOutput([object[]]$Lines, [string]$TargetName) {
    if (-not $Lines) { return $null }
    $exeName = "$TargetName.exe"
    foreach ($line in $Lines) {
        $text = $line.ToString()
        $m = [regex]::Match($text, "->\\s*(?<path>.*$([regex]::Escape($exeName)))\\s*$")
        if ($m.Success) {
            $p = $m.Groups["path"].Value.Trim()
            if (Test-Path $p) { return $p }
        }
    }
    return $null
}

$root = Resolve-RepoRoot
$cmake = Find-CMake

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $root "build-win"
}
if ([string]::IsNullOrWhiteSpace($OutFile)) {
    $OutFile = Join-Path $root ("build-codex-sanity\\{0}.png" -f $Target.ToLower())
}

if (-not $NoBuild) {
    & $cmake -S $root -B $BuildDir -DCMAKE_BUILD_TYPE=$Configuration | Out-Host
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $buildOutput = & $cmake --build $BuildDir --config $Configuration --target $Target 2>&1
    $buildOutput | Out-Host
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$cachePath = Get-ExeCachePath -BuildRoot $BuildDir -TargetName $Target -Config $Configuration
$exe = Read-ExeCache -CachePath $cachePath
if (-not $exe) {
    if (-not $NoBuild) {
        $exe = Parse-ExeFromBuildOutput -Lines $buildOutput -TargetName $Target
    }
    if (-not $exe) {
        $exe = Find-TargetExe -BuildRoot $BuildDir -TargetName $Target -Config $Configuration
    }
    if ($exe) {
        Write-ExeCache -CachePath $cachePath -ExePath $exe
    }
}
if (-not $exe) {
    throw "Executable not found for target '$Target' in '$BuildDir'."
}

$outDir = Split-Path -Parent $OutFile
if ($outDir -and -not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}

$argsList = @()
if (-not [string]::IsNullOrWhiteSpace($Position)) { $argsList += $Position }
if (-not [string]::IsNullOrWhiteSpace($Style)) { $argsList += $Style }
if ($ExeArgs) { $argsList += $ExeArgs }

& $exe $OutFile @argsList | Out-Host
$code = $LASTEXITCODE
if ($code -ne 0) { exit $code }

Write-Output $OutFile

if ($Show) {
    Start-Process $OutFile | Out-Null
}
