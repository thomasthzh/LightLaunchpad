#Requires -Version 5.1
<#
.SYNOPSIS
Builds the isolated liquidDX11 Spotlight. Never installs or updates the live app.
#>
[CmdletBinding()]
param([switch]$Rebuild, [switch]$BuildOnly)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$nativeRoot = Join-Path $repoRoot 'src/LightLaunchpad.NativeUi'
$vendorRoot = Join-Path $PSScriptRoot 'vendor/liquidDX11'
$sourceRoot = Join-Path $PSScriptRoot 'src'
$presentationRoot = Join-Path $repoRoot 'experiments/llp-material-reconstruction'
$presentationSdk = 'C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/um'
if (-not (Test-Path -LiteralPath (Join-Path $presentationSdk 'Presentation.h'))) { throw 'Windows SDK Presentation.h is required.' }
# MinGW's assembler cannot create output objects under this repository's CJK path.
$buildRoot = Join-Path ([IO.Path]::GetTempPath()) 'LLP-liquidDX11-20261103-build'
$outputRoot = Join-Path $repoRoot 'artifacts/liquiddx11-spotlight-20261103'
$profileRoot = Join-Path $outputRoot 'profile'
$outputExe = Join-Path $outputRoot 'LightLaunchpad.LiquidSpotlight.exe'
$gpp = (Get-Command g++.exe -ErrorAction Stop).Source
$windres = Join-Path (Split-Path -Parent $gpp) 'windres.exe'
if (-not (Test-Path -LiteralPath $windres -PathType Leaf)) { throw 'windres is required.' }
foreach ($process in @(Get-Process -Name 'LightLaunchpad.LiquidSpotlight' -ErrorAction SilentlyContinue)) {
    if ($BuildOnly) { continue }
    if ([string]::Equals($process.Path, $outputExe, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Close the isolated prototype before rebuilding it.'
    }
}
New-Item -ItemType Directory -Force -Path $buildRoot, $outputRoot, $profileRoot | Out-Null

# Copy once; subsequent builds preserve edits to the isolated profile.
$liveProfile = Join-Path $env:APPDATA 'LightLaunchpad'
if (-not (Test-Path -LiteralPath (Join-Path $profileRoot 'settings.json'))) {
    if (Test-Path -LiteralPath $liveProfile -PathType Container) {
        foreach ($file in @(Get-ChildItem -LiteralPath $liveProfile -File)) {
            Copy-Item -LiteralPath $file.FullName -Destination $profileRoot
        }
        foreach ($folder in @('layouts', 'icons')) {
            $from = Join-Path $liveProfile $folder
            if (Test-Path -LiteralPath $from -PathType Container) {
                Copy-Item -LiteralPath $from -Destination $profileRoot -Recurse
            }
        }
    }
}

$identity = @'
#pragma once
#define LLP_VERSION_MAJOR 1
#define LLP_VERSION_MINOR 11
#define LLP_VERSION_PATCH 0
#define LLP_VERSION_BUILD 0
#define LLP_FILE_VERSION_COMMA 1,11,0,0
#define LLP_FILE_VERSION_STRING "1.11.0.0"
#define LLP_BUILD_VERSION_STRING "1.11.0-dev+liquidDX11-20261103"
#define LLP_BUILD_VERSION_WIDE L"1.11.0-dev+liquidDX11-20261103"
#define LLP_BUILD_COMMIT_STRING "liquidDX11-20261103"
#define LLP_BUILD_COMMIT_WIDE L"liquidDX11-20261103"
#define LLP_BUILD_CHANNEL_STRING "development"
#define LLP_BUILD_CHANNEL_WIDE L"development"
#define LLP_SINGLETON_NAME_WIDE L"Local\\LightLaunchpad.LiquidSpotlight.Isolated.20261103"
#define LLP_WINDOW_CLASS_NAME_WIDE L"LightLaunchpadLiquidSpotlight.Isolated.20261103"
'@
$identityPath = Join-Path $buildRoot 'LightLaunchpad.BuildIdentity.generated.h'
[IO.File]::WriteAllText($identityPath, $identity, [Text.UTF8Encoding]::new($false))
$hostPath = Join-Path $buildRoot 'NativeHost.cpp'
& (Join-Path $PSScriptRoot 'prepare-host.ps1') -Destination $hostPath -ProfileDirectory $profileRoot | Out-Null

$common = @('-std=c++17','-DUNICODE','-D_UNICODE','-finput-charset=UTF-8','-fexec-charset=UTF-8','-O2',
    '-ffunction-sections','-fdata-sections','-pthread',
    '-I', $buildRoot, '-I', $sourceRoot, '-I', $nativeRoot, '-I', $presentationRoot,
    '-I', (Join-Path $repoRoot 'src/third_party'), '-I', $vendorRoot,
    '-I', (Join-Path $vendorRoot 'examples/example_win32_directx11/src'))

# Cache normal translation units, invalidating all objects whenever a header changes.
$headerHashes = @($identityPath, (Join-Path $presentationRoot 'CompositionPresenter.h')) + @(Get-ChildItem -LiteralPath $nativeRoot, $sourceRoot, $vendorRoot -Recurse -File |
    Where-Object { $_.Extension -in @('.h','.inc') } | Select-Object -ExpandProperty FullName)
$headerFingerprint = ($headerHashes | Sort-Object | ForEach-Object {
    (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash
}) -join ''
$objects = [Collections.Generic.List[string]]::new()
function Compile-Unit {
    param([string]$Source, [string]$Name, [string[]]$Extra = @())
    $object = Join-Path $buildRoot ($Name + '.o')
    $stamp = $object + '.key'
    $key = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash + $headerFingerprint + ($common -join '|') + ($Extra -join '|')
    $valid = -not $Rebuild -and (Test-Path -LiteralPath $object) -and (Test-Path -LiteralPath $stamp) -and
        ([IO.File]::ReadAllText($stamp) -ceq $key)
    if (-not $valid) {
        Write-Output ('Compiling ' + $Name)
        & $gpp @common @Extra -c $Source -o $object
        if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $Source" }
        [IO.File]::WriteAllText($stamp, $key)
    }
    $objects.Add($object)
}
Compile-Unit $hostPath 'NativeHost'
foreach ($name in @('AdaptiveEnvironment','Catalog','EverythingBridge','HistoryPersistence',
    'ClipboardHistory','ClipboardOverlay','Hotwords','InputStats','InputStatsPanel','SettingsStudio',
    'InteractionState','MotionTimeline','OrganizationState','Persistence','SceneGeometry',
    'SearchAccessibility','SearchIndex','ShellBroker','SpotlightAlpha','TextInput')) {
    Compile-Unit (Join-Path $nativeRoot ($name + '.cpp')) $name
}
Compile-Unit (Join-Path $sourceRoot 'MaterialHostUtilities.cpp') 'MaterialHostUtilities'
Compile-Unit (Join-Path $sourceRoot 'InkCompositor.cpp') 'InkCompositor'
Compile-Unit (Join-Path $presentationRoot 'CompositionPresenter.cpp') 'CompositionPresenter' @('-idirafter', $presentationSdk)
foreach ($name in @('imgui','imgui_draw','imgui_tables','imgui_widgets')) {
    Compile-Unit (Join-Path $vendorRoot ($name + '.cpp')) $name
}
foreach ($name in @('backdrop','glass')) {
    Compile-Unit (Join-Path $vendorRoot ('examples/example_win32_directx11/src/glass/' + $name + '.cpp')) ('upstream_' + $name)
}
Compile-Unit (Join-Path $sourceRoot 'LiquidMaterialRenderer.cpp') 'LiquidMaterialRenderer' @('-I', (Join-Path $vendorRoot 'examples/example_win32_directx11/src/glass'))
$resourceObject = Join-Path $buildRoot 'NativeUi.res.o'
Push-Location $repoRoot
try {
    & $windres 'src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.rc' -I $buildRoot -I 'src/LightLaunchpad.NativeUi' -O coff -o $resourceObject
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
} finally { Pop-Location }
$objects.Add($resourceObject)
Write-Output 'Linking isolated Spotlight'
$temporaryExe = Join-Path $buildRoot 'LightLaunchpad.LiquidSpotlight.exe'
$mingwRoot = Split-Path -Parent (Split-Path -Parent $gpp)
$gccLibDir = Get-ChildItem (Join-Path $mingwRoot 'lib/gcc/x86_64-w64-mingw32') -Directory -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
$linkPaths = @('-fno-use-linker-plugin')
if ($gccLibDir) {
    $linkPaths += "-B$($gccLibDir.Replace('\','/'))/"
    $linkPaths += "-L$($gccLibDir.Replace('\','/'))"
}
$linkPaths += "-B$((Join-Path $mingwRoot 'x86_64-w64-mingw32/lib').Replace('\','/'))/"
$linkPaths += "-L$((Join-Path $mingwRoot 'lib').Replace('\','/'))"
$linkPaths += "-L$((Join-Path $mingwRoot 'x86_64-w64-mingw32/lib').Replace('\','/'))"
& $gpp @objects @linkPaths -municode -mwindows -static -static-libgcc -static-libstdc++ -pthread '-Wl,--gc-sections' -s `
    -lshell32 -lshlwapi -lcomctl32 -lcomdlg32 -luser32 -lgdi32 -lmsimg32 -ldwmapi -ld2d1 -ldwrite `
    -ld3d11 -ldxgi -ldxguid -ldcomp -ld3dcompiler -lmincore -limm32 -lole32 -loleaut32 -luiautomationcore -luxtheme -ladvapi32 -luuid -lwinmm -lwindowscodecs `
    -o $temporaryExe
if ($LASTEXITCODE -ne 0) { throw 'Linking failed.' }
if ($BuildOnly) { Write-Output $temporaryExe; return }
Copy-Item -LiteralPath $temporaryExe -Destination $outputExe -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination $outputRoot -Force
Copy-Item -LiteralPath (Join-Path $vendorRoot 'UPSTREAM.md') -Destination $outputRoot -Force
$licenseRoot = Join-Path $outputRoot 'licenses'
New-Item -ItemType Directory -Force -Path $licenseRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $vendorRoot 'LICENSE.txt') -Destination (Join-Path $licenseRoot 'DearImGui-MIT.txt') -Force
Copy-Item -LiteralPath (Join-Path $vendorRoot 'examples/example_win32_directx11/LICENSE') -Destination (Join-Path $licenseRoot 'liquidDX11-MIT.txt') -Force
Copy-Item -LiteralPath (Join-Path $vendorRoot 'examples/example_win32_directx11/THIRD-PARTY-NOTICES.md') -Destination $licenseRoot -Force
Copy-Item -LiteralPath (Join-Path $vendorRoot 'NOTICE') -Destination $licenseRoot -Force
Copy-Item -LiteralPath (Join-Path $vendorRoot 'UPSTREAM_VERSION') -Destination $outputRoot -Force
foreach ($stb in @('imstb_rectpack.h','imstb_textedit.h','imstb_truetype.h')) {
    Copy-Item -LiteralPath (Join-Path $vendorRoot $stb) -Destination $licenseRoot -Force
}
Write-Output $outputExe
