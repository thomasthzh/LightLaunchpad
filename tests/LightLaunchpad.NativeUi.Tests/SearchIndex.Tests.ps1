param()

$ErrorActionPreference = 'Stop'

$llpRepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$llpTempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("lightlaunchpad-search-tests-" + [System.Guid]::NewGuid().ToString('N'))
$llpTestExe = Join-Path $llpTempRoot 'LightLaunchpad.SearchIndex.Tests.exe'
$llpHeaderPath = Join-Path $llpRepoRoot 'src\third_party\pinyin-data\PinyinData.generated.h'
$llpLicensePath = Join-Path $llpRepoRoot 'src\third_party\pinyin-data\LICENSE.MIT'
$llpExpectedHeaderHash = '2D81EAEA6138350852AFD738E6DD9AAB5D381F54B3759B7352250A9FC81E28F5'
$llpExpectedLicenseHash = '9C048697BE2502A16E8BCB282D5D465A07295B2DEF0FFB05A269C5D39DBE1586'

try {
    $llpHeaderHash = (Get-FileHash -LiteralPath $llpHeaderPath -Algorithm SHA256).Hash
    $llpLicenseHash = (Get-FileHash -LiteralPath $llpLicensePath -Algorithm SHA256).Hash
    if ($llpHeaderHash -ne $llpExpectedHeaderHash) {
        throw "Generated pinyin header checksum mismatch. Expected $llpExpectedHeaderHash, actual $llpHeaderHash."
    }
    if ($llpLicenseHash -ne $llpExpectedLicenseHash) {
        throw "Vendored pinyin license checksum mismatch. Expected $llpExpectedLicenseHash, actual $llpLicenseHash."
    }
    $llpHeader = Get-Content -LiteralPath $llpHeaderPath -Raw
    foreach ($llpEvidence in @(
        'Upstream commit: 923b108dc5d45dee061324c011b478fb649f8b73',
        'Source SHA256 kMandarin_8105.txt: 1B1546F6E190C0F1CBF23E3BB436B41C3ACE16F852BE4DAEBC2C7295636628A4',
        'Source SHA256 pinyin.txt: 621F8CA9EFF8519F47E2B17B564FD318161E13BCA07EEA8C8E04993CD5D3B52E'
    )) {
        if (-not $llpHeader.Contains($llpEvidence, [System.StringComparison]::Ordinal)) {
            throw "Generated pinyin header is missing provenance evidence: $llpEvidence"
        }
    }
    Write-Output "PASS PinyinDependencyIntegrity header=$llpHeaderHash license=$llpLicenseHash"

    New-Item -ItemType Directory -Force -Path $llpTempRoot | Out-Null
    $llpGpp = (where.exe g++ | Select-Object -First 1)
    if (-not $llpGpp) { throw 'g++ was not found.' }

    Push-Location $llpRepoRoot
    try {
        & $llpGpp `
            'tests\LightLaunchpad.NativeUi.Tests\SearchIndex.Tests.cpp' `
            'src\LightLaunchpad.NativeUi\SearchIndex.cpp' `
            -I 'src\LightLaunchpad.NativeUi' `
            -I 'src\third_party' `
            -std=c++17 `
            -finput-charset=UTF-8 `
            -fexec-charset=UTF-8 `
            -static `
            -static-libgcc `
            -static-libstdc++ `
            -O2 `
            -o $llpTestExe
        if ($LASTEXITCODE -ne 0) { throw "SearchIndex test compilation failed with exit code $LASTEXITCODE." }
    }
    finally {
        Pop-Location
    }

    & $llpTestExe
    if ($LASTEXITCODE -ne 0) { throw "SearchIndex production C++ tests failed with exit code $LASTEXITCODE." }
}
finally {
    $llpResolvedTemp = [System.IO.Path]::GetFullPath($llpTempRoot)
    $llpExpectedTempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    if ($llpResolvedTemp.StartsWith($llpExpectedTempRoot, [System.StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $llpResolvedTemp)) {
        $llpTempItem = Get-Item -LiteralPath $llpResolvedTemp -Force
        if (($llpTempItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to clean reparse-point test output $llpResolvedTemp."
        }
        Remove-Item -LiteralPath $llpResolvedTemp -Recurse -Force
    }
}
