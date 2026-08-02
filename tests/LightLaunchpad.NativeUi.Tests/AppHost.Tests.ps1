param(
    [switch]$RestartExplorer
)

$ErrorActionPreference = 'Stop'

$llpRepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$llpTempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("lightlaunchpad-apphost-tests-" + [System.Guid]::NewGuid().ToString('N'))
$llpOutputDirectory = Join-Path $llpTempRoot 'development'
$llpAppData = Join-Path $llpTempRoot 'appdata'
$llpLocalAppData = Join-Path $llpTempRoot 'localappdata'
$llpProbePath = Join-Path $llpTempRoot 'startup-probe.json'
$llpLogPath = Join-Path $llpTempRoot 'logs\apphost.log'
$llpProcess = $null
$llpPrimaryProcess = $null
$llpStablePrimaryProcess = $null
$llpExplorerRecoveryProcess = $null
$llpExplorerRestartAttempted = $false

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

function Initialize-NativeWindowInspector {
    if ('LightLaunchpad.Tests.AppHostWindows' -as [type]) {
        return
    }

    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace LightLaunchpad.Tests
{
    public static class AppHostWindows
    {
        private const uint WmClose = 0x0010;
        private const uint WmCommand = 0x0111;
        private const int SwMinimize = 6;
        private delegate bool EnumWindowsCallback(IntPtr window, IntPtr parameter);

        [DllImport("user32.dll")]
        private static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr parameter);

        [DllImport("user32.dll")]
        private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern int GetClassName(IntPtr window, StringBuilder className, int capacity);

        [DllImport("user32.dll")]
        private static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll")]
        private static extern bool ShowWindowAsync(IntPtr window, int command);

        [DllImport("user32.dll")]
        public static extern bool IsIconic(IntPtr window);

        public static IntPtr FindWindowForProcess(int processId, string expectedClassName)
        {
            IntPtr found = IntPtr.Zero;
            EnumWindows(delegate(IntPtr window, IntPtr parameter)
            {
                uint ownerProcessId;
                GetWindowThreadProcessId(window, out ownerProcessId);
                if (ownerProcessId != (uint)processId)
                {
                    return true;
                }

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

        public static void Minimize(IntPtr window)
        {
            ShowWindowAsync(window, SwMinimize);
        }

        public static void Close(IntPtr window)
        {
            if (window != IntPtr.Zero)
            {
                PostMessage(window, WmClose, IntPtr.Zero, IntPtr.Zero);
            }
        }

        public static void SendCommand(IntPtr window, int command)
        {
            PostMessage(window, WmCommand, new IntPtr(command), IntPtr.Zero);
        }
    }
}
'@
}

function Start-IsolatedProcess {
    param(
        [string]$Executable,
        [string[]]$ArgumentList
    )

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

function Wait-ForWindow {
    param(
        [System.Diagnostics.Process]$Process,
        [string]$ClassName,
        [int]$TimeoutMilliseconds = 8000
    )

    $llpDeadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    $llpWindow = [IntPtr]::Zero
    while ($llpWindow -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $llpDeadline) {
        Start-Sleep -Milliseconds 50
        $llpWindow = [LightLaunchpad.Tests.AppHostWindows]::FindWindowForProcess($Process.Id, $ClassName)
    }
    return $llpWindow
}

$llpSourcePath = Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\LightLaunchpad.NativeUi.cpp'
$llpSource = Get-Content -Raw -LiteralPath $llpSourcePath
foreach ($llpForbidden in @(
    'NotifyIconSettings',
    'IsPromoted',
    'PromoteTrayIconVisibility',
    'TrayIconPromotionRetryTimerId'
)) {
    if ($llpSource.IndexOf($llpForbidden, [System.StringComparison]::Ordinal) -ge 0) {
        throw "Native AppHost still contains forbidden private tray implementation '$llpForbidden'."
    }
}
Write-Output 'PASS AppHostArchitecture_DoesNotMutatePrivateTrayRegistry'

try {
    New-Item -ItemType Directory -Force -Path $llpOutputDirectory | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $llpAppData 'LightLaunchpad') | Out-Null
    New-Item -ItemType Directory -Force -Path $llpLocalAppData | Out-Null

    $llpUnavailableFolder = "\\127.0.0.1\llp-unavailable-$([System.Guid]::NewGuid().ToString('N'))"
    $llpSettings = [ordered]@{
        LaunchpadFolder = $llpUnavailableFolder
        Hotkey = 'Alt+Z'
        DisplayMode = 'Spotlight'
    } | ConvertTo-Json
    [System.IO.File]::WriteAllText(
        (Join-Path $llpAppData 'LightLaunchpad\settings.json'),
        $llpSettings,
        [System.Text.UTF8Encoding]::new($false))

    & (Join-Path $llpRepoRoot 'tools\build-native-ui.ps1') `
        -OutputDirectory $llpOutputDirectory `
        -ProductVersion '1.11.0' `
        -BuildCommit 'apphostshelltest' `
        -BuildChannel Development | Out-Null

    $llpExecutable = Join-Path $llpOutputDirectory 'LightLaunchpad.NativeUi.exe'
    $llpPriorAppData = $env:APPDATA
    $llpPriorLocalAppData = $env:LOCALAPPDATA
    try {
        $env:APPDATA = $llpAppData
        $env:LOCALAPPDATA = $llpLocalAppData
        $llpStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
        $llpProcess = Start-Process -FilePath $llpExecutable -ArgumentList @(
            '--startup-probe-file', $llpProbePath,
            '--diagnostics-log-file', $llpLogPath,
            '--simulate-taskbar-created',
            '--exit-after-startup-probe'
        ) -PassThru -WindowStyle Hidden
    }
    finally {
        $env:APPDATA = $llpPriorAppData
        $env:LOCALAPPDATA = $llpPriorLocalAppData
    }

    if (-not $llpProcess.WaitForExit(5000)) {
        throw 'The real NativeUi EXE did not complete the shell-first startup probe within 5 seconds.'
    }
    $llpStopwatch.Stop()
    Assert-Equal 0 $llpProcess.ExitCode 'Shell-first startup probe exit code mismatch.'
    Assert-True (Test-Path -LiteralPath $llpProbePath -PathType Leaf) 'The real NativeUi EXE did not write a startup probe.'
    Assert-True (Test-Path -LiteralPath $llpLogPath -PathType Leaf) 'The real NativeUi EXE did not write a bounded startup log.'

    $llpProbe = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpProbePath | ConvertFrom-Json
    Assert-Equal 'primary' ([string]$llpProbe.processRole) 'Startup probe process role mismatch.'
    Assert-Equal $true ([bool]$llpProbe.windowCreated) 'The hidden message window was not created.'
    Assert-Equal $false ([bool]$llpProbe.catalogStartedBeforeShell) 'Catalog I/O began before shell readiness.'
    Assert-True ([int64]$llpProbe.shellReadyMs -le 1000) "Shell readiness exceeded 1000 ms: $($llpProbe.shellReadyMs) ms."
    Assert-True ([int]$llpProbe.trayAddAttempts -ge 2) 'The TaskbarCreated simulation did not attempt to restore the tray icon.'
    Assert-True ([int]$llpProbe.taskbarRecoveryCount -ge 1) 'The TaskbarCreated recovery path did not run.'
    Assert-True ($llpStopwatch.ElapsedMilliseconds -le 3000) "The probe process took too long to exit: $($llpStopwatch.ElapsedMilliseconds) ms."

    $llpLogItem = Get-Item -LiteralPath $llpLogPath
    Assert-True ($llpLogItem.Length -gt 0) 'The startup log is empty.'
    Assert-True ($llpLogItem.Length -le 262144) "The startup log exceeded its 256 KiB bound: $($llpLogItem.Length) bytes."
    $llpLog = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpLogPath
    Assert-True ($llpLog.IndexOf('shell-ready', [System.StringComparison]::Ordinal) -ge 0) 'The startup log did not record shell readiness.'

    Write-Output 'PASS AppHostBehavior_ShellIsReadyBeforeUnavailableCatalogAndRecoversTaskbar'

    Initialize-NativeWindowInspector
    $llpDevelopmentWindowClass = 'LightLaunchpadNativeUiWindow.development.apphostshelltest'

    if ($RestartExplorer) {
        $llpExplorerProbePath = Join-Path $llpTempRoot 'explorer-recovery-probe.json'
        $llpExplorerRecoveryProcess = Start-IsolatedProcess -Executable $llpExecutable -ArgumentList @(
            '--startup-probe-file', $llpExplorerProbePath,
            '--diagnostics-log-file', $llpLogPath
        )
        $llpExplorerMainWindow = Wait-ForWindow -Process $llpExplorerRecoveryProcess -ClassName $llpDevelopmentWindowClass
        Assert-True ($llpExplorerMainWindow -ne [IntPtr]::Zero) 'Explorer recovery primary did not create its message window.'
        $llpExplorerProbeDeadline = [DateTime]::UtcNow.AddSeconds(5)
        while (-not (Test-Path -LiteralPath $llpExplorerProbePath -PathType Leaf) -and [DateTime]::UtcNow -lt $llpExplorerProbeDeadline) {
            Start-Sleep -Milliseconds 50
        }
        Assert-True (Test-Path -LiteralPath $llpExplorerProbePath -PathType Leaf) 'Explorer recovery primary did not write its initial probe.'

        $llpCurrentSessionId = [System.Diagnostics.Process]::GetCurrentProcess().SessionId
        $llpExplorerProcesses = @(Get-Process -Name explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $llpCurrentSessionId })
        Assert-True ($llpExplorerProcesses.Count -ge 1) 'No Explorer shell process was found in the current session.'
        $llpExplorerRestartAttempted = $true
        foreach ($llpExplorerProcess in $llpExplorerProcesses) {
            Stop-Process -Id $llpExplorerProcess.Id -Force
        }

        $llpExplorerReturnDeadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            Start-Sleep -Milliseconds 100
            $llpReturnedExplorer = @(Get-Process -Name explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $llpCurrentSessionId })
        } while ($llpReturnedExplorer.Count -eq 0 -and [DateTime]::UtcNow -lt $llpExplorerReturnDeadline)
        if ($llpReturnedExplorer.Count -eq 0) {
            Start-Process -FilePath (Join-Path $env:WINDIR 'explorer.exe') | Out-Null
        }

        $llpRecoveryDeadline = [DateTime]::UtcNow.AddSeconds(12)
        $llpExplorerProbe = $null
        do {
            Start-Sleep -Milliseconds 100
            if (Test-Path -LiteralPath $llpExplorerProbePath -PathType Leaf) {
                try {
                    $llpExplorerProbe = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpExplorerProbePath | ConvertFrom-Json
                }
                catch {
                    $llpExplorerProbe = $null
                }
            }
        } while (($null -eq $llpExplorerProbe -or [int]$llpExplorerProbe.taskbarRecoveryCount -lt 1) -and [DateTime]::UtcNow -lt $llpRecoveryDeadline)
        Assert-True ($null -ne $llpExplorerProbe -and [int]$llpExplorerProbe.taskbarRecoveryCount -ge 1) 'The real Explorer restart did not reach the TaskbarCreated recovery path.'
        Assert-Equal $true ([bool]$llpExplorerProbe.trayAdded) 'The tray icon was not re-added after the real Explorer restart.'

        [LightLaunchpad.Tests.AppHostWindows]::SendCommand($llpExplorerMainWindow, 1004)
        $llpRecoveredSettingsWindow = Wait-ForWindow -Process $llpExplorerRecoveryProcess -ClassName 'LightLaunchpadNativeSettingsWindow'
        Assert-True ($llpRecoveredSettingsWindow -ne [IntPtr]::Zero) 'Settings did not remain reachable after the real Explorer restart.'
        Write-Output "PASS AppHostBehavior_RealExplorerRestartRecoversTrayAndSettings recoveryCount=$($llpExplorerProbe.taskbarRecoveryCount) trayAttempts=$($llpExplorerProbe.trayAddAttempts)"

        [LightLaunchpad.Tests.AppHostWindows]::Close($llpExplorerMainWindow)
        Assert-True ($llpExplorerRecoveryProcess.WaitForExit(3000)) 'Explorer recovery primary did not close after verification.'
    }

    $llpActivationReportPath = Join-Path $llpTempRoot 'same-build-activation.json'
    $llpSettingsStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $llpPrimaryProcess = Start-IsolatedProcess -Executable $llpExecutable -ArgumentList @(
        '--diagnostics-ui',
        '--diagnostics-log-file', $llpLogPath
    )
    $llpPrimaryMainWindow = Wait-ForWindow -Process $llpPrimaryProcess -ClassName $llpDevelopmentWindowClass
    Assert-True ($llpPrimaryMainWindow -ne [IntPtr]::Zero) 'The primary development instance did not create its channel-specific message window.'
    $llpPrimarySettingsWindow = Wait-ForWindow -Process $llpPrimaryProcess -ClassName 'LightLaunchpadNativeSettingsWindow'
    $llpSettingsStopwatch.Stop()
    Assert-True ($llpPrimarySettingsWindow -ne [IntPtr]::Zero) 'The primary development instance did not show Diagnostics settings.'
    Assert-True ($llpSettingsStopwatch.ElapsedMilliseconds -le 1000) "Settings was not prompt with an unavailable launch folder: $($llpSettingsStopwatch.ElapsedMilliseconds) ms."
    Write-Output "METRIC AppHost shellReadyMs=$($llpProbe.shellReadyMs) probeExitMs=$($llpStopwatch.ElapsedMilliseconds) settingsWindowMs=$($llpSettingsStopwatch.ElapsedMilliseconds) logBytes=$($llpLogItem.Length)"
    [LightLaunchpad.Tests.AppHostWindows]::Minimize($llpPrimarySettingsWindow)
    $llpMinimizeDeadline = [DateTime]::UtcNow.AddSeconds(2)
    while (-not [LightLaunchpad.Tests.AppHostWindows]::IsIconic($llpPrimarySettingsWindow) -and [DateTime]::UtcNow -lt $llpMinimizeDeadline) {
        Start-Sleep -Milliseconds 25
    }
    Assert-True ([LightLaunchpad.Tests.AppHostWindows]::IsIconic($llpPrimarySettingsWindow)) 'The primary settings window could not be minimized before activation.'

    $llpSecondaryProcess = Start-IsolatedProcess -Executable $llpExecutable -ArgumentList @(
        '--diagnostics-ui',
        '--activation-report-file', $llpActivationReportPath,
        '--diagnostics-log-file', $llpLogPath
    )
    Assert-True ($llpSecondaryProcess.WaitForExit(5000)) 'The matching second instance did not exit after bounded IPC.'
    Assert-Equal 0 $llpSecondaryProcess.ExitCode 'Matching second-instance activation exit code mismatch.'
    $llpActivationReport = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpActivationReportPath | ConvertFrom-Json
    Assert-Equal 'activated' ([string]$llpActivationReport.status) 'Matching second-instance activation status mismatch.'

    $llpRestoreDeadline = [DateTime]::UtcNow.AddSeconds(3)
    while ([LightLaunchpad.Tests.AppHostWindows]::IsIconic($llpPrimarySettingsWindow) -and [DateTime]::UtcNow -lt $llpRestoreDeadline) {
        Start-Sleep -Milliseconds 25
    }
    Assert-Equal $false ([LightLaunchpad.Tests.AppHostWindows]::IsIconic($llpPrimarySettingsWindow)) 'The matching second instance did not restore the first instance window.'
    Assert-Equal $false $llpPrimaryProcess.HasExited 'The primary instance exited during matching activation.'
    $llpMatchingProcesses = @(Get-Process -Name 'LightLaunchpad.NativeUi' -ErrorAction SilentlyContinue | Where-Object {
        try { [System.IO.Path]::GetFullPath($_.Path) -eq [System.IO.Path]::GetFullPath($llpExecutable) } catch { $false }
    })
    Assert-Equal 1 $llpMatchingProcesses.Count 'Matching activation did not leave exactly one process for the tested executable.'
    Write-Output 'PASS AppHostBehavior_MatchingSecondInstanceActivatesPrimaryAndLeavesOneProcess'

    [LightLaunchpad.Tests.AppHostWindows]::Close($llpPrimaryMainWindow)
    Assert-True ($llpPrimaryProcess.WaitForExit(3000)) 'The development primary instance did not close after the activation test.'

    $llpCleanSourceRoot = Join-Path $llpTempRoot 'clean-source'
    New-Item -ItemType Directory -Force -Path (Join-Path $llpCleanSourceRoot 'tools') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.App\Assets') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $llpCleanSourceRoot 'src\third_party\nlohmann') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $llpCleanSourceRoot 'src\third_party\pinyin-data') | Out-Null
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'tools\build-native-ui.ps1') -Destination (Join-Path $llpCleanSourceRoot 'tools\build-native-ui.ps1')
    foreach ($llpNativeFile in @('LightLaunchpad.NativeUi.cpp', 'LightLaunchpad.NativeUi.rc', 'LightLaunchpad.NativeUi.manifest', 'LightLaunchpad.Version.h')) {
        Copy-Item -LiteralPath (Join-Path $llpRepoRoot "src\LightLaunchpad.NativeUi\$llpNativeFile") -Destination (Join-Path $llpCleanSourceRoot "src\LightLaunchpad.NativeUi\$llpNativeFile")
    }
    foreach ($llpPersistenceFile in @('Catalog.cpp', 'Catalog.h', 'InteractionState.cpp', 'InteractionState.h', 'OrganizationState.cpp', 'OrganizationState.h', 'Persistence.cpp', 'Persistence.h', 'SceneGeometry.cpp', 'SceneGeometry.h', 'SearchAccessibility.cpp', 'SearchAccessibility.h', 'SearchIndex.cpp', 'SearchIndex.h', 'ShellBroker.cpp', 'ShellBroker.h', 'TextInput.cpp', 'TextInput.h')) {
        Copy-Item -LiteralPath (Join-Path $llpRepoRoot "src\LightLaunchpad.NativeUi\$llpPersistenceFile") -Destination (Join-Path $llpCleanSourceRoot "src\LightLaunchpad.NativeUi\$llpPersistenceFile")
    }
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\third_party\nlohmann\json.hpp') -Destination (Join-Path $llpCleanSourceRoot 'src\third_party\nlohmann\json.hpp')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\third_party\nlohmann\LICENSE.MIT') -Destination (Join-Path $llpCleanSourceRoot 'src\third_party\nlohmann\LICENSE.MIT')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\third_party\pinyin-data\PinyinData.generated.h') -Destination (Join-Path $llpCleanSourceRoot 'src\third_party\pinyin-data\PinyinData.generated.h')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\third_party\pinyin-data\LICENSE.MIT') -Destination (Join-Path $llpCleanSourceRoot 'src\third_party\pinyin-data\LICENSE.MIT')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.App\Assets\LightLaunchpad.ico') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.App\Assets\LightLaunchpad.ico')
    & git -C $llpCleanSourceRoot init --quiet
    & git -C $llpCleanSourceRoot config core.autocrlf false
    & git -C $llpCleanSourceRoot add -- .
    & git -C $llpCleanSourceRoot -c user.name='LightLaunchpad Tests' -c user.email='tests@lightlaunchpad.local' commit --quiet -m 'apphost test source'
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to create the clean source fixture for stable AppHost testing.'
    }

    $llpStableAOutput = Join-Path $llpTempRoot 'stable-a'
    $llpStableBOutput = Join-Path $llpTempRoot 'stable-b'
    & (Join-Path $llpCleanSourceRoot 'tools\build-native-ui.ps1') -OutputDirectory $llpStableAOutput -ProductVersion '1.11.0' -BuildCommit 'stable-old' -BuildChannel Stable | Out-Null
    & (Join-Path $llpCleanSourceRoot 'tools\build-native-ui.ps1') -OutputDirectory $llpStableBOutput -ProductVersion '1.12.0' -BuildCommit 'stable-new' -BuildChannel Stable | Out-Null
    $llpStableAExecutable = Join-Path $llpStableAOutput 'LightLaunchpad.NativeUi.exe'
    $llpStableBExecutable = Join-Path $llpStableBOutput 'LightLaunchpad.NativeUi.exe'
    $llpMismatchReportPath = Join-Path $llpTempRoot 'stable-mismatch-activation.json'

    $llpStablePrimaryProcess = Start-IsolatedProcess -Executable $llpStableAExecutable -ArgumentList @('--diagnostics-ui', '--diagnostics-log-file', $llpLogPath)
    $llpStableMainWindow = Wait-ForWindow -Process $llpStablePrimaryProcess -ClassName 'LightLaunchpadNativeUiWindow.stable'
    Assert-True ($llpStableMainWindow -ne [IntPtr]::Zero) 'The stable primary instance did not create its stable message window.'
    $llpStableSettingsWindow = Wait-ForWindow -Process $llpStablePrimaryProcess -ClassName 'LightLaunchpadNativeSettingsWindow'
    Assert-True ($llpStableSettingsWindow -ne [IntPtr]::Zero) 'The stable primary instance did not show Diagnostics settings.'
    [LightLaunchpad.Tests.AppHostWindows]::Minimize($llpStableSettingsWindow)

    $llpMismatchProcess = Start-IsolatedProcess -Executable $llpStableBExecutable -ArgumentList @('--diagnostics-ui', '--activation-report-file', $llpMismatchReportPath, '--diagnostics-log-file', $llpLogPath)
    Assert-True ($llpMismatchProcess.WaitForExit(5000)) 'The mismatched stable instance did not exit after bounded IPC.'
    Assert-Equal 3 $llpMismatchProcess.ExitCode 'Mismatched stable activation exit code mismatch.'
    $llpMismatchReport = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpMismatchReportPath | ConvertFrom-Json
    Assert-Equal 'version-or-channel-mismatch' ([string]$llpMismatchReport.status) 'Stable version/channel mismatch was not reported explicitly.'
    Assert-Equal $false $llpStablePrimaryProcess.HasExited 'The stable primary instance exited during mismatch activation.'

    $llpStableRestoreDeadline = [DateTime]::UtcNow.AddSeconds(3)
    while ([LightLaunchpad.Tests.AppHostWindows]::IsIconic($llpStableSettingsWindow) -and [DateTime]::UtcNow -lt $llpStableRestoreDeadline) {
        Start-Sleep -Milliseconds 25
    }
    Assert-Equal $false ([LightLaunchpad.Tests.AppHostWindows]::IsIconic($llpStableSettingsWindow)) 'Mismatch IPC did not activate the already-running stable instance.'
    Write-Output 'PASS AppHostBehavior_MismatchedStableBuildActivatesPrimaryAndReportsMismatch'

    [LightLaunchpad.Tests.AppHostWindows]::Close($llpStableMainWindow)
    Assert-True ($llpStablePrimaryProcess.WaitForExit(3000)) 'The stable primary instance did not close after the mismatch test.'
}
finally {
    if ($llpProcess -and -not $llpProcess.HasExited) {
        Stop-Process -Id $llpProcess.Id -Force
        $llpProcess.WaitForExit()
    }
    foreach ($llpCandidateProcess in @($llpPrimaryProcess, $llpStablePrimaryProcess, $llpExplorerRecoveryProcess)) {
        if ($llpCandidateProcess -and -not $llpCandidateProcess.HasExited) {
            Stop-Process -Id $llpCandidateProcess.Id -Force
            $llpCandidateProcess.WaitForExit()
        }
    }
    if ($llpExplorerRestartAttempted) {
        $llpCurrentSessionId = [System.Diagnostics.Process]::GetCurrentProcess().SessionId
        $llpExplorerProcesses = @(Get-Process -Name explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $llpCurrentSessionId })
        if ($llpExplorerProcesses.Count -eq 0) {
            Start-Process -FilePath (Join-Path $env:WINDIR 'explorer.exe') | Out-Null
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
