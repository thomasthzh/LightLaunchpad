param()

$ErrorActionPreference = 'Stop'

$llpRepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$llpTempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("lightlaunchpad-catalog-broker-tests-" + [System.Guid]::NewGuid().ToString('N'))
$llpOutputDirectory = Join-Path $llpTempRoot 'development'
$llpAppData = Join-Path $llpTempRoot 'appdata'
$llpLocalAppData = Join-Path $llpTempRoot 'localappdata'
$llpUserProfile = Join-Path $llpTempRoot 'profile'
$llpDataDirectory = Join-Path $llpAppData 'LightLaunchpad'
$llpLaunchpad = Join-Path $llpTempRoot 'Launchpad'
$llpImportDestination = Join-Path $llpTempRoot 'ImportedLaunchpad'
$llpImportRoot = Join-Path $llpTempRoot 'StartMenuFixture'
$llpReparseTarget = Join-Path $llpTempRoot 'ReparseTarget'
$llpImportJunction = Join-Path $llpImportRoot 'LinkedApps'
$llpCatalogProbePath = Join-Path $llpTempRoot 'catalog-probe.json'
$llpImportProbePath = Join-Path $llpTempRoot 'import-probe.json'
$llpSearchHistoryPath = Join-Path $llpDataDirectory 'search-history.json'
$llpProcesses = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()

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
        $llpProcess = Start-Process -FilePath $Executable -ArgumentList $ArgumentList -PassThru -WindowStyle Hidden
        $script:llpProcesses.Add($llpProcess)
        return $llpProcess
    }
    finally {
        $env:APPDATA = $llpPriorAppData
        $env:LOCALAPPDATA = $llpPriorLocalAppData
        $env:USERPROFILE = $llpPriorUserProfile
    }
}

