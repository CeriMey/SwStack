param(
    [string]$RepoRoot = "",
    [string]$BuildDir = "",
    [string]$XServerPath = "C:\Program Files (x86)\Xming\Xming.exe",
    [string]$RtspUrl = "rtsp://127.0.0.1:8554/test",
    [string]$DecoderId = "",
    [int]$RtspPort = 8554,
    [switch]$SkipRtspServer
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
    param([string]$Path)
    return (Resolve-Path -LiteralPath $Path).Path
}

function Convert-ToWslPath {
    param([Parameter(Mandatory = $true)][string]$WindowsPath)
    $normalizedPath = $WindowsPath -replace "\\", "/"
    if ($normalizedPath -match "^([A-Za-z]):/(.*)$") {
        $drive = $Matches[1].ToLowerInvariant()
        $rest = $Matches[2]
        return "/mnt/$drive/$rest"
    }
    throw "Unsupported Windows path for WSL conversion: $WindowsPath"
}

function Convert-ToBashLiteral {
    param([Parameter(Mandatory = $true)][string]$Value)
    return "'" + ($Value -replace "'", "'""'""'") + "'"
}

function Get-WslGatewayIp {
    return (wsl.exe bash -lc "ip route show default | cut -d' ' -f3 | head -n1").Trim()
}

function Ensure-XServer {
    param([Parameter(Mandatory = $true)][string]$BinaryPath)

    if (-not (Test-Path -LiteralPath $BinaryPath)) {
        throw "X server not found at $BinaryPath"
    }

    $existing = Get-Process -Name Xming -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $existing) {
        Start-Process -FilePath $BinaryPath `
                      -ArgumentList ":0", "-multiwindow", "-clipboard", "-ac" `
                      -WindowStyle Hidden | Out-Null
        Start-Sleep -Seconds 3
    }

    $listener = Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue |
        Where-Object { $_.LocalPort -eq 6000 } |
        Select-Object -First 1
    if ($null -eq $listener) {
        throw "X server did not open TCP port 6000"
    }
}

function Invoke-WslBash {
    param([Parameter(Mandatory = $true)][string]$Command)
    $tempDir = "C:\Temp\swstack-wsl"
    New-Item -ItemType Directory -Path $tempDir -Force | Out-Null

    $tempFile = Join-Path $tempDir ("codex-" + [Guid]::NewGuid().ToString("N") + ".sh")
    $normalized = ($Command -replace "`r`n", "`n" -replace "`r", "`n").TrimEnd("`n") + "`n"
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($tempFile, $normalized, $utf8NoBom)

    try {
        $wslScriptPath = Convert-ToWslPath -WindowsPath $tempFile
        $output = & wsl.exe bash $wslScriptPath
        if ($LASTEXITCODE -ne 0) {
            throw "WSL command failed: $Command"
        }
        return $output
    } finally {
        Remove-Item -LiteralPath $tempFile -Force -ErrorAction SilentlyContinue
    }
}

function Start-WslRtspServer {
    param(
        [Parameter(Mandatory = $true)][string]$ServerScriptWslPath,
        [Parameter(Mandatory = $true)][int]$Port
    )

    $serverScriptLiteral = Convert-ToBashLiteral -Value $ServerScriptWslPath
    $check = "python3 -c `"import socket; s=socket.socket(); s.settimeout(0.5); " +
        "r=s.connect_ex(('127.0.0.1', $Port)); s.close(); " +
        "print('LISTENING' if r == 0 else 'CLOSED')`""
    $status = (Invoke-WslBash -Command $check | Out-String).Trim()
    if ($status -eq "LISTENING") {
        return
    }

    $start = "nohup python3 $serverScriptLiteral --port $Port >/tmp/sw_gst_rtsp_server.log 2>&1 & echo $! >/tmp/sw_gst_rtsp_server.pid"
    Invoke-WslBash -Command $start

    for ($attempt = 0; $attempt -lt 40; $attempt++) {
        Start-Sleep -Milliseconds 250
        $status = (Invoke-WslBash -Command $check | Out-String).Trim()
        if ($status -eq "LISTENING") {
            return
        }
    }

    throw "RTSP server failed to open port $Port"
}

function Test-X11Display {
    param([Parameter(Mandatory = $true)][string]$Display)
    $probe = "export DISPLAY=$Display; xdpyinfo >/dev/null"
    Invoke-WslBash -Command $probe
}

