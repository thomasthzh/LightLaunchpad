param(
    [string]$OutputDirectory = "release\native-agent"
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $outputDirectoryFull = [System.IO.Path]::GetFullPath($OutputDirectory)
}
else {
    $outputDirectoryFull = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDirectory))
}

$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("lightlaunchpad-native-" + [System.Guid]::NewGuid().ToString("N"))
$temporaryAgent = Join-Path $temporaryDirectory "LightLaunchpad.NativeAgent.exe"

$gpp = (where.exe g++ | Select-Object -First 1)
if (-not $gpp) {
    throw "g++ was not found. Install MinGW-w64 or build the native agent with a C++ toolchain."
}

New-Item -ItemType Directory -Force -Path $outputDirectoryFull | Out-Null
New-Item -ItemType Directory -Force -Path $temporaryDirectory | Out-Null

$pushedLocation = $false
try {
    Push-Location $repoRoot
    $pushedLocation = $true
    & $gpp `
        src\LightLaunchpad.NativeAgent\LightLaunchpad.NativeAgent.cpp `
        -municode `
        -mwindows `
        -O2 `
        -s `
        -lshell32 `
        -luser32 `
        -lkernel32 `
        -o $temporaryAgent

    Copy-Item $temporaryAgent (Join-Path $outputDirectoryFull "LightLaunchpad.NativeAgent.exe") -Force
}
finally {
    if ($pushedLocation) {
        Pop-Location
    }
    Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force -ErrorAction SilentlyContinue
}

New-Item -ItemType Directory -Force -Path (Join-Path $outputDirectoryFull "Assets") | Out-Null
Copy-Item (Join-Path $repoRoot "src\LightLaunchpad.App\Assets\Alice.ico") (Join-Path $outputDirectoryFull "Assets\Alice.ico") -Force

Get-Item (Join-Path $outputDirectoryFull "LightLaunchpad.NativeAgent.exe")