try {
    $llpMainSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\LightLaunchpad.NativeUi.cpp')
    $llpBrokerSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\ShellBroker.cpp')
    $llpBrokerHeader = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\ShellBroker.h')
    foreach ($llpForbiddenMainCall in @('ShellExecuteW(', 'PrivateExtractIconsW(', 'SHGetFileInfoW(', 'CoCreateInstance(CLSID_ShellLink', 'DeleteFileW(sourcePath', 'FindFirstFileW(')) {
        Assert-True (-not $llpMainSource.Contains($llpForbiddenMainCall)) "UI/render module still contains forbidden Shell or catalog call: $llpForbiddenMainCall"
    }
    Assert-True ($llpMainSource.Contains('RequestPersistJson')) 'UI layout edits are not queued as in-memory persistence snapshots.'
    Assert-True ($llpBrokerHeader.Contains('std::shared_ptr<const catalog::Snapshot>')) 'Published catalog snapshot is not immutable at the broker boundary.'
    foreach ($llpRequiredBrokerCall in @('COINIT_APARTMENTTHREADED', 'PrivateExtractIconsW(', 'SHGetFileInfoW(', 'CoCreateInstance(CLSID_ShellLink', 'CLSID_FileOperation', 'stopping.load()')) {
        Assert-True ($llpBrokerSource.Contains($llpRequiredBrokerCall)) "Shell broker is missing required ownership call: $llpRequiredBrokerCall"
    }
    Write-Output 'PASS ShellBrokerArchitecture_UiModuleContainsNoShellOrCatalogIoCalls'

    New-Item -ItemType Directory -Force -Path $llpOutputDirectory, $llpDataDirectory, $llpLocalAppData, $llpUserProfile, $llpLaunchpad, $llpImportDestination, $llpImportRoot, $llpReparseTarget | Out-Null

    $llpCachedSource = Join-Path $llpLaunchpad 'Cached Broken Shortcut.lnk'
    [System.IO.File]::WriteAllText($llpCachedSource, 'not-a-valid-shell-link', [System.Text.UTF8Encoding]::new($false))
    $llpSettings = [ordered]@{
        SchemaVersion = 2
        LaunchpadFolder = $llpLaunchpad
        Hotkey = 'Alt+D'
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
    [System.IO.File]::WriteAllText((Join-Path $llpDataDirectory 'settings.json'), $llpSettings, [System.Text.UTF8Encoding]::new($false))

    $llpEscapedCachedSource = $llpCachedSource.Replace('\', '\\')
    $llpLegacyLayout = @"
{
  "SchemaVersion": 1,
  "Regions": [{ "Id": "cached", "Name": "Cached", "Order": 0 }],
  "Items": [{ "SourcePath": "$llpEscapedCachedSource", "DisplayName": "Cached Item", "RegionId": "cached", "Order": 0 }]
}
"@
    [System.IO.File]::WriteAllText((Join-Path $llpDataDirectory 'layout.json'), $llpLegacyLayout, [System.Text.UTF8Encoding]::new($false))
    $llpFutureSearchHistory = '{"SchemaVersion":99,"Items":[{"StableId":"future-item","LaunchCount":42,"LastLaunchedUnixMilliseconds":9000,"Favorite":true}]}'
    [System.IO.File]::WriteAllText($llpSearchHistoryPath, $llpFutureSearchHistory, [System.Text.UTF8Encoding]::new($false))
    $llpFutureSearchHistoryHash = (Get-FileHash -LiteralPath $llpSearchHistoryPath -Algorithm SHA256).Hash

    & (Join-Path $llpRepoRoot 'tools\build-native-ui.ps1') `
        -OutputDirectory $llpOutputDirectory `
        -ProductVersion '1.11.0' `
        -BuildCommit 'catalogbrokertest' `
        -BuildChannel Development | Out-Null
    $llpExecutable = Join-Path $llpOutputDirectory 'LightLaunchpad.NativeUi.exe'

    $llpCatalogProcess = Start-IsolatedProcess -Executable $llpExecutable -ArgumentList @(
        '--catalog-broker-probe-file', $llpCatalogProbePath,
        '--catalog-broker-delay-ms', '1200')
    if (-not $llpCatalogProcess.WaitForExit(8000)) {
        throw 'The real NativeUi EXE did not complete the catalog broker probe within eight seconds.'
    }
    Assert-Equal 0 $llpCatalogProcess.ExitCode 'Catalog broker probe exit code mismatch.'
    $llpCatalogProbe = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpCatalogProbePath | ConvertFrom-Json
    Assert-Equal 'STA' ([string]$llpCatalogProbe.brokerApartment) 'Shell broker did not run in an STA.'
    Assert-True ([int64]$llpCatalogProbe.uiThreadId -ne [int64]$llpCatalogProbe.brokerThreadId) 'Catalog work ran on the UI thread.'
    Assert-Equal 1 ([int]$llpCatalogProbe.cachedItemCountBeforeRefresh) 'Initial immutable catalog snapshot did not contain the cached item.'
    Assert-Equal 1 ([int]$llpCatalogProbe.cachedItemCountDuringRefresh) 'Cached catalog disappeared while refresh was pending.'
    Assert-True (-not [string]::IsNullOrWhiteSpace([string]$llpCatalogProbe.cachedStableIdBeforeRefresh)) 'Published catalog item did not have a stable ID.'
    Assert-Equal ([string]$llpCatalogProbe.cachedStableIdBeforeRefresh) ([string]$llpCatalogProbe.cachedStableIdAfterRefresh) 'Refresh did not publish the same item by stable ID.'
    Assert-Equal 'x' ([string]$llpCatalogProbe.inputEcho) 'UI message processing did not accept input during refresh.'
    Assert-True ([int]$llpCatalogProbe.inputResponseMs -le 200) 'Input was blocked by background catalog work.'
    Assert-True ([int]$llpCatalogProbe.refreshMs -ge 1100) 'Slow refresh fixture did not exercise the asynchronous path.'
    Assert-True ([int]$llpCatalogProbe.catalogGeneration -ge 2) 'Refreshed immutable snapshot was not published.'
    Assert-Equal $true ([bool]$llpCatalogProbe.shutdownPersistenceQueued) 'Shutdown persistence fixture was not queued.'
    Assert-Equal $false ([bool]$llpCatalogProbe.shutdownHistoryPersistenceQueued) 'Unsupported future search history was queued for an unsafe downgrade overwrite.'
    Assert-Equal $llpFutureSearchHistoryHash (Get-FileHash -LiteralPath $llpSearchHistoryPath -Algorithm SHA256).Hash 'Unsupported future search history was modified.'
    $llpScopedLayouts = @(Get-ChildItem -LiteralPath (Join-Path $llpDataDirectory 'layouts') -Filter '*.json' -File)
    Assert-Equal 1 $llpScopedLayouts.Count 'Catalog probe did not produce exactly one scoped layout.'
    $llpShutdownLayout = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpScopedLayouts[0].FullName | ConvertFrom-Json
    Assert-Equal 'Shutdown Flush Sentinel' ([string]@($llpShutdownLayout.Items)[0].DisplayName) 'Queued layout persistence was lost during broker shutdown.'
    Write-Output "METRIC CatalogBroker inputResponseMs=$([int]$llpCatalogProbe.inputResponseMs) refreshMs=$([int]$llpCatalogProbe.refreshMs) uiThreadId=$([int64]$llpCatalogProbe.uiThreadId) brokerThreadId=$([int64]$llpCatalogProbe.brokerThreadId) generation=$([int]$llpCatalogProbe.catalogGeneration)"
    Write-Output "METRIC SearchHistoryFutureSchema queued=$([bool]$llpCatalogProbe.shutdownHistoryPersistenceQueued) preservedSha256=$llpFutureSearchHistoryHash"
    Write-Output 'PASS CatalogBroker_CachedSnapshotAndInputRemainAvailableDuringSlowRefresh'

    $llpSampleExecutable = Join-Path $llpImportRoot 'SampleApp.exe'
    Copy-Item -LiteralPath $llpExecutable -Destination $llpSampleExecutable
    $llpWshShell = New-Object -ComObject WScript.Shell
    try {
        $llpDuplicateShortcut = $llpWshShell.CreateShortcut((Join-Path $llpImportRoot 'DuplicateAlias.lnk'))
        $llpDuplicateShortcut.TargetPath = $llpSampleExecutable
        $llpDuplicateShortcut.Save()
    }
    finally {
        if ($llpDuplicateShortcut) { [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($llpDuplicateShortcut) }
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($llpWshShell)
    }
    Copy-Item -LiteralPath $llpExecutable -Destination (Join-Path $llpReparseTarget 'MustNotImport.exe')
    New-Item -ItemType Junction -Path $llpImportJunction -Target $llpReparseTarget | Out-Null
    $llpImportProcess = Start-IsolatedProcess -Executable $llpExecutable -ArgumentList @(
        '--shell-import-probe-file', $llpImportProbePath,
        '--shell-import-root', $llpImportRoot,
        '--shell-import-destination', $llpImportDestination,
        '--shell-import-repeat', '2')
    if (-not $llpImportProcess.WaitForExit(8000)) {
        throw 'The real NativeUi EXE did not complete the Shell import probe within eight seconds.'
    }
    Assert-Equal 0 $llpImportProcess.ExitCode 'Shell import probe exit code mismatch.'
    $llpImportProbe = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpImportProbePath | ConvertFrom-Json
    Assert-Equal 'STA' ([string]$llpImportProbe.brokerApartment) 'Start Menu import did not run in the Shell STA.'
    Assert-Equal 1 ([int]$llpImportProbe.firstImported) 'First Start Menu import count mismatch.'
    Assert-Equal 0 ([int]$llpImportProbe.secondImported) 'Repeated Start Menu import created a duplicate.'
    Assert-Equal 1 ([int]$llpImportProbe.skippedReparsePoints) 'Start Menu import did not report the guarded reparse-point directory.'
    Assert-Equal 1 (@(Get-ChildItem -LiteralPath $llpImportDestination -Filter '*.lnk' -File).Count) 'Import did not create exactly one shortcut.'
    Assert-Equal 0 (@(Get-ChildItem -LiteralPath $llpImportDestination -Filter '*.exe' -File).Count) 'Import copied an executable into the launchpad.'
    Assert-Equal 0 (@(Get-ChildItem -LiteralPath $llpImportDestination -File | Where-Object { $_.BaseName -match '\s\([0-9]+\)$' }).Count) 'Repeated import created a numbered duplicate.'
    $llpImportedShortcutPath = @(Get-ChildItem -LiteralPath $llpImportDestination -Filter '*.lnk' -File)[0].FullName
    $llpWshShell = New-Object -ComObject WScript.Shell
    try {
        $llpImportedShortcut = $llpWshShell.CreateShortcut($llpImportedShortcutPath)
        Assert-Equal ([System.IO.Path]::GetFullPath($llpSampleExecutable)) ([System.IO.Path]::GetFullPath([string]$llpImportedShortcut.TargetPath)) 'Imported shortcut target identity mismatch.'
    }
    finally {
        if ($llpImportedShortcut) { [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($llpImportedShortcut) }
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($llpWshShell)
    }
    Write-Output "METRIC ShellImport firstImported=$([int]$llpImportProbe.firstImported) secondImported=$([int]$llpImportProbe.secondImported) skippedReparsePoints=$([int]$llpImportProbe.skippedReparsePoints)"
    Write-Output 'PASS ShellBroker_RepeatedStartMenuImportCreatesOneShortcutWithoutCopyingExecutable'
}
finally {
    foreach ($llpProcess in $llpProcesses) {
        if ($llpProcess -and -not $llpProcess.HasExited) {
            Stop-Process -Id $llpProcess.Id -Force
            $llpProcess.WaitForExit()
        }
    }
    if (Test-Path -LiteralPath $llpImportJunction) {
        $llpResolvedImportRoot = [System.IO.Path]::GetFullPath($llpImportRoot)
        $llpResolvedImportJunction = [System.IO.Path]::GetFullPath($llpImportJunction)
        $llpImportRootBoundary = $llpResolvedImportRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
        if (-not $llpResolvedImportJunction.StartsWith($llpImportRootBoundary, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove test path outside import root $llpResolvedImportJunction."
        }
        $llpJunctionItem = Get-Item -LiteralPath $llpImportJunction -Force
        if (-not $llpJunctionItem.PSIsContainer -or (($llpJunctionItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0)) {
            throw "Refusing to remove non-directory reparse test path $llpResolvedImportJunction."
        }
        if ($llpJunctionItem.LinkType -ne 'Junction') {
            throw "Refusing to remove non-junction test path $llpResolvedImportJunction."
        }
        $llpJunctionTargets = @($llpJunctionItem.Target)
        if ($llpJunctionTargets.Count -ne 1) {
            throw "Refusing to remove junction without exactly one target $llpResolvedImportJunction."
        }
        $llpResolvedJunctionTarget = [System.IO.Path]::GetFullPath([string]$llpJunctionTargets[0])
        $llpResolvedReparseTarget = [System.IO.Path]::GetFullPath($llpReparseTarget)
        $llpResolvedTempRoot = [System.IO.Path]::GetFullPath($llpTempRoot)
        $llpTempRootBoundary = $llpResolvedTempRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
        if (-not $llpResolvedJunctionTarget.Equals($llpResolvedReparseTarget, [System.StringComparison]::OrdinalIgnoreCase) -or -not $llpResolvedJunctionTarget.StartsWith($llpTempRootBoundary, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove junction with unexpected target $llpResolvedJunctionTarget."
        }
        [System.IO.Directory]::Delete($llpResolvedImportJunction, $false)
        if (Test-Path -LiteralPath $llpResolvedImportJunction) {
            throw "Junction deletion did not remove test path $llpResolvedImportJunction."
        }
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
