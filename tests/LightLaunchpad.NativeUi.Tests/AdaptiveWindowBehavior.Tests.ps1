param(
    [string]$EvidenceDirectory = ''
)

$ErrorActionPreference = 'Stop'

$llpRepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$llpTempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("lightlaunchpad-adaptive-window-" + [System.Guid]::NewGuid().ToString('N'))
$llpOutputDirectory = Join-Path $llpTempRoot 'development'
$llpTarget = Join-Path $llpTempRoot 'LaunchProbeTarget.exe'
$llpProcesses = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()

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

function Save-LlpWindowEvidence {
    param([IntPtr]$Window, [string]$Path)
    [void][LightLaunchpadAdaptiveProbeNative]::RedrawWindow($Window, [IntPtr]::Zero, [IntPtr]::Zero, 0x0181)
    [void][LightLaunchpadAdaptiveProbeNative]::UpdateWindow($Window)
    $llpRect = [LightLaunchpadAdaptiveProbeNative+RECT]::new()
    Assert-True ([LightLaunchpadAdaptiveProbeNative]::GetWindowRect($Window, [ref]$llpRect)) 'Could not read adaptive window geometry.'
    $llpWidth = $llpRect.Right - $llpRect.Left
    $llpHeight = $llpRect.Bottom - $llpRect.Top
    $llpBitmap = [System.Drawing.Bitmap]::new($llpWidth, $llpHeight, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $llpGraphics = [System.Drawing.Graphics]::FromImage($llpBitmap)
    $llpDc = $llpGraphics.GetHdc()
    try {
        Assert-True ([LightLaunchpadAdaptiveProbeNative]::PrintWindow($Window, $llpDc, 2)) 'PrintWindow could not capture adaptive window evidence.'
    }
    finally {
        $llpGraphics.ReleaseHdc($llpDc)
        $llpGraphics.Dispose()
    }
    try { $llpBitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png) }
    finally { $llpBitmap.Dispose() }
}

