param(
    [string]$EvidenceDirectory = '',

    [ValidateSet('None', 'initialization')]
    [string]$MaterialFailure = 'None',

    [switch]$TransparentTransitionFailure,

    [switch]$HideSceneRace,

    [ValidateSet('None', 'ShowHide', 'SubmitHide')]
    [string]$LatestHideRace = 'None',

    [switch]$ComposeHideLifetime,

    [switch]$SelfCaptureTransition,

    [switch]$OrdinaryPresentationEvidence
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

function Show-LlpWithConfiguredHotkey {
    param([IntPtr]$Window)
    if ($MaterialFailure -eq 'None') {
        [LightLaunchpadSpotlightProbeNative]::PressAlt9()
    }
    else {
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($Window, 0x0312, [UIntPtr]::new([uint64]0x4C55), [IntPtr]::Zero)
    }
    Wait-LlpCondition { [LightLaunchpadSpotlightProbeNative]::IsWindowVisible($Window) } 3000 'The configured test hotkey path did not show the requested surface.'
}

function Get-LlpMarkerNames {
    if (-not (Test-Path -LiteralPath $llpMarker)) { return @() }
    return @((Get-Content -Encoding UTF8 -LiteralPath $llpMarker | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }) | ForEach-Object { [System.IO.Path]::GetFileName($_) })
}

function Get-LlpMarkerPaths {
    if (-not (Test-Path -LiteralPath $llpMarker)) { return @() }
    return @(
        Get-Content -Encoding UTF8 -LiteralPath $llpMarker |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            ForEach-Object { [System.IO.Path]::GetFullPath($_) }
    )
}

function Get-LlpCommandExecutablePath {
    param([string]$CommandLine)
    if ([string]::IsNullOrWhiteSpace($CommandLine)) { return '' }
    $llpCommand = $CommandLine.Trim()
    if ($llpCommand.StartsWith('"', [System.StringComparison]::Ordinal)) {
        $llpClosingQuote = $llpCommand.IndexOf('"', 1)
        if ($llpClosingQuote -gt 1) { return [System.IO.Path]::GetFullPath($llpCommand.Substring(1, $llpClosingQuote - 1)) }
    }
    try {
        $llpUnquoted = [System.IO.Path]::GetFullPath($llpCommand)
        if (Test-Path -LiteralPath $llpUnquoted -PathType Leaf) { return $llpUnquoted }
    }
    catch {}
    return ''
}

function Get-LlpIsolatedProbeProcesses {
    param([string[]]$ExactPaths = @(), [switch]$AnyWithinTempRoot)
    $llpTempBoundary = [System.IO.Path]::GetFullPath($llpTempRoot).TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    $llpExpected = @($ExactPaths | ForEach-Object {
        $llpPath = [System.IO.Path]::GetFullPath($_)
        Assert-True ($llpPath.StartsWith($llpTempBoundary, [System.StringComparison]::OrdinalIgnoreCase)) "Refusing to inspect a probe process outside the isolated temp root: $llpPath."
        $llpPath
    })
    $llpNativeProcessIds = @()
    if ($AnyWithinTempRoot) {
        $llpNativeProcessIds = @([LightLaunchpadSpotlightProbeNative]::FindProcessIdsWithinDirectory([System.IO.Path]::GetFullPath($llpTempRoot)))
    }
    else {
        foreach ($llpExpectedPath in $llpExpected) {
            $llpNativeProcessIds += @([LightLaunchpadSpotlightProbeNative]::FindProcessIdsByExecutablePath($llpExpectedPath))
        }
    }
    $llpNativeProcessIds = @($llpNativeProcessIds | Sort-Object -Unique)
    $llpMatches = @()
    foreach ($llpCandidate in @(Get-CimInstance Win32_Process)) {
        $llpExecutablePath = ''
        if (-not [string]::IsNullOrWhiteSpace([string]$llpCandidate.ExecutablePath)) {
            try { $llpExecutablePath = [System.IO.Path]::GetFullPath([string]$llpCandidate.ExecutablePath) } catch {}
        }
        $llpCommandPath = Get-LlpCommandExecutablePath ([string]$llpCandidate.CommandLine)
        $llpInsideTemp = ($llpExecutablePath.StartsWith($llpTempBoundary, [System.StringComparison]::OrdinalIgnoreCase) -or
            $llpCommandPath.StartsWith($llpTempBoundary, [System.StringComparison]::OrdinalIgnoreCase))
        $llpExact = @($llpExpected | Where-Object {
            $_.Equals($llpExecutablePath, [System.StringComparison]::OrdinalIgnoreCase) -or
            $_.Equals($llpCommandPath, [System.StringComparison]::OrdinalIgnoreCase)
        }).Count -gt 0
        $llpNativeExact = $llpNativeProcessIds -contains [uint32]$llpCandidate.ProcessId
        if ($llpNativeExact -or ($AnyWithinTempRoot -and $llpInsideTemp) -or ($llpExpected.Count -gt 0 -and $llpExact)) {
            $llpMatches += $llpCandidate
        }
    }
    foreach ($llpNativeProcessId in $llpNativeProcessIds) {
        if (@($llpMatches | Where-Object { [uint32]$_.ProcessId -eq [uint32]$llpNativeProcessId }).Count -eq 0) {
            $llpMatches += [pscustomobject]@{ ProcessId = $llpNativeProcessId; ExecutablePath = ''; CommandLine = '<Toolhelp exact-path match>' }
        }
    }
    return @($llpMatches)
}

function Wait-LlpIsolatedProbeProcessesExited {
    param([string[]]$ExactPaths = @(), [switch]$AnyWithinTempRoot, [string]$Stage)
    $llpDeadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $llpMatches = @(Get-LlpIsolatedProbeProcesses -ExactPaths $ExactPaths -AnyWithinTempRoot:$AnyWithinTempRoot)
        if ($llpMatches.Count -eq 0) { return }
        Start-Sleep -Milliseconds 25
    } while ([DateTime]::UtcNow -lt $llpDeadline)
    $llpHeld = @($llpMatches | ForEach-Object { "PID=$($_.ProcessId) EXE='$($_.ExecutablePath)' CMD='$($_.CommandLine)'" }) -join '; '
    throw "Timed out waiting for isolated launch-probe processes after $Stage; retained temp root without deletion. $llpHeld"
}

