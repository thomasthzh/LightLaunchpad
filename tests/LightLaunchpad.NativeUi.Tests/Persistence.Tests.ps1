param()

$ErrorActionPreference = 'Stop'

$llpRepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$llpTempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("lightlaunchpad-persistence-tests-" + [System.Guid]::NewGuid().ToString('N'))
$llpOutputDirectory = Join-Path $llpTempRoot 'development'
$llpAppData = Join-Path $llpTempRoot 'appdata'
$llpLocalAppData = Join-Path $llpTempRoot 'localappdata'
$llpUserProfile = Join-Path $llpTempRoot 'profile'
$llpDataDirectory = Join-Path $llpAppData 'LightLaunchpad'
$llpSettingsPath = Join-Path $llpDataDirectory 'settings.json'
$llpLegacyLayoutPath = Join-Path $llpDataDirectory 'layout.json'
$llpFolderA = Join-Path $llpTempRoot 'Launchpad-A'
$llpFolderB = Join-Path $llpTempRoot 'Launchpad-B'
$llpFolderC = Join-Path $llpTempRoot 'Launchpad-C'
$llpRocket = [char]::ConvertFromUtf32(0x1F680)
$llpChineseApp = ([string][char]0x5E94) + ([char]0x7528)
$llpChineseResearch = ([string][char]0x7814) + ([char]0x53D1)
$llpChineseCore = ([string][char]0x6838) + ([char]0x5FC3)
$llpChineseText = ([string][char]0x4E2D) + ([char]0x6587)
$llpSourceA = Join-Path $llpFolderA ($llpChineseApp + $llpRocket + '.lnk')
$llpProcess = $null

function Assert-Equal {
    param([object]$Expected, [object]$Actual, [string]$Message)
    if ($Expected -ne $Actual) {
        throw "$Message Expected '$Expected', actual '$Actual'."
    }
}

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw $Message
    }
}

function Start-IsolatedProcess {
    param([string]$Executable, [string[]]$ArgumentList)
    $llpPriorAppData = $env:APPDATA
    $llpPriorLocalAppData = $env:LOCALAPPDATA
    $llpPriorUserProfile = $env:USERPROFILE
    try {
        $env:APPDATA = $llpAppData
        $env:LOCALAPPDATA = $llpLocalAppData
        $env:USERPROFILE = $llpUserProfile
        return Start-Process -FilePath $Executable -ArgumentList $ArgumentList -PassThru -WindowStyle Hidden
    }
    finally {
        $env:APPDATA = $llpPriorAppData
        $env:LOCALAPPDATA = $llpPriorLocalAppData
        $env:USERPROFILE = $llpPriorUserProfile
    }
}

function Invoke-PersistenceProbe {
    param(
        [string]$Executable,
        [string]$Name,
        [switch]$Resave
    )

    $llpProbePath = Join-Path $llpTempRoot "$Name.json"
    $llpArguments = @('--persistence-probe-file', $llpProbePath)
    if ($Resave) {
        $llpArguments += '--persistence-resave'
    }
    $script:llpProcess = Start-IsolatedProcess -Executable $Executable -ArgumentList $llpArguments
    if (-not $script:llpProcess.WaitForExit(5000)) {
        throw 'The real NativeUi EXE did not complete the persistence probe within five seconds.'
    }
    Assert-Equal 0 $script:llpProcess.ExitCode 'Persistence probe exit code mismatch.'
    Assert-True (Test-Path -LiteralPath $llpProbePath -PathType Leaf) 'The real NativeUi EXE did not write a persistence probe.'
    return Get-Content -Raw -Encoding UTF8 -LiteralPath $llpProbePath | ConvertFrom-Json
}

function Set-ActiveLaunchpadFolder {
    param([string]$Folder)
    $llpDocument = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpSettingsPath | ConvertFrom-Json
    $llpDocument.LaunchpadFolder = $Folder
    $llpContent = $llpDocument | ConvertTo-Json -Depth 12
    [System.IO.File]::WriteAllText($llpSettingsPath, $llpContent, [System.Text.UTF8Encoding]::new($false))
}

