param()

$ErrorActionPreference = 'Stop'

$llpRepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$llpTempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("lightlaunchpad-settings-tests-" + [System.Guid]::NewGuid().ToString('N'))
$llpOutputDirectory = Join-Path $llpTempRoot 'development'
$llpAppData = Join-Path $llpTempRoot 'appdata'
$llpLocalAppData = Join-Path $llpTempRoot 'localappdata'
$llpSettingsPath = Join-Path $llpAppData 'LightLaunchpad\settings.json'
$llpProcess = $null
$llpConflictReservation = $null
$llpSettingsBackupPath = "$llpSettingsPath.backup"
$llpRegistryTestId = [System.Guid]::NewGuid().ToString('N')
$llpStartupRegistrySubkey = "Software\LightLaunchpad\Tests\$llpRegistryTestId"
$llpStartupRegistryPsPath = "HKCU:\Software\LightLaunchpad\Tests\$llpRegistryTestId"

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

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace LightLaunchpad.Tests
{
    public sealed class ReservedHotkey : IDisposable
    {
        private const uint WmQuit = 0x0012;
        private static int nextId = 0x5A00;
        private readonly uint modifiers;
        private readonly uint key;
        private readonly int id;
        private readonly ManualResetEvent ready = new ManualResetEvent(false);
        private readonly Thread thread;
        private uint threadId;
        private bool registered;

        [StructLayout(LayoutKind.Sequential)]
        private struct Message
        {
            public IntPtr window;
            public uint message;
            public UIntPtr wParam;
            public IntPtr lParam;
            public uint time;
            public int x;
            public int y;
        }

        [DllImport("user32.dll")]
        private static extern bool RegisterHotKey(IntPtr window, int id, uint modifiers, uint key);

        [DllImport("user32.dll")]
        private static extern bool UnregisterHotKey(IntPtr window, int id);

        [DllImport("user32.dll")]
        private static extern int GetMessage(out Message message, IntPtr window, uint minimum, uint maximum);

        [DllImport("user32.dll")]
        private static extern bool PostThreadMessage(uint threadId, uint message, UIntPtr wParam, IntPtr lParam);

        [DllImport("kernel32.dll")]
        private static extern uint GetCurrentThreadId();

        private ReservedHotkey(uint modifiers, uint key)
        {
            this.modifiers = modifiers;
            this.key = key;
            this.id = Interlocked.Increment(ref nextId);
            this.thread = new Thread(Run);
            this.thread.IsBackground = true;
            this.thread.Start();
            this.ready.WaitOne(3000);
        }

        public static ReservedHotkey TryCreate(uint modifiers, uint key)
        {
            ReservedHotkey reservation = new ReservedHotkey(modifiers, key);
            if (!reservation.registered)
            {
                reservation.Dispose();
                return null;
            }
            return reservation;
        }

        private void Run()
        {
            threadId = GetCurrentThreadId();
            registered = RegisterHotKey(IntPtr.Zero, id, modifiers, key);
            ready.Set();
            if (!registered) return;
            Message message;
            while (GetMessage(out message, IntPtr.Zero, 0, 0) > 0) { }
            UnregisterHotKey(IntPtr.Zero, id);
            registered = false;
        }

        public void Dispose()
        {
            if (threadId != 0 && thread.IsAlive)
            {
                PostThreadMessage(threadId, WmQuit, UIntPtr.Zero, IntPtr.Zero);
                thread.Join(3000);
            }
            ready.Dispose();
        }
    }

    public static class SettingsWindows
    {
        private const uint WmClose = 0x0010;
        private const uint WmCommand = 0x0111;
        private const uint WmGetText = 0x000D;
        private const uint WmGetTextLength = 0x000E;
        private const uint WmSetText = 0x000C;
        private const uint BmGetCheck = 0x00F0;
        private const uint BmSetCheck = 0x00F1;
        private delegate bool EnumWindowsCallback(IntPtr window, IntPtr parameter);

        [DllImport("user32.dll")]
        private static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr parameter);

        [DllImport("user32.dll")]
        private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern int GetClassName(IntPtr window, StringBuilder className, int capacity);

        [DllImport("user32.dll")]
        public static extern IntPtr GetDlgItem(IntPtr parent, int controlId);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wParam, StringBuilder lParam);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wParam, string lParam);

        [DllImport("user32.dll")]
        private static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll")]
        private static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

        public static IntPtr FindWindowForProcess(int processId, string expectedClassName)
        {
            IntPtr found = IntPtr.Zero;
            EnumWindows(delegate(IntPtr window, IntPtr parameter)
            {
                uint ownerProcessId;
                GetWindowThreadProcessId(window, out ownerProcessId);
                if (ownerProcessId != (uint)processId) return true;
                StringBuilder className = new StringBuilder(256);
                GetClassName(window, className, className.Capacity);
                if (String.Equals(className.ToString(), expectedClassName, StringComparison.Ordinal))
                {
                    found = window;
                    return false;
                }
                return true;
            }, IntPtr.Zero);
            return found;
        }

        public static void SendCommand(IntPtr window, int command)
        {
            PostMessage(window, WmCommand, new IntPtr(command), IntPtr.Zero);
        }

        public static void SetText(IntPtr control, string value)
        {
            SendMessage(control, WmSetText, IntPtr.Zero, value);
        }

        public static string ReadText(IntPtr control)
        {
            int length = SendMessage(control, WmGetTextLength, IntPtr.Zero, IntPtr.Zero).ToInt32();
            StringBuilder value = new StringBuilder(length + 1);
            SendMessage(control, WmGetText, new IntPtr(value.Capacity), value);
            return value.ToString();
        }

        public static void SetChecked(IntPtr control, bool isChecked)
        {
            SendMessage(control, BmSetCheck, isChecked ? new IntPtr(1) : IntPtr.Zero, IntPtr.Zero);
        }

        public static bool IsChecked(IntPtr control)
        {
            return SendMessage(control, BmGetCheck, IntPtr.Zero, IntPtr.Zero).ToInt32() == 1;
        }

        public static void Close(IntPtr window)
        {
            PostMessage(window, WmClose, IntPtr.Zero, IntPtr.Zero);
        }
    }
}
'@

