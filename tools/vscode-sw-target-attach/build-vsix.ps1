$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$packDir = Join-Path $here "_packaging"
$outDir = Join-Path $here "dist"

New-Item -ItemType Directory -Force -Path $packDir | Out-Null
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$packJsonPath = Join-Path $packDir "package.json"
if (-not (Test-Path $packJsonPath)) {
    @'
{
  "private": true,
  "name": "sw-target-attach-packaging",
  "version": "0.0.0",
  "dependencies": {
    "vsce": "2.15.0"
  },
  "overrides": {
    "cheerio": "1.0.0"
  }
}
'@ | Set-Content -Encoding UTF8 -Path $packJsonPath
}

Push-Location $packDir
try {
    if (-not (Test-Path (Join-Path $packDir "node_modules"))) {
        npm install | Out-Host
    }
} finally {
    Pop-Location
}

Push-Location $here
try {
    $vsce = Join-Path $packDir "node_modules\\.bin\\vsce.cmd"
    if (-not (Test-Path $vsce)) {
        throw "vsce not found at: $vsce"
    }

    & $vsce package --out (Join-Path $outDir "sw-target-attach.vsix") | Out-Host
    Write-Host ""
    Write-Host "VSIX built:"
    Write-Host "  $outDir\\sw-target-attach.vsix"
} finally {
    Pop-Location
}

