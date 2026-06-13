param(
    [int]$Port = 5004,
    [string]$RtspPath = "video",
    [string]$Device = "OBSBOT Tiny 2 StreamCamera",
    [switch]$Webcam,
    [ValidateSet("mjpeg", "h264")]
    [string]$InputCodec = "mjpeg",
    [int]$Width = 1280,
    [int]$Height = 720,
    [int]$Fps = 30,
    [int]$BitrateKbps = 500,
    [ValidateSet("h264", "h265")]
    [string]$OutputCodec = "h264",
    [int]$GopFrames = 0,
    [switch]$IntraOnly,
    [switch]$TestPattern,
    [ValidateSet("smptebars", "smptehdbars", "testsrc", "testsrc2")]
    [string]$Pattern = "testsrc2",
    [switch]$SampleVideo,
    [string]$VideoFile = "",
    [int]$ApiPort = 9997,
    [string]$ViewerExe = "",
    [string]$DecoderId = "",
    [switch]$NoViewer,
    [switch]$NoRestart,
    [switch]$ReleaseCamera,
    [switch]$Stop
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Step {
    param([Parameter(Mandatory = $true)][string]$Message)
    Write-Host "[webcam-rtsp] $Message"
}

function Get-RequiredTool {
    param([Parameter(Mandatory = $true)][string]$Name)

    $tool = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $tool) {
        throw "$Name was not found in PATH"
    }
    return $tool.Source
}

function Quote-ProcessArgument {
    param([Parameter(Mandatory = $true)][string]$Value)

    if ($Value -notmatch '[\s"]') {
        return $Value
    }
    return '"' + ($Value -replace '"', '\"') + '"'
}

function Join-ProcessArguments {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    return ($Arguments | ForEach-Object { Quote-ProcessArgument -Value $_ }) -join " "
}

function Stop-ProcessId {
    param([int]$ProcessId)

    if ($ProcessId -le 0) {
        return
    }
    $process = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if ($null -ne $process) {
        Write-Step "stopping PID $ProcessId ($($process.ProcessName))"
        Stop-Process -Id $ProcessId -Force -ErrorAction SilentlyContinue
    }
}

function Stop-RecordedRun {
    param([Parameter(Mandatory = $true)][string]$StatePath)

    if (-not (Test-Path -LiteralPath $StatePath)) {
        return
    }

    try {
        $state = Get-Content -LiteralPath $StatePath -Raw | ConvertFrom-Json
        foreach ($propertyName in @("viewerPid", "ffmpegPid", "mediamtxPid")) {
            if ($state.PSObject.Properties.Name -contains $propertyName) {
                Stop-ProcessId -ProcessId ([int]$state.$propertyName)
            }
        }
    } finally {
        Remove-Item -LiteralPath $StatePath -Force -ErrorAction SilentlyContinue
    }
}

function Stop-MatchingRun {
    param(
        [Parameter(Mandatory = $true)][int]$RtspPort,
        [Parameter(Mandatory = $true)][string]$CleanRtspPath
    )

    $urlNeedle = "rtsp://127.0.0.1:$RtspPort/$CleanRtspPath"
    Get-CimInstance Win32_Process -Filter "name='ffmpeg.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -like "*$urlNeedle*" } |
        ForEach-Object { Stop-ProcessId -ProcessId ([int]$_.ProcessId) }

    Get-NetTCPConnection -LocalPort $RtspPort -State Listen -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty OwningProcess -Unique |
        ForEach-Object {
            $process = Get-Process -Id $_ -ErrorAction SilentlyContinue
            if ($null -ne $process -and $process.ProcessName -ieq "mediamtx") {
                Stop-ProcessId -ProcessId ([int]$_.ToString())
            }
        }
}

function Stop-CameraHolders {
    Get-Process WindowsCamera -ErrorAction SilentlyContinue |
        ForEach-Object { Stop-ProcessId -ProcessId $_.Id }

    Get-CimInstance Win32_Process -Filter "name='chrome.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -match 'video_capture\.mojom\.VideoCaptureService' } |
        ForEach-Object { Stop-ProcessId -ProcessId ([int]$_.ProcessId) }
}