function Wait-LlpMarkerProcessesExited {
    param([string]$Stage)
    $llpPaths = @(Get-LlpMarkerPaths)
    if ($llpPaths.Count -gt 0) {
        Wait-LlpIsolatedProbeProcessesExited -ExactPaths $llpPaths -Stage $Stage
    }
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

function Save-LlpMonitorEvidence {
    param([string]$Path, [object]$MonitorBounds)
    [void][LightLaunchpadSpotlightProbeNative]::DwmFlush()
    $llpWidth = [int]$MonitorBounds.right - [int]$MonitorBounds.left
    $llpHeight = [int]$MonitorBounds.bottom - [int]$MonitorBounds.top
    Assert-True ($llpWidth -gt 0 -and $llpHeight -gt 0) 'DISPLAY1 evidence bounds were empty.'
    $llpBitmap = [System.Drawing.Bitmap]::new($llpWidth, $llpHeight, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $llpGraphics = [System.Drawing.Graphics]::FromImage($llpBitmap)
    $llpDestination = $llpGraphics.GetHdc()
    $llpScreen = [LightLaunchpadSpotlightProbeNative]::GetDC([IntPtr]::Zero)
    try {
        Assert-True ($llpScreen -ne [IntPtr]::Zero) 'Could not acquire the real desktop DC for DISPLAY1 evidence.'
        Assert-True ([LightLaunchpadSpotlightProbeNative]::BitBlt(
            $llpDestination, 0, 0, $llpWidth, $llpHeight,
            $llpScreen, [int]$MonitorBounds.left, [int]$MonitorBounds.top, 0x40CC0020)) 'BitBlt could not capture real DISPLAY1 pixels.'
    }
    finally {
        if ($llpScreen -ne [IntPtr]::Zero) { [void][LightLaunchpadSpotlightProbeNative]::ReleaseDC([IntPtr]::Zero, $llpScreen) }
        $llpGraphics.ReleaseHdc($llpDestination)
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
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
public static class LightLaunchpadSpotlightProbeNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct PROCESSENTRY32 {
        public uint dwSize;
        public uint cntUsage;
        public uint th32ProcessID;
        public IntPtr th32DefaultHeapID;
        public uint th32ModuleID;
        public uint cntThreads;
        public uint th32ParentProcessID;
        public int pcPriClassBase;
        public uint dwFlags;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
        public string szExeFile;
    }
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
    public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr dpiContext);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr CreateWindowExW(uint extendedStyle, string className, string title, uint style, int x, int y, int width, int height, IntPtr parent, IntPtr menu, IntPtr instance, IntPtr parameter);
    [DllImport("user32.dll")]
    public static extern bool DestroyWindow(IntPtr hwnd);
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();
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
    [DllImport("user32.dll")]
    private static extern void keybd_event(byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);
    [DllImport("user32.dll")]
    public static extern IntPtr GetDlgItem(IntPtr hwnd, int controlId);
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
    public static extern IntPtr GetWindowLongPtr(IntPtr hwnd, int index);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr CreateToolhelp32Snapshot(uint flags, uint processId);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool Process32FirstW(IntPtr snapshot, ref PROCESSENTRY32 entry);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool Process32NextW(IntPtr snapshot, ref PROCESSENTRY32 entry);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr OpenProcess(uint access, bool inheritHandle, uint processId);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool QueryFullProcessImageNameW(IntPtr process, uint flags, StringBuilder path, ref uint size);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr handle);

    private static uint[] FindProcessIds(Func<string, bool> matches) {
        const uint SnapshotProcesses = 0x00000002;
        const uint QueryLimitedInformation = 0x00001000;
        IntPtr snapshot = CreateToolhelp32Snapshot(SnapshotProcesses, 0);
        if (snapshot == new IntPtr(-1)) return new uint[0];
        var found = new List<uint>();
        try {
            var entry = new PROCESSENTRY32();
            entry.dwSize = (uint)Marshal.SizeOf(typeof(PROCESSENTRY32));
            if (!Process32FirstW(snapshot, ref entry)) return found.ToArray();
            do {
                IntPtr process = OpenProcess(QueryLimitedInformation, false, entry.th32ProcessID);
                if (process == IntPtr.Zero) continue;
                try {
                    var path = new StringBuilder(32768);
                    uint size = (uint)path.Capacity;
                    if (QueryFullProcessImageNameW(process, 0, path, ref size)) {
                        string normalized = Path.GetFullPath(path.ToString());
                        if (matches(normalized)) found.Add(entry.th32ProcessID);
                    }
                }
                finally { CloseHandle(process); }
            } while (Process32NextW(snapshot, ref entry));
        }
        finally { CloseHandle(snapshot); }
        return found.ToArray();
    }

    public static uint[] FindProcessIdsByExecutablePath(string expectedPath) {
        string expected = Path.GetFullPath(expectedPath);
        return FindProcessIds(path => string.Equals(path, expected, StringComparison.OrdinalIgnoreCase));
    }

    public static uint[] FindProcessIdsWithinDirectory(string directory) {
        string boundary = Path.GetFullPath(directory).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        return FindProcessIds(path => path.StartsWith(boundary, StringComparison.OrdinalIgnoreCase));
    }

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
    public static void PressAlt9() {
        const uint KeyUp = 0x0002;
        keybd_event(0x12, 0, 0, UIntPtr.Zero);
        keybd_event(0x39, 0, 0, UIntPtr.Zero);
        keybd_event(0x39, 0, KeyUp, UIntPtr.Zero);
        keybd_event(0x12, 0, KeyUp, UIntPtr.Zero);
    }
    public static IntPtr CreateAuxiliaryWindow(int x, int y, int width, int height) {
        const uint ToolWindow = 0x00000080;
        const uint OverlappedWindowVisible = 0x10CF0000;
        return CreateWindowExW(ToolWindow, "STATIC", "LightLaunchpad DISPLAY1 focus probe", OverlappedWindowVisible, x, y, width, height, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
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
    Assert-True (
        [LightLaunchpadSpotlightProbeNative]::SetThreadDpiAwarenessContext([IntPtr]::new(-4)) -ne [IntPtr]::Zero
    ) 'Could not make DISPLAY1 evidence coordinates per-monitor DPI aware.'

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
        Hotkey = 'Alt+9'
        StartWithWindows = $false
        IconSize = 'Medium'
        ViewMode = 'InlineRegions'
        DisplayMode = $(if ($SelfCaptureTransition) { 'Launchpad' } else { 'Spotlight' })
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
        $llpArguments = @('--spotlight-flow-probe-file', $llpProbePath, '--spotlight-flow-ready-file', $llpReadyPath, '--diagnostics-log-file', $llpLogPath, '--test-monitor-device', $llpTestMonitorDevice)
        if (-not $OrdinaryPresentationEvidence) {
            $llpArguments += '--spotlight-ownership-probe'
        }
        if ($MaterialFailure -ne 'None') {
            $llpArguments += @('--material-force-failure', $MaterialFailure)
        }
        if ($SelfCaptureTransition) {
            $llpArguments += @(
                '--material-force-failure', 'initialization',
                '--spotlight-force-self-capture-evidence',
                '--adaptive-test-high-contrast', '0',
                '--adaptive-test-transparency', '1'
            )
        }
        if ($TransparentTransitionFailure) {
            $llpArguments += '--spotlight-force-transparent-transition-failure'
        }
        if ($HideSceneRace) {
            $llpArguments += @('--spotlight-force-transparent-transition-failure', '--spotlight-force-hide-scene-race')
        }
        if ($LatestHideRace -ne 'None') {
            $llpLatestHideMode = if ($LatestHideRace -eq 'ShowHide') { 'show-hide' } else { 'submit-hide' }
            $llpArguments += @('--spotlight-force-transparent-transition-failure', '--spotlight-force-latest-hide-race', $llpLatestHideMode)
        }
        if ($ComposeHideLifetime) {
            $llpArguments += '--spotlight-force-compose-hide-lifetime'
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
    Assert-Equal $(if ($SelfCaptureTransition) { 'Launchpad' } else { 'Spotlight' }) ([string]$llpReady.displayMode) 'Spotlight probe loaded the wrong initial display mode.'
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
    if ($SelfCaptureTransition) {
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0111, [UIntPtr]::new([uint64]1004), [IntPtr]::Zero)
        $llpSettingsWindow = [IntPtr]::Zero
        Wait-LlpCondition { $script:llpSettingsWindow = [LightLaunchpadSpotlightProbeNative]::FindOtherVisibleWindowForProcess([uint32]$llpProcess.Id, $llpWindow); $script:llpSettingsWindow -ne [IntPtr]::Zero } 5000 'Visible Launchpad did not open Settings for the self-capture transition.'
        $llpSettingsRect = [LightLaunchpadSpotlightProbeNative+RECT]::new()
        Assert-True ([LightLaunchpadSpotlightProbeNative]::GetWindowRect($llpSettingsWindow, [ref]$llpSettingsRect)) 'Could not inspect self-capture Settings geometry.'
        Assert-True (
            $llpSettingsRect.Left -ge [int]$llpMonitorReady.monitorBounds.left -and
            $llpSettingsRect.Top -ge [int]$llpMonitorReady.monitorBounds.top -and
            $llpSettingsRect.Right -le [int]$llpMonitorReady.monitorBounds.right -and
            $llpSettingsRect.Bottom -le [int]$llpMonitorReady.monitorBounds.bottom
        ) 'The self-capture Settings window escaped DISPLAY1.'
        $llpDisplayModeCombo = [LightLaunchpadSpotlightProbeNative]::GetDlgItem($llpSettingsWindow, 3004)
        Assert-Equal 1 ([int64][LightLaunchpadSpotlightProbeNative]::SendMessage($llpDisplayModeCombo, 0x014E, [UIntPtr]::new([uint64]1), [IntPtr]::Zero)) 'Could not select Spotlight for the self-capture transition.'
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpSettingsWindow, 0x0111, [UIntPtr]::new([uint64]3012), [IntPtr]::Zero)
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x804B, [UIntPtr]::Zero, [IntPtr]::Zero)
        $llpSelfCaptureApplyReady = Get-LlpReadyState
        $llpSelfCaptureAppliedSettings = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $llpDataDirectory 'settings.json') | ConvertFrom-Json
        Write-Output "METRIC SpotlightSelfCaptureApply readyMode=$([string]$llpSelfCaptureApplyReady.displayMode) persistedMode=$([string]$llpSelfCaptureAppliedSettings.DisplayMode) solidSurface=$([bool]$llpSelfCaptureApplyReady.solidSurface) evidenceActive=$([bool]$llpSelfCaptureApplyReady.spotlightSelfCaptureEvidenceActive) evidenceCaptured=$([bool]$llpSelfCaptureApplyReady.spotlightSelfCaptureEvidenceCaptured) settingsVisible=$([LightLaunchpadSpotlightProbeNative]::IsWindowVisible($llpSettingsWindow)) mainVisible=$([LightLaunchpadSpotlightProbeNative]::IsWindowVisible($llpWindow))"
        Wait-LlpCondition {
            [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x804B, [UIntPtr]::Zero, [IntPtr]::Zero)
            $script:llpSelfCaptureReady = Get-LlpReadyState
            $null -ne $script:llpSelfCaptureReady -and
                [bool]$script:llpSelfCaptureReady.spotlightSelfCaptureEvidenceCaptured -and
                [string]$script:llpSelfCaptureReady.displayMode -eq 'Spotlight'
        } 10000 'Launchpad-to-Spotlight did not publish self-capture evidence.'
        Write-Output "METRIC SpotlightSelfCaptureMaterial state=$([string]$llpSelfCaptureReady.materialState) path=$([string]$llpSelfCaptureReady.materialPath) failure=$([string]$llpSelfCaptureReady.materialFailure)"
        Assert-Equal 'fallback' ([string]$llpSelfCaptureReady.materialState) 'The self-capture transition did not retain the forced fallback state.'
        Assert-Equal 'fallback' ([string]$llpSelfCaptureReady.materialPath) 'The self-capture transition did not retain direct fallback ownership.'
        Assert-Equal 'ForcedForTest' ([string]$llpSelfCaptureReady.materialFailure) 'The self-capture transition did not retain its forced initialization failure.'
        $llpSelfCaptureWindowRect = [LightLaunchpadSpotlightProbeNative+RECT]::new()
        Assert-True ([LightLaunchpadSpotlightProbeNative]::GetWindowRect($llpWindow, [ref]$llpSelfCaptureWindowRect)) 'Could not inspect the self-capture fallback window.'
        Assert-True (
            $llpSelfCaptureWindowRect.Left -ge [int]$llpSelfCaptureReady.monitorBounds.left -and
            $llpSelfCaptureWindowRect.Top -ge [int]$llpSelfCaptureReady.monitorBounds.top -and
            $llpSelfCaptureWindowRect.Right -le [int]$llpSelfCaptureReady.monitorBounds.right -and
            $llpSelfCaptureWindowRect.Bottom -le [int]$llpSelfCaptureReady.monitorBounds.bottom
        ) 'The self-capture fallback escaped DISPLAY1.'
        $llpEvidenceRoot = if ([string]::IsNullOrWhiteSpace($EvidenceDirectory)) { $llpTempRoot } else { [System.IO.Path]::GetFullPath($EvidenceDirectory) }
        New-Item -ItemType Directory -Force -Path $llpEvidenceRoot | Out-Null
        $llpSelfCaptureScreenPath = Join-Path $llpEvidenceRoot '00-launchpad-settings-to-spotlight-fallback.png'
        Write-Output "METRIC SpotlightSelfCapturePixels main=$([int]$llpSelfCaptureReady.spotlightSelfCaptureMainMarkerPixels) settings=$([int]$llpSelfCaptureReady.spotlightSelfCaptureSettingsMarkerPixels) settingsWindowExists=$([bool]$llpSelfCaptureReady.settingsWindowExists)"
        Assert-Equal 0 ([int]$llpSelfCaptureReady.spotlightSelfCaptureMainMarkerPixels) 'The Spotlight fallback captured pixels owned by the visible Launchpad window.'
        Assert-Equal 0 ([int]$llpSelfCaptureReady.spotlightSelfCaptureSettingsMarkerPixels) 'The Spotlight fallback captured pixels owned by the visible Settings window.'
        Assert-Equal $false ([LightLaunchpadSpotlightProbeNative]::IsWindowVisible($llpSettingsWindow)) 'Settings remained visible across the exclusion capture boundary.'
        Assert-Equal $false ([bool]$llpSelfCaptureReady.settingsWindowExists) 'The applied Settings window remained alive and suppressed Spotlight blur-close.'
        $llpSelfCaptureOwnershipPixels = Save-LlpScreenEvidence $llpWindow $llpSelfCaptureScreenPath $llpSelfCaptureReady.materialOwnershipProbeBounds
        Assert-True ($llpSelfCaptureOwnershipPixels -ge 240) 'The forced self-capture fallback did not retain complete fallback ownership pixels.'
        Assert-Equal 0 ([int]$llpSelfCaptureReady.spotlightFallbackRegionHeaderCount) 'The self-capture fallback leaked Launchpad region structure.'
        Assert-Equal 0 ([int]$llpSelfCaptureReady.spotlightFallbackFavoriteBadgeCount) 'The self-capture fallback leaked Launchpad favorite badges.'
        Assert-True (@($llpSelfCaptureReady.spotlightFallbackPresentedItemStableIds).Count -gt 1) 'The self-capture fallback did not paint its Spotlight item payload.'
        Assert-Equal $false ([bool]$llpSelfCaptureReady.materialDirectLayerTransparent) 'The forced self-capture fallback did not own its direct pixels.'
        $llpAuxiliaryX = [int]$llpSelfCaptureReady.monitorBounds.right - 300
        $llpAuxiliaryY = [int]$llpSelfCaptureReady.monitorBounds.top + 48
        $llpAuxiliaryWindow = [LightLaunchpadSpotlightProbeNative]::CreateAuxiliaryWindow($llpAuxiliaryX, $llpAuxiliaryY, 240, 160)
        Assert-True ($llpAuxiliaryWindow -ne [IntPtr]::Zero) 'Could not create the DISPLAY1 blur-close focus target.'
        try {
            $llpAuxiliaryRect = [LightLaunchpadSpotlightProbeNative+RECT]::new()
            Assert-True ([LightLaunchpadSpotlightProbeNative]::GetWindowRect($llpAuxiliaryWindow, [ref]$llpAuxiliaryRect)) 'Could not inspect the DISPLAY1 blur-close focus target.'
            Assert-True (
                $llpAuxiliaryRect.Left -ge [int]$llpSelfCaptureReady.monitorBounds.left -and
                $llpAuxiliaryRect.Top -ge [int]$llpSelfCaptureReady.monitorBounds.top -and
                $llpAuxiliaryRect.Right -le [int]$llpSelfCaptureReady.monitorBounds.right -and
                $llpAuxiliaryRect.Bottom -le [int]$llpSelfCaptureReady.monitorBounds.bottom
            ) 'The blur-close focus target escaped DISPLAY1.'
            [void][LightLaunchpadSpotlightProbeNative]::SetForegroundWindow($llpAuxiliaryWindow)
            if ([LightLaunchpadSpotlightProbeNative]::GetForegroundWindow() -ne $llpAuxiliaryWindow) {
                [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0006, [UIntPtr]::Zero, $llpAuxiliaryWindow)
            }
            Wait-LlpCondition { -not [LightLaunchpadSpotlightProbeNative]::IsWindowVisible($llpWindow) } 3000 'Spotlight did not hide after real foreground deactivation.'
        }
        finally {
            [void][LightLaunchpadSpotlightProbeNative]::DestroyWindow($llpAuxiliaryWindow)
        }
        Show-LlpWithConfiguredHotkey $llpWindow
        $llpReopenRect = [LightLaunchpadSpotlightProbeNative+RECT]::new()
        Assert-True ([LightLaunchpadSpotlightProbeNative]::GetWindowRect($llpWindow, [ref]$llpReopenRect)) 'Could not inspect Spotlight after blur-close reopen.'
        Assert-True (
            $llpReopenRect.Left -ge [int]$llpSelfCaptureReady.monitorBounds.left -and
            $llpReopenRect.Top -ge [int]$llpSelfCaptureReady.monitorBounds.top -and
            $llpReopenRect.Right -le [int]$llpSelfCaptureReady.monitorBounds.right -and
            $llpReopenRect.Bottom -le [int]$llpSelfCaptureReady.monitorBounds.bottom
        ) 'Reopened Spotlight escaped DISPLAY1 after blur-close.'
        Write-Output "METRIC SpotlightSelfCaptureTransition monitor=$([string]$llpSelfCaptureReady.monitorDevice) mainMarkerPixels=$([int]$llpSelfCaptureReady.spotlightSelfCaptureMainMarkerPixels) settingsMarkerPixels=$([int]$llpSelfCaptureReady.spotlightSelfCaptureSettingsMarkerPixels) ownershipPixels=$llpSelfCaptureOwnershipPixels settingsVisible=$([LightLaunchpadSpotlightProbeNative]::IsWindowVisible($llpSettingsWindow)) blurClosed=True reopened=True screenshot='$llpSelfCaptureScreenPath'"
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
        if (-not $llpProcess.WaitForExit(10000)) { throw 'Self-capture transition probe did not exit.' }
        Assert-Equal 0 $llpProcess.ExitCode 'Self-capture transition probe exit code mismatch.'
        Write-Output 'PASS SpotlightFlow_LaunchpadToSpotlightFallbackExcludesOwnedWindows'
        return
    }
    $llpElement = [System.Windows.Automation.AutomationElement]::FromHandle($llpWindow)
    $llpValueObject = $null
    Assert-True ($llpElement.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$llpValueObject)) 'Spotlight search Value pattern is unavailable.'
    $llpValue = [System.Windows.Automation.ValuePattern]$llpValueObject
    Assert-Equal $true $llpElement.Current.HasKeyboardFocus 'Search was not the only focus after Alt+D.'
    if ($ComposeHideLifetime) {
        Wait-LlpCondition {
            [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x804B, [UIntPtr]::Zero, [IntPtr]::Zero)
            $script:llpComposeHideReady = Get-LlpReadyState
            $null -ne $script:llpComposeHideReady -and
                [bool]$script:llpComposeHideReady.materialComposeHideLifetimeInjected -and
                [bool]$script:llpComposeHideReady.materialComposeLifetimeProbeCompleted -and
                [string]$script:llpComposeHideReady.materialState -eq 'hidden'
        } 10000 'The deterministic Compose/Hide lifetime probe did not reach Hidden.'
        Assert-Equal $llpTestMonitorDevice ([string]$llpComposeHideReady.monitorDevice) 'The Compose/Hide lifetime probe escaped DISPLAY1.'
        Assert-Equal $false ([bool]$llpComposeHideReady.materialComposeSceneExpiredDuringHide) 'Hide released the immutable scene while renderer Step still held its raw scene view.'
        Write-Output "METRIC SpotlightComposeHideLifetime monitor=$([string]$llpComposeHideReady.monitorDevice) entered=$([bool]$llpComposeHideReady.materialComposeLifetimeProbeEntered) completed=$([bool]$llpComposeHideReady.materialComposeLifetimeProbeCompleted) sceneExpired=$([bool]$llpComposeHideReady.materialComposeSceneExpiredDuringHide) state=$([string]$llpComposeHideReady.materialState)"
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
        if (-not $llpProcess.WaitForExit(10000)) { throw 'Compose/Hide lifetime probe did not exit.' }
        Assert-Equal 0 $llpProcess.ExitCode 'Compose/Hide lifetime probe exit code mismatch.'
        Write-Output 'PASS SpotlightFlow_HideKeepsSceneAliveUntilComposeReturns'
        return
    }
    if ($HideSceneRace) {
        try {
            Wait-LlpCondition {
                [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x804B, [UIntPtr]::Zero, [IntPtr]::Zero)
                $script:llpHideSceneRaceReady = Get-LlpReadyState
                $null -ne $script:llpHideSceneRaceReady -and
                    [bool]$script:llpHideSceneRaceReady.materialHideSceneRaceInjected -and
                    [uint64]$script:llpHideSceneRaceReady.materialCommittedActivation -eq [uint64]$script:llpHideSceneRaceReady.materialHideSceneRaceActivation -and
                    [uint64]$script:llpHideSceneRaceReady.materialPresentedSceneGeneration -ge [uint64]$script:llpHideSceneRaceReady.materialHideSceneRaceGeneration -and
                    [string]$script:llpHideSceneRaceReady.materialState -eq 'live'
            } 10000 'A Submit+Show interleaved with renderer Hide cleanup lost the new immutable scene or activation.'
        }
        catch {
            $llpHideSceneRaceLast = Get-LlpReadyState
            Write-Output "DIAGNOSTIC SpotlightHideSceneRace state=$([string]$llpHideSceneRaceLast.materialState) path=$([string]$llpHideSceneRaceLast.materialPath) requested=$([uint64]$llpHideSceneRaceLast.materialRequestedActivation)/$([uint64]$llpHideSceneRaceLast.materialRequestedSceneGeneration) committed=$([uint64]$llpHideSceneRaceLast.materialCommittedActivation)/$([uint64]$llpHideSceneRaceLast.materialPresentedSceneGeneration) injected=$([bool]$llpHideSceneRaceLast.materialHideSceneRaceInjected) race=$([uint64]$llpHideSceneRaceLast.materialHideSceneRaceActivation)/$([uint64]$llpHideSceneRaceLast.materialHideSceneRaceGeneration)"
            throw
        }
        Assert-Equal $llpTestMonitorDevice ([string]$llpHideSceneRaceReady.monitorDevice) 'The Hide scene-race probe escaped DISPLAY1.'
        Write-Output "METRIC SpotlightHideSceneRace monitor=$([string]$llpHideSceneRaceReady.monitorDevice) activation=$([uint64]$llpHideSceneRaceReady.materialHideSceneRaceActivation) generation=$([uint64]$llpHideSceneRaceReady.materialHideSceneRaceGeneration) committed=$([uint64]$llpHideSceneRaceReady.materialCommittedActivation) presentedGeneration=$([uint64]$llpHideSceneRaceReady.materialPresentedSceneGeneration)"
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
        if (-not $llpProcess.WaitForExit(10000)) { throw 'Hide scene-race probe did not exit.' }
        Assert-Equal 0 $llpProcess.ExitCode 'Hide scene-race probe exit code mismatch.'
        Write-Output 'PASS SpotlightFlow_HideCleanupPreservesInterleavedSceneAndShow'
        return
    }
    if ($LatestHideRace -ne 'None') {
        try {
            Wait-LlpCondition {
                [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x804B, [UIntPtr]::Zero, [IntPtr]::Zero)
                $script:llpLatestHideRaceReady = Get-LlpReadyState
                $null -ne $script:llpLatestHideRaceReady -and
                    [bool]$script:llpLatestHideRaceReady.materialLatestHideRaceInjected -and
                    [string]$script:llpLatestHideRaceReady.materialState -ne 'stopping'
            } 10000 'The latest Hide race did not leave its deterministic Stopping barrier.'
        }
        catch {
            $llpLatestHideRaceLast = Get-LlpReadyState
            Write-Output "DIAGNOSTIC SpotlightLatestHideRace mode=$LatestHideRace state=$([string]$llpLatestHideRaceLast.materialState) injected=$([bool]$llpLatestHideRaceLast.materialLatestHideRaceInjected) queued=$([uint64]$llpLatestHideRaceLast.materialQueuedSceneGeneration) current=$([uint64]$llpLatestHideRaceLast.materialCurrentSceneGeneration) showQueued=$([bool]$llpLatestHideRaceLast.materialShowQueued) rebuildQueued=$([bool]$llpLatestHideRaceLast.materialRebuildQueued) requested=$([uint64]$llpLatestHideRaceLast.materialRequestedActivation) committed=$([uint64]$llpLatestHideRaceLast.materialCommittedActivation)"
            throw
        }
        Assert-Equal $llpTestMonitorDevice ([string]$llpLatestHideRaceReady.monitorDevice) 'The latest Hide race escaped DISPLAY1.'
        Assert-Equal 'hidden' ([string]$llpLatestHideRaceReady.materialState) 'A Show/Rebuild queued before the latest Hide restarted after teardown.'
        Assert-Equal 0 ([uint64]$llpLatestHideRaceReady.materialQueuedSceneGeneration) 'A scene submitted before the latest Hide remained queued after teardown.'
        Assert-Equal 0 ([uint64]$llpLatestHideRaceReady.materialCurrentSceneGeneration) 'A scene submitted before the latest Hide crossed the hidden boundary.'
        Assert-Equal $false ([bool]$llpLatestHideRaceReady.materialShowQueued) 'The latest Hide did not cancel a queued Show.'
        Assert-Equal $false ([bool]$llpLatestHideRaceReady.materialRebuildQueued) 'The latest Hide did not cancel a queued Rebuild.'
        if ($LatestHideRace -eq 'ShowHide') {
            Assert-True ([uint64]$llpLatestHideRaceReady.materialCommittedActivation -ne [uint64]$llpLatestHideRaceReady.materialLatestHideRaceActivation) 'The Show queued before the latest Hide committed after teardown.'
        }
        Write-Output "METRIC SpotlightLatestHideRace mode=$LatestHideRace monitor=$([string]$llpLatestHideRaceReady.monitorDevice) state=$([string]$llpLatestHideRaceReady.materialState) queuedGeneration=$([uint64]$llpLatestHideRaceReady.materialQueuedSceneGeneration) currentGeneration=$([uint64]$llpLatestHideRaceReady.materialCurrentSceneGeneration) showQueued=$([bool]$llpLatestHideRaceReady.materialShowQueued) activation=$([uint64]$llpLatestHideRaceReady.materialLatestHideRaceActivation) committed=$([uint64]$llpLatestHideRaceReady.materialCommittedActivation)"
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
        if (-not $llpProcess.WaitForExit(10000)) { throw 'Latest Hide race probe did not exit.' }
        Assert-Equal 0 $llpProcess.ExitCode 'Latest Hide race probe exit code mismatch.'
        Write-Output "PASS SpotlightFlow_LatestHideWinsOver$LatestHideRace"
        return
    }
    if ($TransparentTransitionFailure) {
        Wait-LlpCondition {
            $script:llpTransitionFailureReady = Get-LlpReadyState
            $null -ne $script:llpTransitionFailureReady -and
                [bool]$script:llpTransitionFailureReady.materialTransparentTransitionFailureInjected -and
                [bool]$script:llpTransitionFailureReady.materialTransitionFallbackRestoredAfterHidden
        } 10000 'The forced transparent-layer transition failure did not restore fallback after renderer Hidden.'
        $llpEvidenceRoot = if ([string]::IsNullOrWhiteSpace($EvidenceDirectory)) { $llpTempRoot } else { [System.IO.Path]::GetFullPath($EvidenceDirectory) }
        New-Item -ItemType Directory -Force -Path $llpEvidenceRoot | Out-Null
        $llpTransitionFailureScreenPath = Join-Path $llpEvidenceRoot '00-transition-failure-hidden-fallback.png'
        $llpTransitionFailureMarkerPixels = Save-LlpScreenEvidence $llpWindow $llpTransitionFailureScreenPath $llpTransitionFailureReady.materialOwnershipProbeBounds
        Assert-True ($llpTransitionFailureMarkerPixels -ge 240) 'The real desktop did not show the restored opaque fallback after Hidden acknowledgement.'
        Assert-Equal 'high-fidelity' ([string]$llpTransitionFailureReady.materialTransitionFailurePath) 'The forced direct-layer failure did not occur after a committed DComp frame.'
        Assert-Equal 'live' ([string]$llpTransitionFailureReady.materialTransitionFailureState) 'The forced direct-layer failure did not observe the active committed renderer.'
        Assert-Equal $false ([bool]$llpTransitionFailureReady.materialTransitionDirectTransparentAfterHandler) 'The realistic pre-clear failure did not leave the cached opaque direct layer intact.'
        Assert-Equal $true ([bool]$llpTransitionFailureReady.materialTransitionWindowHiddenAfterFailureHandler) 'The pre-clear failure handler left opaque direct fallback visible over live DComp.'
        Assert-True ([int]$llpTransitionFailureReady.materialTransitionPreHiddenStatusCallbacks -ge 1) 'The deterministic failure flow did not observe a renderer Stopping callback before Hidden.'
        Assert-Equal $false ([bool]$llpTransitionFailureReady.materialTransitionStoppingStatusCallbackSynchronous) 'The renderer delivered its Stopping evidence through a synchronous cross-thread window call.'
        Assert-Equal $false ([bool]$llpTransitionFailureReady.materialTransitionDirectTransparentAfterPreHiddenCallbacks) 'The realistic pre-clear failure unexpectedly discarded cached opaque direct fallback before Hidden.'
        Assert-Equal $true ([bool]$llpTransitionFailureReady.materialTransitionWindowHiddenDuringPreHiddenCallbacks) 'The HWND became visible with opaque fallback before renderer Hidden.'
        Assert-Equal 0 ([int]$llpTransitionFailureReady.materialTransitionPreHiddenVisibleOpaqueOverlapCallbacks) 'A pre-Hidden callback observed visible opaque fallback over live DComp.'
        Assert-Equal 0 ([int]$llpTransitionFailureReady.materialTransitionPreHiddenOpaqueFallbackPaints) 'A pre-Hidden renderer callback painted opaque fallback over the committed DComp frame.'
        Assert-Equal 'hidden' ([string]$llpTransitionFailureReady.materialTransitionFallbackRestoreState) 'Opaque fallback was not restored from the renderer Hidden acknowledgement.'
        Assert-Equal 1 ([int]$llpTransitionFailureReady.materialTransitionHiddenFallbackRestorePaints) 'The Hidden acknowledgement did not restore cached fallback exactly once.'
        Assert-Equal $true ([bool]$llpTransitionFailureReady.materialTransitionWindowVisibleAfterHiddenRestore) 'The current Spotlight activation was not re-shown after the Hidden fallback restore.'
        Assert-Equal 'hidden' ([string]$llpTransitionFailureReady.materialState) 'The failed transparent transition did not finish renderer teardown.'
        Assert-Equal $false ([bool]$llpTransitionFailureReady.materialDirectLayerTransparent) 'The final hidden fallback did not own opaque direct pixels.'
        Write-Output "METRIC SpotlightTransparentTransitionFailure monitor=$([string]$llpTransitionFailureReady.monitorDevice) failureState=$([string]$llpTransitionFailureReady.materialTransitionFailureState) directAfterHandler=$([bool]$llpTransitionFailureReady.materialTransitionDirectTransparentAfterHandler) preHiddenCallbacks=$([int]$llpTransitionFailureReady.materialTransitionPreHiddenStatusCallbacks) preHiddenDirectTransparent=$([bool]$llpTransitionFailureReady.materialTransitionDirectTransparentAfterPreHiddenCallbacks) preHiddenOpaquePaints=$([int]$llpTransitionFailureReady.materialTransitionPreHiddenOpaqueFallbackPaints) restoreState=$([string]$llpTransitionFailureReady.materialTransitionFallbackRestoreState) hiddenRestorePaints=$([int]$llpTransitionFailureReady.materialTransitionHiddenFallbackRestorePaints) markerPixels=$llpTransitionFailureMarkerPixels screenshot='$llpTransitionFailureScreenPath'"
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
        if (-not $llpProcess.WaitForExit(10000)) { throw 'Transparent-transition failure probe did not exit.' }
        Assert-Equal 0 $llpProcess.ExitCode 'Transparent-transition failure probe exit code mismatch.'
        Write-Output 'PASS SpotlightFlow_TransparentTransitionFailureWaitsForHiddenBeforeFallbackRestore'
        return
    }
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
    if ($OrdinaryPresentationEvidence) {
        Assert-Equal 0 ([uint64]$llpMaterialReady.materialHostIdentityCopies) 'Ordinary host copied per-item identities into its immutable scene.'
        Assert-Equal 0 ([uint64]$llpMaterialReady.materialPresentationEvidenceItemCopies) 'Ordinary renderer copied per-item presentation evidence on a live frame.'
        Assert-Equal 0 @($llpMaterialReady.materialPresentedItemStableIds).Count 'Ordinary renderer published per-item stable-ID evidence.'
        Assert-Equal 0 @($llpMaterialReady.materialPresentedItemSourcePaths).Count 'Ordinary renderer published per-item source-path evidence.'
        Write-Output "METRIC SpotlightOrdinaryPresentationEvidence monitor=$([string]$llpMaterialReady.monitorDevice) path=$([string]$llpMaterialReady.materialPath) presented=$([uint64]$llpMaterialReady.materialPresentedFrames) hostCopies=$([uint64]$llpMaterialReady.materialHostIdentityCopies) rendererCopies=$([uint64]$llpMaterialReady.materialPresentationEvidenceItemCopies) stableIds=$(@($llpMaterialReady.materialPresentedItemStableIds).Count) sourcePaths=$(@($llpMaterialReady.materialPresentedItemSourcePaths).Count)"
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
        if (-not $llpProcess.WaitForExit(10000)) { throw 'Ordinary presentation-evidence probe did not exit.' }
        Assert-Equal 0 $llpProcess.ExitCode 'Ordinary presentation-evidence probe exit code mismatch.'
        Write-Output 'PASS SpotlightFlow_OrdinaryRendererSkipsPerItemPresentationEvidence'
        return
    }
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
    $llpModeTransitionOpenOffset = 0
    if ($MaterialFailure -eq 'None') {
        $llpTransitionActivation = [uint64]$llpMaterialReady.materialRequestedActivation
        $llpTransitionSceneGeneration = [uint64]$llpMaterialReady.materialSceneGeneration

        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0111, [UIntPtr]::new([uint64]1004), [IntPtr]::Zero)
        $llpSettingsWindow = [IntPtr]::Zero
        Wait-LlpCondition { $script:llpSettingsWindow = [LightLaunchpadSpotlightProbeNative]::FindOtherVisibleWindowForProcess([uint32]$llpProcess.Id, $llpWindow); $script:llpSettingsWindow -ne [IntPtr]::Zero } 5000 'Visible Spotlight did not open the real Settings window for a mode transition.'
        $llpSettingsRect = [LightLaunchpadSpotlightProbeNative+RECT]::new()
        Assert-True ([LightLaunchpadSpotlightProbeNative]::GetWindowRect($llpSettingsWindow, [ref]$llpSettingsRect)) 'Could not inspect the real Settings transaction window.'
        Assert-True (
            $llpSettingsRect.Left -ge [int]$llpMonitorReady.monitorBounds.left -and
            $llpSettingsRect.Top -ge [int]$llpMonitorReady.monitorBounds.top -and
            $llpSettingsRect.Right -le [int]$llpMonitorReady.monitorBounds.right -and
            $llpSettingsRect.Bottom -le [int]$llpMonitorReady.monitorBounds.bottom
        ) 'The visible Settings transaction window escaped DISPLAY1.'
        $llpDisplayModeCombo = [LightLaunchpadSpotlightProbeNative]::GetDlgItem($llpSettingsWindow, 3004)
        Assert-True ($llpDisplayModeCombo -ne [IntPtr]::Zero) 'Could not discover the Settings DisplayMode control.'
        Assert-Equal 0 ([int64][LightLaunchpadSpotlightProbeNative]::SendMessage($llpDisplayModeCombo, 0x014E, [UIntPtr]::new([uint64]0), [IntPtr]::Zero)) 'Could not select Launchpad in the real Settings combo.'
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpSettingsWindow, 0x0111, [UIntPtr]::new([uint64]3012), [IntPtr]::Zero)
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x804B, [UIntPtr]::Zero, [IntPtr]::Zero)
        $llpSettingsAfterLaunchpadApply = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $llpDataDirectory 'settings.json') | ConvertFrom-Json
        $llpReadyAfterLaunchpadApply = Get-LlpReadyState
        $llpLaunchpadStyle = [int64][LightLaunchpadSpotlightProbeNative]::GetWindowLongPtr($llpWindow, -20)
        $llpLaunchpadTransitionScreenPath = Join-Path $llpEvidenceRoot '03-launchpad-settings-transition.png'
        Save-LlpMonitorEvidence $llpLaunchpadTransitionScreenPath $llpReadyAfterLaunchpadApply.monitorBounds
        Assert-Equal 0 ($llpLaunchpadStyle -band 0x00080000) 'Visible Spotlight-to-Launchpad Settings apply retained WS_EX_LAYERED.'
        Assert-Equal 'Alt+9' ([string]$llpSettingsAfterLaunchpadApply.Hotkey) 'The real Settings transaction did not persist its isolated Alt+9 hotkey.'
        Assert-Equal $false ([bool]$llpReadyAfterLaunchpadApply.launchpadModeTransitionLayeredAfterApply) 'The in-process mode-transition boundary retained the per-pixel Spotlight surface.'
        Assert-Equal $true ([bool]$llpReadyAfterLaunchpadApply.launchpadModeTransitionPaintedBeforeApplyReturn) 'Visible Spotlight-to-Launchpad Settings apply returned before current Launchpad pixels were painted.'
        Assert-True (@('stopping', 'hidden') -contains [string]$llpReadyAfterLaunchpadApply.materialState) 'The renderer was not transitioning or Hidden at the immediate Launchpad frame boundary.'
        Write-Output "METRIC SpotlightSettingsApply persistedMode=$([string]$llpSettingsAfterLaunchpadApply.DisplayMode) persistedHotkey=$([string]$llpSettingsAfterLaunchpadApply.Hotkey) readyMode=$([string]$llpReadyAfterLaunchpadApply.displayMode) materialState=$([string]$llpReadyAfterLaunchpadApply.materialState) materialPath=$([string]$llpReadyAfterLaunchpadApply.materialPath) layered=$([bool]($llpLaunchpadStyle -band 0x00080000)) paintedBeforeReturn=$([bool]$llpReadyAfterLaunchpadApply.launchpadModeTransitionPaintedBeforeApplyReturn) screenshot='$llpLaunchpadTransitionScreenPath'"
        Wait-LlpCondition {
            [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x804B, [UIntPtr]::Zero, [IntPtr]::Zero)
            $script:llpLaunchpadTransition = Get-LlpReadyState
            $null -ne $script:llpLaunchpadTransition -and
                [string]$script:llpLaunchpadTransition.displayMode -eq 'Launchpad' -and
                [string]$script:llpLaunchpadTransition.materialState -eq 'hidden'
        } 5000 'Visible Spotlight-to-Launchpad Settings apply did not tear the renderer down to Hidden.'
        $llpLaunchpadRect = [LightLaunchpadSpotlightProbeNative+RECT]::new()
        Assert-True ([LightLaunchpadSpotlightProbeNative]::GetWindowRect($llpWindow, [ref]$llpLaunchpadRect)) 'Could not inspect Launchpad after the visible Settings transition.'
        Assert-True (
            $llpLaunchpadRect.Left -ge [int]$llpLaunchpadTransition.monitorBounds.left -and
            $llpLaunchpadRect.Top -ge [int]$llpLaunchpadTransition.monitorBounds.top -and
            $llpLaunchpadRect.Right -le [int]$llpLaunchpadTransition.monitorBounds.right -and
            $llpLaunchpadRect.Bottom -le [int]$llpLaunchpadTransition.monitorBounds.bottom
        ) 'The transitioned Launchpad window escaped DISPLAY1.'
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpSettingsWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
        Wait-LlpCondition { -not [LightLaunchpadSpotlightProbeNative]::IsWindow($llpSettingsWindow) } 3000 'The Launchpad transition Settings window did not close.'

        [LightLaunchpadSpotlightProbeNative]::PressAlt9()
        Wait-LlpCondition { -not [LightLaunchpadSpotlightProbeNative]::IsWindowVisible($llpWindow) } 3000 'The isolated Alt+9 Settings registration did not trigger and hide Launchpad through Windows.'
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x804B, [UIntPtr]::Zero, [IntPtr]::Zero)
        $llpHiddenLaunchpadTransition = Get-LlpReadyState
        Assert-Equal 'hidden' ([string]$llpHiddenLaunchpadTransition.materialState) 'Hiding transitioned Launchpad restarted or retained material capture.'

        [LightLaunchpadSpotlightProbeNative]::PressAlt9()
        Wait-LlpCondition { [LightLaunchpadSpotlightProbeNative]::IsWindowVisible($llpWindow) } 3000 'The isolated Alt+9 Settings registration did not trigger and reopen Launchpad through Windows.'
        $llpModeTransitionOpenOffset = 1
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpWindow, 0x0111, [UIntPtr]::new([uint64]1004), [IntPtr]::Zero)
        $llpSettingsWindow = [IntPtr]::Zero
        Wait-LlpCondition { $script:llpSettingsWindow = [LightLaunchpadSpotlightProbeNative]::FindOtherVisibleWindowForProcess([uint32]$llpProcess.Id, $llpWindow); $script:llpSettingsWindow -ne [IntPtr]::Zero } 5000 'Visible Launchpad did not open Settings for the reverse mode transition.'
        $llpSettingsRect = [LightLaunchpadSpotlightProbeNative+RECT]::new()
        Assert-True ([LightLaunchpadSpotlightProbeNative]::GetWindowRect($llpSettingsWindow, [ref]$llpSettingsRect)) 'Could not inspect the reverse Settings transaction window.'
        Assert-True (
            $llpSettingsRect.Left -ge [int]$llpMonitorReady.monitorBounds.left -and
            $llpSettingsRect.Top -ge [int]$llpMonitorReady.monitorBounds.top -and
            $llpSettingsRect.Right -le [int]$llpMonitorReady.monitorBounds.right -and
            $llpSettingsRect.Bottom -le [int]$llpMonitorReady.monitorBounds.bottom
        ) 'The reverse Settings transaction window escaped DISPLAY1.'
        $llpDisplayModeCombo = [LightLaunchpadSpotlightProbeNative]::GetDlgItem($llpSettingsWindow, 3004)
        Assert-Equal 1 ([int64][LightLaunchpadSpotlightProbeNative]::SendMessage($llpDisplayModeCombo, 0x014E, [UIntPtr]::new([uint64]1), [IntPtr]::Zero)) 'Could not select Spotlight in the real Settings combo.'
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpSettingsWindow, 0x0111, [UIntPtr]::new([uint64]3012), [IntPtr]::Zero)
        Wait-LlpCondition {
            $script:llpSpotlightTransition = Get-LlpReadyState
            $null -ne $script:llpSpotlightTransition -and
                [string]$script:llpSpotlightTransition.displayMode -eq 'Spotlight' -and
                [string]$script:llpSpotlightTransition.materialPath -eq 'high-fidelity' -and
                [string]$script:llpSpotlightTransition.materialState -eq 'live' -and
                [uint64]$script:llpSpotlightTransition.materialRequestedActivation -gt $llpTransitionActivation -and
                [uint64]$script:llpSpotlightTransition.materialCommittedActivation -eq [uint64]$script:llpSpotlightTransition.materialRequestedActivation -and
                [uint64]$script:llpSpotlightTransition.materialSceneGeneration -gt $llpTransitionSceneGeneration -and
                [uint64]$script:llpSpotlightTransition.materialPresentedSceneGeneration -ge [uint64]$script:llpSpotlightTransition.materialRequestedSceneGeneration
        } 10000 'Visible Launchpad-to-Spotlight Settings apply did not start and commit a current high-fidelity activation.'
        [void][LightLaunchpadSpotlightProbeNative]::SendMessage($llpSettingsWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
        Wait-LlpCondition { -not [LightLaunchpadSpotlightProbeNative]::IsWindow($llpSettingsWindow) } 3000 'The Spotlight transition Settings window did not close.'
        Write-Output "METRIC SpotlightSettingsModeTransition monitor=$([string]$llpSpotlightTransition.monitorDevice) launchpadState=$([string]$llpLaunchpadTransition.materialState) hiddenState=$([string]$llpHiddenLaunchpadTransition.materialState) spotlightPath=$([string]$llpSpotlightTransition.materialPath) activation=$([uint64]$llpSpotlightTransition.materialRequestedActivation) presentedGeneration=$([uint64]$llpSpotlightTransition.materialPresentedSceneGeneration)"
    }
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
        Wait-LlpMarkerProcessesExited 'catalog-refresh Enter launch'
        $llpMarkerNames = @(Get-LlpMarkerNames)
        Assert-Equal 'Refresh Renamed.exe' $llpMarkerNames[0] 'Enter launch did not agree with the first immutable item displayed after catalog refresh.'
        Wait-LlpCondition { -not [LightLaunchpadSpotlightProbeNative]::IsWindowVisible($llpWindow) } 3000 'Spotlight did not hide after the catalog-refresh launch.'
        Show-LlpWithConfiguredHotkey $llpWindow
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
    Wait-LlpMarkerProcessesExited 'search Enter launch'
    $llpMarkerNames = @(Get-LlpMarkerNames)
    Assert-Equal 'Beta.exe' $llpMarkerNames[$llpCatalogLaunchOffset] 'Search Enter launched the wrong result.'
    Wait-LlpCondition { -not [LightLaunchpadSpotlightProbeNative]::IsWindowVisible($llpWindow) } 3000 'Spotlight did not hide after the first launch.'

    Show-LlpWithConfiguredHotkey $llpWindow
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
    Wait-LlpMarkerProcessesExited 'result-navigation Enter launch'
    $llpMarkerNames = @(Get-LlpMarkerNames)
    Assert-True ($llpMarkerNames[1 + $llpCatalogLaunchOffset] -in @('Flow Alpha.exe', 'Flow Beta.exe')) 'Down/Right/Enter launched a result outside the Flow query.'

    $llpLatestReady = Get-LlpReadyState
    $llpCatalogGeneration = [uint64]$llpLatestReady.catalogGeneration
    Show-LlpWithConfiguredHotkey $llpWindow
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

    Show-LlpWithConfiguredHotkey $llpWindow
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

    Show-LlpWithConfiguredHotkey $llpWindow
    Send-LlpText $llpWindow 'Flow'
    Send-LlpKey $llpWindow 0x28
    Send-LlpText $llpWindow 'X'
    Assert-Equal $true $llpElement.Current.HasKeyboardFocus 'Printable input from results did not return to search.'
    Assert-Equal 'FlowX' $llpValue.Current.Value 'Printable input from results was lost.'
    Send-LlpKey $llpWindow 0x1B
    Send-LlpKey $llpWindow 0x1B

    Show-LlpWithConfiguredHotkey $llpWindow
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

    Show-LlpWithConfiguredHotkey $llpWindow
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
    Assert-Equal 'Alt+9' ([string]$llpProbe.configuredHotkey) 'The isolated real hotkey setting changed after its Settings transaction.'
    Assert-Equal (7 + $llpCatalogLaunchOffset + $llpModeTransitionOpenOffset) ([int]$llpProbe.hotkeyMessages) 'Not all configured hotkey messages opened the requested surface.'
    Assert-Equal (7 + $llpCatalogLaunchOffset + $llpModeTransitionOpenOffset) ([int]$llpProbe.searchFocusedOnOpen) 'Spotlight did not open with search as the sole focus every time.'
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
    Wait-LlpIsolatedProbeProcessesExited -AnyWithinTempRoot -Stage 'SpotlightFlow final cleanup'
    $llpResolvedTemp = [System.IO.Path]::GetFullPath($llpTempRoot)
    $llpExpectedTempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    if ($llpResolvedTemp.StartsWith($llpExpectedTempRoot, [System.StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $llpResolvedTemp)) {
        $llpTempItem = Get-Item -LiteralPath $llpResolvedTemp -Force
        if (($llpTempItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Refusing to clean reparse-point test output $llpResolvedTemp." }
        Remove-Item -LiteralPath $llpResolvedTemp -Recurse -Force
    }
}
