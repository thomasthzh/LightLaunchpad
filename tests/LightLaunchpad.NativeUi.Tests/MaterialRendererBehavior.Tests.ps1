param(
    [ValidateSet('Lifecycle', 'Host', 'Recovery', 'Visual')]
    [string]$Mode = 'Lifecycle',

    [string]$EvidenceDirectory = ''
)

$ErrorActionPreference = 'Stop'

$llpRepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$llpTempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("lightlaunchpad-material-behavior-" + [System.Guid]::NewGuid().ToString('N'))
$llpOutputDirectory = Join-Path $llpTempRoot 'development'
$llpAppData = Join-Path $llpTempRoot 'appdata'
$llpLocalAppData = Join-Path $llpTempRoot 'localappdata'
$llpUserProfile = Join-Path $llpTempRoot 'profile'
$llpDataDirectory = Join-Path $llpAppData 'LightLaunchpad'
$llpLaunchpad = Join-Path $llpTempRoot 'Launchpad'
$llpProbePath = Join-Path $llpTempRoot 'material-probe.json'
$llpReadyPath = Join-Path $llpTempRoot 'material-ready.json'
$llpNormalProbePath = Join-Path $llpTempRoot 'material-normal-probe.json'
$llpNormalReadyPath = Join-Path $llpTempRoot 'material-normal-ready.json'
$llpLogPath = Join-Path $llpTempRoot 'material-startup.log'
$llpStableSource = Join-Path $llpTempRoot 'stable-source'
$llpStableOutput = Join-Path $llpTempRoot 'stable-output'
$llpProcess = $null
$llpTestMonitorDevice = '\\.\DISPLAY1'
$llpCapturedProcesses = @()
$llpRunKeyPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$llpRunBefore = $null
try { $llpRunBefore = (Get-ItemProperty -LiteralPath $llpRunKeyPath -Name LightLaunchpad -ErrorAction Stop).LightLaunchpad } catch {}
$llpProcessIdsBefore = @(Get-Process -Name 'LightLaunchpad.NativeUi' -ErrorAction SilentlyContinue | ForEach-Object { $_.Id } | Sort-Object)
$llpHistoricalTempBefore = @(Get-ChildItem -LiteralPath ([System.IO.Path]::GetTempPath()) -Directory -Filter 'lightlaunchpad-*' | Where-Object { $_.FullName -ne $llpTempRoot } | ForEach-Object { $_.FullName } | Sort-Object)

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Assert-Equal {
    param([object]$Expected, [object]$Actual, [string]$Message)
    if ($Expected -ne $Actual) { throw "$Message Expected '$Expected', actual '$Actual'." }
}

function Wait-LlpFile {
    param([string]$Path, [int]$Seconds = 8)
    $llpDeadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    while (-not (Test-Path -LiteralPath $Path) -and [DateTime]::UtcNow -lt $llpDeadline) {
        Start-Sleep -Milliseconds 25
    }
    Assert-True (Test-Path -LiteralPath $Path -PathType Leaf) "Timed out waiting for $Path."
}

function Assert-LlpDisplay1Containment {
    param([object]$State, [string]$Context)
    Assert-Equal $llpTestMonitorDevice ([string]$State.monitorDevice) "$Context was not fixed to DISPLAY1."
    Assert-True (
        [int]$State.windowRect.left -ge [int]$State.monitorBounds.left -and
        [int]$State.windowRect.top -ge [int]$State.monitorBounds.top -and
        [int]$State.windowRect.right -le [int]$State.monitorBounds.right -and
        [int]$State.windowRect.bottom -le [int]$State.monitorBounds.bottom
    ) "$Context escaped DISPLAY1."
}

if ($Mode -eq 'Visual') {
    Assert-True (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) 'Visual mode requires -EvidenceDirectory.'
    $llpEvidenceRoot = if ([System.IO.Path]::IsPathRooted($EvidenceDirectory)) {
        [System.IO.Path]::GetFullPath($EvidenceDirectory)
    }
    else {
        [System.IO.Path]::GetFullPath((Join-Path $llpRepoRoot $EvidenceDirectory))
    }
    Assert-True (Test-Path -LiteralPath $llpEvidenceRoot -PathType Container) "Visual evidence directory does not exist: $llpEvidenceRoot."
    $llpEvidenceItem = Get-Item -LiteralPath $llpEvidenceRoot -Force
    Assert-True (($llpEvidenceItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) 'Visual evidence directory must not be a reparse point.'
    $llpCases = @(
        'light-flat', 'light-gradient', 'light-low-contrast',
        'dark-flat', 'dark-gradient', 'dark-low-contrast',
        'text-grid', 'checker-fine', 'vertical-lines',
        'saturated-warm', 'saturated-cool', 'black-white-extreme',
        'supplemental-real-desktop'
    )
    $llpManifestPath = Join-Path $llpEvidenceRoot 'manifest.json'
    Assert-True (Test-Path -LiteralPath $llpManifestPath -PathType Leaf) 'Visual evidence manifest is missing.'
    $llpManifest = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpManifestPath | ConvertFrom-Json
    Assert-Equal 13 @($llpManifest.evidence).Count 'Visual evidence manifest case count mismatch.'
    Assert-Equal '\\.\DISPLAY1' ([string]$llpManifest.monitorDevice) 'Visual evidence was not fixed to DISPLAY1.'
    Assert-True (@($llpManifest.executableSha256s).Count -ge 1) 'Visual evidence manifest omitted its actual executable hashes.'
    foreach ($llpExecutableHash in @($llpManifest.executableSha256s)) {
        Assert-True ([string]$llpExecutableHash -match '^[0-9a-f]{64}$') 'Visual evidence manifest contains an invalid executable SHA256.'
    }
    Add-Type -AssemblyName System.Drawing
    foreach ($llpCase in $llpCases) {
        $llpPngPath = Join-Path $llpEvidenceRoot "$llpCase.png"
        $llpDiagnosticsPath = Join-Path $llpEvidenceRoot "$llpCase.diagnostics.json"
        Assert-True (Test-Path -LiteralPath $llpPngPath -PathType Leaf) "Visual evidence PNG missing: $llpCase."
        Assert-True (Test-Path -LiteralPath $llpDiagnosticsPath -PathType Leaf) "Visual evidence diagnostics missing: $llpCase."
        $llpDiagnostics = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpDiagnosticsPath | ConvertFrom-Json
        Assert-Equal $llpCase ([string]$llpDiagnostics.background) "Visual evidence background label mismatch for $llpCase."
        Assert-True ([uint32]$llpDiagnostics.nativeUiPid -gt 0) "Visual evidence NativeUi PID missing for $llpCase."
        Assert-True ([string]$llpDiagnostics.executableSha256 -match '^[0-9a-f]{64}$') "Visual evidence executable SHA256 missing or invalid for $llpCase."
        Assert-Equal 'high-fidelity' ([string]$llpDiagnostics.rendererPath) "Visual evidence renderer path mismatch for $llpCase."
        Assert-Equal $true ([bool]$llpDiagnostics.frozenEvidence) "Visual evidence was not frozen for $llpCase."
        Assert-Equal $true ([bool]$llpDiagnostics.terminal) "Visual evidence freeze was not terminal for $llpCase."
        Assert-Equal '\\.\DISPLAY1' ([string]$llpDiagnostics.monitorDevice) "Visual evidence escaped DISPLAY1 for $llpCase."
        $llpBitmap = [System.Drawing.Bitmap]::new($llpPngPath)
        try {
            $llpExpectedWidth = [int]$llpDiagnostics.viewport.right - [int]$llpDiagnostics.viewport.left
            $llpExpectedHeight = [int]$llpDiagnostics.viewport.bottom - [int]$llpDiagnostics.viewport.top
            Assert-Equal $llpExpectedWidth $llpBitmap.Width "Visual evidence width was not the exact physical window width for $llpCase."
            Assert-Equal $llpExpectedHeight $llpBitmap.Height "Visual evidence height was not the exact physical window height for $llpCase."
        }
        finally { $llpBitmap.Dispose() }
        $llpPixels = $llpDiagnostics.pixelAnalysis
        Assert-True ([double]$llpPixels.searchCenterTranslationDip -le 0.5) "Search center translated above 0.5 DIP for $llpCase."
        Assert-True ([double]$llpPixels.maximumEdgeDisplacementDip -le 1.5) "Search edge displacement exceeded 1.5 DIP for $llpCase."
        Assert-True ([double]$llpPixels.duplicatedComponentRatio -le 0.05) "Duplicated component area exceeded 5% for $llpCase."
        Assert-True ([double]$llpPixels.focusedTileScaleDeltaPercent -le 0.5) "Focused tile scale delta exceeded 0.5% for $llpCase."
        Assert-True ([double]$llpPixels.textContrastMinimum -ge 4.5) "Text contrast fell below 4.5:1 for $llpCase."
        Assert-True ([double]$llpPixels.focusBoundaryTargetMinimum -ge 3.0) "Focus boundary target fell below 3:1 for $llpCase."
        if ([bool]$llpDiagnostics.controlled) {
            Assert-True ([int]$llpPixels.outsideRoundedBoundaryMaxDelta255 -le 2) "Rounded-boundary material leak exceeded 2/255 for $llpCase."
            Assert-True ($null -ne $llpDiagnostics.scene.search -and $null -ne $llpDiagnostics.scene.searchText) "Production search geometry missing for $llpCase."
            Assert-True ($null -ne $llpDiagnostics.focusedTile) "Production focused-tile geometry missing for $llpCase."
            Assert-Equal 'Flow' ([string]$llpDiagnostics.query) "Controlled evidence query mismatch for $llpCase."
            Assert-Equal 'results' ([string]$llpDiagnostics.focus) "Controlled evidence focus mismatch for $llpCase."
            Assert-Equal 5 @($llpPixels.edgeSampleVectors).Count "Controlled search edge-vector count mismatch for $llpCase."
            $llpDipScale = 96.0 / [Math]::Max(1, [int]$llpDiagnostics.effectiveDpi)
            $llpSearchLeft = [double]$llpDiagnostics.scene.search.left * $llpDipScale
            $llpSearchTop = [double]$llpDiagnostics.scene.search.top * $llpDipScale
            $llpSearchRight = [double]$llpDiagnostics.scene.search.right * $llpDipScale
            $llpSearchBottom = [double]$llpDiagnostics.scene.search.bottom * $llpDipScale
            foreach ($llpVector in @($llpPixels.edgeSampleVectors)) {
                Assert-True ([double]$llpVector.displacementDip -le 1.5) "Recorded edge vector exceeded 1.5 DIP for $llpCase."
                Assert-True (
                    [double]$llpVector.sourceXDip -ge $llpSearchLeft -and
                    [double]$llpVector.sourceXDip -le $llpSearchRight -and
                    [double]$llpVector.sourceYDip -ge $llpSearchTop -and
                    [double]$llpVector.sourceYDip -le $llpSearchBottom
                ) "Recorded edge-vector source escaped the production search bounds for $llpCase."
            }
        }
        if ($llpCase -in @('text-grid', 'saturated-cool')) {
            Assert-Equal $true ([bool]$llpDiagnostics.liveAdvancedWithoutReopen) "Live desktop identity did not advance without hide/reopen for $llpCase."
            Assert-True ([uint64]$llpDiagnostics.backgroundFrameIdentityAfter -gt [uint64]$llpDiagnostics.backgroundFrameIdentityBefore) "Live background frame identity did not advance for $llpCase."
        }
    }
    $llpBackgroundSource = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'MaterialEvidenceBackground.cpp')
    Assert-True $llpBackgroundSource.Contains('LightLaunchpadMaterialEvidenceBackground') 'Deterministic background exact class contract is missing.'
    Assert-True ($llpBackgroundSource.Contains('WM_APP + 73') -and $llpBackgroundSource.Contains('WM_APP + 74')) 'Deterministic background message contract is missing.'
    Write-Output 'PASS MaterialRendererBehavior_VisualEvidence'
    return
}

