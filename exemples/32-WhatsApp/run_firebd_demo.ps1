param(
  [Parameter(Mandatory = $false)]
  [string]$Url = "",

  [Parameter(Mandatory = $false)]
  [string]$Auth = "",

  [Parameter(Mandatory = $false)]
  [int]$PollMs = 750,

  [Parameter(Mandatory = $false)]
  [string]$UserA = "alice",

  [Parameter(Mandatory = $false)]
  [string]$UserB = "bob"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\\..")
$exe = Join-Path $repoRoot "build-win\exemples\32-WhatsApp\Release\WhatsApp.exe"
if (!(Test-Path $exe)) {
  throw "WhatsApp.exe introuvable: $exe (build d'abord la cible WhatsApp Release)"
}

function Start-WhatsApp([string]$profile) {
  $args = @("--firebd-poll-ms", "$PollMs", "--profile", $profile)
  if ($Url -and $Url.Trim().Length -gt 0) {
    $args = @("--firebd-url", $Url) + $args
  }
  if ($Auth -and $Auth.Trim().Length -gt 0) {
    $args += @("--firebd-auth", $Auth)
  }
  Start-Process -FilePath $exe -ArgumentList $args | Out-Null
}

Start-WhatsApp $UserA
Start-WhatsApp $UserB

Write-Host "OK: 2 instances lancees ($UserA / $UserB) sur $Url"
