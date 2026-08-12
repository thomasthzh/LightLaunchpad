param(
    [ValidateSet('Lifecycle', 'Host')]
    [string]$Mode = 'Lifecycle'
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
$llpNormalProbePath = Join-Path $llpTempRoot 'material-normal-probe.json'
$llpLogPath = Join-Path $llpTempRoot 'material-startup.log'
$llpStableSource = Join-Path $llpTempRoot 'stable-source'
$llpStableOutput = Join-Path $llpTempRoot 'stable-output'
$llpProcess = $null
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
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $llpSpotlightScript
        Assert-Equal 0 $LASTEXITCODE 'Normal high-fidelity Spotlight host flow failed.'
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $llpSpotlightScript -MaterialFailure initialization
        Assert-Equal 0 $LASTEXITCODE 'Forced-initialization fallback Spotlight host flow failed.'
        Write-Output 'PASS MaterialRendererBehavior_HostHighFidelityAndFallbackCanonicalFlows'
        return
    }

    Assert-Equal 'Lifecycle' $Mode 'Unsupported material behavior mode.'
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
        $llpProcess = Start-Process -FilePath $llpExecutable -ArgumentList @('--material-probe-file', $llpProbePath, '--material-force-failure', 'duplication', '--diagnostics-log-file', $llpLogPath) -PassThru -WindowStyle Hidden
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

    [void][LightLaunchpadMaterialProbeNative]::SendMessage($llpWindow, 0x0312, [UIntPtr]::new([uint64]0x4C55), [IntPtr]::Zero)
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

    Wait-LlpFile $llpProbePath
    $llpProbe = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpProbePath | ConvertFrom-Json
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
    Assert-True ($llpHiddenGeneration -gt 0) 'Hidden probe JSON did not expose a real snapshot generation.'
    Start-Sleep -Seconds 2
    [void][LightLaunchpadMaterialProbeNative]::SendMessage($llpWindow, 0x804D, [UIntPtr]::new([uint64]2), [IntPtr]::Zero)
    $llpRefreshDeadline = [DateTime]::UtcNow.AddSeconds(2)
    do {
        Start-Sleep -Milliseconds 25
        $llpHiddenAgain = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpProbePath | ConvertFrom-Json
    } while ([uint64]$llpHiddenAgain.probeGeneration -le $llpHiddenGeneration -and [DateTime]::UtcNow -lt $llpRefreshDeadline)
    Assert-True ([uint64]$llpHiddenAgain.probeGeneration -gt $llpHiddenGeneration) 'Explicit hidden snapshot did not refresh probe JSON after two idle seconds.'
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
        @('--material-probe-file', (Join-Path $llpTempRoot 'invalid.json'), '--material-force-failure', 'not-valid')
    )) {
        $llpBadProcess = Start-Process -FilePath $llpExecutable -ArgumentList ([string[]]$llpBadArguments) -PassThru -WindowStyle Hidden
        $llpCapturedProcesses += $llpBadProcess
        Assert-True ($llpBadProcess.WaitForExit(5000)) 'Invalid development material switches did not exit.'
        Assert-Equal 2 $llpBadProcess.ExitCode 'Invalid development material switches were not rejected with exit 2.'
    }

    $llpPriorAppData = $env:APPDATA
    $llpPriorLocalAppData = $env:LOCALAPPDATA
    $llpPriorUserProfile = $env:USERPROFILE
    try {
        $env:APPDATA = $llpAppData
        $env:LOCALAPPDATA = $llpLocalAppData
        $env:USERPROFILE = $llpUserProfile
        $llpNormalProcess = Start-Process -FilePath $llpExecutable -ArgumentList @('--material-probe-file', $llpNormalProbePath) -PassThru -WindowStyle Hidden
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
    [void][LightLaunchpadMaterialProbeNative]::SendMessage($llpNormalWindow, 0x0312, [UIntPtr]::new([uint64]0x4C55), [IntPtr]::Zero)
    Wait-LlpFile $llpNormalProbePath
    $llpNormalStateDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 25
        $llpNormalProbe = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpNormalProbePath | ConvertFrom-Json
    } while ([string]$llpNormalProbe.state -notin @('live', 'fallback') -and [DateTime]::UtcNow -lt $llpNormalStateDeadline)
    Assert-True (([string]$llpNormalProbe.state -eq 'live' -and [string]$llpNormalProbe.path -eq 'high-fidelity' -and [uint64]$llpNormalProbe.acquiredFrames -gt 0) -or ([string]$llpNormalProbe.state -eq 'fallback' -and [string]$llpNormalProbe.path -eq 'fallback' -and [string]$llpNormalProbe.failure -ne 'None')) 'Normal material probe reported neither a captured frame nor a stable machine-specific fallback.'
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
        @('--material-force-failure', 'duplication')
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
