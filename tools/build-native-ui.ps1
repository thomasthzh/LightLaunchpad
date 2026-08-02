param(
    [string]$OutputDirectory = "release\native-ui",
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$ProductVersion = "1.11.0",
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$BuildCommit = "",
    [ValidateSet('Development', 'Stable')]
    [string]$BuildChannel = "Development"
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $outputDirectoryFull = [System.IO.Path]::GetFullPath($OutputDirectory)
}
else {
    $outputDirectoryFull = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDirectory))
}

$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("lightlaunchpad-native-ui-" + [System.Guid]::NewGuid().ToString("N"))
$temporaryExe = Join-Path $temporaryDirectory "LightLaunchpad.NativeUi.exe"
$temporaryResourceObject = Join-Path $temporaryDirectory "LightLaunchpad.NativeUi.res.o"
$temporaryIdentityHeader = Join-Path $temporaryDirectory "LightLaunchpad.BuildIdentity.generated.h"

$versionParts = $ProductVersion.Split('.')
$versionMajor = [int]$versionParts[0]
$versionMinor = [int]$versionParts[1]
$versionPatch = [int]$versionParts[2]
if ([string]::IsNullOrWhiteSpace($BuildCommit)) {
    $BuildCommit = ((& git -C $repoRoot rev-parse --short=12 HEAD) -join '').Trim()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($BuildCommit)) {
        throw 'Unable to resolve the build commit.'
    }
}

$repositoryStatus = ((& git -C $repoRoot status --short --untracked-files=all) -join [Environment]::NewLine).Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to determine whether the build is dirty.'
}
$isDirty = -not [string]::IsNullOrWhiteSpace($repositoryStatus)
$channelValue = $BuildChannel.ToLowerInvariant()
if ($BuildChannel -eq 'Stable' -and $isDirty) {
    throw 'Stable NativeUi builds require a clean repository. Use a Development build for dirty work.'
}

$buildVersion = if ($BuildChannel -eq 'Development') {
    "$ProductVersion-dev+$BuildCommit" + $(if ($isDirty) { '.dirty' } else { '' })
}
else {
    $ProductVersion
}
$singletonName = if ($BuildChannel -eq 'Development') {
    "Local\LightLaunchpad.NativeUi.SingleInstance.development.$BuildCommit"
}
else {
    'Local\LightLaunchpad.NativeUi.SingleInstance.stable'
}
$windowClassName = if ($BuildChannel -eq 'Development') {
    "LightLaunchpadNativeUiWindow.development.$BuildCommit"
}
else {
    'LightLaunchpadNativeUiWindow.stable'
}

function ConvertTo-CppStringLiteral {
    param([string]$Value)
    return $Value.Replace('\', '\\').Replace('"', '\"')
}

$escapedBuildVersion = ConvertTo-CppStringLiteral $buildVersion
$escapedBuildCommit = ConvertTo-CppStringLiteral $BuildCommit
$escapedChannel = ConvertTo-CppStringLiteral $channelValue
$escapedSingleton = ConvertTo-CppStringLiteral $singletonName
$escapedWindowClass = ConvertTo-CppStringLiteral $windowClassName
$identityHeader = @"
#pragma once
#define LLP_VERSION_MAJOR $versionMajor
#define LLP_VERSION_MINOR $versionMinor
#define LLP_VERSION_PATCH $versionPatch
#define LLP_VERSION_BUILD 0
#define LLP_FILE_VERSION_COMMA $versionMajor, $versionMinor, $versionPatch, 0
#define LLP_FILE_VERSION_STRING "$ProductVersion.0"
#define LLP_BUILD_VERSION_STRING "$escapedBuildVersion"
#define LLP_BUILD_VERSION_WIDE L"$escapedBuildVersion"
#define LLP_BUILD_COMMIT_STRING "$escapedBuildCommit"
#define LLP_BUILD_COMMIT_WIDE L"$escapedBuildCommit"
#define LLP_BUILD_CHANNEL_STRING "$escapedChannel"
#define LLP_BUILD_CHANNEL_WIDE L"$escapedChannel"
#define LLP_SINGLETON_NAME_WIDE L"$escapedSingleton"
#define LLP_WINDOW_CLASS_NAME_WIDE L"$escapedWindowClass"
"@

$gpp = (where.exe g++ | Select-Object -First 1)
if (-not $gpp) {
    throw "g++ was not found. Install MinGW-w64 or build the native UI with a C++ toolchain."
}

$windres = (where.exe windres | Select-Object -First 1)
if (-not $windres) {
    throw "windres was not found. Install MinGW-w64 so the native UI can embed the app icon."
}

New-Item -ItemType Directory -Force -Path $outputDirectoryFull | Out-Null
$staleAssetsDirectory = Join-Path $outputDirectoryFull "Assets"
if (Test-Path $staleAssetsDirectory) {
    Remove-Item -LiteralPath $staleAssetsDirectory -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $temporaryDirectory | Out-Null
[System.IO.File]::WriteAllText($temporaryIdentityHeader, $identityHeader, [System.Text.UTF8Encoding]::new($false))

$pushedLocation = $false
try {
    Push-Location $repoRoot
    $pushedLocation = $true
    & $windres `
        src\LightLaunchpad.NativeUi\LightLaunchpad.NativeUi.rc `
        -I $temporaryDirectory `
        -I src\LightLaunchpad.NativeUi `
        -O coff `
        -o $temporaryResourceObject
    if ($LASTEXITCODE -ne 0) {
        throw "Native UI resource compilation failed with exit code $LASTEXITCODE."
    }

    & $gpp `
        src\LightLaunchpad.NativeUi\LightLaunchpad.NativeUi.cpp `
        src\LightLaunchpad.NativeUi\Catalog.cpp `
        src\LightLaunchpad.NativeUi\InteractionState.cpp `
        src\LightLaunchpad.NativeUi\Persistence.cpp `
        src\LightLaunchpad.NativeUi\SearchAccessibility.cpp `
        src\LightLaunchpad.NativeUi\SearchIndex.cpp `
        src\LightLaunchpad.NativeUi\ShellBroker.cpp `
        src\LightLaunchpad.NativeUi\TextInput.cpp `
        $temporaryResourceObject `
        -I $temporaryDirectory `
        -I src\LightLaunchpad.NativeUi `
        -I src\third_party `
        -std=c++17 `
        -finput-charset=UTF-8 `
        -fexec-charset=UTF-8 `
        -municode `
        -mwindows `
        -static `
        -static-libgcc `
        -static-libstdc++ `
        -Os `
        -ffunction-sections `
        -fdata-sections `
        '-Wl,--gc-sections' `
        -s `
        -lshell32 `
        -lshlwapi `
        -lcomctl32 `
        -lcomdlg32 `
        -luser32 `
        -lgdi32 `
        -lmsimg32 `
        -ldwmapi `
        -ld2d1 `
        -ldwrite `
        -limm32 `
        -lole32 `
        -loleaut32 `
        -luiautomationcore `
        -luxtheme `
        -ladvapi32 `
        -luuid `
        -o $temporaryExe
    if ($LASTEXITCODE -ne 0) {
        throw "Native UI compilation failed with exit code $LASTEXITCODE."
    }

    Copy-Item $temporaryExe (Join-Path $outputDirectoryFull "LightLaunchpad.NativeUi.exe") -Force
}
finally {
    if ($pushedLocation) {
        Pop-Location
    }
    Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force -ErrorAction SilentlyContinue
}

Get-Item (Join-Path $outputDirectoryFull "LightLaunchpad.NativeUi.exe")
