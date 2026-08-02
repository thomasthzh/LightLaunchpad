$ErrorActionPreference = 'Stop'

$llpRepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$llpTempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("lightlaunchpad-organization-shell-" + [System.Guid]::NewGuid().ToString('N'))
$llpOutputDirectory = Join-Path $llpTempRoot 'development'
$llpLaunchpad = Join-Path $llpTempRoot 'Launchpad'
$llpAppData = Join-Path $llpTempRoot 'appdata'
$llpLocalAppData = Join-Path $llpTempRoot 'localappdata'
$llpUserProfile = Join-Path $llpTempRoot 'profile'
$llpProbePath = Join-Path $llpTempRoot 'organization-probe.json'
$llpProcess = $null

function Assert-Equal {
    param([object]$Expected, [object]$Actual, [string]$Message)
    if ($Expected -ne $Actual) { throw "$Message Expected '$Expected', actual '$Actual'." }
}

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

try {
    New-Item -ItemType Directory -Force -Path $llpOutputDirectory, $llpLaunchpad, $llpAppData, $llpLocalAppData, $llpUserProfile | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $llpLaunchpad 'A.lnk'), 'alpha', [System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::WriteAllText((Join-Path $llpLaunchpad 'B.lnk'), 'bravo', [System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::WriteAllText((Join-Path $llpLaunchpad 'C.lnk'), 'charlie', [System.Text.UTF8Encoding]::new($false))

    $llpBuildCommit = 'organization' + [System.Guid]::NewGuid().ToString('N').Substring(0, 12)
    & (Join-Path $llpRepoRoot 'tools\build-native-ui.ps1') `
        -OutputDirectory $llpOutputDirectory `
        -ProductVersion '1.11.0' `
        -BuildCommit $llpBuildCommit `
        -BuildChannel Development | Out-Null
    $llpExecutable = Join-Path $llpOutputDirectory 'LightLaunchpad.NativeUi.exe'

    $llpPriorAppData = $env:APPDATA
    $llpPriorLocalAppData = $env:LOCALAPPDATA
    $llpPriorUserProfile = $env:USERPROFILE
    try {
        $env:APPDATA = $llpAppData
        $env:LOCALAPPDATA = $llpLocalAppData
        $env:USERPROFILE = $llpUserProfile
        $llpProcess = Start-Process -FilePath $llpExecutable -ArgumentList @(
            '--organization-probe-file', $llpProbePath,
            '--organization-probe-root', $llpLaunchpad) -PassThru -WindowStyle Hidden
    }
    finally {
        $env:APPDATA = $llpPriorAppData
        $env:LOCALAPPDATA = $llpPriorLocalAppData
        $env:USERPROFILE = $llpPriorUserProfile
    }

    if (-not $llpProcess.WaitForExit(15000)) { throw 'The real NativeUi EXE did not complete the recycle/restore probe within fifteen seconds.' }
    Assert-Equal 0 $llpProcess.ExitCode 'Organization probe exit code mismatch.'
    Assert-True (Test-Path -LiteralPath $llpProbePath) 'Organization probe did not publish evidence.'
    $llpProbe = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpProbePath | ConvertFrom-Json

    Assert-Equal $true ([bool]$llpProbe.completed) 'Organization probe did not complete every stage.'
    Assert-Equal 'STA' ([string]$llpProbe.brokerApartment) 'Recycle/restore work did not run in an STA.'
    Assert-True ([int64]$llpProbe.uiThreadId -ne [int64]$llpProbe.brokerThreadId) 'Recycle/restore work ran on the UI thread.'
    Assert-Equal 2 ([int]$llpProbe.batchRemoved) 'Batch recycle did not remove both existing direct-child items.'
    Assert-Equal 1 ([int]$llpProbe.batchFailed) 'Batch recycle did not report the missing item as a partial failure.'
    Assert-True ([uint64]$llpProbe.batchUndoToken -gt 0) 'Batch recycle did not return an exact undo token.'
    Assert-Equal $true ([bool]$llpProbe.batchAbsentAfterRecycle) 'Batch files still existed after successful recycle completion.'
    Assert-Equal 2 ([int]$llpProbe.batchRestored) 'Exact batch undo did not restore both files.'
    Assert-Equal 0 ([int]$llpProbe.batchRestoreFailed) 'Exact batch undo reported a failure.'
    Assert-Equal $true ([bool]$llpProbe.batchContentsRestored) 'Batch undo did not preserve exact file contents.'
    Assert-True ([uint64]$llpProbe.collisionUndoToken -gt 0) 'Collision scenario did not retain an undo record.'
    Assert-True ([int]$llpProbe.collisionRestoreFailed -ge 1) 'Restore collision did not report failure.'
    Assert-Equal $true ([bool]$llpProbe.collisionPreserved) 'Restore collision overwrote or removed the existing original-path file.'
    Assert-Equal 1 ([int]$llpProbe.retryRestored) 'Retry after resolving the collision did not restore the recycled file.'
    Assert-Equal $true ([bool]$llpProbe.retryContentRestored) 'Retry did not restore the original recycled content.'

    Assert-Equal 'alpha' ([System.IO.File]::ReadAllText((Join-Path $llpLaunchpad 'A.lnk'))) 'A.lnk content changed after exact undo.'
    Assert-Equal 'bravo' ([System.IO.File]::ReadAllText((Join-Path $llpLaunchpad 'B.lnk'))) 'B.lnk content changed after exact undo.'
    Assert-Equal 'charlie' ([System.IO.File]::ReadAllText((Join-Path $llpLaunchpad 'C.lnk'))) 'C.lnk content changed after collision retry.'
    Write-Output "METRIC OrganizationShell batchRemoved=$([int]$llpProbe.batchRemoved) batchFailed=$([int]$llpProbe.batchFailed) batchRestored=$([int]$llpProbe.batchRestored) collisionFailed=$([int]$llpProbe.collisionRestoreFailed) retryRestored=$([int]$llpProbe.retryRestored)"
    Write-Output 'PASS OrganizationShell_RealExePartialRecycleExactUndoCollisionSafetyAndRetry'

    $llpDataDirectory = Join-Path $llpAppData 'LightLaunchpad'
    New-Item -ItemType Directory -Force -Path $llpDataDirectory | Out-Null
    $llpSettings = [ordered]@{
        SchemaVersion = 2
        LaunchpadFolder = $llpLaunchpad
        Hotkey = 'Alt+D'
        StartWithWindows = $false
        IconSize = 'Medium'
        ViewMode = 'InlineRegions'
        DisplayMode = 'Launchpad'
        Language = 'English'
        AppSpacing = 8
        WheelSensitivity = 1.0
        ResetSearchOnOpen = $true
    } | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText((Join-Path $llpDataDirectory 'settings.json'), $llpSettings, [System.Text.UTF8Encoding]::new($false))
    $llpLegacyLayout = [ordered]@{
        SchemaVersion = 1
        ViewMode = 'InlineRegions'
        Regions = @(
            [ordered]@{ Id = 'work'; Name = 'Work'; Order = 0 },
            [ordered]@{ Id = 'uncategorized'; Name = 'Uncategorized'; Order = 1 }
        )
        Items = @(
            [ordered]@{ SourcePath = (Join-Path $llpLaunchpad 'A.lnk'); DisplayName = 'Alpha'; RegionId = 'work'; Order = 0 },
            [ordered]@{ SourcePath = (Join-Path $llpLaunchpad 'B.lnk'); DisplayName = 'Beta'; RegionId = 'work'; Order = 1 },
            [ordered]@{ SourcePath = (Join-Path $llpLaunchpad 'C.lnk'); DisplayName = 'Gamma'; RegionId = 'uncategorized'; Order = 0 }
        )
    } | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText((Join-Path $llpDataDirectory 'layout.json'), $llpLegacyLayout, [System.Text.UTF8Encoding]::new($false))
    $llpRegionProbePath = Join-Path $llpTempRoot 'region-undo-probe.json'
    $llpPriorAppData = $env:APPDATA
    $llpPriorLocalAppData = $env:LOCALAPPDATA
    $llpPriorUserProfile = $env:USERPROFILE
    try {
        $env:APPDATA = $llpAppData
        $env:LOCALAPPDATA = $llpLocalAppData
        $env:USERPROFILE = $llpUserProfile
        $llpProcess = Start-Process -FilePath $llpExecutable -ArgumentList @(
            '--region-undo-probe-file', $llpRegionProbePath,
            '--region-undo-probe-region', 'work') -PassThru -WindowStyle Hidden
    }
    finally {
        $env:APPDATA = $llpPriorAppData
        $env:LOCALAPPDATA = $llpPriorLocalAppData
        $env:USERPROFILE = $llpPriorUserProfile
    }
    if (-not $llpProcess.WaitForExit(15000)) { throw 'The real NativeUi EXE did not complete the region undo probe.' }
    Assert-Equal 0 $llpProcess.ExitCode 'Region undo probe exit code mismatch.'
    $llpRegionProbe = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpRegionProbePath | ConvertFrom-Json
    Assert-Equal $true ([bool]$llpRegionProbe.regionDeleted) 'Normal region was not deleted.'
    Assert-Equal $true ([bool]$llpRegionProbe.itemsMovedToUncategorized) 'Region deletion did not move every member to Uncategorized.'
    Assert-Equal $true ([bool]$llpRegionProbe.sourceFilesPreserved) 'Region deletion removed a source file.'
    Assert-Equal $true ([bool]$llpRegionProbe.undoAvailable) 'Region deletion did not expose bounded same-action undo.'
    Assert-Equal $true ([bool]$llpRegionProbe.regionRestored) 'Region undo did not restore region metadata.'
    Assert-Equal $true ([bool]$llpRegionProbe.placementsRestored) 'Region undo did not restore exact item placements.'
    Assert-Equal $true ([bool]$llpRegionProbe.exactOrderRestored) 'Region undo did not restore exact item order.'
    Assert-True ((Test-Path -LiteralPath (Join-Path $llpLaunchpad 'A.lnk')) -and (Test-Path -LiteralPath (Join-Path $llpLaunchpad 'B.lnk')) -and (Test-Path -LiteralPath (Join-Path $llpLaunchpad 'C.lnk'))) 'Region delete/undo changed source-file existence.'
    Write-Output 'PASS RegionUndo_RealExeMovesOnlyToUncategorizedAndRestoresExactLayout'
}
finally {
    if ($llpProcess -and -not $llpProcess.HasExited) {
        $llpProcess.Kill()
        $llpProcess.WaitForExit()
    }
    $llpResolvedTemp = [System.IO.Path]::GetFullPath($llpTempRoot)
    $llpExpectedTempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    if ($llpResolvedTemp.StartsWith($llpExpectedTempRoot, [System.StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $llpResolvedTemp)) {
        $llpTempItem = Get-Item -LiteralPath $llpResolvedTemp -Force
        if (($llpTempItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Refusing to clean reparse-point test output $llpResolvedTemp." }
        Remove-Item -LiteralPath $llpResolvedTemp -Recurse -Force
    }
}
