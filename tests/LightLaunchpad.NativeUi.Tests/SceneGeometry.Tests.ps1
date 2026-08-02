param()

$ErrorActionPreference = 'Stop'

$llpRepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$llpTempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("lightlaunchpad-scene-geometry-tests-" + [System.Guid]::NewGuid().ToString('N'))
$llpTestExe = Join-Path $llpTempRoot 'LightLaunchpad.SceneGeometry.Tests.exe'

try {
    New-Item -ItemType Directory -Force -Path $llpTempRoot | Out-Null
    $llpGpp = (where.exe g++ | Select-Object -First 1)
    if (-not $llpGpp) { throw 'g++ was not found.' }

    Push-Location $llpRepoRoot
    try {
        & $llpGpp `
            'tests\LightLaunchpad.NativeUi.Tests\SceneGeometry.Tests.cpp' `
            'src\LightLaunchpad.NativeUi\SceneGeometry.cpp' `
            -I 'src\LightLaunchpad.NativeUi' `
            -std=c++17 `
            -finput-charset=UTF-8 `
            -fexec-charset=UTF-8 `
            -static `
            -static-libgcc `
            -static-libstdc++ `
            -O2 `
            -o $llpTestExe
        if ($LASTEXITCODE -ne 0) { throw "SceneGeometry test compilation failed with exit code $LASTEXITCODE." }
    }
    finally {
        Pop-Location
    }

    & $llpTestExe
    if ($LASTEXITCODE -ne 0) { throw "SceneGeometry production C++ tests failed with exit code $LASTEXITCODE." }
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
