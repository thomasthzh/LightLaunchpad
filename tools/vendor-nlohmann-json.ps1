param()

$ErrorActionPreference = 'Stop'

$llpVersion = '3.12.0'
$llpExpectedSha256 = 'AAF127C04CB31C406E5B04A63F1AE89369FCCDE6D8FA7CDDA1ED4F32DFC5DE63'
$llpUrl = "https://github.com/nlohmann/json/releases/download/v$llpVersion/json.hpp"
$llpRepoRoot = Split-Path -Parent $PSScriptRoot
$llpDestination = Join-Path $llpRepoRoot 'src\third_party\nlohmann\json.hpp'
$llpTemporaryPath = Join-Path ([System.IO.Path]::GetTempPath()) ("nlohmann-json-$llpVersion-" + [System.Guid]::NewGuid().ToString('N') + '.hpp')

try {
    Invoke-WebRequest -Uri $llpUrl -OutFile $llpTemporaryPath -UseBasicParsing
    $llpActualSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $llpTemporaryPath).Hash
    if ($llpActualSha256 -ne $llpExpectedSha256) {
        throw "nlohmann/json $llpVersion checksum mismatch. Expected $llpExpectedSha256, actual $llpActualSha256."
    }

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $llpDestination) | Out-Null
    Move-Item -LiteralPath $llpTemporaryPath -Destination $llpDestination -Force
    Get-Item -LiteralPath $llpDestination | Select-Object FullName, Length, @{ Name = 'SHA256'; Expression = { (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash } }
}
finally {
    if (Test-Path -LiteralPath $llpTemporaryPath -PathType Leaf) {
        Remove-Item -LiteralPath $llpTemporaryPath -Force
    }
}
