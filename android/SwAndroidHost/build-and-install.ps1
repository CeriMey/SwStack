param(
    [string]$Abi = "x86_64",
    [int]$Api = 36,
    [switch]$Launch
)

$ErrorActionPreference = "Stop"

function Invoke-External {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(ValueFromRemainingArguments = $true)]
        [object[]]$Arguments
    )

    $flatArguments = @()
    foreach ($argument in $Arguments) {
        if ($argument -is [System.Array]) {
            foreach ($nestedArgument in $argument) {
                $flatArguments += [string]$nestedArgument
            }
            continue
        }
        $flatArguments += [string]$argument
    }

    & $FilePath @flatArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed ($LASTEXITCODE): $FilePath $($flatArguments -join ' ')"
    }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$cmake = (Get-Command cmake.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source)
if (-not $cmake) {
    $vsCmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path $vsCmake) {
        $cmake = $vsCmake
    }
}
if (-not $cmake) {
    throw "cmake.exe was not found. Install CMake or Visual Studio CMake tooling."
}

$sdkRoot = if ($env:ANDROID_SDK_ROOT) { $env:ANDROID_SDK_ROOT } else { Join-Path $env:LOCALAPPDATA "Android\Sdk" }
$ndkRoot = if ($env:ANDROID_NDK_ROOT) { $env:ANDROID_NDK_ROOT } else { $null }

if (-not $ndkRoot) {
    $ndkCandidates = Get-ChildItem -Path (Join-Path $sdkRoot "ndk") -Directory | Sort-Object Name -Descending
    if (-not $ndkCandidates) {
        throw "No Android NDK found under $sdkRoot\ndk"
    }
    $ndkRoot = $ndkCandidates[0].FullName
}

$toolchainFile = Join-Path $ndkRoot "build\cmake\android.toolchain.cmake"
$androidJar = Join-Path $sdkRoot "platforms\android-$Api\android.jar"
$adb = Join-Path $sdkRoot "platform-tools\adb.exe"

$buildToolsDir = Get-ChildItem -Path (Join-Path $sdkRoot "build-tools") -Directory |
    Sort-Object Name -Descending |
    Select-Object -First 1
if (-not $buildToolsDir) {
    throw "No Android build-tools found under $sdkRoot\build-tools"
}

$aapt = Join-Path $buildToolsDir.FullName "aapt.exe"
$zipalign = Join-Path $buildToolsDir.FullName "zipalign.exe"
$apksigner = Join-Path $buildToolsDir.FullName "apksigner.bat"

$buildDir = Join-Path $repoRoot "build\android-$Abi-debug"
$stageDir = Join-Path $buildDir "apk-stage"
$libStageDir = Join-Path $stageDir "lib\$Abi"
$unsignedApk = Join-Path $buildDir "SwAndroidHost-unsigned.apk"
$alignedApk = Join-Path $buildDir "SwAndroidHost-aligned.apk"
$signedApk = Join-Path $buildDir "SwAndroidHost-debug.apk"
$nativeLib = Join-Path $buildDir "android\SwAndroidHost\libSwAndroidHost.so"
$cxxSharedLib = switch ($Abi) {
    "arm64-v8a" { Join-Path $ndkRoot "toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib\aarch64-linux-android\libc++_shared.so" }
    "armeabi-v7a" { Join-Path $ndkRoot "toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib\arm-linux-androideabi\libc++_shared.so" }
    "x86" { Join-Path $ndkRoot "toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib\i686-linux-android\libc++_shared.so" }
    "x86_64" { Join-Path $ndkRoot "toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib\x86_64-linux-android\libc++_shared.so" }
    default { $null }
}
$manifest = Join-Path $repoRoot "android\SwAndroidHost\AndroidManifest.xml"
$resDir = Join-Path $repoRoot "android\SwAndroidHost\res"
$debugKeystore = Join-Path $buildDir "debug.keystore"
$packageName = "com.swstack.androidhost"

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
New-Item -ItemType Directory -Force -Path $libStageDir | Out-Null

$generatorArgs = @()
if (Get-Command ninja -ErrorAction SilentlyContinue) {
    $generatorArgs = @("-G", "Ninja")
}

Invoke-External $cmake (@(
    "--fresh",
    "-S", $repoRoot,
    "-B", $buildDir
) + $generatorArgs + @(
    "-DCMAKE_BUILD_TYPE=Debug",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile",
    "-DANDROID=ON",
    "-DANDROID_PLATFORM=latest",
    "-DANDROID_ABI=$Abi",
    "-DANDROID_STL=c++_shared"
))

Invoke-External $cmake @(
    "--build", $buildDir,
    "--target", "SwAndroidHost"
)

if (-not (Test-Path $nativeLib)) {
    throw "Native library not found: $nativeLib"
}
if (-not $cxxSharedLib -or -not (Test-Path $cxxSharedLib)) {
    throw "libc++_shared.so was not found for ABI '$Abi' under $ndkRoot"
}

Copy-Item -Force $nativeLib (Join-Path $libStageDir "libSwAndroidHost.so")
Copy-Item -Force $cxxSharedLib (Join-Path $libStageDir "libc++_shared.so")

if (Test-Path $unsignedApk) { Remove-Item -Force $unsignedApk }
if (Test-Path $alignedApk) { Remove-Item -Force $alignedApk }
if (Test-Path $signedApk) { Remove-Item -Force $signedApk }

Invoke-External $aapt @(
    "package",
    "-f",
    "-M", $manifest,
    "-S", $resDir,
    "-I", $androidJar,
    "-F", $unsignedApk
)

Push-Location $stageDir
try {
    Invoke-External $aapt @(
        "add",
        $unsignedApk,
        "lib/$Abi/libSwAndroidHost.so"
    )
    Invoke-External $aapt @(
        "add",
        $unsignedApk,
        "lib/$Abi/libc++_shared.so"
    )
} finally {
    Pop-Location
}

Invoke-External $zipalign @("-f", "4", $unsignedApk, $alignedApk)

if (-not (Test-Path $debugKeystore)) {
    Invoke-External keytool @(
        "-genkeypair",
        "-keystore", $debugKeystore,
        "-storepass", "android",
        "-keypass", "android",
        "-alias", "androiddebugkey",
        "-keyalg", "RSA",
        "-keysize", "2048",
        "-validity", "10000",
        "-storetype", "JKS",
        "-dname", "CN=Android Debug,O=Android,C=US"
    )
}

Invoke-External $apksigner @(
    "sign",
    "--ks", $debugKeystore,
    "--ks-pass", "pass:android",
    "--key-pass", "pass:android",
    "--out", $signedApk,
    $alignedApk
)

& $adb uninstall $packageName | Out-Null

Invoke-External $adb @("install", $signedApk)

if ($Launch) {
    Invoke-External $adb @(
        "shell",
        "am",
        "start",
        "-n",
        "$packageName/android.app.NativeActivity"
    )
}

Write-Host "APK ready: $signedApk"
