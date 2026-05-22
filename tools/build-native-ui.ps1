param(
    [string]$OutputDirectory = "release\native-ui"
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

$gpp = (where.exe g++ | Select-Object -First 1)
if (-not $gpp) {
    throw "g++ was not found. Install MinGW-w64 or build the native UI with a C++ toolchain."
}

New-Item -ItemType Directory -Force -Path $outputDirectoryFull | Out-Null
New-Item -ItemType Directory -Force -Path $temporaryDirectory | Out-Null

$pushedLocation = $false
try {
    Push-Location $repoRoot
    $pushedLocation = $true
    & $gpp `
        src\LightLaunchpad.NativeUi\LightLaunchpad.NativeUi.cpp `
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
        -ld2d1 `
        -ldwrite `
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

New-Item -ItemType Directory -Force -Path (Join-Path $outputDirectoryFull "Assets") | Out-Null
Copy-Item (Join-Path $repoRoot "src\LightLaunchpad.App\Assets\Alice.ico") (Join-Path $outputDirectoryFull "Assets\Alice.ico") -Force

Get-Item (Join-Path $outputDirectoryFull "LightLaunchpad.NativeUi.exe")
