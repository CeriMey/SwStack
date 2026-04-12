param(
    [string]$RepoRoot = "",
    [string]$BuildDir = "",
    [int]$PacketCount = 100,
    [int]$TimeoutMs = 15000,
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-NormalizedPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return ((Resolve-Path -LiteralPath $Path).Path -replace "\\", "/")
}

function Find-FirstExistingPath {
    param([string[]]$Candidates)
    foreach ($candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $null
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Description,
        [switch]$AllowFailure
    )

    Write-Host "[rtsp-selftest] $Description"
    & $FilePath @Arguments
    $exitCode = $LASTEXITCODE
    if (-not $AllowFailure -and $exitCode -ne 0) {
        throw "$Description failed with exit code $exitCode"
    }
    return $exitCode
}

function Start-LoggedProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$StdoutPath,
        [Parameter(Mandatory = $true)][string]$StderrPath
    )

    $escapedArguments = ($Arguments | ForEach-Object {
        if ($_ -match '[\s"]') {
            '"' + ($_ -replace '"', '\"') + '"'
        } else {
            $_
        }
    }) -join " "

    return Start-Process -FilePath $FilePath `
                         -ArgumentList $escapedArguments `
                         -PassThru `
                         -NoNewWindow `
                         -RedirectStandardOutput $StdoutPath `
                         -RedirectStandardError $StderrPath
}

function Stop-ProcessIfRunning {
    param($Process)
    if ($null -eq $Process) {
        return
    }
    try {
        if (-not $Process.HasExited) {
            Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
            $Process.WaitForExit()
        }
    } catch {
    }
}

function Wait-ForLogPattern {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [int]$TimeoutSeconds = 10
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $Path) {
            $content = Get-Content -LiteralPath $Path -Raw
            if ($content -match $Pattern) {
                return $true
            }
        }
        Start-Sleep -Milliseconds 200
    }
    return $false
}

function Test-Ipv6LoopbackPort {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [int]$TimeoutSeconds = 5
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $client = $null
        try {
            $client = [System.Net.Sockets.TcpClient]::new([System.Net.Sockets.AddressFamily]::InterNetworkV6)
            $asyncResult = $client.BeginConnect("::1", $Port, $null, $null)
            if ($asyncResult.AsyncWaitHandle.WaitOne(500)) {
                $client.EndConnect($asyncResult)
                $client.Close()
                return $true
            }
            $client.Close()
        } catch {
            if ($client) {
                try { $client.Close() } catch {}
            }
        }
        Start-Sleep -Milliseconds 200
    }
    return $false
}

function Find-BuiltExecutable {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Name
    )
    $match = Get-ChildItem -LiteralPath $Root -Recurse -Filter $Name -File |
        Sort-Object FullName |
        Select-Object -First 1
    if ($null -eq $match) {
        throw "Unable to locate built executable $Name under $Root"
    }
    return $match.FullName
}

function New-MediaMtxConfig {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][ValidateSet("none", "basic", "digest")] [string]$AuthMode,
        [switch]$EnableTls,
        [string]$CertPath = "",
        [string]$KeyPath = ""
    )

    $users = @"
authInternalUsers:
- user: any
  pass:
  permissions:
  - action: publish
    path: test
- user: viewer
  pass: secret
  permissions:
  - action: read
    path: test
- user: any
  pass:
  ips: ['127.0.0.1', '::1']
  permissions:
  - action: api
  - action: metrics
  - action: pprof
"@

    if ($AuthMode -eq "none") {
        $users = @"
authInternalUsers:
- user: any
  pass:
  permissions:
  - action: publish
    path: test
  - action: read
    path: test
- user: any
  pass:
  ips: ['127.0.0.1', '::1']
  permissions:
  - action: api
  - action: metrics
  - action: pprof
"@
    }

    $rtspAuthMethods = if ($AuthMode -eq "digest") { "[digest]" } else { "[basic]" }
    $rtspEncryption = if ($EnableTls) { '"optional"' } else { '"no"' }
    $rtspTlsSection = ""
    if ($EnableTls) {
        $normalizedCert = Resolve-NormalizedPath -Path $CertPath
        $normalizedKey = Resolve-NormalizedPath -Path $KeyPath
        $rtspTlsSection = @"
rtspsAddress: :8322
rtspServerKey: $normalizedKey
rtspServerCert: $normalizedCert
"@
    }

$content = @"
logLevel: info
logDestinations: [stdout]
readTimeout: 10s
writeTimeout: 10s
authMethod: internal
$users
rtspAddress: :8554
rtspEncryption: $rtspEncryption
$rtspTlsSection
rtpAddress: :8000
rtcpAddress: :8001
rtspAuthMethods: $rtspAuthMethods
paths:
  test:
"@

    Set-Content -LiteralPath $Path -Value $content -NoNewline
}

function Invoke-ClientCase {
    param(
        [Parameter(Mandatory = $true)][string]$ClientExe,
        [Parameter(Mandatory = $true)][string]$Url,
        [Parameter(Mandatory = $true)][string]$DumpPath,
        [Parameter(Mandatory = $true)][string]$StdoutPath,
        [Parameter(Mandatory = $true)][string]$StderrPath,
        [Parameter(Mandatory = $true)][int]$TimeoutMs,
        [Parameter(Mandatory = $true)][uint64]$MaxPackets,
        [string]$TrustedCa = ""
    )

    $arguments = @(
        "--url", $Url,
        "--dump", $DumpPath,
        "--timeout-ms", "$TimeoutMs",
        "--max-packets", "$MaxPackets"
    )
    if (-not [string]::IsNullOrWhiteSpace($TrustedCa)) {
        $arguments += @("--trusted-ca", $TrustedCa)
    }

    $escapedArguments = ($arguments | ForEach-Object {
        if ($_ -match '[\s"]') {
            '"' + ($_ -replace '"', '\"') + '"'
        } else {
            $_
        }
    }) -join " "

    $process = Start-Process -FilePath $ClientExe `
                             -ArgumentList $escapedArguments `
                             -PassThru `
                             -Wait `
                             -NoNewWindow `
                             -RedirectStandardOutput $StdoutPath `
                             -RedirectStandardError $StderrPath
    $process.Refresh()
    return [int]$process.ExitCode
}

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\\..")).Path
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $RepoRoot "build-rtsp-validation"
}

$mediaMtxExe = Find-FirstExistingPath @(
    "$env:LOCALAPPDATA\\Microsoft\\WinGet\\Packages\\bluenviron.mediamtx_Microsoft.Winget.Source_8wekyb3d8bbwe\\mediamtx.exe"
)
$ffmpegExe = Find-FirstExistingPath @(
    "$env:LOCALAPPDATA\\Microsoft\\WinGet\\Packages\\Gyan.FFmpeg.Essentials_Microsoft.Winget.Source_8wekyb3d8bbwe\\ffmpeg-8.1-essentials_build\\bin\\ffmpeg.exe"
)
$opensslExe = Find-FirstExistingPath @(
    "C:\\Program Files\\Git\\mingw64\\bin\\openssl.exe",
    "C:\\Program Files\\Git\\usr\\bin\\openssl.exe"
)

if (-not $mediaMtxExe) {
    throw "mediamtx.exe not found"
}
if (-not $ffmpegExe) {
    throw "ffmpeg.exe not found"
}
if (-not $opensslExe) {
    throw "openssl.exe not found"
}

if (-not $SkipBuild) {
    Invoke-Checked -FilePath "cmake" `
                   -Arguments @(
                       "-S", $RepoRoot,
                       "-B", $BuildDir,
                       "-DSW_BUILD_exemples_19_RtspUdpClient=ON",
                       "-DSW_BUILD_exemples_47_SocketTrafficMonitorSelfTest=ON"
                   ) `
                   -Description "Configuring RTSP validation build"
    Invoke-Checked -FilePath "cmake" `
                   -Arguments @("--build", $BuildDir, "--config", "Release", "--target", "RtspUdpClient", "SocketTrafficMonitorSelfTest") `
                   -Description "Building RTSP validation targets"
}

$clientExe = Find-BuiltExecutable -Root $BuildDir -Name "RtspUdpClient.exe"
$runtimeDir = Join-Path $BuildDir "rtsp-selftest"
New-Item -ItemType Directory -Path $runtimeDir -Force | Out-Null

$certPath = Join-Path $runtimeDir "localhost_cert.pem"
$keyPath = Join-Path $runtimeDir "localhost_key.pem"
Invoke-Checked -FilePath $opensslExe `
               -Arguments @(
                   "req", "-x509", "-newkey", "rsa:2048", "-sha256", "-days", "2", "-nodes",
                   "-subj", "/CN=127.0.0.1",
                   "-addext", "subjectAltName=IP:127.0.0.1,DNS:localhost",
                   "-keyout", $keyPath,
                   "-out", $certPath
               ) `
               -Description "Generating local RTSPS certificate"

$results = @()

function Run-Case {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][ValidateSet("none", "basic", "digest")] [string]$AuthMode,
        [switch]$EnableTls
    )

    $caseDir = Join-Path $runtimeDir $Name
    New-Item -ItemType Directory -Path $caseDir -Force | Out-Null

    $configPath = Join-Path $caseDir "mediamtx.yml"
    $serverOut = Join-Path $caseDir "mediamtx.stdout.log"
    $serverErr = Join-Path $caseDir "mediamtx.stderr.log"
    $publisherOut = Join-Path $caseDir "publisher.stdout.log"
    $publisherErr = Join-Path $caseDir "publisher.stderr.log"
    $clientOut = Join-Path $caseDir "client.stdout.log"
    $clientErr = Join-Path $caseDir "client.stderr.log"
    $dumpPath = Join-Path $caseDir "client.dump.bin"

    New-MediaMtxConfig -Path $configPath -AuthMode $AuthMode -EnableTls:$EnableTls -CertPath $certPath -KeyPath $keyPath

    $mediaMtxProcess = $null
    $publisherProcess = $null
    try {
        $mediaMtxProcess = Start-LoggedProcess -FilePath $mediaMtxExe `
                                               -Arguments @($configPath) `
                                               -StdoutPath $serverOut `
                                               -StderrPath $serverErr
        if (-not (Wait-ForLogPattern -Path $serverOut -Pattern "listener opened" -TimeoutSeconds 8)) {
            throw "mediamtx did not report startup for $Name"
        }

        $publisherProcess = Start-LoggedProcess -FilePath $ffmpegExe `
                                                -Arguments @(
                                                    "-hide_banner", "-loglevel", "error",
                                                    "-re",
                                                    "-f", "lavfi", "-i", "testsrc=size=640x360:rate=25",
                                                    "-c:v", "libx264",
                                                    "-pix_fmt", "yuv420p",
                                                    "-preset", "ultrafast",
                                                    "-tune", "zerolatency",
                                                    "-g", "25",
                                                    "-an",
                                                    "-f", "rtsp",
                                                    "-rtsp_transport", "tcp",
                                                    "rtsp://127.0.0.1:8554/test"
                                                ) `
                                                -StdoutPath $publisherOut `
                                                -StderrPath $publisherErr
        Start-Sleep -Seconds 2

        $plainUrl = "rtsp://127.0.0.1:8554/test"
        $authedPlainUrl = "rtsp://viewer:secret@127.0.0.1:8554/test"
        $tlsUrl = "rtsps://viewer:secret@127.0.0.1:8322/test?transport=tcp"

        switch ($AuthMode) {
        "none" {
            $exitCode = Invoke-ClientCase -ClientExe $clientExe `
                                          -Url $plainUrl `
                                          -DumpPath $dumpPath `
                                          -StdoutPath $clientOut `
                                          -StderrPath $clientErr `
                                          -TimeoutMs $TimeoutMs `
                                          -MaxPackets $PacketCount
            if ($exitCode -ne 0) {
                throw "Anonymous UDP case failed with exit code $exitCode"
            }
            $serverLog = Get-Content -LiteralPath $serverOut -Raw
            $clientLog = Get-Content -LiteralPath $clientOut -Raw
            if ($clientLog -notmatch "first codec=" -or $clientLog -notmatch "packets=$PacketCount") {
                throw "Anonymous UDP case did not decode the expected packet count"
            }
            if ($serverLog -notmatch "with UDP") {
                throw "Anonymous UDP case was not served over UDP"
            }

            if (Test-Ipv6LoopbackPort -Port 8554 -TimeoutSeconds 3) {
                $ipv6ClientOut = Join-Path $caseDir "client-ipv6.out.log"
                $ipv6ClientErr = Join-Path $caseDir "client-ipv6.err.log"
                $ipv6DumpPath = Join-Path $caseDir "client-ipv6.dump.h264"
                $ipv6Url = "rtsp://[::1]:8554/test"
                $ipv6ExitCode = Invoke-ClientCase -ClientExe $clientExe `
                                                  -Url $ipv6Url `
                                                  -DumpPath $ipv6DumpPath `
                                                  -StdoutPath $ipv6ClientOut `
                                                  -StderrPath $ipv6ClientErr `
                                                  -TimeoutMs $TimeoutMs `
                                                  -MaxPackets $PacketCount
                if ($ipv6ExitCode -ne 0) {
                    throw "IPv6 UDP case failed with exit code $ipv6ExitCode"
                }
                $ipv6ServerLog = Get-Content -LiteralPath $serverOut -Raw
                $ipv6ClientLog = Get-Content -LiteralPath $ipv6ClientOut -Raw
                if ($ipv6ClientLog -notmatch "first codec=" -or $ipv6ClientLog -notmatch "packets=$PacketCount") {
                    throw "IPv6 UDP case did not decode the expected packet count"
                }
                if ($ipv6ServerLog -notmatch "created by \[::1\]" -or $ipv6ServerLog -notmatch "is reading from path 'test', with UDP") {
                    throw "IPv6 UDP case was not served over IPv6 UDP"
                }
            }
        }
        "basic" {
            $unauthorizedExit = Invoke-ClientCase -ClientExe $clientExe `
                                                  -Url $plainUrl `
                                                  -DumpPath $dumpPath `
                                                  -StdoutPath $clientOut `
                                                  -StderrPath $clientErr `
                                                  -TimeoutMs 4000 `
                                                  -MaxPackets 1
            if ($unauthorizedExit -eq 0) {
                throw "Basic-auth negative case unexpectedly succeeded"
            }

            $exitCode = Invoke-ClientCase -ClientExe $clientExe `
                                          -Url $authedPlainUrl `
                                          -DumpPath $dumpPath `
                                          -StdoutPath $clientOut `
                                          -StderrPath $clientErr `
                                          -TimeoutMs $TimeoutMs `
                                          -MaxPackets $PacketCount
            if ($exitCode -ne 0) {
                throw "Basic-auth positive case failed with exit code $exitCode"
            }
            $clientLog = Get-Content -LiteralPath $clientOut -Raw
            if ($clientLog -notmatch "first codec=" -or $clientLog -notmatch "packets=$PacketCount") {
                throw "Basic-auth positive case did not decode the expected packet count"
            }
        }
        "digest" {
            $unauthorizedExit = Invoke-ClientCase -ClientExe $clientExe `
                                                  -Url $plainUrl `
                                                  -DumpPath $dumpPath `
                                                  -StdoutPath $clientOut `
                                                  -StderrPath $clientErr `
                                                  -TimeoutMs 4000 `
                                                  -MaxPackets 1
            if ($unauthorizedExit -eq 0) {
                throw "Digest negative case unexpectedly succeeded"
            }

            $exitCode = Invoke-ClientCase -ClientExe $clientExe `
                                          -Url $authedPlainUrl `
                                          -DumpPath $dumpPath `
                                          -StdoutPath $clientOut `
                                          -StderrPath $clientErr `
                                          -TimeoutMs $TimeoutMs `
                                          -MaxPackets $PacketCount
            if ($exitCode -ne 0) {
                throw "Digest positive case failed with exit code $exitCode"
            }
            $clientLog = Get-Content -LiteralPath $clientOut -Raw
            if ($clientLog -notmatch "first codec=" -or $clientLog -notmatch "packets=$PacketCount") {
                throw "Digest positive case did not decode the expected packet count"
            }
        }
        }

        if ($EnableTls) {
            $exitCode = Invoke-ClientCase -ClientExe $clientExe `
                                          -Url $tlsUrl `
                                          -DumpPath $dumpPath `
                                          -StdoutPath $clientOut `
                                          -StderrPath $clientErr `
                                          -TimeoutMs $TimeoutMs `
                                          -MaxPackets $PacketCount `
                                          -TrustedCa $certPath
            if ($exitCode -ne 0) {
                throw "RTSPS case failed with exit code $exitCode"
            }
            $clientLog = Get-Content -LiteralPath $clientOut -Raw
            if ($clientLog -notmatch "first codec=" -or $clientLog -notmatch "packets=$PacketCount") {
                throw "RTSPS case did not decode the expected packet count"
            }
            $serverLog = Get-Content -LiteralPath $serverOut -Raw
            if ($serverLog -notmatch "RTSPS") {
                throw "RTSPS case did not hit the TLS listener"
            }
        }

        $script:results += "[ok] $Name"
    } finally {
        Stop-ProcessIfRunning -Process $publisherProcess
        Stop-ProcessIfRunning -Process $mediaMtxProcess
    }
}

Run-Case -Name "rtsp-udp-anon" -AuthMode "none"
Run-Case -Name "rtsp-udp-basic" -AuthMode "basic"
Run-Case -Name "rtsp-udp-digest" -AuthMode "digest"
Run-Case -Name "rtsps-basic" -AuthMode "basic" -EnableTls

Write-Host "[rtsp-selftest] Completed:"
$results | ForEach-Object { Write-Host "  $_" }
