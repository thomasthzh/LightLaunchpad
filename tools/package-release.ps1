param(
    [string]$Version = "",
    [string]$OutputRoot = "release",
    [string]$Runtime = "win-x64"
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = ((& git -C $repoRoot rev-parse --short HEAD) -join "").Trim()
    $status = ((& git -C $repoRoot status --short) -join [Environment]::NewLine).Trim()
    if (-not [string]::IsNullOrWhiteSpace($status)) {
        $Version = "$Version-dev"
    }
}

$releaseRoot = Join-Path $repoRoot $OutputRoot
$packageName = "LightLaunchpad-nativeui-$Runtime-$Version"
$packageRoot = Join-Path $releaseRoot $packageName
$zipPath = Join-Path $releaseRoot "$packageName.zip"

$releaseRootFull = [System.IO.Path]::GetFullPath($releaseRoot)
$packageRootFull = [System.IO.Path]::GetFullPath($packageRoot)
if (-not $packageRootFull.StartsWith($releaseRootFull + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Package output must stay under the release directory."
}

New-Item -ItemType Directory -Force -Path $releaseRoot | Out-Null
if (Test-Path $packageRoot) {
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

& (Join-Path $PSScriptRoot "build-native-ui.ps1") -OutputDirectory $packageRoot
if ($LASTEXITCODE -ne 0) {
    throw "Native UI build failed with exit code $LASTEXITCODE."
}

Compress-Archive -Path (Join-Path $packageRoot "*") -DestinationPath $zipPath -Force

[pscustomobject]@{
    PackageDirectory = $packageRoot
    Zip = $zipPath
    NativeUi = Join-Path $packageRoot "LightLaunchpad.NativeUi.exe"
}