function Wait-ForWindow {
    param([System.Diagnostics.Process]$Process, [string]$ClassName, [int]$TimeoutMilliseconds = 8000)
    $llpDeadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    $llpWindow = [IntPtr]::Zero
    while ($llpWindow -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $llpDeadline) {
        Start-Sleep -Milliseconds 50
        $llpWindow = [LightLaunchpad.Tests.SettingsWindows]::FindWindowForProcess($Process.Id, $ClassName)
    }
    return $llpWindow
}

function Start-IsolatedProcess {
    param([string]$Executable, [string[]]$ArgumentList)
    $llpPriorAppData = $env:APPDATA
    $llpPriorLocalAppData = $env:LOCALAPPDATA
    try {
        $env:APPDATA = $llpAppData
        $env:LOCALAPPDATA = $llpLocalAppData
        return Start-Process -FilePath $Executable -ArgumentList $ArgumentList -PassThru -WindowStyle Hidden
    }
    finally {
        $env:APPDATA = $llpPriorAppData
        $env:LOCALAPPDATA = $llpPriorLocalAppData
    }
}

$llpModAlt = 0x0001
$llpModControl = 0x0002
$llpModShift = 0x0004
$llpModNoRepeat = 0x4000
$llpModifiers = $llpModAlt -bor $llpModControl -bor $llpModShift -bor $llpModNoRepeat
$llpInitialHotkey = 'Ctrl+Alt+Shift+9'
$llpConflictHotkey = 'Ctrl+Alt+Shift+8'
$llpWriteFailureHotkey = 'Ctrl+Alt+Shift+7'

