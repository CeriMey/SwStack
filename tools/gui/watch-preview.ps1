param(
    [string]$Target = "TabWidgetSnapshot",
    [string]$Configuration = "Release",
    [string]$BuildDir = "",
    [string]$OutFile = "",
    [string]$Position = "",
    [string]$Style = "",
    [string[]]$ExeArgs = @(),
    [string[]]$WatchPaths = @("src\\core\\gui", "exemples\\30-TabWidgetSnapshot"),
    [int]$DebounceMs = 250
)

$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
    $here = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $here "..\\..")).Path
}

$root = Resolve-RepoRoot
$preview = Join-Path $root "tools\\gui\\preview.ps1"

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $root "build-win"
}
if ([string]::IsNullOrWhiteSpace($OutFile)) {
    $OutFile = Join-Path $root ("build-codex-sanity\\{0}.png" -f $Target.ToLower())
}

$resolvedWatch = @()
foreach ($p in $WatchPaths) {
    $full = Join-Path $root $p
    if (Test-Path $full) {
        $resolvedWatch += $full
    }
}
if ($resolvedWatch.Count -eq 0) {
    throw "No valid WatchPaths found."
}

$watchers = @()
$eventNames = @()
$eventTag = "SwGuiPreview-" + ([guid]::NewGuid().ToString("N"))

$pending = $false
$lastEvent = Get-Date

function OnChange {
    $script:pending = $true
    $script:lastEvent = Get-Date
}

foreach ($p in $resolvedWatch) {
    $w = New-Object System.IO.FileSystemWatcher
    $w.Path = $p
    $w.IncludeSubdirectories = $true
    $w.EnableRaisingEvents = $true
    $w.NotifyFilter = [System.IO.NotifyFilters]'FileName, LastWrite, Size, DirectoryName'

    $watchers += $w
    $nameChanged = "$eventTag-$($watchers.Count)-Changed"
    $nameCreated = "$eventTag-$($watchers.Count)-Created"
    $nameDeleted = "$eventTag-$($watchers.Count)-Deleted"
    $nameRenamed = "$eventTag-$($watchers.Count)-Renamed"

    Register-ObjectEvent $w Changed -SourceIdentifier $nameChanged -Action { OnChange } | Out-Null
    Register-ObjectEvent $w Created -SourceIdentifier $nameCreated -Action { OnChange } | Out-Null
    Register-ObjectEvent $w Deleted -SourceIdentifier $nameDeleted -Action { OnChange } | Out-Null
    Register-ObjectEvent $w Renamed -SourceIdentifier $nameRenamed -Action { OnChange } | Out-Null

    $eventNames += $nameChanged
    $eventNames += $nameCreated
    $eventNames += $nameDeleted
    $eventNames += $nameRenamed
}

Write-Output "Watching:"
foreach ($p in $resolvedWatch) { Write-Output "  - $p" }
Write-Output "Output: $OutFile"

try {
    while ($true) {
        Start-Sleep -Milliseconds 50
        if (-not $pending) { continue }

        $elapsed = (Get-Date) - $lastEvent
        if ($elapsed.TotalMilliseconds -lt $DebounceMs) { continue }

        $pending = $false
        & powershell -ExecutionPolicy Bypass -File $preview -Target $Target -Configuration $Configuration -BuildDir $BuildDir -OutFile $OutFile -Position $Position -Style $Style -ExeArgs $ExeArgs | Out-Host
    }
} finally {
    foreach ($name in $eventNames) {
        try { Unregister-Event -SourceIdentifier $name -ErrorAction SilentlyContinue } catch {}
    }
    foreach ($w in $watchers) {
        try { $w.EnableRaisingEvents = $false; $w.Dispose() } catch {}
    }
}