function Start-Example47 {
    param(
        [Parameter(Mandatory = $true)][string]$BuildDirWslPath,
        [Parameter(Mandatory = $true)][string]$Display,
        [Parameter(Mandatory = $true)][string]$RtspTarget,
        [string]$DecoderId
    )

    $tempDir = "C:\Temp\swstack-wsl"
    New-Item -ItemType Directory -Path $tempDir -Force | Out-Null

    $launchScriptPath = Join-Path $tempDir ("launch-example47-" + [Guid]::NewGuid().ToString("N") + ".sh")
    $buildDirLiteral = Convert-ToBashLiteral -Value $BuildDirWslPath
    $rtspTargetLiteral = Convert-ToBashLiteral -Value $RtspTarget
    $decoderIdLiteral = if ([string]::IsNullOrWhiteSpace($DecoderId)) { "''" } else { Convert-ToBashLiteral -Value $DecoderId }
    $launch = @"
cd $buildDirLiteral
export DISPLAY=$Display
export LIBGL_ALWAYS_INDIRECT=1
export SW_RTSP_AUTOSTART=1
export SW_RTSP_URL=$rtspTargetLiteral
if [ -n $decoderIdLiteral ]; then export SW_RTSP_DECODER_ID=$decoderIdLiteral; fi
exec ./exemples/47-SocketTrafficMonitorSelfTest/SocketTrafficMonitorSelfTest
"@
    $normalized = ($launch -replace "`r`n", "`n" -replace "`r", "`n").TrimEnd("`n") + "`n"
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($launchScriptPath, $normalized, $utf8NoBom)

    $wslLaunchScriptPath = Convert-ToWslPath -WindowsPath $launchScriptPath
    return Start-Process -FilePath "wsl.exe" `
                         -ArgumentList "bash", $wslLaunchScriptPath `
                         -PassThru
}

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = Resolve-RepoPath -Path (Join-Path $PSScriptRoot "..\..")
} else {
    $RepoRoot = Resolve-RepoPath -Path $RepoRoot
}

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $RepoRoot "build-wsl-example47-va"
}
$BuildDir = Resolve-RepoPath -Path $BuildDir

$exampleBinary = Join-Path $BuildDir "exemples\47-SocketTrafficMonitorSelfTest\SocketTrafficMonitorSelfTest"
if (-not (Test-Path -LiteralPath $exampleBinary)) {
    throw "Example 47 binary not found at $exampleBinary"
}

$serverScript = Join-Path $RepoRoot "tools\rtsp\GstRtspTestServer.py"
if (-not (Test-Path -LiteralPath $serverScript)) {
    throw "RTSP server script not found at $serverScript"
}

Ensure-XServer -BinaryPath $XServerPath

$wslGateway = Get-WslGatewayIp
if ([string]::IsNullOrWhiteSpace($wslGateway)) {
    throw "Unable to resolve the WSL gateway IP"
}

$display = "${wslGateway}:0.0"
Test-X11Display -Display $display

if (-not $SkipRtspServer) {
    Start-WslRtspServer -ServerScriptWslPath (Convert-ToWslPath -WindowsPath $serverScript) -Port $RtspPort
}

$exampleLauncher = Start-Example47 -BuildDirWslPath (Convert-ToWslPath -WindowsPath $BuildDir) `
                                   -Display $display `
                                   -RtspTarget $RtspUrl `
                                   -DecoderId $DecoderId

Start-Sleep -Seconds 2
$clients = (Invoke-WslBash -Command "DISPLAY=$display xlsclients -display $display" | Out-String).Trim()
if ([string]::IsNullOrWhiteSpace($clients)) {
    $clients = (Invoke-WslBash -Command "DISPLAY=$display xwininfo -root -tree | sed -n '1,120p'" | Out-String).Trim()
}

Write-Host "[example47-x11] X server      : $XServerPath"
Write-Host "[example47-x11] DISPLAY       : $display"
Write-Host "[example47-x11] RTSP         : $RtspUrl"
Write-Host "[example47-x11] Decoder ID   : $DecoderId"
Write-Host "[example47-x11] Launcher PID  : $($exampleLauncher.Id)"
Write-Host "[example47-x11] RTSP log     : /tmp/sw_gst_rtsp_server.log"
Write-Host "[example47-x11] X clients:"
Write-Host $clients
