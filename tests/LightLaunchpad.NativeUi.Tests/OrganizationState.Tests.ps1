$ErrorActionPreference = 'Stop'

$llpRepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$llpTempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("lightlaunchpad-organization-state-" + [System.Guid]::NewGuid().ToString('N'))
$llpTestExe = Join-Path $llpTempRoot 'LightLaunchpad.OrganizationState.Tests.exe'

try {
    New-Item -ItemType Directory -Force -Path $llpTempRoot | Out-Null
    $llpGpp = (where.exe g++ | Select-Object -First 1)
    if (-not $llpGpp) { throw 'g++ was not found.' }
    Push-Location $llpRepoRoot
    try {
        & $llpGpp `
            'tests\LightLaunchpad.NativeUi.Tests\OrganizationState.Tests.cpp' `
            'src\LightLaunchpad.NativeUi\OrganizationState.cpp' `
            -std=c++17 -O2 -Wall -Wextra -Werror -static -static-libgcc -static-libstdc++ -o $llpTestExe
        if ($LASTEXITCODE -ne 0) { throw "OrganizationState test compilation failed with exit code $LASTEXITCODE." }
    }
    finally { Pop-Location }
    & $llpTestExe
    if ($LASTEXITCODE -ne 0) { throw "OrganizationState tests failed with exit code $LASTEXITCODE." }
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
