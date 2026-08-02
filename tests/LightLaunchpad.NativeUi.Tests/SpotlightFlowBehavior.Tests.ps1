param(
    [string]$EvidenceDirectory = ''
)

$ErrorActionPreference = 'Stop'

$llpRepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$llpTempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("lightlaunchpad-spotlight-flow-" + [System.Guid]::NewGuid().ToString('N'))
$llpOutputDirectory = Join-Path $llpTempRoot 'development'
$llpAppData = Join-Path $llpTempRoot 'appdata'
$llpLocalAppData = Join-Path $llpTempRoot 'localappdata'
$llpUserProfile = Join-Path $llpTempRoot 'profile'
$llpDataDirectory = Join-Path $llpAppData 'LightLaunchpad'
$llpLaunchpad = Join-Path $llpTempRoot 'Launchpad'
$llpMarker = Join-Path $llpTempRoot 'launched.txt'
$llpProbePath = Join-Path $llpTempRoot 'spotlight-flow.json'
$llpReadyPath = Join-Path $llpTempRoot 'spotlight-ready.json'
$llpLogPath = Join-Path $llpTempRoot 'spotlight-flow.log'
$llpTarget = Join-Path $llpTempRoot 'LaunchProbeTarget.exe'
$llpProcess = $null

function Assert-Equal {
    param([object]$Expected, [object]$Actual, [string]$Message)
    if ($Expected -ne $Actual) { throw "$Message Expected '$Expected', actual '$Actual'." }
}

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Wait-LlpCondition {
    param([scriptblock]$Condition, [int]$TimeoutMilliseconds, [string]$Message)
    $llpDeadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    do {
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 25
    } while ([DateTime]::UtcNow -lt $llpDeadline)
    throw $Message
}

function Send-LlpText {
    param([IntPtr]$Window, [string]$Text)
    foreach ($llpCharacter in $Text.ToCharArray()) {
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($Window, 0x0102, [UIntPtr]$llpCharacter, [IntPtr]::Zero)
    }
}

function Send-LlpKey {
    param([IntPtr]$Window, [UInt16]$Key)
    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($Window, 0x0100, [UIntPtr]$Key, [IntPtr]::Zero)
}

function Get-LlpMarkerNames {
    if (-not (Test-Path -LiteralPath $llpMarker)) { return @() }
    return @((Get-Content -Encoding UTF8 -LiteralPath $llpMarker | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }) | ForEach-Object { [System.IO.Path]::GetFileName($_) })
}

function Get-LlpReadyState {
    if (-not (Test-Path -LiteralPath $llpReadyPath)) { return $null }
    try {
        return Get-Content -Raw -Encoding UTF8 -LiteralPath $llpReadyPath | ConvertFrom-Json
    }
    catch {
        return $null
    }
}