try {
    $llpActualStartupProperties = Get-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    $llpActualStartupBefore = [string]$llpActualStartupProperties.LightLaunchpad
    Assert-True (-not [string]::IsNullOrEmpty($llpActualStartupBefore)) 'The protected LightLaunchpad Run baseline is missing; refusing to start the settings test.'
    New-Item -ItemType Directory -Force -Path $llpOutputDirectory | Out-Null
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $llpSettingsPath) | Out-Null
    New-Item -ItemType Directory -Force -Path $llpLocalAppData | Out-Null
    $llpLaunchpadFolder = Join-Path $llpTempRoot 'launchpad'
    New-Item -ItemType Directory -Force -Path $llpLaunchpadFolder | Out-Null
    $llpSettings = [ordered]@{
        LaunchpadFolder = $llpLaunchpadFolder
        Hotkey = $llpInitialHotkey
        DisplayMode = 'Spotlight'
        StartWithWindows = $false
        ResetSearchOnOpen = $true
    } | ConvertTo-Json
    [System.IO.File]::WriteAllText($llpSettingsPath, $llpSettings, [System.Text.UTF8Encoding]::new($false))

    & (Join-Path $llpRepoRoot 'tools\build-native-ui.ps1') `
        -OutputDirectory $llpOutputDirectory `
        -ProductVersion '1.11.0' `
        -BuildCommit 'settingstransactiontest' `
        -BuildChannel Development | Out-Null
    $llpExecutable = Join-Path $llpOutputDirectory 'LightLaunchpad.NativeUi.exe'
    $llpProcess = Start-IsolatedProcess -Executable $llpExecutable -ArgumentList @(
        '--diagnostics-log-file', (Join-Path $llpTempRoot 'settings.log'),
        '--startup-registry-subkey', $llpStartupRegistrySubkey
    )
    $llpMainWindow = Wait-ForWindow -Process $llpProcess -ClassName 'LightLaunchpadNativeUiWindow.development.settingstransactiontest'
    Assert-True ($llpMainWindow -ne [IntPtr]::Zero) 'The settings transaction test EXE did not create its main window.'

    $llpInitialProbe = [LightLaunchpad.Tests.ReservedHotkey]::TryCreate($llpModifiers, [uint32][char]'9')
    if ($llpInitialProbe) {
        $llpInitialProbe.Dispose()
        throw 'The product did not own the initial hotkey before the transaction test.'
    }

    [LightLaunchpad.Tests.SettingsWindows]::SendCommand($llpMainWindow, 1004)
    $llpSettingsWindow = Wait-ForWindow -Process $llpProcess -ClassName 'LightLaunchpadNativeSettingsWindow'
    Assert-True ($llpSettingsWindow -ne [IntPtr]::Zero) 'The real NativeUi settings window did not open.'
    $llpHotkeyControl = [LightLaunchpad.Tests.SettingsWindows]::GetDlgItem($llpSettingsWindow, 3002)
    $llpDiagnosticsControl = [LightLaunchpad.Tests.SettingsWindows]::GetDlgItem($llpSettingsWindow, 3015)
    $llpStartupControl = [LightLaunchpad.Tests.SettingsWindows]::GetDlgItem($llpSettingsWindow, 3010)
    Assert-True ($llpHotkeyControl -ne [IntPtr]::Zero -and $llpDiagnosticsControl -ne [IntPtr]::Zero -and $llpStartupControl -ne [IntPtr]::Zero) 'Required settings controls were not found.'

    $llpConflictReservation = [LightLaunchpad.Tests.ReservedHotkey]::TryCreate($llpModifiers, [uint32][char]'8')
    Assert-True ($null -ne $llpConflictReservation) 'The test could not reserve the candidate conflict hotkey.'
    [LightLaunchpad.Tests.SettingsWindows]::SetText($llpHotkeyControl, $llpConflictHotkey)
    [LightLaunchpad.Tests.SettingsWindows]::SendCommand($llpSettingsWindow, 3012)
    Start-Sleep -Milliseconds 500

    Assert-Equal $llpInitialHotkey ([LightLaunchpad.Tests.SettingsWindows]::ReadText($llpHotkeyControl)) 'Conflict UI did not retain the prior hotkey.'
    $llpPersisted = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpSettingsPath | ConvertFrom-Json
    Assert-Equal $llpInitialHotkey ([string]$llpPersisted.Hotkey) 'Conflict persisted the rejected candidate hotkey.'
    $llpDiagnostics = [LightLaunchpad.Tests.SettingsWindows]::ReadText($llpDiagnosticsControl)
    Assert-True ($llpDiagnostics.IndexOf('conflict', [System.StringComparison]::OrdinalIgnoreCase) -ge 0) 'Settings did not show the hotkey conflict reason.'
    Assert-True ($llpDiagnostics.IndexOf('kept', [System.StringComparison]::OrdinalIgnoreCase) -ge 0) 'Settings did not say that the prior hotkey was retained.'

    $llpRetainedProbe = [LightLaunchpad.Tests.ReservedHotkey]::TryCreate($llpModifiers, [uint32][char]'9')
    if ($llpRetainedProbe) {
        $llpRetainedProbe.Dispose()
        throw 'The rejected hotkey transaction released the prior working hotkey.'
    }
    Write-Output 'PASS SettingsTransaction_HotkeyConflictKeepsPriorRegistrationAndPersistence'

    $llpConflictReservation.Dispose()
    $llpConflictReservation = $null
    [LightLaunchpad.Tests.SettingsWindows]::SetText($llpHotkeyControl, $llpConflictHotkey)
    [LightLaunchpad.Tests.SettingsWindows]::SendCommand($llpSettingsWindow, 3012)
    Start-Sleep -Milliseconds 500
    Assert-Equal $llpConflictHotkey ([LightLaunchpad.Tests.SettingsWindows]::ReadText($llpHotkeyControl)) 'Successful hotkey transaction did not update the UI.'
    $llpPersisted = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpSettingsPath | ConvertFrom-Json
    Assert-Equal $llpConflictHotkey ([string]$llpPersisted.Hotkey) 'Successful hotkey transaction did not persist the candidate.'
    $llpDiagnostics = [LightLaunchpad.Tests.SettingsWindows]::ReadText($llpDiagnosticsControl)
    Assert-True ($llpDiagnostics.IndexOf('Settings saved', [System.StringComparison]::OrdinalIgnoreCase) -ge 0) 'Successful settings transaction did not report success.'

    $llpReleasedOldProbe = [LightLaunchpad.Tests.ReservedHotkey]::TryCreate($llpModifiers, [uint32][char]'9')
    Assert-True ($null -ne $llpReleasedOldProbe) 'Successful transaction did not release the prior hotkey.'
    $llpReleasedOldProbe.Dispose()
    $llpActiveCandidateProbe = [LightLaunchpad.Tests.ReservedHotkey]::TryCreate($llpModifiers, [uint32][char]'8')
    if ($llpActiveCandidateProbe) {
        $llpActiveCandidateProbe.Dispose()
        throw 'Successful transaction did not leave the candidate hotkey registered by the product.'
    }
    Write-Output 'PASS SettingsTransaction_SuccessCommitsCandidateThenReleasesPriorHotkey'

    Move-Item -LiteralPath $llpSettingsPath -Destination $llpSettingsBackupPath
    New-Item -ItemType Directory -Path $llpSettingsPath | Out-Null
    [LightLaunchpad.Tests.SettingsWindows]::SetText($llpHotkeyControl, $llpWriteFailureHotkey)
    [LightLaunchpad.Tests.SettingsWindows]::SetChecked($llpStartupControl, $true)
    [LightLaunchpad.Tests.SettingsWindows]::SendCommand($llpSettingsWindow, 3012)
    Start-Sleep -Milliseconds 500
    Assert-Equal $llpConflictHotkey ([LightLaunchpad.Tests.SettingsWindows]::ReadText($llpHotkeyControl)) 'Write failure did not restore the prior UI value.'
    $llpBackupSettings = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpSettingsBackupPath | ConvertFrom-Json
    Assert-Equal $llpConflictHotkey ([string]$llpBackupSettings.Hotkey) 'Write failure changed the last persisted settings file.'
    Assert-Equal $false (Test-Path -LiteralPath "$llpSettingsPath.tmp") 'Write failure left a temporary settings file behind.'
    $llpDiagnostics = [LightLaunchpad.Tests.SettingsWindows]::ReadText($llpDiagnosticsControl)
    Assert-True ($llpDiagnostics.IndexOf('write failed', [System.StringComparison]::OrdinalIgnoreCase) -ge 0) 'Settings did not show the persistence failure reason.'
    Assert-True ($llpDiagnostics.IndexOf('kept', [System.StringComparison]::OrdinalIgnoreCase) -ge 0) 'Settings did not say that prior state was retained after write failure.'
    Assert-Equal $false ([LightLaunchpad.Tests.SettingsWindows]::IsChecked($llpStartupControl)) 'Write failure did not restore the prior startup checkbox state.'

    $llpIsolatedStartupProperties = Get-ItemProperty -LiteralPath $llpStartupRegistryPsPath -ErrorAction SilentlyContinue
    $llpIsolatedStartupValue = if ($llpIsolatedStartupProperties) { [string]$llpIsolatedStartupProperties.LightLaunchpad } else { '' }
    Assert-True ([string]::IsNullOrEmpty([string]$llpIsolatedStartupValue)) 'Settings failure did not roll back the isolated startup registration value.'
    $llpActualStartupAfter = [string](Get-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run').LightLaunchpad
    Assert-Equal $llpActualStartupBefore $llpActualStartupAfter 'The settings test changed the real LightLaunchpad Run value.'

    $llpStillActiveProbe = [LightLaunchpad.Tests.ReservedHotkey]::TryCreate($llpModifiers, [uint32][char]'8')
    if ($llpStillActiveProbe) {
        $llpStillActiveProbe.Dispose()
        throw 'Settings write failure released the prior working hotkey.'
    }
    $llpRejectedWriteProbe = [LightLaunchpad.Tests.ReservedHotkey]::TryCreate($llpModifiers, [uint32][char]'7')
    Assert-True ($null -ne $llpRejectedWriteProbe) 'Settings write failure left the rejected candidate hotkey registered.'
    $llpRejectedWriteProbe.Dispose()
    Write-Output 'PASS SettingsTransaction_WriteFailureRollsBackHotkeyStartupAndPriorState'

    Remove-Item -LiteralPath $llpSettingsPath
    Move-Item -LiteralPath $llpSettingsBackupPath -Destination $llpSettingsPath
}
finally {
    if ($llpConflictReservation) {
        $llpConflictReservation.Dispose()
    }
    if ($llpProcess -and -not $llpProcess.HasExited) {
        $llpMainWindow = [LightLaunchpad.Tests.SettingsWindows]::FindWindowForProcess($llpProcess.Id, 'LightLaunchpadNativeUiWindow.development.settingstransactiontest')
        if ($llpMainWindow -ne [IntPtr]::Zero) {
            [LightLaunchpad.Tests.SettingsWindows]::Close($llpMainWindow)
        }
        if (-not $llpProcess.WaitForExit(3000)) {
            Stop-Process -Id $llpProcess.Id -Force
            $llpProcess.WaitForExit()
        }
    }
    if (Test-Path -LiteralPath $llpSettingsPath -PathType Container) {
        Remove-Item -LiteralPath $llpSettingsPath
    }
    if ((Test-Path -LiteralPath $llpSettingsBackupPath -PathType Leaf) -and -not (Test-Path -LiteralPath $llpSettingsPath)) {
        Move-Item -LiteralPath $llpSettingsBackupPath -Destination $llpSettingsPath
    }
    if ($llpStartupRegistryPsPath.StartsWith('HKCU:\Software\LightLaunchpad\Tests\', [System.StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $llpStartupRegistryPsPath)) {
        Remove-Item -LiteralPath $llpStartupRegistryPsPath -Force
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
