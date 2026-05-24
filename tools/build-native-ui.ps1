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
$temporaryResourceObject = Join-Path $temporaryDirectory "LightLaunchpad.NativeUi.res.o"

$gpp = (where.exe g++ | Select-Object -First 1)
if (-not $gpp) {
    throw "g++ was not found. Install MinGW-w64 or build the native UI with a C++ toolchain."
}

$windres = (where.exe windres | Select-Object -First 1)
if (-not $windres) {
    throw "windres was not found. Install MinGW-w64 so the native UI can embed the app icon."
}

New-Item -ItemType Directory -Force -Path $outputDirectoryFull | Out-Null
New-Item -ItemType Directory -Force -Path $temporaryDirectory | Out-Null

$pushedLocation = $false
try {
    Push-Location $repoRoot
    $pushedLocation = $true
    & $windres `
        src\LightLaunchpad.NativeUi\LightLaunchpad.NativeUi.rc `
        -O coff `
        -o $temporaryResourceObject
    if ($LASTEXITCODE -ne 0) {
        throw "Native UI resource compilation failed with exit code $LASTEXITCODE."
    }

    & $gpp `
        src\LightLaunchpad.NativeUi\LightLaunchpad.NativeUi.cpp `
        $temporaryResourceObject `
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
        -lole32 `
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

New-Item -ItemType Directory -Force -Path (Join-Path $outputDirectoryFull "Assets") | Out-Null
Copy-Item (Join-Path $repoRoot "src\LightLaunchpad.App\Assets\LightLaunchpad.ico") (Join-Path $outputDirectoryFull "Assets\LightLaunchpad.ico") -Force

Get-Item (Join-Path $outputDirectoryFull "LightLaunchpad.NativeUi.exe")
