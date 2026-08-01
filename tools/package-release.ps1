param(
    [string]$Version = "",
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$ProductVersion = "1.11.0",
    [string]$OutputRoot = "release",
    [string]$Runtime = "win-x64",
    [switch]$Development
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not [string]::IsNullOrWhiteSpace($Version)) {
    $legacyVersion = $Version.Trim().TrimStart('V', 'v')
    if ($legacyVersion -match '^\d+\.\d+$') {
        $legacyVersion += '.0'
    }
    if ($legacyVersion -notmatch '^\d+\.\d+\.\d+$') {
        throw "Version '$Version' is not a semantic product version."
    }
    $ProductVersion = $legacyVersion
}

$buildCommit = ((& git -C $repoRoot rev-parse --short=12 HEAD) -join '').Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($buildCommit)) {
    throw 'Unable to resolve the package build commit.'
}
$status = ((& git -C $repoRoot status --short --untracked-files=all) -join [Environment]::NewLine).Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to determine whether the package source is dirty.'
}
$isDirty = -not [string]::IsNullOrWhiteSpace($status)
if ($isDirty -and -not $Development) {
    throw 'Refusing to create a stable package from a dirty repository. Re-run with -Development for an explicitly labeled development package.'
}

$buildChannel = if ($Development) { 'Development' } else { 'Stable' }
$Version = if ($Development) {
    "$ProductVersion-dev+$buildCommit" + $(if ($isDirty) { '.dirty' } else { '' })
}
else {
    $ProductVersion
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

& (Join-Path $PSScriptRoot "build-native-ui.ps1") `
    -OutputDirectory $packageRoot `
    -ProductVersion $ProductVersion `
    -BuildCommit $buildCommit `
    -BuildChannel $buildChannel
if ($LASTEXITCODE -ne 0) {
    throw "Native UI build failed with exit code $LASTEXITCODE."
}

Compress-Archive -Path (Join-Path $packageRoot "*") -DestinationPath $zipPath -Force

[pscustomobject]@{
    PackageDirectory = $packageRoot
    Zip = $zipPath
    NativeUi = Join-Path $packageRoot "LightLaunchpad.NativeUi.exe"
    Version = $Version
    Commit = $buildCommit
    Channel = $buildChannel.ToLowerInvariant()
}