function Save-LlpWindowEvidence {
    param([IntPtr]$Window, [string]$Path)
    [void][LightLaunchpadSpotlightProbeNative]::UpdateWindow($Window)
    $llpRect = [LightLaunchpadSpotlightProbeNative+RECT]::new()
    Assert-True ([LightLaunchpadSpotlightProbeNative]::GetWindowRect($Window, [ref]$llpRect)) 'Could not read the Spotlight evidence window geometry.'
    $llpWidth = $llpRect.Right - $llpRect.Left
    $llpHeight = $llpRect.Bottom - $llpRect.Top
    Assert-True ($llpWidth -gt 0 -and $llpHeight -gt 0) 'Spotlight evidence window geometry was empty.'
    $llpBitmap = [System.Drawing.Bitmap]::new($llpWidth, $llpHeight, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $llpGraphics = [System.Drawing.Graphics]::FromImage($llpBitmap)
    $llpDc = $llpGraphics.GetHdc()
    try {
        Assert-True ([LightLaunchpadSpotlightProbeNative]::PrintWindow($Window, $llpDc, 2)) 'PrintWindow could not capture the rendered Spotlight window.'
    }
    finally {
        $llpGraphics.ReleaseHdc($llpDc)
        $llpGraphics.Dispose()
    }
    try {
        $llpBitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $llpBitmap.Dispose()
    }
}

try {
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    Add-Type -AssemblyName System.Drawing
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class LightLaunchpadSpotlightProbeNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    private delegate bool EnumWindowsCallback(IntPtr hwnd, IntPtr state);
    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr state);
    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")]
    public static extern bool IsWindow(IntPtr hwnd);
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")]
    public static extern bool UpdateWindow(IntPtr hwnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessage(IntPtr hwnd, uint message, UIntPtr wParam, IntPtr lParam);

    public static IntPtr FindWindowForProcess(uint expectedProcessId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hwnd, state) => {
            uint processId;
            GetWindowThreadProcessId(hwnd, out processId);
            if (processId == expectedProcessId) { found = hwnd; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static IntPtr FindOtherVisibleWindowForProcess(uint expectedProcessId, IntPtr excluded) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hwnd, state) => {
            uint processId;
            GetWindowThreadProcessId(hwnd, out processId);
            if (processId == expectedProcessId && hwnd != excluded && IsWindowVisible(hwnd)) { found = hwnd; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@

    New-Item -ItemType Directory -Force -Path $llpOutputDirectory, $llpDataDirectory, $llpLocalAppData, $llpUserProfile, $llpLaunchpad | Out-Null
    $llpGpp = (where.exe g++ | Select-Object -First 1)
    if (-not $llpGpp) { throw 'g++ was not found.' }
    Push-Location $llpRepoRoot
    try {
        & $llpGpp 'tests\LightLaunchpad.NativeUi.Tests\LaunchProbeTarget.cpp' -std=c++17 -municode -mwindows -static -static-libgcc -static-libstdc++ -O2 -s -o $llpTarget
        if ($LASTEXITCODE -ne 0) { throw "Launch probe target compilation failed with exit code $LASTEXITCODE." }
    }
    finally {
        Pop-Location
    }
    Copy-Item -LiteralPath $llpTarget -Destination (Join-Path $llpLaunchpad 'Beta.exe')
    Copy-Item -LiteralPath $llpTarget -Destination (Join-Path $llpLaunchpad 'Flow Alpha.exe')
    Copy-Item -LiteralPath $llpTarget -Destination (Join-Path $llpLaunchpad 'Flow Beta.exe')

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
        ShowRecentAndFavorites = $false
        TrackLaunchHistory = $false
    } | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText((Join-Path $llpDataDirectory 'settings.json'), $llpSettings, [System.Text.UTF8Encoding]::new($false))

    & (Join-Path $llpRepoRoot 'tools\build-native-ui.ps1') -OutputDirectory $llpOutputDirectory -ProductVersion '1.11.0' -BuildCommit 'spotlightflowtest' -BuildChannel Development | Out-Null
    $llpExecutable = Join-Path $llpOutputDirectory 'LightLaunchpad.NativeUi.exe'

    $llpPriorAppData = $env:APPDATA
    $llpPriorLocalAppData = $env:LOCALAPPDATA
    $llpPriorUserProfile = $env:USERPROFILE
    $llpPriorMarker = $env:LLP_FLOW_MARKER
    try {
        $env:APPDATA = $llpAppData
        $env:LOCALAPPDATA = $llpLocalAppData
        $env:USERPROFILE = $llpUserProfile
        $env:LLP_FLOW_MARKER = $llpMarker
        $llpProcess = Start-Process -FilePath $llpExecutable -ArgumentList @('--spotlight-flow-probe-file', $llpProbePath, '--spotlight-flow-ready-file', $llpReadyPath, '--diagnostics-log-file', $llpLogPath) -PassThru
    }
    finally {
        $env:APPDATA = $llpPriorAppData
        $env:LOCALAPPDATA = $llpPriorLocalAppData
        $env:USERPROFILE = $llpPriorUserProfile
        $env:LLP_FLOW_MARKER = $llpPriorMarker
    }

    Wait-LlpCondition { Test-Path -LiteralPath $llpReadyPath } 12000 'The Spotlight probe catalog did not become ready.'
    $llpReady = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpReadyPath | ConvertFrom-Json
    Assert-Equal 3 ([int]$llpReady.itemCount) 'Spotlight probe catalog item count mismatch.'
    Assert-Equal 'Spotlight' ([string]$llpReady.displayMode) 'Spotlight probe did not load Spotlight mode.'
    $llpCatalogGeneration = [uint64]$llpReady.catalogGeneration
    $llpWindow = [LightLaunchpadSpotlightProbeNative]::FindWindowForProcess([uint32]$llpProcess.Id)
    Assert-True ($llpWindow -ne [IntPtr]::Zero) 'Could not discover the hidden real NativeUi window.'

    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0312, [UIntPtr]0x4C55, [IntPtr]::Zero)
    Assert-True ([LightLaunchpadSpotlightProbeNative]::IsWindowVisible($llpWindow)) 'First Alt+D path did not show Spotlight.'
    $llpElement = [System.Windows.Automation.AutomationElement]::FromHandle($llpWindow)
    $llpValueObject = $null
    Assert-True ($llpElement.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$llpValueObject)) 'Spotlight search Value pattern is unavailable.'
    $llpValue = [System.Windows.Automation.ValuePattern]$llpValueObject
    Assert-Equal $true $llpElement.Current.HasKeyboardFocus 'Search was not the only focus after Alt+D.'
    Send-LlpText $llpWindow 'Beta'
    Assert-Equal 'Beta' $llpValue.Current.Value 'First canonical path did not enter its query.'
    Send-LlpKey $llpWindow 0x0D
    Wait-LlpCondition { (Get-LlpMarkerNames).Count -ge 1 } 10000 'Search Enter did not launch the first exact result.'
    $llpMarkerNames = @(Get-LlpMarkerNames)
    Assert-Equal 'Beta.exe' $llpMarkerNames[0] 'Search Enter launched the wrong result.'
    Wait-LlpCondition { -not [LightLaunchpadSpotlightProbeNative]::IsWindowVisible($llpWindow) } 3000 'Spotlight did not hide after the first launch.'

    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0312, [UIntPtr]0x4C55, [IntPtr]::Zero)
    Assert-Equal '' $llpValue.Current.Value 'Second Alt+D did not reset the search query.'
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
        $llpEvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceDirectory)
        New-Item -ItemType Directory -Force -Path $llpEvidenceRoot | Out-Null
        Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot '00-empty-search.png')
    }
    Send-LlpText $llpWindow 'Flow'
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
        Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot '01-search-focus.png')
    }
    Send-LlpKey $llpWindow 0x28
    Assert-Equal $false $llpElement.Current.HasKeyboardFocus 'Down did not move focus from search into results.'
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
        Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot '02-result-focus.png')
    }
    Send-LlpKey $llpWindow 0x27
    Send-LlpKey $llpWindow 0x0D
    Wait-LlpCondition { (Get-LlpMarkerNames).Count -ge 2 } 10000 'Result navigation Enter did not launch.'
    $llpMarkerNames = @(Get-LlpMarkerNames)
    Assert-True ($llpMarkerNames[1] -in @('Flow Alpha.exe', 'Flow Beta.exe')) 'Down/Right/Enter launched a result outside the Flow query.'

    $llpLatestReady = Get-LlpReadyState
    $llpCatalogGeneration = [uint64]$llpLatestReady.catalogGeneration
    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0312, [UIntPtr]0x4C55, [IntPtr]::Zero)
    Wait-LlpCondition {
        $script:llpRefreshState = Get-LlpReadyState
        $null -ne $script:llpRefreshState -and [uint64]$script:llpRefreshState.catalogGeneration -gt $llpCatalogGeneration
    } 10000 'Catalog refresh associated with the focus-preservation open did not publish.'
    $llpCatalogGeneration = [uint64]$llpRefreshState.catalogGeneration
    Send-LlpText $llpWindow 'Flow'
    Send-LlpKey $llpWindow 0x28
    $llpAddedFlowPath = [System.IO.Path]::GetFullPath((Join-Path $llpLaunchpad 'Flow A.exe'))
    Copy-Item -LiteralPath $llpTarget -Destination $llpAddedFlowPath
    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0111, [UIntPtr]1002, [IntPtr]::Zero)
    $llpRefreshState = $null
    Wait-LlpCondition {
        $script:llpRefreshState = Get-LlpReadyState
        $null -ne $script:llpRefreshState -and [uint64]$script:llpRefreshState.catalogGeneration -gt $llpCatalogGeneration
    } 10000 'Catalog refresh after result insertion did not publish.'
    $llpCatalogGeneration = [uint64]$llpRefreshState.catalogGeneration
    Assert-Equal 'results' ([string]$llpRefreshState.focus) 'Result focus was not retained across insertion refresh.'
    Assert-Equal 'Flow Beta.exe' ([System.IO.Path]::GetFileName([string]$llpRefreshState.focusedResultPath)) 'Refresh did not preserve the focused stable result.'

    $llpFlowBetaPath = [System.IO.Path]::GetFullPath((Join-Path $llpLaunchpad 'Flow Beta.exe'))
    $llpRemovedFlowBetaPath = [System.IO.Path]::GetFullPath((Join-Path $llpTempRoot 'Flow Beta.removed'))
    $llpLaunchpadBoundary = [System.IO.Path]::GetFullPath($llpLaunchpad).TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    $llpTempBoundary = [System.IO.Path]::GetFullPath($llpTempRoot).TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    Assert-True ($llpFlowBetaPath.StartsWith($llpLaunchpadBoundary, [System.StringComparison]::OrdinalIgnoreCase)) 'Focused refresh fixture escaped the isolated Launchpad directory.'
    Assert-True ($llpRemovedFlowBetaPath.StartsWith($llpTempBoundary, [System.StringComparison]::OrdinalIgnoreCase)) 'Refresh quarantine path escaped the isolated test directory.'
    $llpFlowBetaItem = Get-Item -LiteralPath $llpFlowBetaPath -Force
    Assert-True (-not $llpFlowBetaItem.PSIsContainer) 'Focused refresh fixture was not a file.'
    Assert-True (($llpFlowBetaItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) 'Refusing to move a reparse-point refresh fixture.'
    Move-Item -LiteralPath $llpFlowBetaPath -Destination $llpRemovedFlowBetaPath
    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0111, [UIntPtr]1002, [IntPtr]::Zero)
    Wait-LlpCondition {
        $script:llpRefreshState = Get-LlpReadyState
        $null -ne $script:llpRefreshState -and [uint64]$script:llpRefreshState.catalogGeneration -gt $llpCatalogGeneration
    } 10000 'Catalog refresh after focused-result removal did not publish.'
    Assert-Equal 'results' ([string]$llpRefreshState.focus) 'Missing focused result unexpectedly changed focus ownership.'
    Assert-Equal '' ([string]$llpRefreshState.focusedResultPath) 'Missing stable result was silently substituted with another launch target.'
    Send-LlpKey $llpWindow 0x0D
    Start-Sleep -Milliseconds 300
    Assert-Equal 2 (@(Get-LlpMarkerNames).Count) 'Enter after focused-result removal launched an unintended replacement.'
    Send-LlpKey $llpWindow 0x1B
    Send-LlpKey $llpWindow 0x1B
    Send-LlpKey $llpWindow 0x1B

    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0312, [UIntPtr]0x4C55, [IntPtr]::Zero)
    Send-LlpText $llpWindow 'Flow'
    Send-LlpKey $llpWindow 0x28
    Send-LlpKey $llpWindow 0x1B
    Assert-Equal $true $llpElement.Current.HasKeyboardFocus 'Esc from results did not return focus to search.'
    Assert-Equal 'Flow' $llpValue.Current.Value 'Esc from results unexpectedly changed the query.'
    Send-LlpKey $llpWindow 0x1B
    Assert-Equal '' $llpValue.Current.Value 'Second Esc did not clear the query.'
    Assert-True ([LightLaunchpadSpotlightProbeNative]::IsWindowVisible($llpWindow)) 'Clearing the query closed Spotlight too early.'
    Send-LlpKey $llpWindow 0x1B
    Assert-True (-not [LightLaunchpadSpotlightProbeNative]::IsWindowVisible($llpWindow)) 'Third Esc did not close empty Spotlight.'

    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0312, [UIntPtr]0x4C55, [IntPtr]::Zero)
    Send-LlpText $llpWindow 'Flow'
    Send-LlpKey $llpWindow 0x28
    Send-LlpText $llpWindow 'X'
    Assert-Equal $true $llpElement.Current.HasKeyboardFocus 'Printable input from results did not return to search.'
    Assert-Equal 'FlowX' $llpValue.Current.Value 'Printable input from results was lost.'
    Send-LlpKey $llpWindow 0x1B
    Send-LlpKey $llpWindow 0x1B

    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0312, [UIntPtr]0x4C55, [IntPtr]::Zero)
    Send-LlpText $llpWindow 'Flow'
    Send-LlpKey $llpWindow 0x09
    Assert-Equal $false $llpElement.Current.HasKeyboardFocus 'Tab did not move from search to results.'
    Send-LlpKey $llpWindow 0x09
    Assert-Equal $false $llpElement.Current.HasKeyboardFocus 'Tab did not retain non-search focus on the auxiliary control.'
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
        Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot '03-auxiliary-focus.png')
    }
    Send-LlpKey $llpWindow 0x09
    Assert-Equal $true $llpElement.Current.HasKeyboardFocus 'Tab did not wrap from auxiliary control to search.'
    Send-LlpKey $llpWindow 0x1B
    Send-LlpKey $llpWindow 0x1B

    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0312, [UIntPtr]0x4C55, [IntPtr]::Zero)
    Send-LlpText $llpWindow 'Flow'
    Send-LlpKey $llpWindow 0x09
    Send-LlpKey $llpWindow 0x09
    Send-LlpKey $llpWindow 0x0D
    Assert-True (-not [LightLaunchpadSpotlightProbeNative]::IsWindowVisible($llpWindow)) 'Enter on the auxiliary control did not hide Spotlight.'
    $llpSettingsWindow = [IntPtr]::Zero
    Wait-LlpCondition { $script:llpSettingsWindow = [LightLaunchpadSpotlightProbeNative]::FindOtherVisibleWindowForProcess([uint32]$llpProcess.Id, $llpWindow); $script:llpSettingsWindow -ne [IntPtr]::Zero } 5000 'Enter on the auxiliary control did not open Settings.'
    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpSettingsWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
    Wait-LlpCondition { -not [LightLaunchpadSpotlightProbeNative]::IsWindow($llpSettingsWindow) } 3000 'The auxiliary Settings window did not close.'

    Wait-LlpCondition { (Get-LlpMarkerNames).Count -eq 2 } 3000 'Unexpected extra launch occurred during focus-state tests.'
    Start-Sleep -Milliseconds 200
    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
    if (-not $llpProcess.WaitForExit(10000)) { throw 'Spotlight flow probe did not exit.' }
    Assert-Equal 0 $llpProcess.ExitCode 'Spotlight flow probe exit code mismatch.'
    $llpProbe = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpProbePath | ConvertFrom-Json
    Assert-Equal 'Alt+D' ([string]$llpProbe.configuredHotkey) 'Canonical hotkey setting changed.'
    Assert-Equal 7 ([int]$llpProbe.hotkeyMessages) 'Not all simulated Alt+D messages opened Spotlight.'
    Assert-Equal 7 ([int]$llpProbe.searchFocusedOnOpen) 'Spotlight did not open with search as the sole focus every time.'
    Assert-True (@($llpProbe.selectedIndexOnOpen | Where-Object { [int]$_ -ne -1 }).Count -eq 0) 'Spotlight painted a result as selected on open.'
    Assert-Equal 1 ([int]$llpProbe.searchEnterLaunches) 'Search Enter launch count mismatch.'
    Assert-Equal 1 ([int]$llpProbe.resultEnterLaunches) 'Focused-result Enter launch count mismatch.'
    Assert-Equal 2 ([int]$llpProbe.successfulLaunches) 'Shell broker did not report both launches successful.'
    $llpFocusedNames = @($llpProbe.focusedResultPaths | ForEach-Object { [System.IO.Path]::GetFileName([string]$_) })
    Assert-True ($llpFocusedNames.Count -ge 2) 'The real EXE did not report both Down and Right result-focus states.'
    Assert-Equal 'Flow Beta.exe' $llpFocusedNames[0] 'Down did not focus the first ranked Flow result.'
    Assert-Equal 'Flow Alpha.exe' $llpFocusedNames[1] 'Right did not advance to the second ranked Flow result.'
    Assert-Equal 'Flow Alpha.exe' $llpMarkerNames[1] 'Enter did not launch the result focused by Right.'
    $llpReadySamples = @($llpProbe.inputReadyMs | ForEach-Object { [int64]$_ } | Sort-Object)
    $llpP95 = $llpReadySamples[[Math]::Min($llpReadySamples.Count - 1, [Math]::Floor($llpReadySamples.Count * 0.95))]
    Write-Output "METRIC SpotlightFlow inputReadySamplesMs=$($llpReadySamples -join ',')"
    Assert-True ($llpP95 -le 180) "Hotkey-to-input-ready P95 exceeded 180 ms: $llpP95 ms."
    Write-Output "METRIC SpotlightFlow hotkeyToInputReadyP95Ms=$llpP95 launches=$([int]$llpProbe.successfulLaunches) opens=$([int]$llpProbe.hotkeyMessages)"
    Write-Output 'PASS SpotlightFlow_AltDSearchEnterAndResultNavigationLaunchExactTargets'
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
        if (($llpTempItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Refusing to clean reparse-point test output $llpResolvedTemp." }
        Remove-Item -LiteralPath $llpResolvedTemp -Recurse -Force
    }
}