function Start-LoggedProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$StdoutPath,
        [Parameter(Mandatory = $true)][string]$StderrPath,
        [string]$WorkingDirectory = ""
    )

    $argumentLine = Join-ProcessArguments -Arguments $Arguments
    $startArgs = @{
        FilePath = $FilePath
        ArgumentList = $argumentLine
        WindowStyle = "Hidden"
        RedirectStandardOutput = $StdoutPath
        RedirectStandardError = $StderrPath
        PassThru = $true
    }
    if (-not [string]::IsNullOrWhiteSpace($WorkingDirectory)) {
        $startArgs.WorkingDirectory = $WorkingDirectory
    }
    return Start-Process @startArgs
}

function Assert-ProcessRunning {
    param(
        [Parameter(Mandatory = $true)]$Process,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$StdoutPath,
        [Parameter(Mandatory = $true)][string]$StderrPath
    )

    if ($Process.HasExited) {
        Write-Host ""
        Write-Step "$Name exited with code $($Process.ExitCode)"
        Get-Content -LiteralPath $StdoutPath, $StderrPath -ErrorAction SilentlyContinue |
            Select-Object -Last 120 |
            ForEach-Object { Write-Host $_ }
        throw "$Name failed to start"
    }
}

function Test-PortAvailable {
    param([Parameter(Mandatory = $true)][int]$RtspPort)

    $listener = Get-NetTCPConnection -LocalPort $RtspPort -State Listen -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -ne $listener) {
        $owner = Get-Process -Id $listener.OwningProcess -ErrorAction SilentlyContinue
        $ownerName = if ($null -ne $owner) { $owner.ProcessName } else { "unknown" }
        throw "Port $RtspPort is already used by PID $($listener.OwningProcess) ($ownerName)"
    }
}

