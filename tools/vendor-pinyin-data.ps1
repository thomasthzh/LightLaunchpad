param()

$ErrorActionPreference = 'Stop'

$llpCommit = '923b108dc5d45dee061324c011b478fb649f8b73'
$llpExpected = @{
    'kMandarin_8105.txt' = '1B1546F6E190C0F1CBF23E3BB436B41C3ACE16F852BE4DAEBC2C7295636628A4'
    'pinyin.txt' = '621F8CA9EFF8519F47E2B17B564FD318161E13BCA07EEA8C8E04993CD5D3B52E'
    'LICENSE' = '9C048697BE2502A16E8BCB282D5D465A07295B2DEF0FFB05A269C5D39DBE1586'
}
$llpRepoRoot = Split-Path -Parent $PSScriptRoot
$llpDestination = Join-Path $llpRepoRoot 'src\third_party\pinyin-data'
$llpHeaderPath = Join-Path $llpDestination 'PinyinData.generated.h'
$llpLicensePath = Join-Path $llpDestination 'LICENSE.MIT'
$llpUtf8 = [System.Text.UTF8Encoding]::new($false)

function Get-VerifiedBytes {
    param([System.Net.Http.HttpClient]$Client, [string]$Name)
    $llpUri = "https://raw.githubusercontent.com/mozillazg/pinyin-data/$llpCommit/$Name"
    $llpBytes = $Client.GetByteArrayAsync($llpUri).GetAwaiter().GetResult()
    $llpHash = [Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData($llpBytes))
    if ($llpHash -ne $llpExpected[$Name]) {
        throw "pinyin-data checksum mismatch for $Name. Expected $($llpExpected[$Name]), actual $llpHash."
    }
    return $llpBytes
}

function ConvertTo-TonelessPinyin {
    param([string]$Value)
    $llpPrepared = $Value.ToLowerInvariant().Replace([char]0x00FC, 'v').Replace([char]0x00EA, 'e')
    $llpBuilder = [System.Text.StringBuilder]::new()
    foreach ($llpCharacter in $llpPrepared.Normalize([System.Text.NormalizationForm]::FormD).ToCharArray()) {
        $llpCategory = [Globalization.CharUnicodeInfo]::GetUnicodeCategory($llpCharacter)
        if ($llpCategory -ne [Globalization.UnicodeCategory]::NonSpacingMark -and (($llpCharacter -ge 'a' -and $llpCharacter -le 'z') -or $llpCharacter -eq '^')) {
            [void]$llpBuilder.Append($llpCharacter)
        }
    }
    return $llpBuilder.ToString().Replace('^', '')
}

function Read-PinyinMap {
    param([string]$Text)
    $llpMap = @{}
    foreach ($llpLine in $Text -split "`r?`n") {
        if ($llpLine -notmatch '^\s*U\+([0-9A-Fa-f]+):\s*([^#\s]+)') { continue }
        $llpCodePoint = [Convert]::ToInt32($Matches[1], 16)
        $llpReadings = [System.Collections.Generic.List[string]]::new()
        foreach ($llpRaw in $Matches[2].Split(',')) {
            $llpReading = ConvertTo-TonelessPinyin $llpRaw
            if (-not [string]::IsNullOrWhiteSpace($llpReading) -and -not $llpReadings.Contains($llpReading)) {
                $llpReadings.Add($llpReading)
            }
        }
        if ($llpReadings.Count -gt 0) { $llpMap[$llpCodePoint] = $llpReadings }
    }
    return $llpMap
}

function Format-ArrayLines {
    param([string[]]$Values, [int]$PerLine = 12)
    $llpLines = [System.Collections.Generic.List[string]]::new()
    for ($llpIndex = 0; $llpIndex -lt $Values.Count; $llpIndex += $PerLine) {
        $llpEnd = [Math]::Min($Values.Count - 1, $llpIndex + $PerLine - 1)
        $llpLines.Add('    ' + (($Values[$llpIndex..$llpEnd]) -join ', ') + ',')
    }
    return $llpLines -join "`n"
}

