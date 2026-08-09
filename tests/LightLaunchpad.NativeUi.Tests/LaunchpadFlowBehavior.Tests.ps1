param(
    [string]$EvidenceDirectory = ''
)

$ErrorActionPreference = 'Stop'

$llpRepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$llpTempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("lightlaunchpad-launchpad-flow-" + [System.Guid]::NewGuid().ToString('N'))
$llpOutputDirectory = Join-Path $llpTempRoot 'development'
$llpAppData = Join-Path $llpTempRoot 'appdata'
$llpLocalAppData = Join-Path $llpTempRoot 'localappdata'
$llpUserProfile = Join-Path $llpTempRoot 'profile'
$llpDataDirectory = Join-Path $llpAppData 'LightLaunchpad'
$llpLaunchpad = Join-Path $llpTempRoot 'Launchpad'
$llpMarker = Join-Path $llpTempRoot 'launched.txt'
$llpProbePath = Join-Path $llpTempRoot 'launchpad-flow.json'
$llpReadyPath = Join-Path $llpTempRoot 'launchpad-ready.json'
$llpLogPath = Join-Path $llpTempRoot 'launchpad-flow.log'
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

function New-LlpMouseLParam {
    param([int]$X, [int]$Y)
    $llpPacked = (($Y -band 0xffff) -shl 16) -bor ($X -band 0xffff)
    return [IntPtr]$llpPacked
}

function Send-LlpMouse {
    param([IntPtr]$Window, [uint32]$Message, [int]$X, [int]$Y, [uint32]$Keys = 0)
    [void][LightLaunchpadLaunchpadProbeNative]::SendMessage($Window, $Message, [UIntPtr]::new([uint64]$Keys), (New-LlpMouseLParam $X $Y))
}

function Send-LlpClick {
    param([IntPtr]$Window, [int]$X, [int]$Y, [bool]$Control = $false)
    $llpDownKeys = if ($Control) { 0x0009 } else { 0x0001 }
    $llpUpKeys = if ($Control) { 0x0008 } else { 0 }
    Send-LlpMouse $Window 0x0201 $X $Y $llpDownKeys
    Send-LlpMouse $Window 0x0202 $X $Y $llpUpKeys
}

function Send-LlpKey {
    param([IntPtr]$Window, [UInt16]$Key)
    [void][LightLaunchpadLaunchpadProbeNative]::SendMessage($Window, 0x0100, [UIntPtr]::new([uint64]$Key), [IntPtr]::Zero)
}

function Invoke-LlpContextCommand {
    param([IntPtr]$Window, [int]$X, [int]$Y, [uint32]$Command)
    Assert-True ([LightLaunchpadLaunchpadProbeNative]::PostMessage($Window, 0x0205, [UIntPtr]::Zero, (New-LlpMouseLParam $X $Y))) 'Could not open the item context menu.'
    Start-Sleep -Milliseconds 180
    Assert-True ([LightLaunchpadLaunchpadProbeNative]::PostMessage($Window, 0x0100, [UIntPtr]::new([uint64]0x1B), [IntPtr]::Zero)) 'Could not dismiss the item context menu.'
    Start-Sleep -Milliseconds 100
    Assert-True ([LightLaunchpadLaunchpadProbeNative]::PostMessage($Window, 0x0111, [UIntPtr]::new([uint64]$Command), [IntPtr]::Zero)) 'Could not invoke the context command.'
}

function Get-LlpMarkerNames {
    if (-not (Test-Path -LiteralPath $llpMarker)) { return @() }
    return @((Get-Content -Encoding UTF8 -LiteralPath $llpMarker | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }) | ForEach-Object { [System.IO.Path]::GetFileName($_) })
}

function Save-LlpWindowEvidence {
    param([IntPtr]$Window, [string]$Path)
    [void][LightLaunchpadLaunchpadProbeNative]::UpdateWindow($Window)
    $llpRect = [LightLaunchpadLaunchpadProbeNative+RECT]::new()
    Assert-True ([LightLaunchpadLaunchpadProbeNative]::GetWindowRect($Window, [ref]$llpRect)) 'Could not read the Launchpad evidence window geometry.'
    $llpWidth = $llpRect.Right - $llpRect.Left
    $llpHeight = $llpRect.Bottom - $llpRect.Top
    Assert-True ($llpWidth -gt 0 -and $llpHeight -gt 0) 'Launchpad evidence window geometry was empty.'
    $llpBitmap = [System.Drawing.Bitmap]::new($llpWidth, $llpHeight, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $llpGraphics = [System.Drawing.Graphics]::FromImage($llpBitmap)
    $llpDc = $llpGraphics.GetHdc()
    try {
        Assert-True ([LightLaunchpadLaunchpadProbeNative]::PrintWindow($Window, $llpDc, 2)) 'PrintWindow could not capture the rendered Launchpad window.'
    }
    finally {
        $llpGraphics.ReleaseHdc($llpDc)
        $llpGraphics.Dispose()
    }
    try { $llpBitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png) }
    finally { $llpBitmap.Dispose() }
}