function New-SampleVideo {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string]$FfmpegPath,
        [Parameter(Mandatory = $true)][int]$VideoWidth,
        [Parameter(Mandatory = $true)][int]$VideoHeight,
        [Parameter(Mandatory = $true)][int]$VideoFps
    )

    if (Test-Path -LiteralPath $FilePath) {
        return
    }

    Write-Step "generating sample video $FilePath"
    & $FfmpegPath -hide_banner -loglevel error -y `
        -f lavfi `
        -i "testsrc2=size=${VideoWidth}x${VideoHeight}:rate=${VideoFps}:duration=20" `
        -vf "format=yuv420p" `
        -an `
        -c:v libx264 `
        -preset veryfast `
        -profile:v baseline `
        -b:v 2500k `
        -g $VideoFps `
        -keyint_min $VideoFps `
        -bf 0 `
        -movflags +faststart `
        $FilePath
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to generate sample video"
    }
}

$cleanRtspPath = $RtspPath.Trim("/")
if ([string]::IsNullOrWhiteSpace($cleanRtspPath)) {
    throw "RtspPath cannot be empty"
}

$selectedSourceCount = 0
if ($Webcam) {
    $selectedSourceCount++
}
if ($TestPattern) {
    $selectedSourceCount++
}
if ($SampleVideo) {
    $selectedSourceCount++
}
if (-not [string]::IsNullOrWhiteSpace($VideoFile)) {
    $selectedSourceCount++
}
if ($selectedSourceCount -gt 1) {
    throw "Choose only one source: -Webcam, -TestPattern, -SampleVideo, or -VideoFile."
}

$stateRoot = Join-Path $env:TEMP "swstack-webcam-rtsp"
$stateDir = Join-Path $stateRoot ("$Port-$cleanRtspPath")
$statePath = Join-Path $stateDir "state.json"
$configPath = Join-Path $stateDir "mediamtx.yml"
$mediamtxOut = Join-Path $stateDir "mediamtx.out.log"
$mediamtxErr = Join-Path $stateDir "mediamtx.err.log"
$ffmpegOut = Join-Path $stateDir "ffmpeg.out.log"
$ffmpegErr = Join-Path $stateDir "ffmpeg.err.log"

New-Item -ItemType Directory -Force -Path $stateDir | Out-Null

if ($Stop) {
    Stop-RecordedRun -StatePath $statePath
    Stop-MatchingRun -RtspPort $Port -CleanRtspPath $cleanRtspPath
    Write-Step "stopped rtsp://127.0.0.1:$Port/$cleanRtspPath"
    return
}

if (-not $NoRestart) {
    Stop-RecordedRun -StatePath $statePath
    Stop-MatchingRun -RtspPort $Port -CleanRtspPath $cleanRtspPath
    Start-Sleep -Milliseconds 500
}

if ($ReleaseCamera) {
    Stop-CameraHolders
    Start-Sleep -Milliseconds 800
}

Test-PortAvailable -RtspPort $Port

$mediamtx = Get-RequiredTool -Name "mediamtx"
$ffmpeg = Get-RequiredTool -Name "ffmpeg"
$ffprobe = Get-RequiredTool -Name "ffprobe"

Remove-Item -LiteralPath $mediamtxOut, $mediamtxErr, $ffmpegOut, $ffmpegErr -ErrorAction SilentlyContinue

@"
logLevel: info
rtspAddress: :$Port
rtspTransports: [tcp]
api: yes
apiAddress: 127.0.0.1:$ApiPort
paths:
  ${cleanRtspPath}:
    source: publisher
"@ | Set-Content -LiteralPath $configPath -Encoding ASCII

Write-Step "starting MediaMTX on rtsp://127.0.0.1:$Port/$cleanRtspPath"
$mediamtxProcess = Start-LoggedProcess `
    -FilePath $mediamtx `
    -Arguments @($configPath) `
    -StdoutPath $mediamtxOut `
    -StderrPath $mediamtxErr

Start-Sleep -Seconds 2
Assert-ProcessRunning -Process $mediamtxProcess -Name "MediaMTX" -StdoutPath $mediamtxOut -StderrPath $mediamtxErr

$bitrate = "${BitrateKbps}k"
$bufferSize = "$([Math]::Max($BitrateKbps * 2, $BitrateKbps))k"
$rtspUrl = "rtsp://127.0.0.1:$Port/$cleanRtspPath"

if ($SampleVideo -and [string]::IsNullOrWhiteSpace($VideoFile)) {
    $VideoFile = Join-Path $stateDir "sample-video.mp4"
    New-SampleVideo -FilePath $VideoFile -FfmpegPath $ffmpeg -VideoWidth $Width -VideoHeight $Height -VideoFps $Fps
}

$ffmpegInputArgs = if (-not [string]::IsNullOrWhiteSpace($VideoFile)) {
    $resolvedVideoFile = (Resolve-Path -LiteralPath $VideoFile).Path
    @(
        "-hide_banner", "-loglevel", "info",
        "-re",
        "-stream_loop", "-1",
        "-i", $resolvedVideoFile
    )
} elseif ($TestPattern) {
    @(
        "-hide_banner", "-loglevel", "info",
        "-re",
        "-f", "lavfi",
        "-i", "${Pattern}=size=${Width}x${Height}:rate=$Fps"
    )
} else {
    @(
        "-hide_banner", "-loglevel", "info",
        "-f", "dshow",
        "-rtbufsize", "64M",
        "-video_size", "${Width}x${Height}",
        "-framerate", "$Fps",
        "-vcodec", $InputCodec,
        "-i", "video=$Device"
    )
}

$videoFilter = if (-not [string]::IsNullOrWhiteSpace($VideoFile)) {
    "fps=$Fps,scale=${Width}:${Height}:force_original_aspect_ratio=decrease,pad=${Width}:${Height}:(ow-iw)/2:(oh-ih)/2,format=yuv420p"
} else {
    "format=yuv420p"
}

$codecLabel = if ($OutputCodec -eq "h265") { "H.265" } else { "H.264" }
$gopSize = if ($IntraOnly) {
    1
} elseif ($GopFrames -gt 0) {
    $GopFrames
} else {
    $Fps
}
$gopLabel = if ($IntraOnly) { "intra-only" } else { "gop-${gopSize}" }
$encoderArgs = if ($OutputCodec -eq "h265") {
    $x265Params = if ($IntraOnly) {
        "repeat-headers=1:aud=1:keyint=1:min-keyint=1:scenecut=0:bframes=0:ref=1:rc-lookahead=0:vbv-maxrate=${BitrateKbps}:vbv-bufsize=$([Math]::Max($BitrateKbps * 2, $BitrateKbps))"
    } else {
        "repeat-headers=1:aud=1:open-gop=0:keyint=${Fps}:min-keyint=${Fps}:scenecut=0:bframes=0:ref=1:rc-lookahead=0:vbv-maxrate=${BitrateKbps}:vbv-bufsize=$([Math]::Max($BitrateKbps * 2, $BitrateKbps))"
    }
    @(
        "-c:v", "libx265",
        "-preset", "veryfast",
        "-tune", "zerolatency",
        "-profile:v", "main",
        "-b:v", $bitrate,
        "-maxrate", $bitrate,
        "-bufsize", $bufferSize,
        "-g", "$gopSize",
        "-keyint_min", "$gopSize",
        "-bf", "0",
        "-x265-params", $x265Params
    )
} else {
    @(
        "-c:v", "libx264",
        "-preset", "veryfast",
        "-tune", "zerolatency",
        "-profile:v", "baseline",
        "-b:v", $bitrate,
        "-maxrate", $bitrate,
        "-bufsize", $bufferSize,
        "-g", "$gopSize",
        "-keyint_min", "$gopSize",
        "-bf", "0",
        "-x264-params", "scenecut=0:repeat-headers=1"
    )
}

$ffmpegArgs = $ffmpegInputArgs + @(
    "-an",
    "-vf", $videoFilter
) + $encoderArgs + @(
    "-f", "rtsp",
    "-rtsp_transport", "tcp",
    $rtspUrl
)

$sourceLabel = if (-not [string]::IsNullOrWhiteSpace($VideoFile)) {
    "video file $VideoFile"
} elseif ($TestPattern) {
    "test pattern $Pattern"
} else {
    $Device
}
Write-Step "publishing $sourceLabel as $codecLabel ${Width}x${Height}@$Fps ${BitrateKbps}k $gopLabel"
$ffmpegProcess = Start-LoggedProcess `
    -FilePath $ffmpeg `
    -Arguments $ffmpegArgs `
    -StdoutPath $ffmpegOut `
    -StderrPath $ffmpegErr

Start-Sleep -Seconds 5
Assert-ProcessRunning -Process $ffmpegProcess -Name "FFmpeg" -StdoutPath $ffmpegOut -StderrPath $ffmpegErr

Write-Step "checking stream with ffprobe"
$probeOutput = & $ffprobe -hide_banner -loglevel error -rtsp_transport tcp `
    -select_streams v:0 `
    -show_entries stream=codec_name,width,height,r_frame_rate,avg_frame_rate `
    -of default=noprint_wrappers=1 `
    $rtspUrl
if ($LASTEXITCODE -ne 0) {
    throw "ffprobe failed against $rtspUrl"
}

$viewerProcess = $null
$viewerRtspUrl = "$rtspUrl`?transport=tcp"
if (-not [string]::IsNullOrWhiteSpace($DecoderId)) {
    $viewerRtspUrl += "&decoder=$DecoderId"
}
if (-not $NoViewer) {
    if ([string]::IsNullOrWhiteSpace($ViewerExe)) {
        $repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path
        $ViewerExe = Join-Path $repoRoot "build-rtsp-webcam\exemples\47-SocketTrafficMonitorSelfTest\Release\SocketTrafficMonitorSelfTest.exe"
    }

    if (Test-Path -LiteralPath $ViewerExe) {
        Write-Step "starting SwStack viewer"
        $env:SW_RTSP_URL = $viewerRtspUrl
        $env:SW_RTSP_AUTOSTART = "1"
        if (-not [string]::IsNullOrWhiteSpace($DecoderId)) {
            $env:SW_RTSP_DECODER_ID = $DecoderId
        } else {
            Remove-Item Env:\SW_RTSP_DECODER_ID -ErrorAction SilentlyContinue
        }
        $viewerProcess = Start-Process -FilePath $ViewerExe `
            -WorkingDirectory (Split-Path -LiteralPath $ViewerExe) `
            -WindowStyle Normal `
            -PassThru
    } else {
        Write-Step "viewer not found: $ViewerExe"
    }
}

$state = [ordered]@{
    url = $viewerRtspUrl
    mediamtxPid = $mediamtxProcess.Id
    ffmpegPid = $ffmpegProcess.Id
    mediamtxLog = $mediamtxErr
    ffmpegLog = $ffmpegErr
    source = $sourceLabel
    outputCodec = $OutputCodec
    intraOnly = [bool]$IntraOnly
    decoderId = $DecoderId
    startedAt = (Get-Date).ToString("o")
}
if ($null -ne $viewerProcess) {
    $state.viewerPid = $viewerProcess.Id
}
$state | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $statePath -Encoding ASCII

Write-Host ""
Write-Step "ready"
Write-Host "URL: $viewerRtspUrl"
Write-Host "MediaMTX PID: $($mediamtxProcess.Id)"
Write-Host "FFmpeg PID: $($ffmpegProcess.Id)"
if ($null -ne $viewerProcess) {
    Write-Host "SwStack viewer PID: $($viewerProcess.Id)"
}
Write-Host "Probe:"
$probeOutput | ForEach-Object { Write-Host "  $_" }
Write-Host "Stop: powershell -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Stop"
