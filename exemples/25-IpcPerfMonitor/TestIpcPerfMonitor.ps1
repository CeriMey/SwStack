param(
  [string]$BuildDir = "build-win",
  [string]$Server = "perf/perfServer",
  [string]$Client = "perf/perfClient",
  [int]$Count = 2000,
  [int]$Warmup = 200,
  [int]$Payload = 0,
  [int]$TimeoutMs = 1000
)

$ErrorActionPreference = "Stop"

function Get-ExePath([string]$relative) {
  $p = Join-Path (Get-Location) $relative
  if (!(Test-Path $p)) { throw "Missing executable: $p" }
  return $p
}

$exe = Get-ExePath (Join-Path $BuildDir "exemples/25-IpcPerfMonitor/Release/IpcPerfMonitor.exe")

try {
  Get-Process IpcPerfMonitor -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

  Write-Host "[TEST] Starting server: $Server -> $Client"
  $srv = Start-Process -FilePath $exe -ArgumentList @(
    "--mode=server",
    "--self=$Server",
    "--peer=$Client"
  ) -PassThru -WindowStyle Hidden

  Start-Sleep -Milliseconds 300

  Write-Host "[TEST] Running client..."
  & $exe --mode=client --self=$Client --peer=$Server --count=$Count --warmup=$Warmup --payload=$Payload --timeout_ms=$TimeoutMs
}
finally {
  if ($srv) {
    Stop-Process -Id $srv.Id -Force -ErrorAction SilentlyContinue
  }
}