try {
    Add-Type -AssemblyName System.Drawing
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class LightLaunchpadLaunchpadProbeNative {
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
    public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")]
    public static extern bool UpdateWindow(IntPtr hwnd);
    [DllImport("user32.dll")]
    public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessage(IntPtr hwnd, uint message, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern bool PostMessage(IntPtr hwnd, uint message, UIntPtr wParam, IntPtr lParam);

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
}
'@

    Assert-True ([LightLaunchpadLaunchpadProbeNative]::SetProcessDpiAwarenessContext([IntPtr](-4))) 'Could not enable Per-Monitor-V2 coordinates for the Launchpad flow probe.'

    New-Item -ItemType Directory -Force -Path $llpOutputDirectory, $llpDataDirectory, $llpLocalAppData, $llpUserProfile, $llpLaunchpad | Out-Null
    $llpGpp = (where.exe g++ | Select-Object -First 1)
    if (-not $llpGpp) { throw 'g++ was not found.' }
    Push-Location $llpRepoRoot
    try {
        & $llpGpp 'tests\LightLaunchpad.NativeUi.Tests\LaunchProbeTarget.cpp' -std=c++17 -municode -mwindows -static -static-libgcc -static-libstdc++ -O2 -s -o $llpTarget
        if ($LASTEXITCODE -ne 0) { throw "Launch probe target compilation failed with exit code $LASTEXITCODE." }
    }
    finally { Pop-Location }
    Copy-Item -LiteralPath $llpTarget -Destination (Join-Path $llpLaunchpad 'Flow Alpha.exe')
    Copy-Item -LiteralPath $llpTarget -Destination (Join-Path $llpLaunchpad 'Flow Beta.exe')
    Copy-Item -LiteralPath $llpTarget -Destination (Join-Path $llpLaunchpad 'Flow Gamma.exe')

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
        ShowRecentAndFavorites = $false
        TrackLaunchHistory = $false
    } | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText((Join-Path $llpDataDirectory 'settings.json'), $llpSettings, [System.Text.UTF8Encoding]::new($false))

    & (Join-Path $llpRepoRoot 'tools\build-native-ui.ps1') -OutputDirectory $llpOutputDirectory -ProductVersion '1.11.0' -BuildCommit 'launchpadflowtest' -BuildChannel Development | Out-Null
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
        $llpProcess = Start-Process -FilePath $llpExecutable -ArgumentList @('--launchpad-flow-probe-file', $llpProbePath, '--launchpad-flow-ready-file', $llpReadyPath, '--diagnostics-log-file', $llpLogPath) -PassThru
    }
    finally {
        $env:APPDATA = $llpPriorAppData
        $env:LOCALAPPDATA = $llpPriorLocalAppData
        $env:USERPROFILE = $llpPriorUserProfile
        $env:LLP_FLOW_MARKER = $llpPriorMarker
    }

    Wait-LlpCondition { Test-Path -LiteralPath $llpReadyPath } 12000 'The Launchpad probe catalog did not become ready.'
    $llpReady = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpReadyPath | ConvertFrom-Json
    Assert-Equal 3 ([int]$llpReady.itemCount) 'Launchpad probe catalog item count mismatch.'
    Assert-Equal 'Launchpad' ([string]$llpReady.displayMode) 'Launchpad probe did not load Launchpad mode.'
    $llpWindow = [LightLaunchpadLaunchpadProbeNative]::FindWindowForProcess([uint32]$llpProcess.Id)
    Assert-True ($llpWindow -ne [IntPtr]::Zero) 'Could not discover the hidden real NativeUi window.'

    [void][LightLaunchpadLaunchpadProbeNative]::SendMessage($llpWindow, 0x0312, [UIntPtr]::new([uint64]0x4C55), [IntPtr]::Zero)
    Assert-True ([LightLaunchpadLaunchpadProbeNative]::IsWindowVisible($llpWindow)) 'First Alt+D path did not show Launchpad.'
    Wait-LlpCondition {
        try {
            $script:llpReady = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpReadyPath | ConvertFrom-Json
            return @($script:llpReady.hitBounds).Count -eq 3
        }
        catch { return $false }
    } 5000 'Launchpad did not publish the shared rendered hit geometry.'
    $llpReady = $script:llpReady
    $llpFirstX = ([int]$llpReady.hitBounds[0].left + [int]$llpReady.hitBounds[0].right) / 2
    $llpSecondX = ([int]$llpReady.hitBounds[1].left + [int]$llpReady.hitBounds[1].right) / 2
    $llpThirdX = ([int]$llpReady.hitBounds[2].left + [int]$llpReady.hitBounds[2].right) / 2
    $llpTileY = ([int]$llpReady.hitBounds[0].top + [int]$llpReady.hitBounds[0].bottom) / 2
    $llpEditX = ([int]$llpReady.editBounds.left + [int]$llpReady.editBounds.right) / 2
    $llpEditY = ([int]$llpReady.editBounds.top + [int]$llpReady.editBounds.bottom) / 2
    $llpUndoX = ([int]$llpReady.undoBounds.left + [int]$llpReady.undoBounds.right) / 2
    $llpUndoY = ([int]$llpReady.undoBounds.top + [int]$llpReady.undoBounds.bottom) / 2
    $llpRegionX = ([int]$llpReady.regionBounds[0].left + [int]$llpReady.regionBounds[0].right) / 2
    $llpRegionY = ([int]$llpReady.regionBounds[0].top + [int]$llpReady.regionBounds[0].bottom) / 2
    $llpMarqueeX = [Math]::Max([int]$llpReady.contentClip.left + 1, [int]$llpReady.hitBounds[0].left - 20)
    $llpMarqueeY = [int]$llpReady.hitBounds[0].top + 4
    Start-Sleep -Milliseconds 250
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
        $llpEvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceDirectory)
        New-Item -ItemType Directory -Force -Path $llpEvidenceRoot | Out-Null
        Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot '00-browse-normal.png')
    }

    Send-LlpMouse $llpWindow 0x0200 $llpFirstX $llpTileY 0
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) { Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot '01-browse-hover.png') }
    Send-LlpMouse $llpWindow 0x0201 $llpFirstX $llpTileY 1
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) { Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot '02-browse-pressed.png') }
    Assert-Equal 0 (@(Get-LlpMarkerNames).Count) 'Browse press launched before release.'
    Send-LlpMouse $llpWindow 0x0202 $llpFirstX $llpTileY 0
    try {
        Wait-LlpCondition { (Get-LlpMarkerNames).Count -eq 1 } 10000 'Browse click did not launch exactly one item.'
    }
    catch {
        $llpDebugReady = if (Test-Path -LiteralPath $llpReadyPath) { Get-Content -Raw -Encoding UTF8 -LiteralPath $llpReadyPath } else { '<missing ready state>' }
        throw "$($_.Exception.Message) Ready state: $llpDebugReady"
    }
    Wait-LlpCondition { -not [LightLaunchpadLaunchpadProbeNative]::IsWindowVisible($llpWindow) } 3000 'Launchpad did not hide after browse launch.'

    [void][LightLaunchpadLaunchpadProbeNative]::SendMessage($llpWindow, 0x0312, [UIntPtr]::new([uint64]0x4C55), [IntPtr]::Zero)
    Send-LlpKey $llpWindow 0x28
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) { Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot '03-browse-keyboard-focus.png') }
    Send-LlpKey $llpWindow 0x26
    Send-LlpMouse $llpWindow 0x0201 $llpFirstX $llpTileY 1
    Send-LlpMouse $llpWindow 0x0200 ($llpFirstX + 320) ($llpTileY + 240) 1
    Send-LlpMouse $llpWindow 0x0202 ($llpFirstX + 320) ($llpTileY + 240) 0
    Start-Sleep -Milliseconds 150
    Assert-Equal 1 (@(Get-LlpMarkerNames).Count) 'Browse drag gesture launched or edited an item.'

    Send-LlpClick $llpWindow $llpEditX $llpEditY
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) { Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot '04-edit-empty-selection.png') }
    Send-LlpClick $llpWindow $llpFirstX $llpTileY
    Assert-Equal 1 (@(Get-LlpMarkerNames).Count) 'Edit selection click launched an item.'
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) { Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot '05-edit-selected.png') }
    Send-LlpClick $llpWindow $llpSecondX $llpTileY $true
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) { Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot '06-edit-multiselected.png') }

    Invoke-LlpContextCommand $llpWindow $llpFirstX $llpTileY 2005
    $llpSearchHistoryPath = Join-Path $llpDataDirectory 'search-history.json'
    Wait-LlpCondition {
        if (-not (Test-Path -LiteralPath $llpSearchHistoryPath)) { return $false }
        try { return @((Get-Content -Raw -Encoding UTF8 -LiteralPath $llpSearchHistoryPath | ConvertFrom-Json).Items | Where-Object { $_.Favorite }).Count -eq 2 }
        catch { return $false }
    } 5000 'Favorite command did not persist both selected items.'
    Start-Sleep -Milliseconds 150
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) { Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot '08-edit-favorites-saved.png') }

    Assert-True ([LightLaunchpadLaunchpadProbeNative]::PostMessage($llpWindow, 0x0111, [UIntPtr]::new([uint64]2003), [IntPtr]::Zero)) 'Could not invoke batch delete.'
    $llpFirstPath = Join-Path $llpLaunchpad 'Flow Alpha.exe'
    $llpSecondPath = Join-Path $llpLaunchpad 'Flow Beta.exe'
    Wait-LlpCondition { -not (Test-Path -LiteralPath $llpFirstPath) -and -not (Test-Path -LiteralPath $llpSecondPath) } 8000 'Batch delete did not move both selected files to the Recycle Bin.'
    Start-Sleep -Milliseconds 250
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) { Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot '09-edit-recycle-undo.png') }

    Send-LlpClick $llpWindow $llpUndoX $llpUndoY
    Wait-LlpCondition { (Test-Path -LiteralPath $llpFirstPath) -and (Test-Path -LiteralPath $llpSecondPath) } 8000 'Undo did not restore both recycled files.'
    Wait-LlpCondition {
        try { return [int](Get-Content -Raw -Encoding UTF8 -LiteralPath $llpReadyPath | ConvertFrom-Json).itemCount -eq 3 }
        catch { return $false }
    } 8000 'Catalog did not republish all restored items.'
    $llpRestoredReady = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpReadyPath | ConvertFrom-Json
    $llpRestoredNames = @($llpRestoredReady.itemPaths | ForEach-Object { [System.IO.Path]::GetFileName([string]$_) })
    Assert-Equal 'Flow Alpha.exe,Flow Beta.exe,Flow Gamma.exe' ($llpRestoredNames -join ',') 'Undo did not restore the exact pre-delete layout order.'
    Start-Sleep -Milliseconds 200
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) { Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot '10-edit-undo-restored.png') }

    Send-LlpKey $llpWindow 0x0D
    Start-Sleep -Milliseconds 150
    Assert-Equal 1 (@(Get-LlpMarkerNames).Count) 'Enter in Edit mode launched an item.'

    Send-LlpMouse $llpWindow 0x0201 $llpMarqueeX $llpMarqueeY 1
    Send-LlpMouse $llpWindow 0x0200 $llpThirdX $llpTileY 1
    Send-LlpMouse $llpWindow 0x0202 $llpThirdX $llpTileY 0
    Send-LlpMouse $llpWindow 0x0201 $llpFirstX $llpTileY 1
    Send-LlpMouse $llpWindow 0x0200 $llpThirdX $llpTileY 1
    Start-Sleep -Milliseconds 250
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) { Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot '07-edit-drag-drop-target.png') }
    Send-LlpMouse $llpWindow 0x0202 $llpThirdX $llpTileY 0
    Send-LlpMouse $llpWindow 0x0201 $llpRegionX $llpRegionY 1
    Send-LlpMouse $llpWindow 0x0200 $llpRegionX ($llpRegionY + 48) 1
    Send-LlpMouse $llpWindow 0x0202 $llpRegionX ($llpRegionY + 48) 0
    Assert-Equal 1 (@(Get-LlpMarkerNames).Count) 'Edit organization gestures launched an item.'

    Send-LlpClick $llpWindow $llpEditX $llpEditY
    Send-LlpClick $llpWindow $llpSecondX $llpTileY
    Wait-LlpCondition { (Get-LlpMarkerNames).Count -eq 2 } 10000 'Browse mode did not resume single-click launch after Done.'
    $llpMarkerNames = @(Get-LlpMarkerNames)
    Assert-Equal 2 $llpMarkerNames.Count 'Browse clicks did not launch exactly two tile targets.'

    [void][LightLaunchpadLaunchpadProbeNative]::SendMessage($llpWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
    if (-not $llpProcess.WaitForExit(10000)) { throw 'Launchpad flow probe did not exit.' }
    Assert-Equal 0 $llpProcess.ExitCode 'Launchpad flow probe exit code mismatch.'
    $llpProbe = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpProbePath | ConvertFrom-Json
    Assert-Equal 'Alt+D' ([string]$llpProbe.configuredHotkey) 'Canonical hotkey setting changed.'
    Assert-Equal 2 ([int]$llpProbe.hotkeyMessages) 'Not all simulated Alt+D messages opened Launchpad.'
    Assert-Equal 2 ([int]$llpProbe.modeTransitions) 'Edit/Done did not produce the exact mode transition pair.'
    Assert-Equal 3 ([int]$llpProbe.browsePresses) 'Browse press accounting mismatch.'
    Assert-Equal 2 ([int]$llpProbe.browseClickLaunches) 'Browse single-click launch accounting mismatch.'
    Assert-Equal 3 ([int]$llpProbe.editSelectionClicks) 'Edit item-press accounting mismatch.'
    Assert-True ([int]$llpProbe.maxSelectedItems -ge 2) 'Edit mode did not retain multi-selection.'
    Assert-Equal 1 ([int]$llpProbe.marqueeBegins) 'Edit marquee did not begin exactly once.'
    Assert-Equal 1 ([int]$llpProbe.itemDragBegins) 'Edit item drag did not begin exactly once.'
    Assert-Equal 1 ([int]$llpProbe.regionDragBegins) 'Edit region drag did not begin exactly once.'
    Assert-Equal 1 ([int]$llpProbe.blockedBrowseDrags) 'Browse drag was not explicitly rejected.'
    Assert-Equal 2 ([int]$llpProbe.successfulLaunches) 'Shell broker did not report both browse launches successful.'
    Assert-Equal 2 @($llpProbe.requestedPaths).Count 'Launchpad did not preserve the exact two clicked source paths.'
    Assert-Equal 'browse' ([string]$llpProbe.finalMode) 'Launchpad did not finish in Browse mode.'
    Assert-Equal 0 ([int]$llpProbe.finalSelectedItems) 'Browse mode retained persistent edit selection.'

    $llpRestartProbePath = Join-Path $llpTempRoot 'launchpad-restart-flow.json'
    $llpRestartReadyPath = Join-Path $llpTempRoot 'launchpad-restart-ready.json'
    $llpPriorAppData = $env:APPDATA
    $llpPriorLocalAppData = $env:LOCALAPPDATA
    $llpPriorUserProfile = $env:USERPROFILE
    try {
        $env:APPDATA = $llpAppData
        $env:LOCALAPPDATA = $llpLocalAppData
        $env:USERPROFILE = $llpUserProfile
        $llpProcess = Start-Process -FilePath $llpExecutable -ArgumentList @('--launchpad-flow-probe-file', $llpRestartProbePath, '--launchpad-flow-ready-file', $llpRestartReadyPath, '--diagnostics-log-file', $llpLogPath) -PassThru
    }
    finally {
        $env:APPDATA = $llpPriorAppData
        $env:LOCALAPPDATA = $llpPriorLocalAppData
        $env:USERPROFILE = $llpPriorUserProfile
    }
    Wait-LlpCondition { Test-Path -LiteralPath $llpRestartReadyPath } 10000 'Restart catalog did not become ready.'
    $llpRestartWindow = [LightLaunchpadLaunchpadProbeNative]::FindWindowForProcess([uint32]$llpProcess.Id)
    Assert-True ($llpRestartWindow -ne [IntPtr]::Zero) 'Could not discover the restarted NativeUi window.'
    [void][LightLaunchpadLaunchpadProbeNative]::SendMessage($llpRestartWindow, 0x0312, [UIntPtr]::new([uint64]0x4C55), [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) { Save-LlpWindowEvidence $llpRestartWindow (Join-Path $llpEvidenceRoot '11-restart-favorites-persisted.png') }
    [void][LightLaunchpadLaunchpadProbeNative]::SendMessage($llpRestartWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
    if (-not $llpProcess.WaitForExit(10000)) { throw 'Restart favorite probe did not exit.' }
    Assert-Equal 0 $llpProcess.ExitCode 'Restart favorite probe exit code mismatch.'
    Write-Output "METRIC LaunchpadFlow launches=$([int]$llpProbe.successfulLaunches) editSelections=$([int]$llpProbe.editSelectionClicks) maxSelected=$([int]$llpProbe.maxSelectedItems)"
    Write-Output 'METRIC FavoritePersistence selected=2 persisted=2 restartCatalog=3'
    Write-Output 'PASS LaunchpadFlow_BrowseLaunchesAndEditOrganizesWithoutAccidentalLaunch'
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
