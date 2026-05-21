param(
    [string]$Version = "",
    [string]$OutputRoot = "release",
    [string]$Runtime = "win-x64"
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$dotnet = Join-Path $repoRoot ".dotnet\dotnet.exe"
if (-not (Test-Path $dotnet)) {
    $dotnet = "dotnet"
}

if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = ((& git -C $repoRoot rev-parse --short HEAD) -join "").Trim()
    $status = ((& git -C $repoRoot status --short) -join [Environment]::NewLine).Trim()
    if (-not [string]::IsNullOrWhiteSpace($status)) {
        $Version = "$Version-dev"
    }
}

$releaseRoot = Join-Path $repoRoot $OutputRoot
$packageName = "LightLaunchpad-$Runtime-$Version"
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

& $dotnet publish (Join-Path $repoRoot "src\LightLaunchpad.App\LightLaunchpad.App.csproj") `
    -c Release `
    -r $Runtime `
    --self-contained true `
    -p:PublishSingleFile=true `
    -p:EnableCompressionInSingleFile=true `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -o $packageRoot

Compress-Archive -Path (Join-Path $packageRoot "*") -DestinationPath $zipPath -Force

[pscustomobject]@{
    PackageDirectory = $packageRoot
    Zip = $zipPath
    App = Join-Path $packageRoot "LightLaunchpad.App.exe"
}