$llpExpectedRegionName = $llpChineseResearch + ' "' + $llpChineseCore + '" } ' + $llpRocket
$llpExpectedDisplayName = $llpChineseApp + ' "Alpha" } ' + $llpChineseText + ' ' + $llpRocket

try {
    $llpJsonHeader = Join-Path $llpRepoRoot 'src\third_party\nlohmann\json.hpp'
    Assert-Equal 'AAF127C04CB31C406E5B04A63F1AE89369FCCDE6D8FA7CDDA1ED4F32DFC5DE63' `
        (Get-FileHash -Algorithm SHA256 -LiteralPath $llpJsonHeader).Hash `
        'Vendored nlohmann/json 3.12.0 checksum mismatch.'
    Write-Output 'PASS Persistence_DependencyChecksumMatchesNlohmannJson_3_12_0'

    New-Item -ItemType Directory -Force -Path $llpOutputDirectory, $llpDataDirectory, $llpLocalAppData, $llpUserProfile, $llpFolderA, $llpFolderB, $llpFolderC | Out-Null
    [System.IO.File]::WriteAllText($llpSourceA, '', [System.Text.UTF8Encoding]::new($false))

    $llpSettings = [ordered]@{
        SchemaVersion = 2
        LaunchpadFolder = $llpFolderA
        Hotkey = 'Ctrl+Alt+Shift+6'
        StartWithWindows = $false
        IconSize = 'Medium'
        ViewMode = 'InlineRegions'
        DisplayMode = 'Spotlight'
        Language = 'English'
        SpotlightWidth = 980
        SpotlightHeight = 720
        AppSpacing = 8
        WheelSensitivity = 1.0
        ResetSearchOnOpen = $true
    } | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText($llpSettingsPath, $llpSettings, [System.Text.UTF8Encoding]::new($false))

    $llpEscapedSource = $llpSourceA.Replace('\', '\\')
    $llpLegacyJson = @"
{
  "SchemaVersion": 1,
  "ViewMode": "InlineRegions",
  "Regions": [
    { "Id": "research", "Name": "\u7814\u53D1 \"\u6838\u5FC3\" } \uD83D\uDE80", "Order": 0 }
  ],
  "Items": [
    {
      "SourcePath": "$llpEscapedSource",
      "DisplayName": "\u5E94\u7528 \"Alpha\" } \u4E2D\u6587 \uD83D\uDE80",
      "RegionId": "research",
      "Order": 0
    }
  ]
}
"@
    [System.IO.File]::WriteAllText($llpLegacyLayoutPath, $llpLegacyJson, [System.Text.UTF8Encoding]::new($false))

    & (Join-Path $llpRepoRoot 'tools\build-native-ui.ps1') `
        -OutputDirectory $llpOutputDirectory `
        -ProductVersion '1.11.0' `
        -BuildCommit 'persistencetest' `
        -BuildChannel Development | Out-Null
    $llpExecutable = Join-Path $llpOutputDirectory 'LightLaunchpad.NativeUi.exe'

    $llpProbe = Invoke-PersistenceProbe -Executable $llpExecutable -Name 'migration-roundtrip' -Resave
    Assert-Equal 2 ([int]$llpProbe.settings.schemaVersion) 'Settings schema version mismatch.'
    Assert-Equal ([System.IO.Path]::GetFullPath($llpFolderA)) ([System.IO.Path]::GetFullPath([string]$llpProbe.settings.launchpadFolder)) 'Settings launchpad folder mismatch.'
    Assert-Equal 2 ([int]$llpProbe.layout.schemaVersion) 'Scoped layout schema version mismatch.'
    Assert-Equal $true ([bool]$llpProbe.layout.migratedLegacy) 'Legacy layout was not migrated.'
    $llpResearchRegions = @($llpProbe.layout.regions | Where-Object { $_.id -eq 'research' })
    Assert-Equal 1 $llpResearchRegions.Count 'Migrated research region count mismatch.'
    Assert-Equal $llpExpectedRegionName ([string]$llpResearchRegions[0].name) 'Region Unicode/escape round trip mismatch.'
    Assert-Equal 1 @($llpProbe.layout.items).Count 'Migrated item count mismatch.'
    Assert-Equal $llpExpectedDisplayName ([string]$llpProbe.layout.items[0].displayName) 'Item Unicode/escape round trip mismatch.'
    Assert-True (Test-Path -LiteralPath ([string]$llpProbe.layout.path) -PathType Leaf) 'Scoped layout file was not created.'
    $llpLayoutAPath = [string]$llpProbe.layout.path
    $llpLegacyArchives = @(Get-ChildItem -LiteralPath $llpDataDirectory -Filter 'layout.json.migrated-v1*' -File)
    Assert-Equal 1 $llpLegacyArchives.Count 'The migrated legacy layout was not archived exactly once.'
    Write-Output 'PASS Persistence_LegacyJsonMigratesAndRoundTripsEscapesUnicodeAndSurrogatePairs'

    Set-ActiveLaunchpadFolder -Folder $llpFolderB
    $llpFolderBProbe = Invoke-PersistenceProbe -Executable $llpExecutable -Name 'folder-b' -Resave
    Assert-True ([string]$llpFolderBProbe.layout.path -ne $llpLayoutAPath) 'Different launchpad folders resolved to the same scoped layout path.'
    Assert-Equal 0 @($llpFolderBProbe.layout.items).Count 'Folder B displayed items from folder A.'
    Assert-Equal 0 @($llpFolderBProbe.layout.missingItems).Count 'Folder B inherited missing metadata from folder A.'
    Assert-Equal 0 @($llpFolderBProbe.layout.regions | Where-Object { $_.id -eq 'research' }).Count 'Folder B inherited regions from folder A.'

    Set-ActiveLaunchpadFolder -Folder $llpFolderA
    $llpFolderAProbe = Invoke-PersistenceProbe -Executable $llpExecutable -Name 'folder-a-return' -Resave
    Assert-Equal $llpLayoutAPath ([string]$llpFolderAProbe.layout.path) 'Switching back did not resolve the original folder scope.'
    Assert-Equal 1 @($llpFolderAProbe.layout.items).Count 'Switching back did not restore folder A items.'
    Assert-Equal $llpExpectedDisplayName ([string]$llpFolderAProbe.layout.items[0].displayName) 'Switching back lost item metadata.'
    Write-Output 'PASS Persistence_FolderScopesPreventLeakageAndRestorePriorLayout'

    $llpValidSettingsBytes = [System.IO.File]::ReadAllBytes($llpSettingsPath)
    $llpInvalidSettings = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpSettingsPath | ConvertFrom-Json
    $llpInvalidSettings.SpotlightWidth = 'wide'
    [System.IO.File]::WriteAllText(
        $llpSettingsPath,
        ($llpInvalidSettings | ConvertTo-Json -Depth 12),
        [System.Text.UTF8Encoding]::new($false))
    $llpInvalidSettingsProbe = Invoke-PersistenceProbe -Executable $llpExecutable -Name 'invalid-settings-type'
    Assert-True ([string]$llpInvalidSettingsProbe.settings.error -match 'SpotlightWidth') 'Invalid settings field type was not reported.'
    Assert-Equal 980 ([int]$llpInvalidSettingsProbe.settings.spotlightWidth) 'Invalid settings field did not use the safe default.'

    $llpInvalidSettings.SchemaVersion = 999
    [System.IO.File]::WriteAllText(
        $llpSettingsPath,
        ($llpInvalidSettings | ConvertTo-Json -Depth 12),
        [System.Text.UTF8Encoding]::new($false))
    $llpFutureSettingsHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $llpSettingsPath).Hash
    $llpFutureSettingsProbe = Invoke-PersistenceProbe -Executable $llpExecutable -Name 'future-settings-schema'
    Assert-True ([string]$llpFutureSettingsProbe.settings.error -match 'Unsupported settings schema version 999') 'Future settings schema was not rejected with diagnostics.'
    Assert-Equal $llpFutureSettingsHash (Get-FileHash -Algorithm SHA256 -LiteralPath $llpSettingsPath).Hash 'Rejected future settings schema was modified.'

    $llpEscapedFolderA = $llpFolderA.Replace('\', '\\')
    [System.IO.File]::WriteAllText(
        $llpSettingsPath,
        "{`"SchemaVersion`":1,`"LaunchpadFolder`":`"$llpEscapedFolderA`"}",
        [System.Text.UTF8Encoding]::new($false))
    $llpLegacySettingsProbe = Invoke-PersistenceProbe -Executable $llpExecutable -Name 'legacy-settings-defaults'
    Assert-Equal $true ([bool]$llpLegacySettingsProbe.settings.migrated) 'Schema v1 settings were not identified for migration.'
    Assert-Equal 'Alt+D' ([string]$llpLegacySettingsProbe.settings.hotkey) 'Missing schema v1 hotkey did not retain the default.'
    Assert-Equal 980 ([int]$llpLegacySettingsProbe.settings.spotlightWidth) 'Missing schema v1 width did not retain the default.'
    Assert-Equal '' ([string]$llpLegacySettingsProbe.settings.error) 'Compatible missing schema v1 fields produced an error.'
    [System.IO.File]::WriteAllBytes($llpSettingsPath, $llpValidSettingsBytes)

    $llpValidLayoutBytes = [System.IO.File]::ReadAllBytes($llpLayoutAPath)
    $llpDuplicateLayout = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpLayoutAPath | ConvertFrom-Json
    $llpDuplicateLayout.Items = @($llpDuplicateLayout.Items) + @($llpDuplicateLayout.Items[0])
    [System.IO.File]::WriteAllText(
        $llpLayoutAPath,
        ($llpDuplicateLayout | ConvertTo-Json -Depth 12),
        [System.Text.UTF8Encoding]::new($false))
    $llpDuplicateLayoutProbe = Invoke-PersistenceProbe -Executable $llpExecutable -Name 'duplicate-layout-item'
    Assert-Equal 1 @($llpDuplicateLayoutProbe.layout.items).Count 'Duplicate stable source paths produced duplicate visible items.'
    Assert-True ([string]$llpDuplicateLayoutProbe.layout.error -match 'duplicate') 'Duplicate stable source path was not reported.'

    $llpInvalidLayout = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpLayoutAPath | ConvertFrom-Json
    $llpInvalidLayout.PSObject.Properties.Remove('FolderIdentity')
    [System.IO.File]::WriteAllText(
        $llpLayoutAPath,
        ($llpInvalidLayout | ConvertTo-Json -Depth 12),
        [System.Text.UTF8Encoding]::new($false))
    $llpInvalidLayoutProbe = Invoke-PersistenceProbe -Executable $llpExecutable -Name 'missing-folder-identity'
    Assert-True ([string]$llpInvalidLayoutProbe.layout.error -match 'FolderIdentity') 'Schema v2 layout without FolderIdentity was not rejected.'
    Assert-Equal 0 @($llpInvalidLayoutProbe.layout.regions | Where-Object { $_.id -eq 'research' }).Count 'Rejected layout content leaked into the active scope.'

    $llpInvalidLayout.SchemaVersion = 999
    [System.IO.File]::WriteAllText(
        $llpLayoutAPath,
        ($llpInvalidLayout | ConvertTo-Json -Depth 12),
        [System.Text.UTF8Encoding]::new($false))
    $llpFutureLayoutHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $llpLayoutAPath).Hash
    $llpFutureLayoutProbe = Invoke-PersistenceProbe -Executable $llpExecutable -Name 'future-layout-schema'
    Assert-True ([string]$llpFutureLayoutProbe.layout.error -match 'Unsupported layout schema version 999') 'Future layout schema was not rejected with diagnostics.'
    Assert-Equal $llpFutureLayoutHash (Get-FileHash -Algorithm SHA256 -LiteralPath $llpLayoutAPath).Hash 'Rejected future layout schema was modified.'
    [System.IO.File]::WriteAllBytes($llpLayoutAPath, $llpValidLayoutBytes)

    Set-ActiveLaunchpadFolder -Folder $llpFolderC
    [System.IO.File]::WriteAllText(
        $llpLegacyLayoutPath,
        '{"SchemaVersion":999,"Regions":[],"Items":[]}',
        [System.Text.UTF8Encoding]::new($false))
    $llpFutureLegacyHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $llpLegacyLayoutPath).Hash
    $llpArchiveCountBeforeFutureLegacy = @(Get-ChildItem -LiteralPath $llpDataDirectory -Filter 'layout.json.migrated-v1*' -File).Count
    $llpFutureLegacyProbe = Invoke-PersistenceProbe -Executable $llpExecutable -Name 'future-legacy-layout-schema'
    Assert-True ([string]$llpFutureLegacyProbe.layout.error -match 'Unsupported layout schema version 999') 'Future legacy layout schema was not rejected with diagnostics.'
    Assert-Equal $llpFutureLegacyHash (Get-FileHash -Algorithm SHA256 -LiteralPath $llpLegacyLayoutPath).Hash 'Rejected future legacy layout was modified or archived.'
    Assert-Equal $llpArchiveCountBeforeFutureLegacy @(Get-ChildItem -LiteralPath $llpDataDirectory -Filter 'layout.json.migrated-v1*' -File).Count 'Rejected future legacy layout created a migration archive.'
    Remove-Item -LiteralPath $llpLegacyLayoutPath
    Set-ActiveLaunchpadFolder -Folder $llpFolderA
    Write-Output 'PASS Persistence_ValidationRejectsInvalidFieldsAndUnsupportedSchemasWithoutMutation'

    Remove-Item -LiteralPath $llpSourceA
    $llpMissingProbe = Invoke-PersistenceProbe -Executable $llpExecutable -Name 'missing-item' -Resave
    Assert-Equal 0 @($llpMissingProbe.layout.items).Count 'A missing source remained visible.'
    Assert-Equal 1 @($llpMissingProbe.layout.missingItems).Count 'Missing source metadata was not retained.'
    Assert-Equal $llpExpectedDisplayName ([string]$llpMissingProbe.layout.missingItems[0].displayName) 'Missing source metadata changed.'

    [System.IO.File]::WriteAllText($llpSourceA, '', [System.Text.UTF8Encoding]::new($false))
    $llpReattachedProbe = Invoke-PersistenceProbe -Executable $llpExecutable -Name 'reattached-item' -Resave
    Assert-Equal 1 @($llpReattachedProbe.layout.items).Count 'Returned source was not reattached.'
    Assert-Equal 0 @($llpReattachedProbe.layout.missingItems).Count 'Returned source remained in missing metadata.'
    Assert-Equal $llpExpectedDisplayName ([string]$llpReattachedProbe.layout.items[0].displayName) 'Reattached source lost its metadata.'
    Write-Output 'PASS Persistence_MissingMetadataReattachesWhenStablePathReturns'

    $llpLayoutBackupPath = "$llpLayoutAPath.bak"
    Assert-True (Test-Path -LiteralPath $llpLayoutBackupPath -PathType Leaf) 'Scoped layout backup was not created.'
    $llpLayoutBackupHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $llpLayoutBackupPath).Hash
    [System.IO.File]::WriteAllText($llpLayoutAPath, '{"SchemaVersion":2,"Items":[', [System.Text.UTF8Encoding]::new($false))
    $llpLayoutRecoveryProbe = Invoke-PersistenceProbe -Executable $llpExecutable -Name 'layout-recovery'
    Assert-Equal $true ([bool]$llpLayoutRecoveryProbe.layout.recoveredFromBackup) 'Truncated layout did not recover from backup.'
    Assert-True (Test-Path -LiteralPath ([string]$llpLayoutRecoveryProbe.layout.quarantinePath) -PathType Leaf) 'Truncated layout was not quarantined.'
    Assert-Equal $llpLayoutBackupHash (Get-FileHash -Algorithm SHA256 -LiteralPath $llpLayoutBackupPath).Hash 'Layout recovery overwrote the last good backup.'
    Assert-Equal 1 @($llpLayoutRecoveryProbe.layout.items).Count 'Layout recovery lost the visible item.'
    $llpRestoredLayout = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpLayoutAPath | ConvertFrom-Json
    Assert-Equal 2 ([int]$llpRestoredLayout.SchemaVersion) 'Recovered layout primary is not valid schema v2 JSON.'

    $llpSettingsBackupPath = "$llpSettingsPath.bak"
    Assert-True (Test-Path -LiteralPath $llpSettingsBackupPath -PathType Leaf) 'Settings backup was not created.'
    $llpSettingsBackupHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $llpSettingsBackupPath).Hash
    [System.IO.File]::WriteAllText($llpSettingsPath, '{"SchemaVersion":2,"LaunchpadFolder":', [System.Text.UTF8Encoding]::new($false))
    $llpSettingsRecoveryProbe = Invoke-PersistenceProbe -Executable $llpExecutable -Name 'settings-recovery'
    Assert-Equal $true ([bool]$llpSettingsRecoveryProbe.settings.recoveredFromBackup) 'Truncated settings did not recover from backup.'
    Assert-Equal $llpSettingsBackupHash (Get-FileHash -Algorithm SHA256 -LiteralPath $llpSettingsBackupPath).Hash 'Settings recovery overwrote the last good backup.'
    $llpSettingsQuarantines = @(Get-ChildItem -LiteralPath $llpDataDirectory -Filter 'settings.json.corrupt-*' -File)
    Assert-True ($llpSettingsQuarantines.Count -ge 1) 'Truncated settings was not quarantined.'
    Assert-Equal ([System.IO.Path]::GetFullPath($llpFolderA)) ([System.IO.Path]::GetFullPath([string]$llpSettingsRecoveryProbe.settings.launchpadFolder)) 'Settings recovery did not restore the last good launchpad folder.'
    $llpRestoredSettings = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpSettingsPath | ConvertFrom-Json
    Assert-Equal 2 ([int]$llpRestoredSettings.SchemaVersion) 'Recovered settings primary is not valid schema v2 JSON.'
    Assert-Equal 0 (@(Get-ChildItem -LiteralPath $llpDataDirectory -Recurse -File | Where-Object { $_.Name -match '\.(tmp|recovery\.tmp)$' }).Count) 'Persistence left temporary files behind.'
    Write-Output 'PASS Persistence_TruncatedPrimaryRecoversFromBackupWithoutOverwritingLastGood'

    Remove-Item -LiteralPath $llpSettingsPath -Force
    Remove-Item -LiteralPath $llpSettingsBackupPath -Force
    $llpFirstRunProbe = Invoke-PersistenceProbe -Executable $llpExecutable -Name 'first-run-defaults'
    Assert-Equal '' ([string]$llpFirstRunProbe.settings.error) 'Normal first run without settings was reported as corruption.'
    Assert-Equal 'Alt+D' ([string]$llpFirstRunProbe.settings.hotkey) 'Normal first run did not use the default hotkey.'
    Assert-Equal 980 ([int]$llpFirstRunProbe.settings.spotlightWidth) 'Normal first run did not use the default Spotlight width.'
    Write-Output 'PASS Persistence_FirstRunUsesDefaultsWithoutFalseCorruptionDiagnostics'
}
finally {
    if ($llpProcess -and -not $llpProcess.HasExited) {
        Stop-Process -Id $llpProcess.Id -Force
        $llpProcess.WaitForExit()
    }
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
