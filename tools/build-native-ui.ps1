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

function Get-NativeRuntimeDllNames {
    param(
        [string]$BinaryPath,
        [string]$ToolchainDirectory,
        [string]$Objdump
    )

    $pending = [System.Collections.Generic.Queue[string]]::new()
    $seen = @{}

    function Add-RuntimeImport {
        param([string]$Name)

        if (-not $seen.ContainsKey($Name)) {
            $seen[$Name] = $true
            $pending.Enqueue($Name)
        }
    }

    function Read-RuntimeImports {
        param([string]$Path)

        & $Objdump -p $Path |
            ForEach-Object {
                if ($_ -match 'DLL Name:\s+(lib(?:gcc|stdc\+\+|winpthread)[^\s]+\.dll)') {
                    Add-RuntimeImport $matches[1]
                }
            }
    }

    Read-RuntimeImports $BinaryPath
    while ($pending.Count -gt 0) {
        $runtimeDllName = $pending.Dequeue()
        $runtimeDll = Join-Path $ToolchainDirectory $runtimeDllName
        if (Test-Path $runtimeDll) {
            Read-RuntimeImports $runtimeDll
        }
    }

    $seen.Keys | Sort-Object
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
        -municode `
        -mwindows `
        -static-libgcc `
        -static-libstdc++ `
        -O2 `
        -s `
        -lshell32 `
        -lshlwapi `
        -luser32 `
        -lgdi32 `
        -lole32 `
        -luuid `
        -o $temporaryExe
    if ($LASTEXITCODE -ne 0) {
        throw "Native UI compilation failed with exit code $LASTEXITCODE."
    }

    Copy-Item $temporaryExe (Join-Path $outputDirectoryFull "LightLaunchpad.NativeUi.exe") -Force

    $toolchainDirectory = Split-Path -Parent $gpp
    $objdump = (where.exe objdump 2>$null | Select-Object -First 1)
    $runtimeDllNames = @()
    if ($objdump) {
        $runtimeDllNames = @(Get-NativeRuntimeDllNames $temporaryExe $toolchainDirectory $objdump)
    }

    if ($runtimeDllNames.Count -eq 0) {
        $runtimeDllNames = @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")
    }

    foreach ($runtimeDllName in $runtimeDllNames) {
        $runtimeDll = Join-Path $toolchainDirectory $runtimeDllName
        if (-not (Test-Path $runtimeDll)) {
            throw "Native UI runtime dependency not found: $runtimeDllName."
        }

        Copy-Item $runtimeDll (Join-Path $outputDirectoryFull $runtimeDllName) -Force
    }
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
