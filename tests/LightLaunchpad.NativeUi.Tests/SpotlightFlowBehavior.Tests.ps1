param(
    [string]$EvidenceDirectory = '',

    [ValidateSet('None', 'initialization')]
    [string]$MaterialFailure = 'None'
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
$llpTestMonitorDevice = '\\.\DISPLAY1'

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

function Test-LlpFileReadyForLaunch {
    param([string]$Path)
    try {
        $llpStream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::None)
        $llpStream.Dispose()
        return $true
    }
    catch {
        return $false
    }
}

function Write-LlpNavigationDiagnostic {
    param([string]$Stage)
    $llpNavigationState = Get-LlpReadyState
    Write-Output "METRIC SpotlightNavigation stage=$Stage focus=$([string]$llpNavigationState.focus) uiaSearchFocused=$([bool]$llpElement.Current.HasKeyboardFocus) foregroundHwnd=$([uint64]$llpNavigationState.foregroundWindow) foregroundPid=$([uint32]$llpNavigationState.foregroundProcessId) composing=$([bool]$llpNavigationState.searchComposing) searchValue='$([string]$llpNavigationState.searchValue)' visible=$([bool]$llpNavigationState.windowVisible) sceneGeneration=$([uint64]$llpNavigationState.materialSceneGeneration)"
}

function Send-LlpText {
    param([IntPtr]$Window, [string]$Text)
    # Direct WM_CHAR injection represents committed text, so cancel any ambient
    # desktop IME composition that began when the real window took focus.
    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($Window, 0x010E, [UIntPtr]::Zero, [IntPtr]::Zero)
    foreach ($llpCharacter in $Text.ToCharArray()) {
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($Window, 0x0102, [UIntPtr]::new([uint64]$llpCharacter), [IntPtr]::Zero)
    }
}