function Invoke-LlpAdaptiveCase {
    param(
        [string]$Name,
        [int]$Dpi,
        [int]$TextScale,
        [int]$HighContrast,
        [int]$Transparency,
        [int]$Animations,
        [bool]$UseOverrides
    )

    $llpCaseRoot = Join-Path $llpTempRoot $Name
    $llpAppData = Join-Path $llpCaseRoot 'appdata'
    $llpLocalAppData = Join-Path $llpCaseRoot 'localappdata'
    $llpUserProfile = Join-Path $llpCaseRoot 'profile'
    $llpLaunchpad = Join-Path $llpCaseRoot 'Launchpad'
    $llpDataDirectory = Join-Path $llpAppData 'LightLaunchpad'
    $llpReadyPath = Join-Path $llpCaseRoot 'ready.json'
    $llpProbePath = Join-Path $llpCaseRoot 'probe.json'
    $llpMarker = Join-Path $llpCaseRoot 'launched.txt'
    $llpLogPath = Join-Path $llpCaseRoot 'adaptive.log'
    New-Item -ItemType Directory -Force -Path $llpDataDirectory, $llpLocalAppData, $llpUserProfile, $llpLaunchpad | Out-Null
    Copy-Item -LiteralPath $llpTarget -Destination (Join-Path $llpLaunchpad 'Adaptive Target.exe')
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

    $llpArguments = [System.Collections.Generic.List[string]]::new()
    foreach ($llpArgument in @('--spotlight-flow-probe-file', $llpProbePath, '--spotlight-flow-ready-file', $llpReadyPath, '--diagnostics-log-file', $llpLogPath)) { $llpArguments.Add($llpArgument) }
    if ($UseOverrides) {
        foreach ($llpArgument in @(
            '--adaptive-test-dpi', [string]$Dpi,
            '--adaptive-test-text-scale', [string]$TextScale,
            '--adaptive-test-high-contrast', [string]$HighContrast,
            '--adaptive-test-transparency', [string]$Transparency,
            '--adaptive-test-animations', [string]$Animations
        )) { $llpArguments.Add($llpArgument) }
    }

    $llpPriorAppData = $env:APPDATA
    $llpPriorLocalAppData = $env:LOCALAPPDATA
    $llpPriorUserProfile = $env:USERPROFILE
    $llpPriorMarker = $env:LLP_FLOW_MARKER
    try {
        $env:APPDATA = $llpAppData
        $env:LOCALAPPDATA = $llpLocalAppData
        $env:USERPROFILE = $llpUserProfile
        $env:LLP_FLOW_MARKER = $llpMarker
        $llpProcess = Start-Process -FilePath (Join-Path $llpOutputDirectory 'LightLaunchpad.NativeUi.exe') -ArgumentList ([string[]]$llpArguments) -PassThru
        $llpProcesses.Add($llpProcess)
    }
    finally {
        $env:APPDATA = $llpPriorAppData
        $env:LOCALAPPDATA = $llpPriorLocalAppData
        $env:USERPROFILE = $llpPriorUserProfile
        $env:LLP_FLOW_MARKER = $llpPriorMarker
    }

    Wait-LlpCondition { Test-Path -LiteralPath $llpReadyPath } 12000 "$Name catalog did not become ready."
    $llpWindow = [LightLaunchpadAdaptiveProbeNative]::FindWindowForProcess([uint32]$llpProcess.Id)
    Assert-True ($llpWindow -ne [IntPtr]::Zero) "$Name real NativeUi window was not found."
    [void][LightLaunchpadAdaptiveProbeNative]::SendMessage($llpWindow, 0x0312, [UIntPtr]::new([uint64]0x4C55), [IntPtr]::Zero)
    Assert-True ([LightLaunchpadAdaptiveProbeNative]::IsWindowVisible($llpWindow)) "$Name Spotlight did not become visible."
    foreach ($llpCharacter in 'Adaptive'.ToCharArray()) {
        [void][LightLaunchpadAdaptiveProbeNative]::SendMessage($llpWindow, 0x0102, [UIntPtr]::new([uint64][char]$llpCharacter), [IntPtr]::Zero)
    }
    [void][LightLaunchpadAdaptiveProbeNative]::UpdateWindow($llpWindow)
    [void][LightLaunchpadAdaptiveProbeNative]::SendMessage($llpWindow, 0x804B, [UIntPtr]::Zero, [IntPtr]::Zero)
    Wait-LlpCondition {
        try {
            $script:llpReady = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpReadyPath | ConvertFrom-Json
            return [int]$script:llpReady.hitCount -gt 0
        }
        catch { return $false }
    } 5000 "$Name did not publish painted scene geometry."

    $llpReady = $script:llpReady
    $llpElement = [System.Windows.Automation.AutomationElement]::FromHandle($llpWindow)
    $llpValueObject = $null
    Assert-True ($llpElement.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$llpValueObject)) "$Name search Value pattern is unavailable."
    Assert-Equal $true $llpElement.Current.HasKeyboardFocus "$Name search was not keyboard focused."
    [LightLaunchpadAdaptiveProbeNative]::BeginAlertHook([uint32]$llpProcess.Id)
    try {
        [void][LightLaunchpadAdaptiveProbeNative]::SendMessage($llpWindow, 0x804B, [UIntPtr]::new([uint64]1), [IntPtr]::Zero)
        Wait-LlpCondition { [LightLaunchpadAdaptiveProbeNative]::PumpMessages(); return [LightLaunchpadAdaptiveProbeNative]::AlertCount -gt 0 } 3000 "$Name organization feedback did not raise an accessibility alert."
        Assert-Equal $true $llpElement.Current.HasKeyboardFocus "$Name status announcement stole search focus."
    }
    finally { [LightLaunchpadAdaptiveProbeNative]::EndAlertHook() }
    if ($UseOverrides) {
        Assert-Equal $Dpi ([int]$llpReady.effectiveDpi) "$Name did not use the requested DPI matrix value."
        Assert-Equal $TextScale ([int]$llpReady.textScalePercent) "$Name did not use the requested text scale."
        Assert-Equal ([bool]$HighContrast) ([bool]$llpReady.highContrast) "$Name high-contrast policy mismatch."
        Assert-Equal (-not [bool]$Animations) ([bool]$llpReady.reducedMotion) "$Name reduced-motion policy mismatch."
        Assert-Equal ([bool]($HighContrast -or -not $Transparency)) ([bool]$llpReady.solidSurface) "$Name solid-surface policy mismatch."
        $llpAccessibleTileDip = [Math]::Max(128, 56 + 24 + [Math]::Ceiling(38 * $TextScale / 100.0) + 8)
        Assert-Equal ([int]($llpAccessibleTileDip * $Dpi / 96)) ([int]$llpReady.scene.tileSizePx) "$Name accessible tile DIP conversion mismatch."
    }
    else {
        Assert-True ([int]$llpReady.effectiveDpi -ge 96) 'Actual monitor DPI was invalid.'
    }
    Assert-Equal 'foreground' ([string]$llpReady.monitorSource) "$Name did not select the foreground monitor."
    $llpWidth = [int]$llpReady.windowRect.right - [int]$llpReady.windowRect.left
    $llpHeight = [int]$llpReady.windowRect.bottom - [int]$llpReady.windowRect.top
    Assert-True ($llpWidth -gt 0 -and $llpHeight -gt 0) "$Name reported an empty window."
    Assert-True ([int]$llpReady.scene.search.left -ge 0 -and [int]$llpReady.scene.search.right -le $llpWidth) "$Name search surface clipped horizontally."
    Assert-True ([int]$llpReady.scene.search.bottom -lt [int]$llpReady.scene.contentClip.top) "$Name search and content clip overlap."
    Assert-True ([int]$llpReady.firstHit.top -ge [int]$llpReady.scene.contentClip.top) "$Name first rendered item escaped the content clip."
    Assert-True ([int]$llpReady.firstHit.right -le [int]$llpReady.scene.contentClip.right) "$Name first rendered item clipped horizontally."

    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
        $llpEvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceDirectory)
        New-Item -ItemType Directory -Force -Path $llpEvidenceRoot | Out-Null
        Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot "$Name.png")
    }

    if (-not $UseOverrides) {
        $llpOriginalDpi = [int]$llpReady.effectiveDpi
        $llpOriginalRect = [LightLaunchpadAdaptiveProbeNative+RECT]::new()
        $llpOriginalRect.Left = [int]$llpReady.windowRect.left
        $llpOriginalRect.Top = [int]$llpReady.windowRect.top
        $llpOriginalRect.Right = $llpOriginalRect.Left + 1600
        $llpOriginalRect.Bottom = $llpOriginalRect.Top + 1200
        [void][LightLaunchpadAdaptiveProbeNative]::SendDpiChanged($llpWindow, 192, $llpOriginalRect)
        [void][LightLaunchpadAdaptiveProbeNative]::SendMessage($llpWindow, 0x804B, [UIntPtr]::Zero, [IntPtr]::Zero)
        $llpDpiChanged = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpReadyPath | ConvertFrom-Json
        Assert-Equal 192 ([int]$llpDpiChanged.effectiveDpi) 'WM_DPICHANGED did not update production scene DPI.'
        Assert-Equal 1600 ([int]$llpDpiChanged.windowRect.right - [int]$llpDpiChanged.windowRect.left) 'WM_DPICHANGED did not apply the suggested width.'
        Assert-Equal 1200 ([int]$llpDpiChanged.windowRect.bottom - [int]$llpDpiChanged.windowRect.top) 'WM_DPICHANGED did not apply the suggested height.'
        Assert-Equal 256 ([int]$llpDpiChanged.scene.tileSizePx) 'WM_DPICHANGED did not rebuild DIP scene geometry.'

        $llpRestoreRect = [LightLaunchpadAdaptiveProbeNative+RECT]::new()
        $llpRestoreRect.Left = [int]$llpReady.windowRect.left
        $llpRestoreRect.Top = [int]$llpReady.windowRect.top
        $llpRestoreRect.Right = [int]$llpReady.windowRect.right
        $llpRestoreRect.Bottom = [int]$llpReady.windowRect.bottom
        [void][LightLaunchpadAdaptiveProbeNative]::SendDpiChanged($llpWindow, [uint32]$llpOriginalDpi, $llpRestoreRect)
        [void][LightLaunchpadAdaptiveProbeNative]::SetForegroundWindow($llpWindow)
        Wait-LlpCondition { [LightLaunchpadAdaptiveProbeNative]::GetForegroundWindow() -eq $llpWindow } 3000 'Could not establish the NativeUi window as foreground before WM_DISPLAYCHANGE.'
        $llpCursor = [LightLaunchpadAdaptiveProbeNative+POINT]::new()
        Assert-True ([LightLaunchpadAdaptiveProbeNative]::GetCursorPos([ref]$llpCursor)) 'Could not read the cursor position before WM_DISPLAYCHANGE.'
        $llpCursorMonitor = [LightLaunchpadAdaptiveProbeNative]::MonitorFromPoint($llpCursor, [LightLaunchpadAdaptiveProbeNative]::MonitorDefaultToPrimary)
        Assert-True ($llpCursorMonitor -ne [IntPtr]::Zero) 'Could not resolve the cursor monitor before WM_DISPLAYCHANGE.'
        $llpCursorMonitorInfo = [LightLaunchpadAdaptiveProbeNative+MONITORINFO]::new()
        $llpCursorMonitorInfo.cbSize = [System.Runtime.InteropServices.Marshal]::SizeOf([type][LightLaunchpadAdaptiveProbeNative+MONITORINFO])
        Assert-True ([LightLaunchpadAdaptiveProbeNative]::GetMonitorInfoW($llpCursorMonitor, [ref]$llpCursorMonitorInfo)) 'Could not read the cursor monitor work area before WM_DISPLAYCHANGE.'
        Assert-Equal $llpWindow ([LightLaunchpadAdaptiveProbeNative]::GetForegroundWindow()) 'NativeUi lost foreground before WM_DISPLAYCHANGE.'
        [void][LightLaunchpadAdaptiveProbeNative]::SendMessage($llpWindow, 0x007E, [UIntPtr]::Zero, [IntPtr]::Zero)
        [void][LightLaunchpadAdaptiveProbeNative]::SendMessage($llpWindow, 0x804B, [UIntPtr]::Zero, [IntPtr]::Zero)
        $llpReady = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpReadyPath | ConvertFrom-Json
        Assert-Equal $llpOriginalDpi ([int]$llpReady.effectiveDpi) 'Display-change reposition did not return to the monitor DPI.'
        Assert-Equal 'cursor' ([string]$llpReady.monitorSource) 'Display-change reposition did not use the cursor monitor fallback.'

        $llpDisplayRect = [LightLaunchpadAdaptiveProbeNative+RECT]::new()
        Assert-True ([LightLaunchpadAdaptiveProbeNative]::GetWindowRect($llpWindow, [ref]$llpDisplayRect)) 'Could not read the NativeUi window placement after WM_DISPLAYCHANGE.'
        $llpDisplayWidth = $llpDisplayRect.Right - $llpDisplayRect.Left
        $llpDisplayHeight = $llpDisplayRect.Bottom - $llpDisplayRect.Top
        $llpExpectedLeft = $llpCursorMonitorInfo.rcWork.Left + [int][Math]::Truncate((($llpCursorMonitorInfo.rcWork.Right - $llpCursorMonitorInfo.rcWork.Left) - $llpDisplayWidth) / 2.0)
        $llpExpectedTop = $llpCursorMonitorInfo.rcWork.Top + [int][Math]::Truncate((($llpCursorMonitorInfo.rcWork.Bottom - $llpCursorMonitorInfo.rcWork.Top) - $llpDisplayHeight) / 2.0)
        Assert-True ([Math]::Abs($llpDisplayRect.Left - $llpExpectedLeft) -le 1) 'WM_DISPLAYCHANGE did not horizontally center the NativeUi window in the cursor monitor work area.'
        Assert-True ([Math]::Abs($llpDisplayRect.Top - $llpExpectedTop) -le 1) 'WM_DISPLAYCHANGE did not vertically center the NativeUi window in the cursor monitor work area.'
        Assert-True ($llpDisplayRect.Left -ge $llpCursorMonitorInfo.rcWork.Left -and $llpDisplayRect.Top -ge $llpCursorMonitorInfo.rcWork.Top -and $llpDisplayRect.Right -le $llpCursorMonitorInfo.rcWork.Right -and $llpDisplayRect.Bottom -le $llpCursorMonitorInfo.rcWork.Bottom) 'WM_DISPLAYCHANGE placed the NativeUi window outside the cursor monitor work area.'
    }

    $llpCenterX = ([int]$llpReady.firstHit.left + [int]$llpReady.firstHit.right) / 2
    $llpCenterY = ([int]$llpReady.firstHit.top + [int]$llpReady.firstHit.bottom) / 2
    $llpPoint = [IntPtr](($llpCenterY -shl 16) -bor ($llpCenterX -band 0xFFFF))
    [void][LightLaunchpadAdaptiveProbeNative]::SendMessage($llpWindow, 0x0201, [UIntPtr]::new([uint64]1), $llpPoint)
    [void][LightLaunchpadAdaptiveProbeNative]::SendMessage($llpWindow, 0x0202, [UIntPtr]::Zero, $llpPoint)
    Wait-LlpCondition { Test-Path -LiteralPath $llpMarker } 8000 "$Name shared draw/hit geometry did not launch the rendered target."
    Wait-LlpCondition { -not [LightLaunchpadAdaptiveProbeNative]::IsWindowVisible($llpWindow) } 3000 "$Name did not hide after launch."

    [void][LightLaunchpadAdaptiveProbeNative]::SendMessage($llpWindow, 0x0111, [UIntPtr]::new([uint64]1004), [IntPtr]::Zero)
    $llpSettingsWindow = [IntPtr]::Zero
    Wait-LlpCondition { $script:llpSettingsWindow = [LightLaunchpadAdaptiveProbeNative]::FindOtherVisibleWindowForProcess([uint32]$llpProcess.Id, $llpWindow); return $script:llpSettingsWindow -ne [IntPtr]::Zero } 5000 "$Name Settings window did not open."
    $llpSettingsWindow = $script:llpSettingsWindow
    $llpSettingsElement = [System.Windows.Automation.AutomationElement]::FromHandle($llpSettingsWindow)
    $llpSettingsChildren = $llpSettingsElement.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
    Assert-True ($llpSettingsChildren.Count -ge 15) "$Name Settings controls were not exposed to UI Automation."
    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
        Start-Sleep -Milliseconds 200
        Save-LlpWindowEvidence $llpSettingsWindow (Join-Path $llpEvidenceRoot "$Name-settings-top.png")
        [void][LightLaunchpadAdaptiveProbeNative]::SendMessage($llpSettingsWindow, 0x0115, [UIntPtr]::new([uint64]7), [IntPtr]::Zero)
        Start-Sleep -Milliseconds 200
        Save-LlpWindowEvidence $llpSettingsWindow (Join-Path $llpEvidenceRoot "$Name-settings-bottom.png")
    }
    [void][LightLaunchpadAdaptiveProbeNative]::SendMessage($llpSettingsWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
    [void][LightLaunchpadAdaptiveProbeNative]::SendMessage($llpWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
    Assert-True ($llpProcess.WaitForExit(10000)) "$Name process did not exit."
    Assert-Equal 0 $llpProcess.ExitCode "$Name process exit code mismatch."
    Write-Output "PASS AdaptiveWindow_$Name dpi=$([int]$llpReady.effectiveDpi) textScale=$([int]$llpReady.textScalePercent) solid=$([bool]$llpReady.solidSurface) reducedMotion=$([bool]$llpReady.reducedMotion) size=${llpWidth}x${llpHeight}"
}

try {
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    Add-Type -AssemblyName System.Drawing
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class LightLaunchpadAdaptiveProbeNative {
    public const uint MonitorDefaultToPrimary = 1;
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct MONITORINFO { public uint cbSize; public RECT rcMonitor; public RECT rcWork; public uint dwFlags; }
    [StructLayout(LayoutKind.Sequential)] private struct MSG { public IntPtr hwnd; public uint message; public UIntPtr wParam; public IntPtr lParam; public uint time; public POINT pt; }
    private delegate bool EnumWindowsCallback(IntPtr hwnd, IntPtr state);
    private delegate void WinEventDelegate(IntPtr hook, uint eventType, IntPtr hwnd, int objectId, int childId, uint threadId, uint time);
    private static WinEventDelegate alertCallback;
    private static IntPtr alertHook;
    public static int AlertCount { get; private set; }
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr state);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll")] private static extern IntPtr SetWinEventHook(uint eventMin, uint eventMax, IntPtr module, WinEventDelegate callback, uint processId, uint threadId, uint flags);
    [DllImport("user32.dll")] private static extern bool UnhookWinEvent(IntPtr hook);
    [DllImport("user32.dll")] private static extern bool PeekMessage(out MSG message, IntPtr hwnd, uint min, uint max, uint remove);
    [DllImport("user32.dll")] private static extern bool TranslateMessage(ref MSG message);
    [DllImport("user32.dll")] private static extern IntPtr DispatchMessage(ref MSG message);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool UpdateWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool RedrawWindow(IntPtr hwnd, IntPtr updateRect, IntPtr updateRegion, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT point);
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromPoint(POINT point, uint flags);
    [DllImport("user32.dll", ExactSpelling = true)] public static extern bool GetMonitorInfoW(IntPtr monitor, ref MONITORINFO info);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr hwnd, uint message, UIntPtr wParam, IntPtr lParam);
    public static IntPtr SendDpiChanged(IntPtr hwnd, uint dpi, RECT suggested) {
        IntPtr memory = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(RECT)));
        try {
            Marshal.StructureToPtr(suggested, memory, false);
            ulong packed = ((ulong)dpi << 16) | dpi;
            return SendMessage(hwnd, 0x02E0, new UIntPtr(packed), memory);
        }
        finally { Marshal.FreeHGlobal(memory); }
    }
    public static IntPtr FindWindowForProcess(uint expectedProcessId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hwnd, state) => { uint processId; GetWindowThreadProcessId(hwnd, out processId); if (processId == expectedProcessId) { found = hwnd; return false; } return true; }, IntPtr.Zero);
        return found;
    }
    public static IntPtr FindOtherVisibleWindowForProcess(uint expectedProcessId, IntPtr excluded) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hwnd, state) => { uint processId; GetWindowThreadProcessId(hwnd, out processId); if (processId == expectedProcessId && hwnd != excluded && IsWindowVisible(hwnd)) { found = hwnd; return false; } return true; }, IntPtr.Zero);
        return found;
    }
    public static void BeginAlertHook(uint processId) {
        EndAlertHook();
        AlertCount = 0;
        alertCallback = (hook, eventType, hwnd, objectId, childId, threadId, time) => { AlertCount++; };
        alertHook = SetWinEventHook(0x0002, 0x0002, IntPtr.Zero, alertCallback, processId, 0, 0);
        if (alertHook == IntPtr.Zero) throw new InvalidOperationException("SetWinEventHook failed.");
    }
    public static void PumpMessages() {
        MSG message;
        while (PeekMessage(out message, IntPtr.Zero, 0, 0, 1)) { TranslateMessage(ref message); DispatchMessage(ref message); }
    }
    public static void EndAlertHook() {
        if (alertHook != IntPtr.Zero) { UnhookWinEvent(alertHook); alertHook = IntPtr.Zero; }
        alertCallback = null;
    }
}
'@

    New-Item -ItemType Directory -Force -Path $llpOutputDirectory | Out-Null
    $llpGpp = (where.exe g++ | Select-Object -First 1)
    if (-not $llpGpp) { throw 'g++ was not found.' }
    Push-Location $llpRepoRoot
    try {
        & $llpGpp 'tests\LightLaunchpad.NativeUi.Tests\LaunchProbeTarget.cpp' -std=c++17 -municode -mwindows -static -static-libgcc -static-libstdc++ -O2 -s -o $llpTarget
        if ($LASTEXITCODE -ne 0) { throw "Launch probe target compilation failed with exit code $LASTEXITCODE." }
    }
    finally { Pop-Location }
    & (Join-Path $llpRepoRoot 'tools\build-native-ui.ps1') -OutputDirectory $llpOutputDirectory -ProductVersion '1.11.0' -BuildCommit 'adaptivewindowtest' -BuildChannel Development | Out-Null

    Invoke-LlpAdaptiveCase -Name 'actual-monitor' -Dpi 0 -TextScale 0 -HighContrast 0 -Transparency 0 -Animations 0 -UseOverrides $false
    Invoke-LlpAdaptiveCase -Name 'dpi-100-text-100' -Dpi 96 -TextScale 100 -HighContrast 0 -Transparency 1 -Animations 1 -UseOverrides $true
    Invoke-LlpAdaptiveCase -Name 'dpi-150-text-150-solid-reduced-motion' -Dpi 144 -TextScale 150 -HighContrast 0 -Transparency 0 -Animations 0 -UseOverrides $true
    Invoke-LlpAdaptiveCase -Name 'dpi-200-text-200-high-contrast' -Dpi 192 -TextScale 200 -HighContrast 1 -Transparency 0 -Animations 0 -UseOverrides $true
    Write-Output '4/4 adaptive real-window cases passed'
}
finally {
    foreach ($llpProcess in $llpProcesses) {
        if ($llpProcess -and -not $llpProcess.HasExited) { Stop-Process -Id $llpProcess.Id -Force; $llpProcess.WaitForExit() }
    }
    $llpResolvedTemp = [System.IO.Path]::GetFullPath($llpTempRoot)
    $llpExpectedTempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    if ($llpResolvedTemp.StartsWith($llpExpectedTempRoot, [System.StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $llpResolvedTemp)) {
        $llpTempItem = Get-Item -LiteralPath $llpResolvedTemp -Force
        if (($llpTempItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Refusing to clean reparse-point test output $llpResolvedTemp." }
        Remove-Item -LiteralPath $llpResolvedTemp -Recurse -Force
    }
}
