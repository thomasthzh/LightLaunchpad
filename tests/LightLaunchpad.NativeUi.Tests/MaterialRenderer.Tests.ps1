param()

$ErrorActionPreference = 'Stop'

$llpRepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$llpTempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("lightlaunchpad-material-renderer-tests-" + [System.Guid]::NewGuid().ToString('N'))
$llpTestExe = Join-Path $llpTempRoot 'LightLaunchpad.MaterialRenderer.Tests.exe'
$llpProductionObject = Join-Path $llpTempRoot 'MaterialRenderer.production.o'

try {
    New-Item -ItemType Directory -Force -Path $llpTempRoot | Out-Null
    $llpGpp = (where.exe g++ | Select-Object -First 1)
    if (-not $llpGpp) { throw 'g++ was not found.' }
    $llpNm = Join-Path (Split-Path -Parent $llpGpp) 'nm.exe'
    if (-not (Test-Path -LiteralPath $llpNm -PathType Leaf)) { throw "nm.exe was not found beside $llpGpp." }

    Push-Location $llpRepoRoot
    try {
        & $llpGpp `
            'tests\LightLaunchpad.NativeUi.Tests\MaterialRenderer.Tests.cpp' `
            'src\LightLaunchpad.NativeUi\MaterialRenderer.cpp' `
            'src\LightLaunchpad.NativeUi\AdaptiveEnvironment.cpp' `
            -I 'src\LightLaunchpad.NativeUi' `
            -DLLP_MATERIAL_TESTING=1 `
            -std=c++17 `
            -finput-charset=UTF-8 `
            -fexec-charset=UTF-8 `
            -static `
            -static-libgcc `
            -static-libstdc++ `
            -O2 `
            -ld3d11 `
            -ldxgi `
            -ldcomp `
            -ld2d1 `
            -ldwrite `
            -lgdi32 `
            -luser32 `
            -lole32 `
            -luuid `
            -o $llpTestExe
        if ($LASTEXITCODE -ne 0) { throw "MaterialRenderer test compilation failed with exit code $LASTEXITCODE." }

        & $llpGpp `
            -c 'src\LightLaunchpad.NativeUi\MaterialRenderer.cpp' `
            -I 'src\LightLaunchpad.NativeUi' `
            -std=c++17 `
            -finput-charset=UTF-8 `
            -fexec-charset=UTF-8 `
            -O2 `
            -o $llpProductionObject
        if ($LASTEXITCODE -ne 0) { throw "MaterialRenderer production object compilation failed with exit code $LASTEXITCODE." }

        $llpProductionSymbols = ((& $llpNm --defined-only $llpProductionObject 2>&1) -join [Environment]::NewLine)
        if ($LASTEXITCODE -ne 0) { throw "Could not inspect MaterialRenderer production object symbols with $llpNm." }
        if ($llpProductionSymbols.Contains('LLP_MaterialRunFakeLifecycleScenario')) {
            throw 'Production MaterialRenderer object exposed the private fake lifecycle runner without LLP_MATERIAL_TESTING.'
        }
        $llpRunnerSymbols = ((& $llpNm --defined-only $llpTestExe 2>&1) -join [Environment]::NewLine)
        if ($LASTEXITCODE -ne 0) { throw "Could not inspect MaterialRenderer test runner symbols with $llpNm." }
        if (-not $llpRunnerSymbols.Contains('LLP_MaterialRunFakeLifecycleScenario')) {
            throw 'Explicit LLP_MATERIAL_TESTING runner did not contain the private fake lifecycle symbol.'
        }
        Write-Output "METRIC MaterialFakeAbi productionSymbol=false runnerSymbol=true nm='$llpNm'"
    }
    finally {
        Pop-Location
    }

    & $llpTestExe
    if ($LASTEXITCODE -ne 0) { throw "MaterialRenderer production C++ tests failed with exit code $LASTEXITCODE." }
}
finally {
    $llpResolvedTemp = [System.IO.Path]::GetFullPath($llpTempRoot)
    $llpExpectedTempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    if ($llpResolvedTemp.StartsWith($llpExpectedTempRoot, [System.StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $llpResolvedTemp)) {
        $llpTempItem = Get-Item -LiteralPath $llpResolvedTemp -Force
        if (($llpTempItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Refusing to clean reparse-point test output $llpResolvedTemp." }
        Remove-Item -LiteralPath $llpResolvedTemp -Recurse -Force
    }
}