function Send-LlpKey {
    param([IntPtr]$Window, [UInt16]$Key)
    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($Window, 0x0100, [UIntPtr]::new([uint64]$Key), [IntPtr]::Zero)
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

function Save-LlpScreenEvidence {
    param([IntPtr]$Window, [string]$Path, [object]$MarkerBounds)
    [void][LightLaunchpadSpotlightProbeNative]::DwmFlush()
    $llpRect = [LightLaunchpadSpotlightProbeNative+RECT]::new()
    Assert-True ([LightLaunchpadSpotlightProbeNative]::GetWindowRect($Window, [ref]$llpRect)) 'Could not read the Spotlight screen-capture geometry.'
    $llpOriginalCursor = [LightLaunchpadSpotlightProbeNative+POINT]::new()
    Assert-True ([LightLaunchpadSpotlightProbeNative]::GetCursorPos([ref]$llpOriginalCursor)) 'Could not read the cursor before compositor evidence.'
    $llpEvidenceCursorX = [int]$script:llpMonitorReady.monitorBounds.left + 16
    $llpEvidenceCursorY = [int]$script:llpMonitorReady.monitorBounds.bottom - 16
    Assert-True (
        $llpEvidenceCursorX -ge [int]$script:llpMonitorReady.monitorBounds.left -and
        $llpEvidenceCursorX -lt [int]$script:llpMonitorReady.monitorBounds.right -and
        $llpEvidenceCursorY -ge [int]$script:llpMonitorReady.monitorBounds.top -and
        $llpEvidenceCursorY -lt [int]$script:llpMonitorReady.monitorBounds.bottom -and
        -not ($llpEvidenceCursorX -ge $llpRect.Left -and $llpEvidenceCursorX -lt $llpRect.Right -and $llpEvidenceCursorY -ge $llpRect.Top -and $llpEvidenceCursorY -lt $llpRect.Bottom)
    ) 'The deterministic compositor-evidence cursor point was not on DISPLAY1 outside the Spotlight window.'
    Assert-True ([LightLaunchpadSpotlightProbeNative]::SetCursorPos($llpEvidenceCursorX, $llpEvidenceCursorY)) 'Could not move the cursor outside Spotlight for deterministic compositor evidence.'
    try {
        $llpEvidenceCursor = [LightLaunchpadSpotlightProbeNative+POINT]::new()
        Assert-True ([LightLaunchpadSpotlightProbeNative]::GetCursorPos([ref]$llpEvidenceCursor)) 'Could not verify the cursor position for compositor evidence.'
        Assert-Equal $llpEvidenceCursorX $llpEvidenceCursor.X 'Compositor-evidence cursor X escaped the requested DISPLAY1 point.'
        Assert-Equal $llpEvidenceCursorY $llpEvidenceCursor.Y 'Compositor-evidence cursor Y escaped the requested DISPLAY1 point.'
        $llpWidth = $llpRect.Right - $llpRect.Left
        $llpHeight = $llpRect.Bottom - $llpRect.Top
        Assert-True ($llpWidth -gt 0 -and $llpHeight -gt 0) 'Spotlight screen-capture geometry was empty.'
        $llpBitmap = [System.Drawing.Bitmap]::new($llpWidth, $llpHeight, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $llpGraphics = [System.Drawing.Graphics]::FromImage($llpBitmap)
        $llpDestination = $llpGraphics.GetHdc()
        $llpScreen = [LightLaunchpadSpotlightProbeNative]::GetDC([IntPtr]::Zero)
        try {
            Assert-True ($llpScreen -ne [IntPtr]::Zero) 'Could not acquire the real desktop DC for Spotlight evidence.'
            Assert-True ([LightLaunchpadSpotlightProbeNative]::BitBlt(
                $llpDestination, 0, 0, $llpWidth, $llpHeight,
                $llpScreen, $llpRect.Left, $llpRect.Top, 0x40CC0020)) 'BitBlt could not capture the real composed desktop pixels.'
        }
        finally {
            if ($llpScreen -ne [IntPtr]::Zero) { [void][LightLaunchpadSpotlightProbeNative]::ReleaseDC([IntPtr]::Zero, $llpScreen) }
            $llpGraphics.ReleaseHdc($llpDestination)
            $llpGraphics.Dispose()
        }
        try {
            $llpBitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
            $llpMarkerPixels = 0
            for ($llpY = [int]$MarkerBounds.top; $llpY -lt [int]$MarkerBounds.bottom; $llpY++) {
                for ($llpX = [int]$MarkerBounds.left; $llpX -lt [int]$MarkerBounds.right; $llpX++) {
                    $llpPixel = $llpBitmap.GetPixel($llpX, $llpY)
                    if ($llpPixel.R -ge 248 -and $llpPixel.G -le 7 -and $llpPixel.B -ge 248) { $llpMarkerPixels++ }
                }
            }
            return $llpMarkerPixels
        }
        finally {
            $llpBitmap.Dispose()
        }
    }
    finally {
        [void][LightLaunchpadSpotlightProbeNative]::SetCursorPos($llpOriginalCursor.X, $llpOriginalCursor.Y)
    }
}

try {
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    Add-Type -AssemblyName System.Drawing
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;
public static class LightLaunchpadSpotlightProbeNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X, Y; }
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
    [DllImport("user32.dll")]
    public static extern bool GetCursorPos(out POINT point);
    [DllImport("user32.dll")]
    public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")]
    public static extern IntPtr GetDC(IntPtr hwnd);
    [DllImport("user32.dll")]
    public static extern int ReleaseDC(IntPtr hwnd, IntPtr hdc);
    [DllImport("gdi32.dll")]
    public static extern bool BitBlt(IntPtr destination, int x, int y, int width, int height, IntPtr source, int sourceX, int sourceY, uint operation);
    [DllImport("dwmapi.dll")]
    public static extern int DwmFlush();
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
    public static string StableItemId(string sourcePath) {
        string identity = Path.GetFullPath(sourcePath).Replace('/', '\\');
        while (identity.Length > 3 && identity.EndsWith("\\", StringComparison.Ordinal)) {
            identity = identity.Substring(0, identity.Length - 1);
        }
        identity = identity.ToLowerInvariant();
        ulong hash = 1469598103934665603UL;
        unchecked {
            foreach (char ch in identity) {
                hash ^= (byte)(ch & 0xFF);
                hash *= 1099511628211UL;
                hash ^= (byte)((ch >> 8) & 0xFF);
                hash *= 1099511628211UL;
            }
        }
        return "item-" + hash.ToString("x16");
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
    $llpRefreshSeedPath = [System.IO.Path]::GetFullPath((Join-Path $llpLaunchpad 'Refresh Seed.exe'))
    $llpRefreshAddedPath = [System.IO.Path]::GetFullPath((Join-Path $llpLaunchpad 'Refresh Added.exe'))
    $llpRefreshRenamedPath = [System.IO.Path]::GetFullPath((Join-Path $llpLaunchpad 'Refresh Renamed.exe'))
    Copy-Item -LiteralPath $llpTarget -Destination $llpRefreshSeedPath
    foreach ($llpIndex in 1..48) {
        Copy-Item -LiteralPath $llpTarget -Destination (Join-Path $llpLaunchpad ("Scroll Item {0:D2}.exe" -f $llpIndex))
    }

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
        ShowRecentAndFavorites = $true
        TrackLaunchHistory = $false
    } | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText((Join-Path $llpDataDirectory 'settings.json'), $llpSettings, [System.Text.UTF8Encoding]::new($false))
    $llpSearchHistory = [ordered]@{
        SchemaVersion = 1
        Items = @(
            [ordered]@{ StableId = [LightLaunchpadSpotlightProbeNative]::StableItemId($llpRefreshSeedPath); LaunchCount = [uint64]1; LastLaunchedUnixMilliseconds = [uint64]1; Favorite = $true },
            [ordered]@{ StableId = [LightLaunchpadSpotlightProbeNative]::StableItemId((Join-Path $llpLaunchpad 'Beta.exe')); LaunchCount = [uint64]0; LastLaunchedUnixMilliseconds = [uint64]0; Favorite = $true },
            [ordered]@{ StableId = [LightLaunchpadSpotlightProbeNative]::StableItemId($llpRefreshAddedPath); LaunchCount = [uint64]20; LastLaunchedUnixMilliseconds = [uint64]20; Favorite = $true },
            [ordered]@{ StableId = [LightLaunchpadSpotlightProbeNative]::StableItemId($llpRefreshRenamedPath); LaunchCount = [uint64]30; LastLaunchedUnixMilliseconds = [uint64]30; Favorite = $true }
        )
    } | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText((Join-Path $llpDataDirectory 'search-history.json'), $llpSearchHistory, [System.Text.UTF8Encoding]::new($false))

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
        $llpArguments = @('--spotlight-flow-probe-file', $llpProbePath, '--spotlight-flow-ready-file', $llpReadyPath, '--diagnostics-log-file', $llpLogPath, '--spotlight-ownership-probe', '--test-monitor-device', $llpTestMonitorDevice)
        if ($MaterialFailure -ne 'None') {
            $llpArguments += @('--material-force-failure', $MaterialFailure)
        }
        $llpProcess = Start-Process -FilePath $llpExecutable -ArgumentList $llpArguments -PassThru
    }
    finally {
        $env:APPDATA = $llpPriorAppData
        $env:LOCALAPPDATA = $llpPriorLocalAppData
        $env:USERPROFILE = $llpPriorUserProfile
        $env:LLP_FLOW_MARKER = $llpPriorMarker
    }

    Wait-LlpCondition { Test-Path -LiteralPath $llpReadyPath } 12000 'The Spotlight probe catalog did not become ready.'
    $llpReady = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpReadyPath | ConvertFrom-Json
    Assert-Equal 52 ([int]$llpReady.itemCount) 'Spotlight probe catalog item count mismatch.'
    Assert-Equal 'Spotlight' ([string]$llpReady.displayMode) 'Spotlight probe did not load Spotlight mode.'
    $llpCatalogGeneration = [uint64]$llpReady.catalogGeneration
    $llpWindow = [LightLaunchpadSpotlightProbeNative]::FindWindowForProcess([uint32]$llpProcess.Id)
    Assert-True ($llpWindow -ne [IntPtr]::Zero) 'Could not discover the hidden real NativeUi window.'

    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0312, [UIntPtr]::new([uint64]0x4C55), [IntPtr]::Zero)
    Assert-True ([LightLaunchpadSpotlightProbeNative]::IsWindowVisible($llpWindow)) 'First Alt+D path did not show Spotlight.'
    $script:llpMonitorReady = Get-LlpReadyState
    Assert-Equal $llpTestMonitorDevice ([string]$llpMonitorReady.monitorDevice) 'Visible Spotlight test window was not fixed to DISPLAY1.'
    $llpWindowRect = [LightLaunchpadSpotlightProbeNative+RECT]::new()
    Assert-True ([LightLaunchpadSpotlightProbeNative]::GetWindowRect($llpWindow, [ref]$llpWindowRect)) 'Could not inspect the visible Spotlight test window rectangle.'
    Assert-True (
        $llpWindowRect.Left -ge [int]$llpMonitorReady.monitorBounds.left -and
        $llpWindowRect.Top -ge [int]$llpMonitorReady.monitorBounds.top -and
        $llpWindowRect.Right -le [int]$llpMonitorReady.monitorBounds.right -and
        $llpWindowRect.Bottom -le [int]$llpMonitorReady.monitorBounds.bottom
    ) 'Visible Spotlight test window escaped the requested DISPLAY1 bounds.'
    Write-Output "METRIC SpotlightMonitor device=$([string]$llpMonitorReady.monitorDevice) window=$($llpWindowRect.Left),$($llpWindowRect.Top),$($llpWindowRect.Right),$($llpWindowRect.Bottom) bounds=$([int]$llpMonitorReady.monitorBounds.left),$([int]$llpMonitorReady.monitorBounds.top),$([int]$llpMonitorReady.monitorBounds.right),$([int]$llpMonitorReady.monitorBounds.bottom)"
    $llpElement = [System.Windows.Automation.AutomationElement]::FromHandle($llpWindow)
    $llpValueObject = $null
    Assert-True ($llpElement.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$llpValueObject)) 'Spotlight search Value pattern is unavailable.'
    $llpValue = [System.Windows.Automation.ValuePattern]$llpValueObject
    Assert-Equal $true $llpElement.Current.HasKeyboardFocus 'Search was not the only focus after Alt+D.'
    Wait-LlpCondition {
        $script:llpMaterialReady = Get-LlpReadyState
        if ($null -eq $script:llpMaterialReady -or [uint64]$script:llpMaterialReady.materialSceneGeneration -eq 0) { return $false }
        if ($MaterialFailure -eq 'None') {
            return [string]$script:llpMaterialReady.materialPath -eq 'high-fidelity' -and
                [string]$script:llpMaterialReady.materialState -eq 'live' -and
                $null -ne $script:llpMaterialReady.PSObject.Properties['materialRequestedActivation'] -and
                $null -ne $script:llpMaterialReady.PSObject.Properties['materialRequestedSceneGeneration'] -and
                $null -ne $script:llpMaterialReady.PSObject.Properties['materialCommittedActivation'] -and
                $null -ne $script:llpMaterialReady.PSObject.Properties['materialPresentedSceneGeneration'] -and
                [uint64]$script:llpMaterialReady.materialRequestedActivation -gt 0 -and
                [uint64]$script:llpMaterialReady.materialCommittedActivation -eq [uint64]$script:llpMaterialReady.materialRequestedActivation -and
                [uint64]$script:llpMaterialReady.materialRequestedSceneGeneration -gt 0 -and
                [uint64]$script:llpMaterialReady.materialPresentedSceneGeneration -ge [uint64]$script:llpMaterialReady.materialRequestedSceneGeneration
        }
        return [string]$script:llpMaterialReady.materialPath -eq 'fallback' -and [string]$script:llpMaterialReady.materialState -eq 'fallback' -and [string]$script:llpMaterialReady.materialFailure -eq 'ForcedForTest'
    } 10000 'Spotlight did not commit the current activation and immutable scene before accepting input.'
    $llpInitialMaterialGeneration = [uint64]$llpMaterialReady.materialSceneGeneration
    $llpEvidenceRoot = if ([string]::IsNullOrWhiteSpace($EvidenceDirectory)) { $llpTempRoot } else { [System.IO.Path]::GetFullPath($EvidenceDirectory) }
    New-Item -ItemType Directory -Force -Path $llpEvidenceRoot | Out-Null
    $llpInitialScreenPath = Join-Path $llpEvidenceRoot $(if ($MaterialFailure -eq 'None') { '00-live-ownership.png' } else { '00-fallback-ownership.png' })
    if ($MaterialFailure -eq 'None') {
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x804B, [UIntPtr]::new([uint64]2), [IntPtr]::Zero)
        Wait-LlpCondition {
            $script:llpMaterialReady = Get-LlpReadyState
            return [string]$script:llpMaterialReady.materialPath -eq 'high-fidelity' -and [string]$script:llpMaterialReady.materialState -eq 'frozen-evidence'
        } 10000 'Spotlight did not freeze its committed DComp frame for compositor-level ownership evidence.'
    }
    $llpInitialMarkerPixels = Save-LlpScreenEvidence $llpWindow $llpInitialScreenPath $llpMaterialReady.materialOwnershipProbeBounds
    if ($MaterialFailure -eq 'None') {
        Assert-Equal 0 $llpInitialMarkerPixels 'The committed DComp frame still exposed the fallback-only ownership signature on the real desktop.'
        Assert-Equal $true ([bool]$llpMaterialReady.materialDirectLayerTransparent) 'The high-fidelity handoff did not make the layered HWND bottom pixels transparent.'
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x804B, [UIntPtr]::new([uint64]3), [IntPtr]::Zero)
        try {
            Wait-LlpCondition {
                $script:llpMaterialReady = Get-LlpReadyState
                return [string]$script:llpMaterialReady.materialPath -eq 'high-fidelity' -and
                    [string]$script:llpMaterialReady.materialState -eq 'live' -and
                    [bool]$script:llpMaterialReady.materialDirectLayerTransparent
            } 10000 'Spotlight did not resume its live renderer after compositor-level ownership evidence.'
        }
        catch {
            $llpResumeState = Get-LlpReadyState
            throw "$($_.Exception.Message) state=$([string]$llpResumeState.materialState) path=$([string]$llpResumeState.materialPath) failure=$([string]$llpResumeState.materialFailure) requestedActivation=$([uint64]$llpResumeState.materialRequestedActivation) committedActivation=$([uint64]$llpResumeState.materialCommittedActivation) requestedSceneGeneration=$([uint64]$llpResumeState.materialRequestedSceneGeneration) sceneGeneration=$([uint64]$llpResumeState.materialSceneGeneration) presentedSceneGeneration=$([uint64]$llpResumeState.materialPresentedSceneGeneration) directTransparent=$([bool]$llpResumeState.materialDirectLayerTransparent)"
        }
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x804B, [UIntPtr]::new([uint64]2), [IntPtr]::Zero)
        Wait-LlpCondition {
            $script:llpMaterialReady = Get-LlpReadyState
            return [string]$script:llpMaterialReady.materialState -eq 'frozen-evidence'
        } 10000 'Spotlight did not freeze its rebuilt DComp frame for compositor-level ownership evidence.'
        $llpRebuildScreenPath = Join-Path $llpEvidenceRoot '01-live-rebuild-ownership.png'
        $llpRebuildMarkerPixels = Save-LlpScreenEvidence $llpWindow $llpRebuildScreenPath $llpMaterialReady.materialOwnershipProbeBounds
        Assert-Equal 0 $llpRebuildMarkerPixels 'The rebuilt DComp frame exposed stale fallback-only direct pixels on the real desktop.'
        Assert-Equal $true ([bool]$llpMaterialReady.materialDirectLayerTransparent) 'The rebuilt DComp frame did not own a transparent direct bottom layer.'
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x804B, [UIntPtr]::new([uint64]3), [IntPtr]::Zero)
        Wait-LlpCondition {
            $script:llpMaterialReady = Get-LlpReadyState
            return [string]$script:llpMaterialReady.materialState -eq 'live' -and
                [bool]$script:llpMaterialReady.materialDirectLayerTransparent
        } 10000 'Spotlight did not resume after rebuilt ownership evidence.'
        Write-Output "METRIC SpotlightRebuildOwnership directTransparent=$([bool]$llpMaterialReady.materialDirectLayerTransparent) markerPixels=$llpRebuildMarkerPixels screenshot='$llpRebuildScreenPath'"
    }
    else {
        Assert-True ($null -ne $llpMaterialReady.PSObject.Properties['spotlightFallbackRegionHeaderCount']) 'Fallback diagnostics did not report the actually painted region-header count.'
        Assert-True ($null -ne $llpMaterialReady.PSObject.Properties['spotlightFallbackFavoriteBadgeCount']) 'Fallback diagnostics did not report the actually painted favorite-badge count.'
        Assert-True ($null -ne $llpMaterialReady.PSObject.Properties['spotlightFallbackPresentedItemStableIds']) 'Fallback diagnostics did not report the actually painted immutable item identities.'
        Assert-Equal 0 ([int]$llpMaterialReady.spotlightFallbackRegionHeaderCount) 'Spotlight fallback leaked Launchpad region headers.'
        Assert-Equal 0 ([int]$llpMaterialReady.spotlightFallbackFavoriteBadgeCount) 'Spotlight fallback leaked Launchpad-only favorite badges.'
        $llpFallbackIds = @($llpMaterialReady.spotlightFallbackPresentedItemStableIds)
        $llpFallbackTitles = @($llpMaterialReady.spotlightFallbackPresentedItemTitles)
        $llpFallbackBounds = @($llpMaterialReady.spotlightFallbackPresentedItemBounds)
        $llpFallbackIconTops = @($llpMaterialReady.spotlightFallbackPresentedItemIconTops)
        $llpFallbackTitleTops = @($llpMaterialReady.spotlightFallbackPresentedItemTitleTops)
        Assert-True ($llpFallbackIds.Count -gt 1) 'Spotlight fallback did not paint the full visible immutable item payload.'
        Assert-Equal $llpFallbackIds.Count $llpFallbackTitles.Count 'Spotlight fallback identity/title evidence count mismatch.'
        Assert-Equal $llpFallbackIds.Count $llpFallbackBounds.Count 'Spotlight fallback identity/bounds evidence count mismatch.'
        Assert-Equal $llpFallbackIds.Count $llpFallbackIconTops.Count 'Spotlight fallback identity/icon-layout evidence count mismatch.'
        Assert-Equal $llpFallbackIds.Count $llpFallbackTitleTops.Count 'Spotlight fallback identity/title-layout evidence count mismatch.'
        Assert-Equal $llpFallbackIds.Count @($llpFallbackIds | Sort-Object -Unique).Count 'Spotlight fallback painted duplicate immutable identities.'
        for ($llpFallbackIndex = 0; $llpFallbackIndex -lt $llpFallbackBounds.Count; $llpFallbackIndex++) {
            $llpFallbackBoundsValue = $llpFallbackBounds[$llpFallbackIndex]
            Assert-True (-not [string]::IsNullOrWhiteSpace([string]$llpFallbackTitles[$llpFallbackIndex])) 'Spotlight fallback painted an item without its label.'
            Assert-Equal ([int]$llpFallbackBoundsValue.top + 8) ([int]$llpFallbackIconTops[$llpFallbackIndex]) 'Spotlight fallback icon top diverged from the DComp item formula.'
            Assert-Equal ([int]$llpFallbackBoundsValue.bottom - [Math]::Max(36, [int]$llpMaterialReady.scene.tileFontPx * 2)) ([int]$llpFallbackTitleTops[$llpFallbackIndex]) 'Spotlight fallback title top diverged from the DComp item formula.'
            Assert-True (
                [int]$llpFallbackBoundsValue.left -ge [int]$llpMaterialReady.scene.contentClip.left -and
                [int]$llpFallbackBoundsValue.top -ge [int]$llpMaterialReady.scene.contentClip.top -and
                [int]$llpFallbackBoundsValue.right -le [int]$llpMaterialReady.scene.contentClip.right -and
                [int]$llpFallbackBoundsValue.bottom -le [int]$llpMaterialReady.scene.contentClip.bottom -and
                [int]$llpFallbackBoundsValue.right -gt [int]$llpFallbackBoundsValue.left -and
                [int]$llpFallbackBoundsValue.bottom -gt [int]$llpFallbackBoundsValue.top
            ) 'Spotlight fallback painted an item outside the Spotlight content clip.'
            for ($llpOtherIndex = $llpFallbackIndex + 1; $llpOtherIndex -lt $llpFallbackBounds.Count; $llpOtherIndex++) {
                $llpOtherBounds = $llpFallbackBounds[$llpOtherIndex]
                $llpOverlaps = [int]$llpFallbackBoundsValue.left -lt [int]$llpOtherBounds.right -and
                    [int]$llpFallbackBoundsValue.right -gt [int]$llpOtherBounds.left -and
                    [int]$llpFallbackBoundsValue.top -lt [int]$llpOtherBounds.bottom -and
                    [int]$llpFallbackBoundsValue.bottom -gt [int]$llpOtherBounds.top
                Assert-True (-not $llpOverlaps) 'Spotlight fallback painted overlapping item bounds.'
            }
        }
        Assert-True ($llpInitialMarkerPixels -ge 240) 'Forced fallback did not retain its fallback-only ownership signature on the real desktop.'
        Assert-Equal $false ([bool]$llpMaterialReady.materialDirectLayerTransparent) 'Forced fallback unexpectedly discarded its layered HWND pixels.'
    }
    Write-Output "METRIC SpotlightOwnership state=$([string]$llpMaterialReady.materialState) directTransparent=$([bool]$llpMaterialReady.materialDirectLayerTransparent) markerPixels=$llpInitialMarkerPixels screenshot='$llpInitialScreenPath'"
    $llpCatalogLaunchOffset = 0
    if ($MaterialFailure -eq 'None') {
        $llpExpectedSeedStableId = [LightLaunchpadSpotlightProbeNative]::StableItemId($llpRefreshSeedPath)
        $llpExpectedAddedStableId = [LightLaunchpadSpotlightProbeNative]::StableItemId($llpRefreshAddedPath)
        $llpExpectedRenamedStableId = [LightLaunchpadSpotlightProbeNative]::StableItemId($llpRefreshRenamedPath)
        $llpCatalogGeneration = [uint64]$llpMaterialReady.catalogGeneration
        $llpPresentedCatalogGeneration = [uint64]$llpMaterialReady.materialAuxiliarySceneGeneration
        Copy-Item -LiteralPath $llpTarget -Destination $llpRefreshAddedPath
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0111, [UIntPtr]::new([uint64]1002), [IntPtr]::Zero)
        Wait-LlpCondition {
            $script:llpRefreshState = Get-LlpReadyState
            $null -ne $script:llpRefreshState -and [uint64]$script:llpRefreshState.catalogGeneration -gt $llpCatalogGeneration
        } 10000 'Empty-query catalog add did not publish from the Shell STA.'
        Wait-LlpCondition {
            $script:llpRefreshState = Get-LlpReadyState
            $null -ne $script:llpRefreshState -and
                [uint64]$script:llpRefreshState.materialAuxiliarySceneGeneration -gt $llpPresentedCatalogGeneration -and
                [string]@($script:llpRefreshState.materialPresentedItemStableIds)[0] -eq $llpExpectedAddedStableId
        } 10000 'Empty-query catalog add did not present an advanced immutable material scene with the added identity.'
        Assert-Equal $llpExpectedAddedStableId ([string]@($llpRefreshState.materialPresentedItemStableIds)[0]) 'Catalog add presented the wrong first immutable stable ID.'
        Assert-Equal $llpRefreshAddedPath ([string]@($llpRefreshState.materialPresentedItemSourcePaths)[0]) 'Catalog add presented the wrong first immutable source path.'

        $llpCatalogGeneration = [uint64]$llpRefreshState.catalogGeneration
        $llpPresentedCatalogGeneration = [uint64]$llpRefreshState.materialAuxiliarySceneGeneration
        Move-Item -LiteralPath $llpRefreshAddedPath -Destination $llpRefreshRenamedPath
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0111, [UIntPtr]::new([uint64]1002), [IntPtr]::Zero)
        Wait-LlpCondition {
            $script:llpRefreshState = Get-LlpReadyState
            $null -ne $script:llpRefreshState -and
                [uint64]$script:llpRefreshState.catalogGeneration -gt $llpCatalogGeneration -and
                [uint64]$script:llpRefreshState.materialAuxiliarySceneGeneration -gt $llpPresentedCatalogGeneration -and
                [string]@($script:llpRefreshState.materialPresentedItemStableIds)[0] -eq $llpExpectedRenamedStableId
        } 10000 'Empty-query catalog rename did not publish and present a new immutable scene.'
        Assert-Equal $llpExpectedRenamedStableId ([string]@($llpRefreshState.materialPresentedItemStableIds)[0]) 'Catalog rename presented the wrong first immutable stable ID.'
        Assert-Equal $llpRefreshRenamedPath ([string]@($llpRefreshState.materialPresentedItemSourcePaths)[0]) 'Catalog rename presented the wrong first immutable source path.'
        Assert-True (-not (@($llpRefreshState.materialPresentedItemStableIds) -contains $llpExpectedAddedStableId)) 'Catalog rename retained the removed stable ID in the presented immutable scene.'

        $llpCatalogGeneration = [uint64]$llpRefreshState.catalogGeneration
        $llpPresentedCatalogGeneration = [uint64]$llpRefreshState.materialAuxiliarySceneGeneration
        $llpRefreshRemovedPath = [System.IO.Path]::GetFullPath((Join-Path $llpTempRoot 'Refresh Renamed.removed'))
        $llpTempBoundaryForRefresh = [System.IO.Path]::GetFullPath($llpTempRoot).TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
        Assert-True ($llpRefreshRenamedPath.StartsWith($llpTempBoundaryForRefresh, [System.StringComparison]::OrdinalIgnoreCase)) 'Catalog rename fixture escaped the isolated test root.'
        Assert-True ($llpRefreshRemovedPath.StartsWith($llpTempBoundaryForRefresh, [System.StringComparison]::OrdinalIgnoreCase)) 'Catalog removal quarantine escaped the isolated test root.'
        $llpRefreshRenamedItem = Get-Item -LiteralPath $llpRefreshRenamedPath -Force
        Assert-True (-not $llpRefreshRenamedItem.PSIsContainer -and ($llpRefreshRenamedItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) 'Refusing to move a non-file or reparse-point catalog fixture.'
        Move-Item -LiteralPath $llpRefreshRenamedPath -Destination $llpRefreshRemovedPath
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0111, [UIntPtr]::new([uint64]1002), [IntPtr]::Zero)
        Wait-LlpCondition {
            $script:llpRefreshState = Get-LlpReadyState
            $null -ne $script:llpRefreshState -and
                [uint64]$script:llpRefreshState.catalogGeneration -gt $llpCatalogGeneration -and
                [uint64]$script:llpRefreshState.materialAuxiliarySceneGeneration -gt $llpPresentedCatalogGeneration -and
                [string]@($script:llpRefreshState.materialPresentedItemStableIds)[0] -eq $llpExpectedSeedStableId
        } 10000 'Empty-query catalog removal did not publish and present a new immutable scene.'
        Assert-True (-not (@($llpRefreshState.materialPresentedItemStableIds) -contains $llpExpectedRenamedStableId)) 'Catalog removal retained the deleted stable ID in the presented immutable scene.'

        $llpCatalogGeneration = [uint64]$llpRefreshState.catalogGeneration
        $llpPresentedCatalogGeneration = [uint64]$llpRefreshState.materialAuxiliarySceneGeneration
        Move-Item -LiteralPath $llpRefreshRemovedPath -Destination $llpRefreshRenamedPath
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0111, [UIntPtr]::new([uint64]1002), [IntPtr]::Zero)
        Wait-LlpCondition {
            $script:llpRefreshState = Get-LlpReadyState
            $null -ne $script:llpRefreshState -and
                [uint64]$script:llpRefreshState.catalogGeneration -gt $llpCatalogGeneration -and
                [uint64]$script:llpRefreshState.materialAuxiliarySceneGeneration -gt $llpPresentedCatalogGeneration -and
                [string]@($script:llpRefreshState.materialPresentedItemStableIds)[0] -eq $llpExpectedRenamedStableId
        } 10000 'Restored empty-query fixture did not publish and present a new immutable scene.'
        Assert-Equal $llpExpectedRenamedStableId ([string]@($llpRefreshState.materialPresentedItemStableIds)[0]) 'Restored catalog fixture did not become the first displayed immutable item.'
        Assert-Equal $llpRefreshRenamedPath ([string]@($llpRefreshState.materialPresentedItemSourcePaths)[0]) 'Restored catalog fixture source identity did not match the displayed item.'
        Wait-LlpCondition { Test-LlpFileReadyForLaunch $llpRefreshRenamedPath } 10000 'Restored executable remained locked and was not ready for the real Shell launch.'
        Send-LlpKey $llpWindow 0x0D
        Wait-LlpCondition { (Get-LlpMarkerNames).Count -ge 1 } 10000 'Enter did not launch the first item displayed after catalog refresh.'
        $llpMarkerNames = @(Get-LlpMarkerNames)
        Assert-Equal 'Refresh Renamed.exe' $llpMarkerNames[0] 'Enter launch did not agree with the first immutable item displayed after catalog refresh.'
        Wait-LlpCondition { -not [LightLaunchpadSpotlightProbeNative]::IsWindowVisible($llpWindow) } 3000 'Spotlight did not hide after the catalog-refresh launch.'
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0312, [UIntPtr]::new([uint64]0x4C55), [IntPtr]::Zero)
        Assert-Equal '' $llpValue.Current.Value 'Catalog-refresh reopen did not keep the empty query.'
        $llpCatalogLaunchOffset = 1
        Wait-LlpCondition {
            $script:llpMaterialReady = Get-LlpReadyState
            [string]$script:llpMaterialReady.materialState -eq 'live' -and [bool]$script:llpMaterialReady.materialDirectLayerTransparent
        } 10000 'Catalog-refresh reopen did not return to the live high-fidelity renderer.'
    }
    if ($MaterialFailure -eq 'None') {
        Wait-LlpCondition {
            $script:llpInitialAuxiliary = Get-LlpReadyState
            $null -ne $script:llpInitialAuxiliary -and
                [bool]$script:llpInitialAuxiliary.materialAuxiliaryRendered -and
                [uint64]$script:llpInitialAuxiliary.materialAuxiliarySceneGeneration -gt 0
        } 3000 'High-fidelity Spotlight did not present the immutable auxiliary control.'
        Assert-Equal '...' ([string]$llpInitialAuxiliary.materialAuxiliaryText) 'High-fidelity auxiliary label mismatch.'
        Assert-Equal $false ([bool]$llpInitialAuxiliary.materialAuxiliaryFocused) 'Auxiliary control was focused on initial open.'
        Assert-Equal ([int]$llpInitialAuxiliary.scene.auxiliary.left) ([int]$llpInitialAuxiliary.materialAuxiliaryBounds.left) 'Auxiliary presented left bound mismatch.'
        Assert-Equal ([int]$llpInitialAuxiliary.scene.auxiliary.top) ([int]$llpInitialAuxiliary.materialAuxiliaryBounds.top) 'Auxiliary presented top bound mismatch.'
        Assert-Equal ([int]$llpInitialAuxiliary.scene.auxiliary.right) ([int]$llpInitialAuxiliary.materialAuxiliaryBounds.right) 'Auxiliary presented right bound mismatch.'
        Assert-Equal ([int]$llpInitialAuxiliary.scene.auxiliary.bottom) ([int]$llpInitialAuxiliary.materialAuxiliaryBounds.bottom) 'Auxiliary presented bottom bound mismatch.'
    }
    $llpInitialMaterialGeneration = [uint64](Get-LlpReadyState).materialSceneGeneration
    Send-LlpText $llpWindow 'Beta'
    Assert-Equal 'Beta' $llpValue.Current.Value 'First canonical path did not enter its query.'
    Wait-LlpCondition {
        $script:llpMaterialAfterSearch = Get-LlpReadyState
        $null -ne $script:llpMaterialAfterSearch -and [uint64]$script:llpMaterialAfterSearch.materialSceneGeneration -gt $llpInitialMaterialGeneration
    } 3000 'Typing did not advance the immutable material scene generation.'
    $llpSearchMaterialGeneration = [uint64]$llpMaterialAfterSearch.materialSceneGeneration
    Send-LlpKey $llpWindow 0x0D
    Wait-LlpCondition { (Get-LlpMarkerNames).Count -ge (1 + $llpCatalogLaunchOffset) } 10000 'Search Enter did not launch the first exact result.'
    $llpMarkerNames = @(Get-LlpMarkerNames)
    Assert-Equal 'Beta.exe' $llpMarkerNames[$llpCatalogLaunchOffset] 'Search Enter launched the wrong result.'
    Wait-LlpCondition { -not [LightLaunchpadSpotlightProbeNative]::IsWindowVisible($llpWindow) } 3000 'Spotlight did not hide after the first launch.'

    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0312, [UIntPtr]::new([uint64]0x4C55), [IntPtr]::Zero)
    Assert-Equal '' $llpValue.Current.Value 'Second Alt+D did not reset the search query.'
    if ($MaterialFailure -eq 'None') {
        $llpPriorActivation = [uint64]$llpMaterialReady.materialRequestedActivation
        Wait-LlpCondition {
            $script:llpMaterialReady = Get-LlpReadyState
            return [string]$script:llpMaterialReady.materialState -eq 'live' -and
                [uint64]$script:llpMaterialReady.materialRequestedActivation -gt $llpPriorActivation -and
                [uint64]$script:llpMaterialReady.materialCommittedActivation -eq [uint64]$script:llpMaterialReady.materialRequestedActivation -and
                [uint64]$script:llpMaterialReady.materialPresentedSceneGeneration -ge [uint64]$script:llpMaterialReady.materialRequestedSceneGeneration -and
                [bool]$script:llpMaterialReady.materialDirectLayerTransparent
        } 10000 'Reopened Spotlight cleared its fallback before the current activation and immutable scene committed.'
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x804B, [UIntPtr]::new([uint64]2), [IntPtr]::Zero)
        Wait-LlpCondition {
            $script:llpMaterialReady = Get-LlpReadyState
            return [string]$script:llpMaterialReady.materialState -eq 'frozen-evidence'
        } 10000 'Reopened Spotlight did not freeze for compositor-level ownership evidence.'
        $llpReopenScreenPath = Join-Path $llpEvidenceRoot '02-live-reopen-ownership.png'
        $llpReopenMarkerPixels = Save-LlpScreenEvidence $llpWindow $llpReopenScreenPath $llpMaterialReady.materialOwnershipProbeBounds
        Assert-Equal 0 $llpReopenMarkerPixels 'Hide/show left stale fallback-only direct pixels beneath the reopened DComp frame.'
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x804B, [UIntPtr]::new([uint64]3), [IntPtr]::Zero)
        Wait-LlpCondition {
            $script:llpMaterialReady = Get-LlpReadyState
            return [string]$script:llpMaterialReady.materialState -eq 'live' -and
                [bool]$script:llpMaterialReady.materialDirectLayerTransparent
        } 10000 'Reopened Spotlight did not resume after ownership evidence.'
        Write-Output "METRIC SpotlightReopenOwnership directTransparent=$([bool]$llpMaterialReady.materialDirectLayerTransparent) markerPixels=$llpReopenMarkerPixels screenshot='$llpReopenScreenPath'"
    }
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
        Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot '00-empty-search.png')
    }
    Send-LlpText $llpWindow 'Flow'
    Wait-LlpCondition {
        $script:llpMaterialBeforeNavigation = Get-LlpReadyState
        $null -ne $script:llpMaterialBeforeNavigation -and [uint64]$script:llpMaterialBeforeNavigation.materialSceneGeneration -gt $llpSearchMaterialGeneration
    } 3000 'Second-query submission did not advance the immutable material scene generation.'
    $llpBeforeDownGeneration = [uint64]$llpMaterialBeforeNavigation.materialSceneGeneration
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
        Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot '01-search-focus.png')
    }
    Write-LlpNavigationDiagnostic 'before-down'
    Send-LlpKey $llpWindow 0x28
    Write-LlpNavigationDiagnostic 'after-down'
    Assert-Equal $false $llpElement.Current.HasKeyboardFocus 'Down did not move focus from search into results.'
    Wait-LlpCondition {
        $script:llpMaterialAfterDown = Get-LlpReadyState
        $null -ne $script:llpMaterialAfterDown -and [uint64]$script:llpMaterialAfterDown.materialSceneGeneration -gt $llpBeforeDownGeneration
    } 3000 'Down did not advance the immutable material scene generation.'
    $llpAfterDownGeneration = [uint64]$llpMaterialAfterDown.materialSceneGeneration
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
        Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot '02-result-focus.png')
    }
    Write-LlpNavigationDiagnostic 'before-right'
    Send-LlpKey $llpWindow 0x27
    Write-LlpNavigationDiagnostic 'after-right'
    Wait-LlpCondition {
        $script:llpMaterialAfterRight = Get-LlpReadyState
        $null -ne $script:llpMaterialAfterRight -and [uint64]$script:llpMaterialAfterRight.materialSceneGeneration -gt $llpAfterDownGeneration
    } 3000 'Right did not advance the immutable material scene generation.'
    Send-LlpKey $llpWindow 0x0D
    Wait-LlpCondition { (Get-LlpMarkerNames).Count -ge (2 + $llpCatalogLaunchOffset) } 10000 'Result navigation Enter did not launch.'
    $llpMarkerNames = @(Get-LlpMarkerNames)
    Assert-True ($llpMarkerNames[1 + $llpCatalogLaunchOffset] -in @('Flow Alpha.exe', 'Flow Beta.exe')) 'Down/Right/Enter launched a result outside the Flow query.'

    $llpLatestReady = Get-LlpReadyState
    $llpCatalogGeneration = [uint64]$llpLatestReady.catalogGeneration
    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0312, [UIntPtr]::new([uint64]0x4C55), [IntPtr]::Zero)
    Wait-LlpCondition {
        $script:llpRefreshState = Get-LlpReadyState
        $null -ne $script:llpRefreshState -and [uint64]$script:llpRefreshState.catalogGeneration -gt $llpCatalogGeneration
    } 10000 'Catalog refresh associated with the focus-preservation open did not publish.'
    $llpCatalogGeneration = [uint64]$llpRefreshState.catalogGeneration
    Send-LlpText $llpWindow 'Flow'
    Send-LlpKey $llpWindow 0x28
    $llpAddedFlowPath = [System.IO.Path]::GetFullPath((Join-Path $llpLaunchpad 'Flow A.exe'))
    Copy-Item -LiteralPath $llpTarget -Destination $llpAddedFlowPath
    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0111, [UIntPtr]::new([uint64]1002), [IntPtr]::Zero)
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
    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0111, [UIntPtr]::new([uint64]1002), [IntPtr]::Zero)
    Wait-LlpCondition {
        $script:llpRefreshState = Get-LlpReadyState
        $null -ne $script:llpRefreshState -and [uint64]$script:llpRefreshState.catalogGeneration -gt $llpCatalogGeneration
    } 10000 'Catalog refresh after focused-result removal did not publish.'
    Assert-Equal 'results' ([string]$llpRefreshState.focus) 'Missing focused result unexpectedly changed focus ownership.'
    Assert-Equal '' ([string]$llpRefreshState.focusedResultPath) 'Missing stable result was silently substituted with another launch target.'
    Send-LlpKey $llpWindow 0x0D
    Start-Sleep -Milliseconds 300
    Assert-Equal (2 + $llpCatalogLaunchOffset) (@(Get-LlpMarkerNames).Count) 'Enter after focused-result removal launched an unintended replacement.'
    Send-LlpKey $llpWindow 0x1B
    Send-LlpKey $llpWindow 0x1B
    Send-LlpKey $llpWindow 0x1B

    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0312, [UIntPtr]::new([uint64]0x4C55), [IntPtr]::Zero)
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

    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0312, [UIntPtr]::new([uint64]0x4C55), [IntPtr]::Zero)
    Send-LlpText $llpWindow 'Flow'
    Send-LlpKey $llpWindow 0x28
    Send-LlpText $llpWindow 'X'
    Assert-Equal $true $llpElement.Current.HasKeyboardFocus 'Printable input from results did not return to search.'
    Assert-Equal 'FlowX' $llpValue.Current.Value 'Printable input from results was lost.'
    Send-LlpKey $llpWindow 0x1B
    Send-LlpKey $llpWindow 0x1B

    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0312, [UIntPtr]::new([uint64]0x4C55), [IntPtr]::Zero)
    $llpTabQuery = if ($MaterialFailure -eq 'None') { 'Scroll' } else { 'Flow' }
    Send-LlpText $llpWindow $llpTabQuery
    if ($MaterialFailure -eq 'None') {
        Wait-LlpCondition {
            $script:llpBeforeWheel = Get-LlpReadyState
            $null -ne $script:llpBeforeWheel -and
                [string]$script:llpBeforeWheel.materialPath -eq 'high-fidelity' -and
                [int]$script:llpBeforeWheel.hitCount -gt 0
        } 3000 'High-fidelity Spotlight did not publish pre-wheel visible geometry.'
        $llpBeforeWheelGeneration = [uint64]$llpBeforeWheel.materialSceneGeneration
        $llpBeforeWheelTop = [int]$llpBeforeWheel.firstHit.top
        $llpWheelDownWParam = [Convert]::ToUInt64('FF880000', 16)
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x020A, [UIntPtr]::new($llpWheelDownWParam), [IntPtr]::Zero)
        Wait-LlpCondition {
            $script:llpAfterWheel = Get-LlpReadyState
            $null -ne $script:llpAfterWheel -and
                [uint64]$script:llpAfterWheel.materialSceneGeneration -gt $llpBeforeWheelGeneration -and
                [int]$script:llpAfterWheel.firstHit.top -ne $llpBeforeWheelTop
        } 3000 'Wheel scrolling did not submit advanced high-fidelity scene geometry.'
        Assert-Equal $true $llpElement.Current.HasKeyboardFocus 'Wheel scrolling stole search keyboard focus.'
        Assert-Equal 'Scroll' $llpValue.Current.Value 'Wheel scrolling changed the search query.'
    }
    Send-LlpKey $llpWindow 0x09
    Assert-Equal $false $llpElement.Current.HasKeyboardFocus 'Tab did not move from search to results.'
    $llpAuxiliaryBeforeFocusGeneration = if ($MaterialFailure -eq 'None') { [uint64](Get-LlpReadyState).materialAuxiliarySceneGeneration } else { 0 }
    Send-LlpKey $llpWindow 0x09
    Assert-Equal $false $llpElement.Current.HasKeyboardFocus 'Tab did not retain non-search focus on the auxiliary control.'
    if ($MaterialFailure -eq 'None') {
        Wait-LlpCondition {
            $script:llpFocusedAuxiliary = Get-LlpReadyState
            $null -ne $script:llpFocusedAuxiliary -and
                [bool]$script:llpFocusedAuxiliary.materialAuxiliaryRendered -and
                [bool]$script:llpFocusedAuxiliary.materialAuxiliaryFocused -and
                [uint64]$script:llpFocusedAuxiliary.materialAuxiliarySceneGeneration -gt $llpAuxiliaryBeforeFocusGeneration
        } 3000 'High-fidelity Spotlight did not present the focused auxiliary control state.'
    }
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
        Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot '03-auxiliary-focus.png')
    }
    $llpFocusedAuxiliaryGeneration = if ($MaterialFailure -eq 'None') { [uint64]$llpFocusedAuxiliary.materialAuxiliarySceneGeneration } else { 0 }
    Send-LlpKey $llpWindow 0x09
    Assert-Equal $true $llpElement.Current.HasKeyboardFocus 'Tab did not wrap from auxiliary control to search.'
    if ($MaterialFailure -eq 'None') {
        Wait-LlpCondition {
            $script:llpUnfocusedAuxiliary = Get-LlpReadyState
            $null -ne $script:llpUnfocusedAuxiliary -and
                [bool]$script:llpUnfocusedAuxiliary.materialAuxiliaryRendered -and
                -not [bool]$script:llpUnfocusedAuxiliary.materialAuxiliaryFocused -and
                [uint64]$script:llpUnfocusedAuxiliary.materialAuxiliarySceneGeneration -gt $llpFocusedAuxiliaryGeneration
        } 3000 'High-fidelity Spotlight did not present the unfocused auxiliary control state after focus wrap.'
    }
    Send-LlpKey $llpWindow 0x1B
    Send-LlpKey $llpWindow 0x1B

    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0312, [UIntPtr]::new([uint64]0x4C55), [IntPtr]::Zero)
    Send-LlpText $llpWindow 'Flow'
    Send-LlpKey $llpWindow 0x09
    Send-LlpKey $llpWindow 0x09
    Send-LlpKey $llpWindow 0x0D
    Assert-True (-not [LightLaunchpadSpotlightProbeNative]::IsWindowVisible($llpWindow)) 'Enter on the auxiliary control did not hide Spotlight.'
    $llpSettingsWindow = [IntPtr]::Zero
    Wait-LlpCondition { $script:llpSettingsWindow = [LightLaunchpadSpotlightProbeNative]::FindOtherVisibleWindowForProcess([uint32]$llpProcess.Id, $llpWindow); $script:llpSettingsWindow -ne [IntPtr]::Zero } 5000 'Enter on the auxiliary control did not open Settings.'
    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpSettingsWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
    Wait-LlpCondition { -not [LightLaunchpadSpotlightProbeNative]::IsWindow($llpSettingsWindow) } 3000 'The auxiliary Settings window did not close.'

    Wait-LlpCondition { (Get-LlpMarkerNames).Count -eq (2 + $llpCatalogLaunchOffset) } 3000 'Unexpected extra launch occurred during focus-state tests.'
    Start-Sleep -Milliseconds 200
    [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
    if (-not $llpProcess.WaitForExit(10000)) { throw 'Spotlight flow probe did not exit.' }
    Assert-Equal 0 $llpProcess.ExitCode 'Spotlight flow probe exit code mismatch.'
    $llpProbe = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpProbePath | ConvertFrom-Json
    Assert-Equal 'Alt+D' ([string]$llpProbe.configuredHotkey) 'Canonical hotkey setting changed.'
    Assert-Equal (7 + $llpCatalogLaunchOffset) ([int]$llpProbe.hotkeyMessages) 'Not all simulated Alt+D messages opened Spotlight.'
    Assert-Equal (7 + $llpCatalogLaunchOffset) ([int]$llpProbe.searchFocusedOnOpen) 'Spotlight did not open with search as the sole focus every time.'
    Assert-True (@($llpProbe.selectedIndexOnOpen | Where-Object { [int]$_ -ne -1 }).Count -eq 0) 'Spotlight painted a result as selected on open.'
    Assert-Equal (1 + $llpCatalogLaunchOffset) ([int]$llpProbe.searchEnterLaunches) 'Search Enter launch count mismatch.'
    Assert-Equal 1 ([int]$llpProbe.resultEnterLaunches) 'Focused-result Enter launch count mismatch.'
    Assert-Equal (2 + $llpCatalogLaunchOffset) ([int]$llpProbe.successfulLaunches) 'Shell broker successful-launch count mismatch.'
    if ($MaterialFailure -eq 'None') {
        Assert-Equal 'high-fidelity' ([string]$llpProbe.materialPath) 'Normal Spotlight host did not finish on the high-fidelity material path.'
        Assert-True ([uint64]$llpProbe.materialPresentedFrames -gt 0) 'Normal Spotlight host did not present a composed material frame.'
    }
    else {
        Assert-Equal 'fallback' ([string]$llpProbe.materialPath) 'Forced initialization failure did not retain the single material fallback.'
        Assert-Equal 'ForcedForTest' ([string]$llpProbe.materialFailure) 'Forced initialization failure did not retain its stable reason.'
    }
    Assert-True ([uint64]$llpProbe.materialSceneGeneration -ge [uint64]$llpMaterialAfterRight.materialSceneGeneration) 'Final Spotlight probe lost the latest immutable material scene generation.'
    $llpFocusedNames = @($llpProbe.focusedResultPaths | ForEach-Object { [System.IO.Path]::GetFileName([string]$_) })
    Assert-True ($llpFocusedNames.Count -ge 2) 'The real EXE did not report both Down and Right result-focus states.'
    Assert-Equal 'Flow Beta.exe' $llpFocusedNames[0] 'Down did not focus the first ranked Flow result.'
    Assert-Equal 'Flow Alpha.exe' $llpFocusedNames[1] 'Right did not advance to the second ranked Flow result.'
    Assert-Equal 'Flow Alpha.exe' $llpMarkerNames[1 + $llpCatalogLaunchOffset] 'Enter did not launch the result focused by Right.'
    $llpReadySamples = @($llpProbe.inputReadyMs | ForEach-Object { [int64]$_ } | Sort-Object)
    $llpP95 = $llpReadySamples[[Math]::Min($llpReadySamples.Count - 1, [Math]::Floor($llpReadySamples.Count * 0.95))]
    Write-Output "METRIC SpotlightFlow inputReadySamplesMs=$($llpReadySamples -join ',')"
    Assert-True ($llpP95 -le 180) "Hotkey-to-input-ready P95 exceeded 180 ms: $llpP95 ms."
    Write-Output "METRIC SpotlightFlow hotkeyToInputReadyP95Ms=$llpP95 launches=$([int]$llpProbe.successfulLaunches) opens=$([int]$llpProbe.hotkeyMessages) materialFailure=$MaterialFailure materialPath=$([string]$llpProbe.materialPath) materialSceneGeneration=$([uint64]$llpProbe.materialSceneGeneration) materialPresentedFrames=$([uint64]$llpProbe.materialPresentedFrames)"
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