$llpClient = [System.Net.Http.HttpClient]::new()
try {
    $llpDefaultBytes = Get-VerifiedBytes -Client $llpClient -Name 'kMandarin_8105.txt'
    $llpAllBytes = Get-VerifiedBytes -Client $llpClient -Name 'pinyin.txt'
    $llpLicenseBytes = Get-VerifiedBytes -Client $llpClient -Name 'LICENSE'
}
finally {
    $llpClient.Dispose()
}

$llpDefaults = Read-PinyinMap $llpUtf8.GetString($llpDefaultBytes)
$llpAll = Read-PinyinMap $llpUtf8.GetString($llpAllBytes)
$llpCodePoints = @($llpDefaults.Keys | Sort-Object)
if ($llpCodePoints.Count -lt 8000) { throw "Generated pinyin table is unexpectedly small: $($llpCodePoints.Count)." }

$llpSetToIndex = [System.Collections.Generic.Dictionary[string,int]]::new([System.StringComparer]::Ordinal)
$llpReadingSets = [System.Collections.Generic.List[string]]::new()
$llpIndexes = [System.Collections.Generic.List[int]]::new()
foreach ($llpCodePoint in $llpCodePoints) {
    $llpReadings = [System.Collections.Generic.List[string]]::new()
    foreach ($llpReading in $llpDefaults[$llpCodePoint]) {
        if (-not $llpReadings.Contains($llpReading)) { $llpReadings.Add($llpReading) }
    }
    if ($llpAll.ContainsKey($llpCodePoint)) {
        foreach ($llpReading in $llpAll[$llpCodePoint]) {
            if (-not $llpReadings.Contains($llpReading)) { $llpReadings.Add($llpReading) }
        }
    }
    $llpSet = $llpReadings -join ','
    if (-not $llpSetToIndex.ContainsKey($llpSet)) {
        $llpSetToIndex[$llpSet] = $llpReadingSets.Count
        $llpReadingSets.Add($llpSet)
    }
    $llpIndexes.Add($llpSetToIndex[$llpSet])
}
if ($llpReadingSets.Count -gt [uint16]::MaxValue) { throw 'Generated reading-set index exceeds uint16_t.' }

$llpCodePointValues = @($llpCodePoints | ForEach-Object { '0x{0:X4}' -f $_ })
$llpIndexValues = @($llpIndexes | ForEach-Object { $_.ToString() })
$llpSetValues = @($llpReadingSets | ForEach-Object { '"' + $_ + '"' })
$llpHeader = @"
#pragma once

// Generated by tools/vendor-pinyin-data.ps1 from mozillazg/pinyin-data.
// Upstream commit: $llpCommit
// Source SHA256 kMandarin_8105.txt: $($llpExpected['kMandarin_8105.txt'])
// Source SHA256 pinyin.txt: $($llpExpected['pinyin.txt'])
// License: MIT; see LICENSE.MIT beside this file.

#include <array>
#include <cstdint>
#include <string_view>

namespace llp::search::pinyin_data
{
inline constexpr std::array<uint32_t, $($llpCodePoints.Count)> CodePoints = {
$(Format-ArrayLines $llpCodePointValues)
};

inline constexpr std::array<uint16_t, $($llpIndexes.Count)> ReadingSetIndexes = {
$(Format-ArrayLines $llpIndexValues)
};

inline constexpr std::array<std::string_view, $($llpReadingSets.Count)> ReadingSets = {
$(Format-ArrayLines $llpSetValues 6)
};
}
"@

New-Item -ItemType Directory -Force -Path $llpDestination | Out-Null
[System.IO.File]::WriteAllText($llpHeaderPath, $llpHeader, $llpUtf8)
[System.IO.File]::WriteAllBytes($llpLicensePath, $llpLicenseBytes)
Write-Output "Generated $llpHeaderPath with $($llpCodePoints.Count) code points and $($llpReadingSets.Count) unique reading sets."
Write-Output "Header SHA256: $((Get-FileHash -LiteralPath $llpHeaderPath -Algorithm SHA256).Hash)"
