param(
  [int]$Port = 8088,
  [string]$BuildDir = "build-win",
  [string]$Domain = "demo",
  [string]$Device = "device1",
  [string]$ConfigId = "DemoDevice",
  [int]$AppearTimeoutSec = 10,
  [int]$DisappearTimeoutSec = 20
)

$ErrorActionPreference = "Stop"

function Get-ExePath([string]$relative) {
  $p = Join-Path (Get-Location) $relative
  if (!(Test-Path $p)) { throw "Missing executable: $p" }
  return $p
}

function Get-Json([string]$url) {
  return Invoke-RestMethod -Method GET -Uri $url -TimeoutSec 3
}

function Wait-Until([int]$timeoutSec, [scriptblock]$predicate, [string]$why) {
  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  while ($sw.Elapsed.TotalSeconds -lt $timeoutSec) {
    try {
      if (& $predicate) { return $true }
    } catch {
      # ignore transient
    }
    Start-Sleep -Milliseconds 300
  }
  throw "Timeout ($timeoutSec s): $why"
}

$root = Get-Location
$webExe  = Get-ExePath (Join-Path $BuildDir "tools/SwNode/SwAPI/SwBridge/Release/SwBridge.exe")
$devExe  = Get-ExePath (Join-Path $BuildDir "exemples/23-ConfigurableObjectDemo/Release/ConfigurableObjectDemo.exe")
$baseUrl = "http://127.0.0.1:$Port"

$webProc = $null
$devProc = $null

try {
  # Ensure previous manual runs don't keep the .exe locked (LNK1104).
  Get-Process SwBridge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

  Write-Host "[TEST] Starting SwBridge on port $Port..."
  $webProc = Start-Process -FilePath $webExe -ArgumentList @("$Port") -PassThru -WindowStyle Hidden
  Start-Sleep -Milliseconds 500

  Write-Host "[TEST] Starting device (debug subscriber) $Domain/$Device $ConfigId..."
  $devProc = Start-Process -FilePath $devExe -ArgumentList @("$Domain/$Device", "$ConfigId") -PassThru -WindowStyle Hidden

  $null = Wait-Until $AppearTimeoutSec {
    $apps = Get-Json "$baseUrl/api/apps"
    foreach ($a in $apps) {
      if ($a.domain -eq $Domain -and $a.clientCount -ge 1) { return $true }
    }
    return $false
  } "apps registry should contain domain '$Domain'"

  $null = Wait-Until $AppearTimeoutSec {
    $devs = Get-Json "$baseUrl/api/devices?domain=$Domain"
    foreach ($d in $devs) {
      if ($d.object -eq $Device) { return $true }
    }
    return $false
  } "devices should contain '$Device' for domain '$Domain'"

  Write-Host "[TEST] OK: device appeared"

  $null = Wait-Until $AppearTimeoutSec {
    $conns = Get-Json "$baseUrl/api/connections?target=$Domain/$Device"
    foreach ($c in $conns) {
      if ($c.object -eq $Device -and $c.signal -eq "ping" -and $c.subPid -eq $devProc.Id) { return $true }
    }
    return $false
  } "connections should contain a subscription entry for pid=$($devProc.Id) on signal 'ping'"

  Write-Host "[TEST] OK: connection entry present"

  Write-Host "[TEST] Killing device PID=$($devProc.Id)..."
  Stop-Process -Id $devProc.Id -Force
  $devProc = $null

  $null = Wait-Until $DisappearTimeoutSec {
    $devs = Get-Json "$baseUrl/api/devices?domain=$Domain"
    foreach ($d in $devs) {
      if ($d.object -eq $Device) { return $false }
    }
    return $true
  } "device '$Device' should disappear after kill (TTL-based cleanup)"

  Write-Host "[TEST] OK: device disappeared"

  Write-Host "[TEST] Restarting device..."
  $devProc = Start-Process -FilePath $devExe -ArgumentList @("$Domain/$Device", "$ConfigId") -PassThru -WindowStyle Hidden

  $null = Wait-Until $AppearTimeoutSec {
    $devs = Get-Json "$baseUrl/api/devices?domain=$Domain"
    foreach ($d in $devs) {
      if ($d.object -eq $Device) { return $true }
    }
    return $false
  } "device '$Device' should re-appear after restart"

  Write-Host "[TEST] OK: device re-appeared"

  Write-Host "[TEST] Sending configAll with arrays/objects..."
  $cfg = @{
    lists = @{
      tags = @("ir","rgb")
      histogram = @(9,8,7)
      any = @("x", 2, $true)
    }
    maps = @{
      thresholds = @{ low = 5; high = 99 }
      any = @{ mode = "manual"; enabled = $false; gain = 3.5 }
    }
  }
  $body = @{ target = "$Domain/$Device"; config = $cfg } | ConvertTo-Json -Depth 8
  $resp = Invoke-RestMethod -Method POST -Uri "$baseUrl/api/configAll" -Body $body -ContentType "application/json" -TimeoutSec 3
  if (-not $resp.ok) { throw "configAll failed" }

  $null = Wait-Until $AppearTimeoutSec {
    $c = Get-Json "$baseUrl/api/config?target=$Domain/$Device"
    return ($c.lists.tags.Count -eq 2 -and $c.lists.tags[0] -eq "ir" -and $c.maps.thresholds.low -eq 5)
  } "configAll should be reflected in /api/config"

  Write-Host "[TEST] OK: configAll applied"
  Write-Host "[TEST] PASS"
}
finally {
  if ($devProc -ne $null) {
    try { Stop-Process -Id $devProc.Id -Force } catch {}
  }
  if ($webProc -ne $null) {
    try { Stop-Process -Id $webProc.Id -Force } catch {}
  }
}