try {
    if ($Mode -eq 'Host') {
        $llpHostSourcePath = Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\LightLaunchpad.NativeUi.cpp'
        $llpRendererSourcePath = Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\MaterialRenderer.cpp'
        $llpBuildSourcePath = Join-Path $llpRepoRoot 'tools\build-native-ui.ps1'
        $llpHostSource = Get-Content -Raw -LiteralPath $llpHostSourcePath
        $llpRendererSource = Get-Content -Raw -LiteralPath $llpRendererSourcePath
        $llpBuildSource = Get-Content -Raw -LiteralPath $llpBuildSourcePath
        foreach ($llpLibrary in @('-ld3d11', '-ldxgi', '-ldcomp')) {
            Assert-True $llpBuildSource.Contains($llpLibrary) "ARCHITECTURE production build does not link $llpLibrary."
        }
        foreach ($llpLegacyName in @('SpotlightBackdropSourceRectForLens', 'SpotlightBackdropSourceRectForLensGdi', 'DrawSearchEdgeRefraction', 'DrawLiquidGlassFocusLens', 'DrawFocusedTileLens')) {
            Assert-True (-not $llpHostSource.Contains($llpLegacyName)) "ARCHITECTURE legacy nested material function remains: $llpLegacyName."
        }
        Assert-True $llpHostSource.Contains('const bool allowSystemBackdrop = !spotlight && !g_adaptivePolicy.solidSurface;') 'ARCHITECTURE Spotlight system-backdrop exclusion is absent.'
        Assert-True $llpHostSource.Contains('DWMSBT_NONE') 'ARCHITECTURE Spotlight does not explicitly disable the DWM system backdrop.'
        Assert-True (-not $llpRendererSource.Contains('Sleep(')) 'ARCHITECTURE MaterialRenderer contains a blocking Sleep call.'
        $llpShowHideStart = $llpHostSource.IndexOf('void ShowNativeUi()')
        $llpShowHideEnd = $llpHostSource.IndexOf('void ToggleNativeUi()', $llpShowHideStart)
        Assert-True ($llpShowHideStart -ge 0 -and $llpShowHideEnd -gt $llpShowHideStart) 'ARCHITECTURE could not locate the show/hide host boundary.'
        Assert-True (-not $llpHostSource.Substring($llpShowHideStart, $llpShowHideEnd - $llpShowHideStart).Contains('Sleep(')) 'ARCHITECTURE Spotlight show/hide contains a blocking Sleep call.'

        $llpSpotlightScript = Join-Path $PSScriptRoot 'SpotlightFlowBehavior.Tests.ps1'
        function Invoke-LlpCanonicalHostFlow {
            param([string]$Name, [string[]]$FlowArguments = @())
            $llpFlowOutput = @(& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $llpSpotlightScript @FlowArguments 2>&1)
            $llpFlowExitCode = $LASTEXITCODE
            $llpFlowOutput | ForEach-Object { Write-Output $_ }
            Assert-Equal 0 $llpFlowExitCode "$Name Spotlight host flow failed."
            Assert-True (@($llpFlowOutput | Where-Object { [string]$_ -eq 'PASS SpotlightFlow_AltDSearchEnterAndResultNavigationLaunchExactTargets' }).Count -eq 1) "$Name Spotlight host flow omitted the canonical PASS marker."
            $llpLaunchMetrics = @($llpFlowOutput | Where-Object { [string]$_ -match '^METRIC SpotlightFlow hotkeyToInputReadyP95Ms=' })
            Assert-Equal 1 $llpLaunchMetrics.Count "$Name Spotlight host flow omitted its canonical launch metric."
            $llpLaunchMatch = [regex]::Match([string]$llpLaunchMetrics[0], '(?:^| )launches=([0-9]+)(?: |$)')
            Assert-True ($llpLaunchMatch.Success -and [int]$llpLaunchMatch.Groups[1].Value -ge 2) "$Name Spotlight host flow did not perform both real Shell launches."
        }
        Invoke-LlpCanonicalHostFlow -Name 'Normal high-fidelity'
        Invoke-LlpCanonicalHostFlow -Name 'Forced-initialization fallback' -FlowArguments @('-MaterialFailure', 'initialization')
        Write-Output 'PASS MaterialRendererBehavior_HostHighFidelityAndFallbackCanonicalFlows'
        return
    }

    $llpMaterialHostSource = Get-Content -Raw -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\LightLaunchpad.NativeUi.cpp')
    $llpDesktopSwitchBypassName = 'LightLaunchpad.MaterialDesktopSwitch' + 'ForDevelopment'
    Assert-True (-not $llpMaterialHostSource.Contains($llpDesktopSwitchBypassName)) 'ARCHITECTURE desktop-switch recovery retained its private registered-message bypass.'
    Assert-True ($llpMaterialHostSource.Contains('SetWinEventHook(') -and $llpMaterialHostSource.Contains('EVENT_SYSTEM_DESKTOPSWITCH')) 'ARCHITECTURE desktop-switch recovery lost its real User32 event hook.'
    Assert-True ($Mode -in @('Lifecycle', 'Recovery')) 'Unsupported material behavior mode.'
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Concurrent;
using System.Runtime.InteropServices;
public static class LightLaunchpadMaterialProbeNative {
    public delegate bool EnumWindowsCallback(IntPtr hwnd, IntPtr state);
    private delegate void WinEventDelegate(IntPtr hook, uint eventType, IntPtr hwnd, int objectId, int childId, uint threadId, uint time);
    [StructLayout(LayoutKind.Sequential)] private struct POINT { public int x; public int y; }
    [StructLayout(LayoutKind.Sequential)] private struct MSG {
        public IntPtr hwnd;
        public uint message;
        public UIntPtr wParam;
        public IntPtr lParam;
        public uint time;
        public POINT point;
        public uint privateValue;
    }
    private static WinEventDelegate windowEventCallback;
    private static IntPtr windowEventHook;
    private static readonly ConcurrentDictionary<uint, int> windowEvents = new ConcurrentDictionary<uint, int>();
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr state);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll")] private static extern IntPtr GetAncestor(IntPtr hwnd, uint flags);
    [DllImport("user32.dll", SetLastError=true)] private static extern IntPtr SetWinEventHook(uint eventMin, uint eventMax, IntPtr module, WinEventDelegate callback, uint processId, uint threadId, uint flags);
    [DllImport("user32.dll")] private static extern bool UnhookWinEvent(IntPtr hook);
    [DllImport("user32.dll")] private static extern bool PeekMessage(out MSG message, IntPtr hwnd, uint minimum, uint maximum, uint remove);
    [DllImport("user32.dll")] private static extern bool TranslateMessage(ref MSG message);
    [DllImport("user32.dll")] private static extern IntPtr DispatchMessage(ref MSG message);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd, uint message, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern uint RegisterWindowMessage(string message);
    [DllImport("user32.dll")] public static extern void NotifyWinEvent(uint eventType, IntPtr hwnd, int objectId, int childId);
    [DllImport("user32.dll")] public static extern bool GetWindowDisplayAffinity(IntPtr hwnd, out uint affinity);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")] public static extern bool DestroyWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool UpdateWindow(IntPtr hwnd);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern IntPtr CreateWindowExW(uint extendedStyle, string className, string title, uint style, int x, int y, int width, int height, IntPtr parent, IntPtr menu, IntPtr instance, IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    public static IntPtr CreateAuxiliaryWindow(int x, int y, int width, int height) {
        const uint ToolWindow = 0x00000080;
        const uint OverlappedWindowVisible = 0x10CF0000;
        IntPtr window = CreateWindowExW(ToolWindow, "STATIC", "LightLaunchpad committed evidence candidate", OverlappedWindowVisible, x, y, width, height, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
        if (window != IntPtr.Zero) UpdateWindow(window);
        return window;
    }
    public static IntPtr FindWindowForProcess(uint expectedProcessId) {
        IntPtr result = IntPtr.Zero;
        EnumWindows((hwnd, state) => {
            uint processId;
            GetWindowThreadProcessId(hwnd, out processId);
            if (processId == expectedProcessId) { result = hwnd; return false; }
            return true;
        }, IntPtr.Zero);
        return result;
    }
    private static void OnWindowEvent(IntPtr hook, uint eventType, IntPtr hwnd, int objectId, int childId, uint threadId, uint time) {
        if ((eventType != 0x8000 && eventType != 0x8002) || hwnd == IntPtr.Zero || objectId != 0 || childId != 0) return;
        if (GetAncestor(hwnd, 2) != hwnd) return;
        uint processId;
        GetWindowThreadProcessId(hwnd, out processId);
        if (processId != 0) windowEvents.AddOrUpdate(processId, 1, (key, count) => count + 1);
    }
    public static void StartWindowEventMonitor() {
        if (windowEventHook != IntPtr.Zero) throw new InvalidOperationException("Window event monitor is already active.");
        windowEvents.Clear();
        windowEventCallback = OnWindowEvent;
        windowEventHook = SetWinEventHook(0x8000, 0x8002, IntPtr.Zero, windowEventCallback, 0, 0, 0);
        if (windowEventHook == IntPtr.Zero) throw new InvalidOperationException("SetWinEventHook failed.");
    }
    public static void PumpWindowEvents() {
        MSG message;
        while (PeekMessage(out message, IntPtr.Zero, 0, 0, 1)) {
            TranslateMessage(ref message);
            DispatchMessage(ref message);
        }
    }
    public static int WindowEventCount(uint processId) {
        int count;
        return windowEvents.TryGetValue(processId, out count) ? count : 0;
    }
    public static void StopWindowEventMonitor() {
        if (windowEventHook != IntPtr.Zero) {
            UnhookWinEvent(windowEventHook);
            windowEventHook = IntPtr.Zero;
        }
        windowEventCallback = null;
    }
}
'@

    function Invoke-LlpAdaptiveProbe {
        param(
            [string]$Name,
            [int]$HighContrast,
            [int]$Transparency,
            [int]$Animations,
            [string]$ExpectedPath,
            [bool]$ExpectedReducedMotion,
            [bool]$ExpectedCapture,
            [string]$ForceFailure = '',
            [string]$ExpectedState = 'live',
            [string]$ExpectedFailure = 'None',
            [uint64]$ExpectedRebuilds = 0,
            [bool]$RebuildAfterFallback = $false,
            [bool]$DesktopSwitchAfterFallback = $false
        )
        $llpAdaptiveProbePath = Join-Path $llpTempRoot ("adaptive-$Name.json")
        $llpAdaptiveReadyPath = Join-Path $llpTempRoot ("adaptive-$Name-ready.json")
        $llpAdaptiveProcess = $null
        $llpPriorAppData = $env:APPDATA
        $llpPriorLocalAppData = $env:LOCALAPPDATA
        $llpPriorUserProfile = $env:USERPROFILE
        try {
            $env:APPDATA = $llpAppData
            $env:LOCALAPPDATA = $llpLocalAppData
            $env:USERPROFILE = $llpUserProfile
            $llpAdaptiveArguments = @(
                '--material-probe-file', $llpAdaptiveProbePath,
                '--material-probe-ready-file', $llpAdaptiveReadyPath,
                '--test-monitor-device', $llpTestMonitorDevice,
                '--adaptive-test-high-contrast', [string]$HighContrast,
                '--adaptive-test-transparency', [string]$Transparency,
                '--adaptive-test-animations', [string]$Animations
            )
            if (-not [string]::IsNullOrEmpty($ForceFailure)) {
                $llpAdaptiveArguments += @('--material-force-failure', $ForceFailure)
            }
            $llpAdaptiveProcess = Start-Process -FilePath $llpExecutable -ArgumentList $llpAdaptiveArguments -PassThru -WindowStyle Hidden
            $script:llpCapturedProcesses += $llpAdaptiveProcess
        }
        finally {
            $env:APPDATA = $llpPriorAppData
            $env:LOCALAPPDATA = $llpPriorLocalAppData
            $env:USERPROFILE = $llpPriorUserProfile
        }

        $llpAdaptiveWindow = [IntPtr]::Zero
        $llpAdaptiveDeadline = [DateTime]::UtcNow.AddSeconds(8)
        while ($llpAdaptiveWindow -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $llpAdaptiveDeadline) {
            Start-Sleep -Milliseconds 25
            $llpAdaptiveProcess.Refresh()
            if ($llpAdaptiveProcess.HasExited) { break }
            $llpAdaptiveWindow = [LightLaunchpadMaterialProbeNative]::FindWindowForProcess([uint32]$llpAdaptiveProcess.Id)
        }
        Assert-True (-not $llpAdaptiveProcess.HasExited) "$Name adaptive probe exited before creating a NativeUi window."
        Assert-True ($llpAdaptiveWindow -ne [IntPtr]::Zero) "$Name adaptive probe did not create a NativeUi window."

        Wait-LlpFile $llpAdaptiveReadyPath
        $llpAdaptiveReady = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpAdaptiveReadyPath | ConvertFrom-Json
        Assert-LlpDisplay1Containment $llpAdaptiveReady "$Name adaptive ready state"
        [void][LightLaunchpadMaterialProbeNative]::ShowWindow($llpAdaptiveWindow, 5)
        [void][LightLaunchpadMaterialProbeNative]::SetForegroundWindow($llpAdaptiveWindow)

        Wait-LlpFile $llpAdaptiveProbePath
        $llpAdaptiveStateDeadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            Start-Sleep -Milliseconds 25
            $llpAdaptiveProbe = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpAdaptiveProbePath | ConvertFrom-Json
        } while (([string]$llpAdaptiveProbe.state -ne $ExpectedState -or [string]$llpAdaptiveProbe.path -ne $ExpectedPath) -and [DateTime]::UtcNow -lt $llpAdaptiveStateDeadline)
        Assert-LlpDisplay1Containment $llpAdaptiveProbe "$Name visible adaptive state"
        Assert-Equal $ExpectedState ([string]$llpAdaptiveProbe.state) "$Name adaptive route selected the wrong lifecycle state."
        Assert-Equal $ExpectedPath ([string]$llpAdaptiveProbe.path) "$Name adaptive route selected the wrong render path."
        Assert-Equal $ExpectedFailure ([string]$llpAdaptiveProbe.failure) "$Name adaptive route reported the wrong stable failure."
        Assert-Equal $ExpectedReducedMotion ([bool]$llpAdaptiveProbe.reducedMotion) "$Name adaptive route reported the wrong reduced-motion state."
        Assert-Equal $ExpectedRebuilds ([uint64]$llpAdaptiveProbe.deviceRebuilds) "$Name adaptive route used the wrong rebuild count."
        Assert-True ([int]$llpAdaptiveProbe.dpi -gt 0) "$Name adaptive route did not report a measured DPI."
        if ($ExpectedCapture) {
            Assert-True ([uint64]$llpAdaptiveProbe.acquiredFrames -gt 0) "$Name adaptive route did not acquire a real desktop frame."
            Assert-True (-not [string]::IsNullOrWhiteSpace([string]$llpAdaptiveProbe.adapter)) "$Name adaptive route did not report its copied adapter identity."
            Assert-Equal $llpTestMonitorDevice ([string]$llpAdaptiveProbe.output) "$Name adaptive route reported the wrong captured output."
        }
        else {
            Assert-Equal 0 ([uint64]$llpAdaptiveProbe.acquiredFrames) "$Name solid route acquired desktop frames."
            Assert-Equal 0 ([uint64]$llpAdaptiveProbe.presentedFrames) "$Name solid route presented Desktop Duplication frames."
            if ($ExpectedPath -eq 'solid') {
                Assert-True ([string]::IsNullOrEmpty([string]$llpAdaptiveProbe.adapter)) "$Name solid route exposed a capture adapter."
                Assert-True ([string]::IsNullOrEmpty([string]$llpAdaptiveProbe.output)) "$Name solid route exposed a capture output."
            }
            $llpAdaptiveAffinity = [uint32]::MaxValue
            Assert-True ([LightLaunchpadMaterialProbeNative]::GetWindowDisplayAffinity($llpAdaptiveWindow, [ref]$llpAdaptiveAffinity)) "$Name non-capture route affinity could not be read."
            Assert-Equal 0 $llpAdaptiveAffinity "$Name non-capture route retained capture exclusion."
        }

        if ($RebuildAfterFallback -or $DesktopSwitchAfterFallback) {
            if ($DesktopSwitchAfterFallback) {
                [LightLaunchpadMaterialProbeNative]::NotifyWinEvent(0x0020, $llpAdaptiveWindow, 0, 0)
            }
            else {
                [void][LightLaunchpadMaterialProbeNative]::SendMessage($llpAdaptiveWindow, 0x007E, [UIntPtr]::Zero, [IntPtr]::Zero)
            }
            $llpAdaptiveRebuildDeadline = [DateTime]::UtcNow.AddSeconds(5)
            do {
                Start-Sleep -Milliseconds 25
                $llpAdaptiveRebuilt = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpAdaptiveProbePath | ConvertFrom-Json
            } while (([string]$llpAdaptiveRebuilt.state -ne 'live' -or [string]$llpAdaptiveRebuilt.path -ne 'high-fidelity') -and [DateTime]::UtcNow -lt $llpAdaptiveRebuildDeadline)
            Assert-LlpDisplay1Containment $llpAdaptiveRebuilt "$Name rebuilt display state"
            Assert-Equal 'live' ([string]$llpAdaptiveRebuilt.state) "$Name environment rebuild did not return live."
            Assert-Equal 'high-fidelity' ([string]$llpAdaptiveRebuilt.path) "$Name environment rebuild did not restore high fidelity."
            Assert-Equal 'None' ([string]$llpAdaptiveRebuilt.failure) "$Name environment rebuild retained the old failure."
            Assert-True ([uint64]$llpAdaptiveRebuilt.deviceRebuilds -gt $ExpectedRebuilds) "$Name environment rebuild did not start a new recovery epoch."
            Assert-True ([uint64]$llpAdaptiveRebuilt.acquiredFrames -gt 0) "$Name environment rebuild did not acquire a new real frame."
        }

        [void][LightLaunchpadMaterialProbeNative]::SendMessage($llpAdaptiveWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
        Assert-True ($llpAdaptiveProcess.WaitForExit(8000)) "$Name adaptive probe did not exit."
        Assert-Equal 0 $llpAdaptiveProcess.ExitCode "$Name adaptive probe exit code mismatch."
        Write-Output "METRIC MaterialAdaptive name=$Name path=$([string]$llpAdaptiveProbe.path) reducedMotion=$([bool]$llpAdaptiveProbe.reducedMotion) acquired=$([uint64]$llpAdaptiveProbe.acquiredFrames) presented=$([uint64]$llpAdaptiveProbe.presentedFrames) dpi=$([int]$llpAdaptiveProbe.dpi)"
    }

    New-Item -ItemType Directory -Force -Path $llpOutputDirectory, $llpDataDirectory, $llpLocalAppData, $llpUserProfile, $llpLaunchpad | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $llpLaunchpad 'Alpha.url'), "[InternetShortcut]`r`nURL=https://example.invalid/alpha`r`n", [System.Text.UTF8Encoding]::new($false))
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

    $llpBuildCommit = 'material' + [System.Guid]::NewGuid().ToString('N').Substring(0, 12)
    & (Join-Path $llpRepoRoot 'tools\build-native-ui.ps1') -OutputDirectory $llpOutputDirectory -ProductVersion '1.11.0' -BuildCommit $llpBuildCommit -BuildChannel Development | Out-Null
    $llpExecutable = Join-Path $llpOutputDirectory 'LightLaunchpad.NativeUi.exe'
    $llpExecutableHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $llpExecutable).Hash
    $llpObjdump = (where.exe objdump | Select-Object -First 1)
    Assert-True (-not [string]::IsNullOrWhiteSpace($llpObjdump)) 'objdump was not found for production fake-symbol verification.'
    $llpDevelopmentSymbols = ((& $llpObjdump -t $llpExecutable 2>&1) -join [Environment]::NewLine)
    Assert-Equal 0 $LASTEXITCODE 'objdump could not inspect the development production executable.'
    Assert-True (-not $llpDevelopmentSymbols.Contains('LLP_MaterialRunFakeLifecycleScenario')) 'Development production executable contained the private fake lifecycle runner.'

    $llpPriorAppData = $env:APPDATA
    $llpPriorLocalAppData = $env:LOCALAPPDATA
    $llpPriorUserProfile = $env:USERPROFILE
    try {
        $env:APPDATA = $llpAppData
        $env:LOCALAPPDATA = $llpLocalAppData
        $env:USERPROFILE = $llpUserProfile
        $llpProcess = Start-Process -FilePath $llpExecutable -ArgumentList @('--material-probe-file', $llpProbePath, '--material-probe-ready-file', $llpReadyPath, '--material-force-failure', 'duplication', '--diagnostics-log-file', $llpLogPath, '--test-monitor-device', $llpTestMonitorDevice, '--adaptive-test-high-contrast', '0', '--adaptive-test-transparency', '1', '--adaptive-test-animations', '1') -PassThru -WindowStyle Hidden
        $llpCapturedProcesses += $llpProcess
    }
    finally {
        $env:APPDATA = $llpPriorAppData
        $env:LOCALAPPDATA = $llpPriorLocalAppData
        $env:USERPROFILE = $llpPriorUserProfile
    }

    $llpDeadline = [DateTime]::UtcNow.AddSeconds(8)
    $llpWindow = [IntPtr]::Zero
    while ($llpWindow -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $llpDeadline) {
        Start-Sleep -Milliseconds 25
        $llpProcess.Refresh()
        if ($llpProcess.HasExited) { break }
        $llpWindow = [LightLaunchpadMaterialProbeNative]::FindWindowForProcess([uint32]$llpProcess.Id)
    }
    Assert-True (-not $llpProcess.HasExited) 'Development material probe exited before creating its isolated window.'
    Assert-True ($llpWindow -ne [IntPtr]::Zero) 'Development material probe did not create a real NativeUi window.'

    Wait-LlpFile $llpReadyPath
    $llpReady = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpReadyPath | ConvertFrom-Json
    Assert-Equal $llpTestMonitorDevice ([string]$llpReady.monitorDevice) 'Material probe ready state was not fixed to DISPLAY1 before interaction.'
    Assert-True (
        [int]$llpReady.windowRect.left -ge [int]$llpReady.monitorBounds.left -and
        [int]$llpReady.windowRect.top -ge [int]$llpReady.monitorBounds.top -and
        [int]$llpReady.windowRect.right -le [int]$llpReady.monitorBounds.right -and
        [int]$llpReady.windowRect.bottom -le [int]$llpReady.monitorBounds.bottom
    ) 'Material probe window escaped DISPLAY1 before interaction.'

    [void][LightLaunchpadMaterialProbeNative]::SendMessage($llpWindow, 0x0312, [UIntPtr]::new([uint64]0x4C55), [IntPtr]::Zero)
    Wait-LlpFile $llpProbePath
    $llpProbe = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpProbePath | ConvertFrom-Json
    foreach ($llpField in @('path', 'failure', 'state', 'adapter', 'output', 'dpi', 'acquiredFrames', 'presentedFrames', 'droppedScenes', 'deviceRebuilds', 'lastFrameIdentity', 'firstFrameMs', 'frameP95Ms', 'evidenceFrozen', 'reducedMotion', 'statusPublications', 'diagnosticsCaptureStepStringCopies', 'diagnosticsCaptureStepVectorCopies')) {
        Assert-True ($llpProbe.PSObject.Properties.Name -contains $llpField) "Material diagnostics schema is missing '$llpField'."
    }
    Assert-Equal $llpTestMonitorDevice ([string]$llpProbe.monitorDevice) 'Visible fallback material probe was not fixed to DISPLAY1.'
    Assert-True (
        [int]$llpProbe.windowRect.left -ge [int]$llpProbe.monitorBounds.left -and
        [int]$llpProbe.windowRect.top -ge [int]$llpProbe.monitorBounds.top -and
        [int]$llpProbe.windowRect.right -le [int]$llpProbe.monitorBounds.right -and
        [int]$llpProbe.windowRect.bottom -le [int]$llpProbe.monitorBounds.bottom
    ) 'Visible fallback material probe escaped the requested DISPLAY1 bounds.'
    $llpElement = [System.Windows.Automation.AutomationElement]::FromHandle($llpWindow)
    Assert-True ($null -ne $llpElement) 'UI Automation could not discover the material-probe search element.'
    $llpValueObject = $null
    Assert-True ($llpElement.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$llpValueObject)) 'Material fallback lost the UI Automation Value pattern.'
    $llpValue = [System.Windows.Automation.ValuePattern]$llpValueObject
    $llpExpectedQuery = ([char]0x4E2D).ToString() + 'alpha'
    $llpValue.SetValue($llpExpectedQuery)
    Assert-Equal $llpExpectedQuery $llpValue.Current.Value 'Forced material fallback did not remain searchable.'
    Assert-Equal $true $llpElement.Current.HasKeyboardFocus 'Forced material fallback lost search focus.'
    $llpPingStarted = [DateTime]::UtcNow
    [void][LightLaunchpadMaterialProbeNative]::SendMessage($llpWindow, 0x0000, [UIntPtr]::Zero, [IntPtr]::Zero)
    Assert-True ((([DateTime]::UtcNow - $llpPingStarted).TotalMilliseconds) -lt 500) 'Forced material fallback did not remain responsive.'

    Assert-Equal 'fallback' ([string]$llpProbe.path) 'Forced duplication failure did not select fallback.'
    Assert-Equal 'ForcedForTest' ([string]$llpProbe.failure) 'Forced duplication failure did not publish its stable reason.'

    [void][LightLaunchpadMaterialProbeNative]::SendMessage($llpWindow, 0x804D, [UIntPtr]::new([uint64]1), [IntPtr]::Zero)
    $llpHiddenDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 25
        $llpHidden = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpProbePath | ConvertFrom-Json
    } while ([string]$llpHidden.state -ne 'hidden' -and [DateTime]::UtcNow -lt $llpHiddenDeadline)
    Assert-Equal 'hidden' ([string]$llpHidden.state) 'Renderer did not report hidden cleanup.'
    $llpHiddenAcquired = [uint64]$llpHidden.acquiredFrames
    $llpHiddenPresented = [uint64]$llpHidden.presentedFrames
    $llpHiddenGeneration = [uint64]$llpHidden.probeGeneration
    $llpHiddenStatusPublications = [uint64]$llpHidden.statusPublications
    Assert-True ($llpHiddenGeneration -gt 0) 'Hidden probe JSON did not expose a real snapshot generation.'
    Start-Sleep -Seconds 2
    [void][LightLaunchpadMaterialProbeNative]::SendMessage($llpWindow, 0x804D, [UIntPtr]::new([uint64]2), [IntPtr]::Zero)
    $llpRefreshDeadline = [DateTime]::UtcNow.AddSeconds(2)
    do {
        Start-Sleep -Milliseconds 25
        $llpHiddenAgain = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpProbePath | ConvertFrom-Json
    } while ([uint64]$llpHiddenAgain.probeGeneration -le $llpHiddenGeneration -and [DateTime]::UtcNow -lt $llpRefreshDeadline)
    Assert-True ([uint64]$llpHiddenAgain.probeGeneration -gt $llpHiddenGeneration) 'Explicit hidden snapshot did not refresh probe JSON after two idle seconds.'
    Assert-True ([uint64]$llpHiddenAgain.statusPublications -gt $llpHiddenStatusPublications) 'Explicit hidden snapshot bypassed a renderer status publication.'
    Assert-Equal $llpHiddenAcquired ([uint64]$llpHiddenAgain.acquiredFrames) 'Hidden renderer continued acquiring desktop frames.'
    Assert-Equal $llpHiddenPresented ([uint64]$llpHiddenAgain.presentedFrames) 'Hidden renderer continued presenting frames.'

    [void][LightLaunchpadMaterialProbeNative]::SendMessage($llpWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
    Assert-True ($llpProcess.WaitForExit(8000)) 'Development material probe did not exit after closing its captured window.'
    Assert-Equal 0 $llpProcess.ExitCode 'Development material probe exit code mismatch.'
    $llpStartupLog = Get-Content -Raw -Encoding Unicode -LiteralPath $llpLogPath
    Assert-True (-not $llpStartupLog.Contains('hotkey-registered')) 'Material behavior probe registered the real global hotkey.'
    Assert-True (-not $llpStartupLog.Contains('tray-added')) 'Material behavior probe added the real notification-area icon.'

    foreach ($llpBadArguments in @(
        @('--material-force-failure', 'duplication'),
        @('--test-monitor-device', $llpTestMonitorDevice),
        @('--material-probe-file', (Join-Path $llpTempRoot 'invalid.json'), '--material-force-failure', 'not-valid')
    )) {
        $llpBadProcess = Start-Process -FilePath $llpExecutable -ArgumentList ([string[]]$llpBadArguments) -PassThru -WindowStyle Hidden
        $llpCapturedProcesses += $llpBadProcess
        Assert-True ($llpBadProcess.WaitForExit(5000)) 'Invalid development material switches did not exit.'
        Assert-Equal 2 $llpBadProcess.ExitCode 'Invalid development material switches were not rejected with exit 2.'
    }

    Invoke-LlpAdaptiveProbe -Name 'high-contrast' -HighContrast 1 -Transparency 1 -Animations 1 -ExpectedPath 'solid' -ExpectedReducedMotion $false -ExpectedCapture $false
    Invoke-LlpAdaptiveProbe -Name 'transparency-disabled' -HighContrast 0 -Transparency 0 -Animations 1 -ExpectedPath 'solid' -ExpectedReducedMotion $false -ExpectedCapture $false
    Invoke-LlpAdaptiveProbe -Name 'reduced-motion' -HighContrast 0 -Transparency 1 -Animations 0 -ExpectedPath 'high-fidelity' -ExpectedReducedMotion $true -ExpectedCapture $true
    Invoke-LlpAdaptiveProbe -Name 'access-loss-recovered' -HighContrast 0 -Transparency 1 -Animations 1 -ExpectedPath 'high-fidelity' -ExpectedReducedMotion $false -ExpectedCapture $true -ForceFailure 'access-lost-once' -ExpectedRebuilds 1
    Invoke-LlpAdaptiveProbe -Name 'access-loss-rebuild-failed' -HighContrast 0 -Transparency 1 -Animations 1 -ExpectedPath 'fallback' -ExpectedReducedMotion $false -ExpectedCapture $false -ForceFailure 'access-lost-rebuild-failure' -ExpectedState 'fallback' -ExpectedFailure 'ForcedForTest' -ExpectedRebuilds 1
    Invoke-LlpAdaptiveProbe -Name 'second-access-loss' -HighContrast 0 -Transparency 1 -Animations 1 -ExpectedPath 'fallback' -ExpectedReducedMotion $false -ExpectedCapture $false -ForceFailure 'access-lost' -ExpectedState 'fallback' -ExpectedFailure 'ForcedForTest' -ExpectedRebuilds 1
    Invoke-LlpAdaptiveProbe -Name 'display-change-after-fallback' -HighContrast 0 -Transparency 1 -Animations 1 -ExpectedPath 'fallback' -ExpectedReducedMotion $false -ExpectedCapture $false -ForceFailure 'access-lost' -ExpectedState 'fallback' -ExpectedFailure 'ForcedForTest' -ExpectedRebuilds 1 -RebuildAfterFallback $true
    Invoke-LlpAdaptiveProbe -Name 'desktop-switch-after-fallback' -HighContrast 0 -Transparency 1 -Animations 1 -ExpectedPath 'fallback' -ExpectedReducedMotion $false -ExpectedCapture $false -ForceFailure 'access-lost' -ExpectedState 'fallback' -ExpectedFailure 'ForcedForTest' -ExpectedRebuilds 1 -DesktopSwitchAfterFallback $true

    $llpPriorAppData = $env:APPDATA
    $llpPriorLocalAppData = $env:LOCALAPPDATA
    $llpPriorUserProfile = $env:USERPROFILE
    try {
        $env:APPDATA = $llpAppData
        $env:LOCALAPPDATA = $llpLocalAppData
        $env:USERPROFILE = $llpUserProfile
        $llpNormalProcess = Start-Process -FilePath $llpExecutable -ArgumentList @('--material-probe-file', $llpNormalProbePath, '--material-probe-ready-file', $llpNormalReadyPath, '--material-evidence', '--test-monitor-device', $llpTestMonitorDevice, '--adaptive-test-high-contrast', '0', '--adaptive-test-transparency', '1', '--adaptive-test-animations', '1') -PassThru -WindowStyle Hidden
        $llpCapturedProcesses += $llpNormalProcess
    }
    finally {
        $env:APPDATA = $llpPriorAppData
        $env:LOCALAPPDATA = $llpPriorLocalAppData
        $env:USERPROFILE = $llpPriorUserProfile
    }
    $llpNormalWindow = [IntPtr]::Zero
    $llpNormalDeadline = [DateTime]::UtcNow.AddSeconds(8)
    while ($llpNormalWindow -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $llpNormalDeadline) {
        Start-Sleep -Milliseconds 25
        $llpNormalProcess.Refresh()
        if ($llpNormalProcess.HasExited) { break }
        $llpNormalWindow = [LightLaunchpadMaterialProbeNative]::FindWindowForProcess([uint32]$llpNormalProcess.Id)
    }
    Assert-True ($llpNormalWindow -ne [IntPtr]::Zero) 'Normal material probe did not create its isolated NativeUi window.'
    Wait-LlpFile $llpNormalReadyPath
    $llpNormalReady = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpNormalReadyPath | ConvertFrom-Json
    Assert-Equal $llpTestMonitorDevice ([string]$llpNormalReady.monitorDevice) 'Normal material ready state was not fixed to DISPLAY1 before interaction.'
    Assert-True (
        [int]$llpNormalReady.windowRect.left -ge [int]$llpNormalReady.monitorBounds.left -and
        [int]$llpNormalReady.windowRect.top -ge [int]$llpNormalReady.monitorBounds.top -and
        [int]$llpNormalReady.windowRect.right -le [int]$llpNormalReady.monitorBounds.right -and
        [int]$llpNormalReady.windowRect.bottom -le [int]$llpNormalReady.monitorBounds.bottom
    ) 'Normal material window escaped DISPLAY1 before interaction.'
    [void][LightLaunchpadMaterialProbeNative]::SendMessage($llpNormalWindow, 0x0312, [UIntPtr]::new([uint64]0x4C55), [IntPtr]::Zero)
    Wait-LlpFile $llpNormalProbePath
    $llpNormalStateDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 25
        $llpNormalProbe = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpNormalProbePath | ConvertFrom-Json
    } while (([string]$llpNormalProbe.state -notin @('live', 'fallback') -or
        ([string]$llpNormalProbe.state -eq 'live' -and [uint64]$llpNormalProbe.presentedSceneGeneration -eq 0)) -and
        [DateTime]::UtcNow -lt $llpNormalStateDeadline)
    Assert-Equal $llpTestMonitorDevice ([string]$llpNormalProbe.monitorDevice) 'Visible normal material probe was not fixed to DISPLAY1.'
    Assert-True (
        [int]$llpNormalProbe.windowRect.left -ge [int]$llpNormalProbe.monitorBounds.left -and
        [int]$llpNormalProbe.windowRect.top -ge [int]$llpNormalProbe.monitorBounds.top -and
        [int]$llpNormalProbe.windowRect.right -le [int]$llpNormalProbe.monitorBounds.right -and
        [int]$llpNormalProbe.windowRect.bottom -le [int]$llpNormalProbe.monitorBounds.bottom
    ) 'Visible normal material probe escaped the requested DISPLAY1 bounds.'
    Assert-True (([string]$llpNormalProbe.state -eq 'live' -and [string]$llpNormalProbe.path -eq 'high-fidelity' -and [uint64]$llpNormalProbe.acquiredFrames -gt 0) -or ([string]$llpNormalProbe.state -eq 'fallback' -and [string]$llpNormalProbe.path -eq 'fallback' -and [string]$llpNormalProbe.failure -ne 'None')) 'Normal material probe reported neither a captured frame nor a stable machine-specific fallback.'
    Assert-Equal 'live' ([string]$llpNormalProbe.state) 'Evidence freeze requires a live real Desktop Duplication frame.'
    $llpCopyBaselineGeneration = [uint64]$llpNormalProbe.probeGeneration
    $llpCopyBaselineStrings = [uint64]$llpNormalProbe.diagnosticsCaptureStepStringCopies
    $llpCopyBaselineVectors = [uint64]$llpNormalProbe.diagnosticsCaptureStepVectorCopies
    Start-Sleep -Milliseconds 250
    [void][LightLaunchpadMaterialProbeNative]::SendMessage($llpNormalWindow, 0x804D, [UIntPtr]::new([uint64]2), [IntPtr]::Zero)
    $llpCopyDeadline = [DateTime]::UtcNow.AddSeconds(2)
    do {
        Start-Sleep -Milliseconds 25
        $llpCopyProbe = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpNormalProbePath | ConvertFrom-Json
    } while ([uint64]$llpCopyProbe.probeGeneration -le $llpCopyBaselineGeneration -and [DateTime]::UtcNow -lt $llpCopyDeadline)
    $llpCopyStringsTotal = [uint64]$llpCopyProbe.diagnosticsCaptureStepStringCopies
    $llpCopyVectorsTotal = [uint64]$llpCopyProbe.diagnosticsCaptureStepVectorCopies
    $llpCopyStringsDelta = [uint64]($llpCopyStringsTotal - $llpCopyBaselineStrings)
    $llpCopyVectorsDelta = [uint64]($llpCopyVectorsTotal - $llpCopyBaselineVectors)
    Assert-Equal $llpCopyBaselineStrings $llpCopyStringsTotal 'Ordinary real capture steps copied adapter/output/auxiliary strings.'
    Assert-Equal $llpCopyBaselineVectors $llpCopyVectorsTotal 'Ordinary real capture steps copied presentation evidence vectors.'
    Assert-Equal ([uint64]0) $llpCopyStringsDelta 'Ordinary real capture-step string-copy delta was not zero.'
    Assert-Equal ([uint64]0) $llpCopyVectorsDelta 'Ordinary real capture-step vector-copy delta was not zero.'
    Write-Output "METRIC MaterialDiagnosticsRealCaptureCopies stringsTotal=$llpCopyStringsTotal vectorsTotal=$llpCopyVectorsTotal stringsDelta=$llpCopyStringsDelta vectorsDelta=$llpCopyVectorsDelta presented=$([uint64]$llpCopyProbe.presentedFrames)"
    [void][LightLaunchpadMaterialProbeNative]::SendMessage($llpNormalWindow, 0x804D, [UIntPtr]::new([uint64]3), [IntPtr]::Zero)
    $llpBaselineDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 25
        $llpBaselineProbe = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpNormalProbePath | ConvertFrom-Json
    } while (($null -eq $llpBaselineProbe.PSObject.Properties['evidenceBaselineCaptured'] -or -not [bool]$llpBaselineProbe.evidenceBaselineCaptured) -and [DateTime]::UtcNow -lt $llpBaselineDeadline)
    Assert-Equal $true ([bool]$llpBaselineProbe.evidenceBaselineCaptured) 'Real backend did not capture the last committed desktop evidence before the candidate frame.'
    Assert-True (-not [string]::IsNullOrWhiteSpace([string]$llpBaselineProbe.evidenceBaselineDesktopHash)) 'Committed desktop evidence did not publish a stable GPU texture hash.'
    Assert-True ([uint64]$llpBaselineProbe.evidenceBaselineFrameIdentity -gt 0) 'Committed desktop evidence did not publish the last presented frame identity.'
    Assert-True ([uint64]$llpBaselineProbe.evidenceBaselineSceneGeneration -gt 0) 'Committed desktop evidence did not publish the last presented scene generation.'
    $llpEvidenceCandidateWindow = [LightLaunchpadMaterialProbeNative]::CreateAuxiliaryWindow(
        ([int]$llpBaselineProbe.monitorBounds.left + 48),
        ([int]$llpBaselineProbe.monitorBounds.top + 48),
        240,
        160)
    Assert-True ($llpEvidenceCandidateWindow -ne [IntPtr]::Zero) 'Could not create the real DISPLAY1 unpresented-candidate delta.'
    try {
        [void][LightLaunchpadMaterialProbeNative]::SendMessage($llpNormalWindow, 0x804D, [UIntPtr]::new([uint64]4), [IntPtr]::Zero)
        $llpCandidateDeadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            Start-Sleep -Milliseconds 25
            $llpCandidateProbe = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpNormalProbePath | ConvertFrom-Json
        } while (($null -eq $llpCandidateProbe.PSObject.Properties['evidenceCandidateCaptured'] -or -not [bool]$llpCandidateProbe.evidenceCandidateCaptured) -and [DateTime]::UtcNow -lt $llpCandidateDeadline)
        Assert-Equal $true ([bool]$llpCandidateProbe.evidenceCandidateCaptured) 'Real backend did not acquire the intentionally unpresented desktop candidate.'
        Assert-True ([string]$llpCandidateProbe.evidenceCandidateDesktopHash -ne [string]$llpBaselineProbe.evidenceBaselineDesktopHash) 'The real candidate desktop pixels did not differ from the committed desktop pixels.'
        Assert-True ([uint64]$llpCandidateProbe.evidenceCandidateFrameIdentity -ne [uint64]$llpBaselineProbe.evidenceBaselineFrameIdentity) 'The real candidate frame identity did not differ from the committed frame identity.'
        Assert-Equal ([uint64]$llpBaselineProbe.presentedFrames) ([uint64]$llpCandidateProbe.presentedFrames) 'The intentionally unpresented candidate advanced the presentation count.'
    }
    finally {
        [void][LightLaunchpadMaterialProbeNative]::DestroyWindow($llpEvidenceCandidateWindow)
    }
    $llpFreezeMessage = [LightLaunchpadMaterialProbeNative]::RegisterWindowMessage('LightLaunchpad.MaterialEvidenceFreeze')
    Assert-True ($llpFreezeMessage -ne 0) 'Could not register the material evidence freeze message.'
    [void][LightLaunchpadMaterialProbeNative]::SendMessage($llpNormalWindow, $llpFreezeMessage, [UIntPtr]::Zero, [IntPtr]::Zero)
    $llpFreezeDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 25
        $llpFrozenProbe = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpNormalProbePath | ConvertFrom-Json
    } while ([string]$llpFrozenProbe.state -ne 'frozen-evidence' -and [DateTime]::UtcNow -lt $llpFreezeDeadline)
    Assert-Equal 'frozen-evidence' ([string]$llpFrozenProbe.state) 'Registered evidence message did not freeze the real renderer.'
    Assert-Equal $true ([bool]$llpFrozenProbe.evidenceFrozen) 'Frozen renderer diagnostics did not set evidenceFrozen.'
    Assert-Equal $true ([bool]$llpFrozenProbe.evidenceFreezeCommitSucceeded) 'Evidence freeze did not publish a successful real DComp commit.'
    Assert-Equal ([string]$llpBaselineProbe.evidenceBaselineDesktopHash) ([string]$llpFrozenProbe.evidenceFreezeDesktopHash) 'Evidence freeze did not use the exact last committed desktop texture.'
    Assert-Equal ([uint64]$llpBaselineProbe.evidenceBaselineFrameIdentity) ([uint64]$llpFrozenProbe.evidenceFreezeFrameIdentity) 'Evidence freeze did not use the exact last presented desktop frame.'
    Assert-Equal ([uint64]$llpBaselineProbe.evidenceBaselineSceneGeneration) ([uint64]$llpFrozenProbe.evidenceFreezeSceneGeneration) 'Evidence freeze did not use the exact last presented scene.'
    Assert-Equal ([uint64]$llpCandidateProbe.presentedFrames + 1) ([uint64]$llpFrozenProbe.presentedFrames) 'Evidence freeze did not commit exactly one final redraw.'
    $llpAffinity = [uint32]::MaxValue
    Assert-True ([LightLaunchpadMaterialProbeNative]::GetWindowDisplayAffinity($llpNormalWindow, [ref]$llpAffinity)) 'Could not read frozen window display affinity.'
    Assert-Equal 0 $llpAffinity 'Frozen evidence retained WDA_EXCLUDEFROMCAPTURE.'
    $llpFrozenGeneration = [uint64]$llpFrozenProbe.probeGeneration
    [void][LightLaunchpadMaterialProbeNative]::SendMessage($llpNormalWindow, $llpFreezeMessage, [UIntPtr]::Zero, [IntPtr]::Zero)
    [void][LightLaunchpadMaterialProbeNative]::SendMessage($llpNormalWindow, 0x804D, [UIntPtr]::new([uint64]2), [IntPtr]::Zero)
    $llpTerminalDeadline = [DateTime]::UtcNow.AddSeconds(2)
    do {
        Start-Sleep -Milliseconds 25
        $llpTerminalProbe = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpNormalProbePath | ConvertFrom-Json
    } while ([uint64]$llpTerminalProbe.probeGeneration -le $llpFrozenGeneration -and [DateTime]::UtcNow -lt $llpTerminalDeadline)
    Assert-Equal 'frozen-evidence' ([string]$llpTerminalProbe.state) 'A second freeze request resumed the terminal real renderer.'
    Assert-Equal ([uint64]$llpFrozenProbe.presentedFrames) ([uint64]$llpTerminalProbe.presentedFrames) 'Terminal frozen evidence presented another frame.'
    Assert-Equal ([string]$llpFrozenProbe.evidenceFreezeDesktopHash) ([string]$llpTerminalProbe.evidenceFreezeDesktopHash) 'Terminal frozen evidence changed its committed desktop proof.'
    Write-Output "METRIC MaterialFreezeEvidence baselineHash=$([string]$llpBaselineProbe.evidenceBaselineDesktopHash) candidateHash=$([string]$llpCandidateProbe.evidenceCandidateDesktopHash) freezeHash=$([string]$llpFrozenProbe.evidenceFreezeDesktopHash) baselineFrame=$([uint64]$llpBaselineProbe.evidenceBaselineFrameIdentity) candidateFrame=$([uint64]$llpCandidateProbe.evidenceCandidateFrameIdentity) scene=$([uint64]$llpFrozenProbe.evidenceFreezeSceneGeneration) presentedBefore=$([uint64]$llpCandidateProbe.presentedFrames) presentedAfter=$([uint64]$llpFrozenProbe.presentedFrames) terminal=true"
    [void][LightLaunchpadMaterialProbeNative]::SendMessage($llpNormalWindow, 0x804D, [UIntPtr]::new([uint64]1), [IntPtr]::Zero)
    [void][LightLaunchpadMaterialProbeNative]::SendMessage($llpNormalWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
    Assert-True ($llpNormalProcess.WaitForExit(8000)) 'Normal material probe did not exit.'
    Assert-Equal 0 $llpNormalProcess.ExitCode 'Normal material probe exit code mismatch.'

    New-Item -ItemType Directory -Force -Path $llpStableSource | Out-Null
    $llpTrackedFiles = @(& git -C $llpRepoRoot ls-files)
    Assert-True ($LASTEXITCODE -eq 0 -and $llpTrackedFiles.Count -gt 0) 'Could not enumerate tracked source for the isolated stable build.'
    foreach ($llpRelativePath in $llpTrackedFiles) {
        $llpSourcePath = [System.IO.Path]::GetFullPath((Join-Path $llpRepoRoot $llpRelativePath))
        Assert-True ($llpSourcePath.StartsWith($llpRepoRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) 'Tracked source escaped the repository boundary.'
        $llpSourceItem = Get-Item -LiteralPath $llpSourcePath -Force
        Assert-True (-not $llpSourceItem.PSIsContainer -and (($llpSourceItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0)) "Refusing to copy non-file or reparse source $llpSourcePath."
        $llpDestinationPath = Join-Path $llpStableSource $llpRelativePath
        $llpDestinationParent = Split-Path -Parent $llpDestinationPath
        if (-not (Test-Path -LiteralPath $llpDestinationParent)) { New-Item -ItemType Directory -Force -Path $llpDestinationParent | Out-Null }
        Copy-Item -LiteralPath $llpSourcePath -Destination $llpDestinationPath
    }
    & git -C $llpStableSource init --quiet
    if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the isolated stable source repository.' }
    & git -C $llpStableSource config core.autocrlf false
    if ($LASTEXITCODE -ne 0) { throw 'Could not configure isolated stable source line endings.' }
    & git -C $llpStableSource add --all
    if ($LASTEXITCODE -ne 0) { throw 'Could not stage the isolated stable source repository.' }
    & git -C $llpStableSource -c user.name=LightLaunchpadTest -c user.email=test@invalid commit --quiet -m baseline
    if ($LASTEXITCODE -ne 0) { throw 'Could not commit the isolated stable source repository.' }
    & (Join-Path $llpStableSource 'tools\build-native-ui.ps1') -OutputDirectory $llpStableOutput -ProductVersion '1.11.0' -BuildCommit $llpBuildCommit -BuildChannel Stable | Out-Null
    $llpStableExecutable = Join-Path $llpStableOutput 'LightLaunchpad.NativeUi.exe'
    $llpStableHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $llpStableExecutable).Hash
    $llpStableSymbols = ((& $llpObjdump -t $llpStableExecutable 2>&1) -join [Environment]::NewLine)
    Assert-Equal 0 $LASTEXITCODE 'objdump could not inspect the stable production executable.'
    Assert-True (-not $llpStableSymbols.Contains('LLP_MaterialRunFakeLifecycleScenario')) 'Stable production executable contained the private fake lifecycle runner.'
    $llpStableWindowEvents = 0
    foreach ($llpStableArguments in @(
        @('--material-probe-file', (Join-Path $llpTempRoot 'stable-probe.json')),
        @('--material-probe-ready-file', (Join-Path $llpTempRoot 'stable-ready.json')),
        @('--material-evidence'),
        @('--material-force-failure', 'duplication'),
        @('--test-monitor-device', $llpTestMonitorDevice)
    )) {
        $llpStableProcess = $null
        [LightLaunchpadMaterialProbeNative]::StartWindowEventMonitor()
        try {
            $llpPriorAppData = $env:APPDATA
            $llpPriorLocalAppData = $env:LOCALAPPDATA
            $llpPriorUserProfile = $env:USERPROFILE
            try {
                $env:APPDATA = $llpAppData
                $env:LOCALAPPDATA = $llpLocalAppData
                $env:USERPROFILE = $llpUserProfile
                $llpStableProcess = Start-Process -FilePath $llpStableExecutable -ArgumentList ([string[]]$llpStableArguments) -PassThru -WindowStyle Hidden
            }
            finally {
                $env:APPDATA = $llpPriorAppData
                $env:LOCALAPPDATA = $llpPriorLocalAppData
                $env:USERPROFILE = $llpPriorUserProfile
            }
            $llpCapturedProcesses += $llpStableProcess
            $llpStableDeadline = [DateTime]::UtcNow.AddSeconds(5)
            do {
                [LightLaunchpadMaterialProbeNative]::PumpWindowEvents()
                Start-Sleep -Milliseconds 10
                $llpStableProcess.Refresh()
            } while (-not $llpStableProcess.HasExited -and [DateTime]::UtcNow -lt $llpStableDeadline)
            for ($llpPump = 0; $llpPump -lt 8; $llpPump++) {
                [LightLaunchpadMaterialProbeNative]::PumpWindowEvents()
                Start-Sleep -Milliseconds 10
            }
            Assert-True $llpStableProcess.HasExited 'Stable material test switch did not exit before app startup.'
            Assert-Equal 2 $llpStableProcess.ExitCode 'Stable material test switch was not rejected with exit 2.'
            $llpStableProcessWindowEvents = [LightLaunchpadMaterialProbeNative]::WindowEventCount([uint32]$llpStableProcess.Id)
            $llpStableWindowEvents += $llpStableProcessWindowEvents
            Assert-Equal 0 $llpStableProcessWindowEvents 'Stable material switch emitted a top-level window create/show event before rejection.'
            Assert-Equal ([IntPtr]::Zero) ([LightLaunchpadMaterialProbeNative]::FindWindowForProcess([uint32]$llpStableProcess.Id)) 'Stable material switch left a window after rejection.'
        }
        finally {
            [LightLaunchpadMaterialProbeNative]::StopWindowEventMonitor()
        }
    }
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $llpTempRoot 'stable-probe.json'))) 'Stable material switch created a probe file before rejection.'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $llpTempRoot 'stable-ready.json'))) 'Stable material ready switch created a file before rejection.'

    $llpRunAfter = $null
    try { $llpRunAfter = (Get-ItemProperty -LiteralPath $llpRunKeyPath -Name LightLaunchpad -ErrorAction Stop).LightLaunchpad } catch {}
    Assert-Equal $llpRunBefore $llpRunAfter 'Material lifecycle harness changed the real startup registration.'
    $llpProcessIdsAfter = @(Get-Process -Name 'LightLaunchpad.NativeUi' -ErrorAction SilentlyContinue | ForEach-Object { $_.Id } | Sort-Object)
    Assert-Equal ($llpProcessIdsBefore -join ',') ($llpProcessIdsAfter -join ',') 'Material lifecycle harness left an uncaptured NativeUi process.'
    $llpHistoricalTempAfter = @(Get-ChildItem -LiteralPath ([System.IO.Path]::GetTempPath()) -Directory -Filter 'lightlaunchpad-*' | Where-Object { $_.FullName -ne $llpTempRoot } | ForEach-Object { $_.FullName } | Sort-Object)
    Assert-Equal ($llpHistoricalTempBefore -join '|') ($llpHistoricalTempAfter -join '|') 'Material lifecycle harness changed historical LightLaunchpad temp roots.'

    Write-Output "METRIC MaterialLifecycle exe='$llpExecutable' sha256=$llpExecutableHash stableSha256=$llpStableHash forcedAcquired=$([uint64]$llpHidden.acquiredFrames) hiddenGeneration=$llpHiddenGeneration refreshedGeneration=$([uint64]$llpHiddenAgain.probeGeneration) normalPath=$([string]$llpNormalProbe.path) normalFailure=$([string]$llpNormalProbe.failure) normalAcquired=$([uint64]$llpNormalProbe.acquiredFrames) normalRebuilds=$([uint64]$llpNormalProbe.deviceRebuilds) normalLastFrame=$([uint64]$llpNormalProbe.lastFrameIdentity)"
    Write-Output "METRIC MaterialIsolation protectedPids=$($llpProcessIdsAfter -join ',') historicalTempRoots=$($llpHistoricalTempAfter.Count) stableWindowEvents=$llpStableWindowEvents productionFakeSymbol=false startupUnchanged=true"
    Write-Output 'PASS MaterialRendererBehavior_ForcedFallbackSearchAndHiddenLifecycle'
}
finally {
    foreach ($llpCapturedProcess in $llpCapturedProcesses) {
        if ($llpCapturedProcess -and -not $llpCapturedProcess.HasExited) {
            Stop-Process -Id $llpCapturedProcess.Id -Force
            $llpCapturedProcess.WaitForExit()
        }
    }
    $llpResolvedTemp = [System.IO.Path]::GetFullPath($llpTempRoot)
    $llpExpectedTempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    if ($llpResolvedTemp.StartsWith($llpExpectedTempRoot, [System.StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $llpResolvedTemp)) {
        $llpTempItem = Get-Item -LiteralPath $llpResolvedTemp -Force
        if (($llpTempItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Refusing to clean reparse-point test output $llpResolvedTemp." }
        Remove-Item -LiteralPath $llpResolvedTemp -Recurse -Force
    }
}
