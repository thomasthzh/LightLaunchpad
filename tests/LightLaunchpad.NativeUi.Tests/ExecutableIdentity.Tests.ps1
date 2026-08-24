param()

$ErrorActionPreference = 'Stop'

$llpRepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$llpTempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("lightlaunchpad-identity-tests-" + [System.Guid]::NewGuid().ToString('N'))
$llpOutputDirectory = Join-Path $llpTempRoot 'development'
$llpExpectedCommit = 'testc0de'
$llpPackageTestId = [System.Guid]::NewGuid().ToString('N')
$llpPackageOutputRelative = "release\identity-tests-$llpPackageTestId"
$llpPackageOutputAbsolute = [System.IO.Path]::GetFullPath((Join-Path $llpRepoRoot $llpPackageOutputRelative))
$llpDirtyMarker = Join-Path $llpRepoRoot ".llp-package-dirty-test-$llpPackageTestId.tmp"
$llpTestMonitorDevice = '\\.\DISPLAY1'

function Assert-Equal {
    param(
        [object]$Expected,
        [object]$Actual,
        [string]$Message
    )

    if ($Expected -ne $Actual) {
        throw "$Message Expected '$Expected', actual '$Actual'."
    }
}

function Assert-Contains {
    param(
        [string]$Expected,
        [string]$Actual,
        [string]$Message
    )

    if ([string]$Actual -eq '' -or ([string]$Actual).IndexOf($Expected, [System.StringComparison]::Ordinal) -lt 0) {
        throw "$Message Missing '$Expected'."
    }
}

function Get-EmbeddedManifest {
    param([string]$ExecutablePath)

    if (-not ('LightLaunchpad.Tests.NativeResources' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

namespace LightLaunchpad.Tests
{
    public static class NativeResources
    {
        private const uint LoadLibraryAsDataFile = 0x00000002;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryEx(string fileName, IntPtr file, uint flags);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr LoadResource(IntPtr module, IntPtr resource);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr LockResource(IntPtr resourceData);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint SizeofResource(IntPtr module, IntPtr resource);

        [DllImport("kernel32.dll")]
        private static extern bool FreeLibrary(IntPtr module);

        public static byte[] ReadManifest(string executablePath)
        {
            IntPtr module = LoadLibraryEx(executablePath, IntPtr.Zero, LoadLibraryAsDataFile);
            if (module == IntPtr.Zero)
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "LoadLibraryEx failed.");
            }

            try
            {
                IntPtr resource = FindResource(module, new IntPtr(1), new IntPtr(24));
                if (resource == IntPtr.Zero)
                {
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "The executable has no RT_MANIFEST resource with ID 1.");
                }

                uint size = SizeofResource(module, resource);
                IntPtr loaded = LoadResource(module, resource);
                IntPtr data = LockResource(loaded);
                if (size == 0 || loaded == IntPtr.Zero || data == IntPtr.Zero)
                {
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "Loading the manifest resource failed.");
                }

                byte[] bytes = new byte[size];
                Marshal.Copy(data, bytes, 0, checked((int)size));
                return bytes;
            }
            finally
            {
                FreeLibrary(module);
            }
        }
    }
}
'@
    }

    $llpBytes = [LightLaunchpad.Tests.NativeResources]::ReadManifest($ExecutablePath)
    return [System.Text.Encoding]::UTF8.GetString($llpBytes).Trim([char]0xFEFF, [char]0)
}

function Initialize-NativeWindowInspector {
    if ('LightLaunchpad.Tests.NativeWindows' -as [type]) {
        return
    }

    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace LightLaunchpad.Tests
{
    public static class NativeWindows
    {
        private const uint WmClose = 0x0010;
        private const uint WmGetText = 0x000D;
        private const uint WmGetTextLength = 0x000E;
        private delegate bool EnumWindowsCallback(IntPtr window, IntPtr parameter);
        private delegate bool EnumDisplayMonitorsCallback(IntPtr monitor, IntPtr deviceContext, IntPtr monitorRect, IntPtr state);

        [StructLayout(LayoutKind.Sequential)]
        private struct RECT { public int left; public int top; public int right; public int bottom; }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct MONITORINFOEX {
            public uint size;
            public RECT monitor;
            public RECT work;
            public uint flags;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string device;
        }

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

        [DllImport("user32.dll")]
        private static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll")]
        private static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll")]
        private static extern bool GetWindowRect(IntPtr window, out RECT rect);

        [DllImport("user32.dll")]
        private static extern bool EnumDisplayMonitors(IntPtr deviceContext, IntPtr clip, EnumDisplayMonitorsCallback callback, IntPtr state);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern bool GetMonitorInfo(IntPtr monitor, ref MONITORINFOEX info);

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

        public static string ReadWindowText(IntPtr window)
        {
            int length = SendMessage(window, WmGetTextLength, IntPtr.Zero, IntPtr.Zero).ToInt32();
            StringBuilder value = new StringBuilder(length + 1);
            SendMessage(window, WmGetText, new IntPtr(value.Capacity), value);
            return value.ToString();
        }

        public static bool IsFullyContainedByMonitor(IntPtr window, string expectedDevice)
        {
            RECT windowRect;
            if (!GetWindowRect(window, out windowRect)) return false;
            bool found = false;
            bool contained = false;
            EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, delegate(IntPtr monitor, IntPtr dc, IntPtr rect, IntPtr state) {
                MONITORINFOEX info = new MONITORINFOEX();
                info.size = (uint)Marshal.SizeOf(typeof(MONITORINFOEX));
                if (!GetMonitorInfo(monitor, ref info)) return true;
                if (!String.Equals(info.device, expectedDevice, StringComparison.OrdinalIgnoreCase)) return true;
                found = true;
                contained = windowRect.left >= info.monitor.left && windowRect.top >= info.monitor.top
                    && windowRect.right <= info.monitor.right && windowRect.bottom <= info.monitor.bottom;
                return false;
            }, IntPtr.Zero);
            return found && contained;
        }

        public static void CloseWindow(IntPtr window)
        {
            if (window != IntPtr.Zero)
            {
                PostMessage(window, WmClose, IntPtr.Zero, IntPtr.Zero);
            }
        }
    }
}
'@
}

$llpUiProcess = $null
try {
    New-Item -ItemType Directory -Force -Path $llpOutputDirectory | Out-Null

    $llpSpoofWorkingDirectory = Join-Path $llpTempRoot ("toolchain-spoof-cwd-" + [System.Guid]::NewGuid().ToString('N'))
    $llpSpoofOutputDirectory = Join-Path $llpTempRoot 'toolchain-spoof-output'
    New-Item -ItemType Directory -Path $llpSpoofWorkingDirectory | Out-Null
    [System.IO.File]::WriteAllText(
        (Join-Path $llpSpoofWorkingDirectory 'libuiautomationcore.a'),
        'not-an-import-library',
        [System.Text.UTF8Encoding]::new($false))
    $llpStrawberryBin = 'C:\Strawberry\c\bin'
    if (-not (Test-Path -LiteralPath (Join-Path $llpStrawberryBin 'g++.exe') -PathType Leaf)) {
        throw "The incomplete Strawberry compiler fixture is unavailable at $llpStrawberryBin."
    }
    $llpGitDirectory = Split-Path -Parent (Get-Command git.exe -ErrorAction Stop).Source
    $llpSystem32 = Join-Path $env:SystemRoot 'System32'
    $llpPriorPath = $env:PATH
    $llpPriorLocation = Get-Location
    $llpIncompleteToolchainRejected = $false
    try {
        $env:PATH = @($llpStrawberryBin, $llpGitDirectory, $llpSystem32) -join ';'
        Set-Location -LiteralPath $llpSpoofWorkingDirectory
        try {
            & (Join-Path $llpRepoRoot 'tools\build-native-ui.ps1') `
                -OutputDirectory $llpSpoofOutputDirectory `
                -ProductVersion '1.11.0' `
                -BuildCommit 'toolchainprobe' `
                -BuildChannel Development | Out-Null
        }
        catch {
            $llpIncompleteToolchainRejected =
                $_.Exception.Message.IndexOf('No MinGW-w64 toolchain on PATH can link the complete NativeUi', [System.StringComparison]::Ordinal) -ge 0 -and
                $_.Exception.Message.IndexOf('libuiautomationcore.a', [System.StringComparison]::Ordinal) -ge 0
        }
    }
    finally {
        Set-Location -LiteralPath $llpPriorLocation
        $env:PATH = $llpPriorPath
    }
    if (-not $llpIncompleteToolchainRejected) {
        throw 'NativeUi build did not reject the incomplete Strawberry toolchain before compile/link.'
    }
    if (Test-Path -LiteralPath (Join-Path $llpSpoofOutputDirectory 'LightLaunchpad.NativeUi.exe') -PathType Leaf) {
        throw 'Incomplete toolchain probe unexpectedly produced a NativeUi EXE.'
    }
    Write-Output 'PASS ExecutableIdentity_RejectsRelativeCompilerLibrarySpoofBeforeBuild'

    & (Join-Path $llpRepoRoot 'tools\build-native-ui.ps1') `
        -OutputDirectory $llpOutputDirectory `
        -ProductVersion '1.11.0' `
        -BuildCommit $llpExpectedCommit `
        -BuildChannel Development | Out-Null

    $llpExecutable = Join-Path $llpOutputDirectory 'LightLaunchpad.NativeUi.exe'
    if (-not (Test-Path -LiteralPath $llpExecutable -PathType Leaf)) {
        throw "NativeUi build did not produce $llpExecutable."
    }

    $llpVersion = (Get-Item -LiteralPath $llpExecutable).VersionInfo
    Assert-Equal 'LightLaunchpad' $llpVersion.ProductName 'Product name resource mismatch.'
    Assert-Equal 'LightLaunchpad Native UI' $llpVersion.FileDescription 'File description resource mismatch.'
    Assert-Equal 'LightLaunchpad.NativeUi.exe' $llpVersion.OriginalFilename 'Original filename resource mismatch.'
    Assert-Equal '1.11.0.0' $llpVersion.FileVersion 'File version resource mismatch.'
    Assert-Contains "1.11.0-dev+$llpExpectedCommit" $llpVersion.ProductVersion 'Development product version mismatch.'

    $llpManifest = Get-EmbeddedManifest -ExecutablePath $llpExecutable
    Assert-Contains '<dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">' $llpManifest 'DPI-awareness manifest element mismatch.'
    Assert-Contains 'PerMonitorV2,PerMonitor</dpiAwareness>' $llpManifest 'Per-Monitor V2 manifest value mismatch.'

    Write-Output 'PASS ExecutableIdentity_EmbedsVersionAndPerMonitorV2Manifest'

    $llpDiagnosticsPath = Join-Path $llpTempRoot 'diagnostics.json'
    $llpDiagnosticsProcess = Start-Process `
        -FilePath $llpExecutable `
        -ArgumentList @('--diagnostics-file', $llpDiagnosticsPath) `
        -PassThru `
        -Wait `
        -WindowStyle Hidden
    Assert-Equal 0 $llpDiagnosticsProcess.ExitCode 'Diagnostics process exit code mismatch.'
    if (-not (Test-Path -LiteralPath $llpDiagnosticsPath -PathType Leaf)) {
        throw "The real NativeUi EXE did not write diagnostics to $llpDiagnosticsPath."
    }

    $llpDiagnostics = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpDiagnosticsPath | ConvertFrom-Json
    Assert-Equal 'LightLaunchpad' $llpDiagnostics.productName 'Diagnostics product name mismatch.'
    Assert-Contains "1.11.0-dev+$llpExpectedCommit" $llpDiagnostics.version 'Diagnostics development version mismatch.'
    Assert-Equal $llpExpectedCommit $llpDiagnostics.commit 'Diagnostics commit mismatch.'
    Assert-Equal 'development' $llpDiagnostics.channel 'Diagnostics channel mismatch.'
    Assert-Contains $llpExpectedCommit $llpDiagnostics.buildId 'Diagnostics build ID mismatch.'
    Assert-Equal "Local\LightLaunchpad.NativeUi.SingleInstance.development.$llpExpectedCommit" $llpDiagnostics.singletonName 'Diagnostics singleton name mismatch.'
    Assert-Equal "LightLaunchpadNativeUiWindow.development.$llpExpectedCommit" $llpDiagnostics.windowClass 'Diagnostics IPC window class mismatch.'
    Assert-Equal ([System.IO.Path]::GetFullPath($llpExecutable)) ([System.IO.Path]::GetFullPath([string]$llpDiagnostics.executablePath)) 'Diagnostics executable path mismatch.'
    $llpExpectedStablePath = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs\LightLaunchpad\LightLaunchpad.NativeUi.exe'
    Assert-Equal $llpExpectedStablePath ([string]$llpDiagnostics.stableInstallPath) 'Diagnostics stable install path mismatch.'
    Assert-Equal $false ([bool]$llpDiagnostics.isStableInstall) 'Diagnostics stable-install state mismatch.'
    Assert-Contains 'PerMonitorV2' ([string]$llpDiagnostics.dpiAwareness) 'Diagnostics DPI-awareness mismatch.'
    foreach ($llpPropertyName in @('renderer', 'monitor', 'startupPath', 'hotkeyStatus')) {
        if ($llpDiagnostics.PSObject.Properties.Name -notcontains $llpPropertyName) {
            throw "Diagnostics is missing the '$llpPropertyName' property."
        }
    }

    Write-Output 'PASS ExecutableIdentity_ReportsBuildAndRuntimeDiagnostics'

    $llpCleanSourceRoot = Join-Path $llpTempRoot 'clean-source'
    New-Item -ItemType Directory -Force -Path (Join-Path $llpCleanSourceRoot 'tools') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.App\Assets') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $llpCleanSourceRoot 'src\third_party\nlohmann') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $llpCleanSourceRoot 'src\third_party\pinyin-data') | Out-Null
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'tools\build-native-ui.ps1') -Destination (Join-Path $llpCleanSourceRoot 'tools\build-native-ui.ps1')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\LightLaunchpad.NativeUi.cpp') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\LightLaunchpad.NativeUi.cpp')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\LightLaunchpad.NativeUi.rc') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\LightLaunchpad.NativeUi.rc')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\LightLaunchpad.NativeUi.manifest') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\LightLaunchpad.NativeUi.manifest')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\LightLaunchpad.Version.h') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\LightLaunchpad.Version.h')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\AdaptiveEnvironment.cpp') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\AdaptiveEnvironment.cpp')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\AdaptiveEnvironment.h') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\AdaptiveEnvironment.h')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\MaterialRenderer.cpp') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\MaterialRenderer.cpp')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\MaterialRenderer.h') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\MaterialRenderer.h')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\Persistence.cpp') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\Persistence.cpp')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\Persistence.h') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\Persistence.h')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\Catalog.cpp') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\Catalog.cpp')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\Catalog.h') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\Catalog.h')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\ShellBroker.cpp') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\ShellBroker.cpp')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\ShellBroker.h') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\ShellBroker.h')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\SearchIndex.cpp') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\SearchIndex.cpp')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\SearchIndex.h') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\SearchIndex.h')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\InteractionState.cpp') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\InteractionState.cpp')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\InteractionState.h') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\InteractionState.h')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\OrganizationState.cpp') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\OrganizationState.cpp')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\OrganizationState.h') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\OrganizationState.h')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\SceneGeometry.cpp') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\SceneGeometry.cpp')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\SceneGeometry.h') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\SceneGeometry.h')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\SearchAccessibility.cpp') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\SearchAccessibility.cpp')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\SearchAccessibility.h') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\SearchAccessibility.h')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\TextInput.cpp') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\TextInput.cpp')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.NativeUi\TextInput.h') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.NativeUi\TextInput.h')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\third_party\nlohmann\json.hpp') -Destination (Join-Path $llpCleanSourceRoot 'src\third_party\nlohmann\json.hpp')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\third_party\nlohmann\LICENSE.MIT') -Destination (Join-Path $llpCleanSourceRoot 'src\third_party\nlohmann\LICENSE.MIT')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\third_party\pinyin-data\PinyinData.generated.h') -Destination (Join-Path $llpCleanSourceRoot 'src\third_party\pinyin-data\PinyinData.generated.h')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\third_party\pinyin-data\LICENSE.MIT') -Destination (Join-Path $llpCleanSourceRoot 'src\third_party\pinyin-data\LICENSE.MIT')
    Copy-Item -LiteralPath (Join-Path $llpRepoRoot 'src\LightLaunchpad.App\Assets\LightLaunchpad.ico') -Destination (Join-Path $llpCleanSourceRoot 'src\LightLaunchpad.App\Assets\LightLaunchpad.ico')
    & git -C $llpCleanSourceRoot init --quiet
    & git -C $llpCleanSourceRoot config core.autocrlf false
    & git -C $llpCleanSourceRoot add -- .
    & git -C $llpCleanSourceRoot -c user.name='LightLaunchpad Tests' -c user.email='tests@lightlaunchpad.local' commit --quiet -m 'identity test source'
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to create the clean source fixture for stable identity testing.'
    }

    $llpStableOutput = Join-Path $llpCleanSourceRoot 'stable-output'
    & (Join-Path $llpCleanSourceRoot 'tools\build-native-ui.ps1') `
        -OutputDirectory $llpStableOutput `
        -ProductVersion '1.11.0' `
        -BuildCommit 'stablec0de' `
        -BuildChannel Stable | Out-Null
    $llpStableExecutable = Join-Path $llpStableOutput 'LightLaunchpad.NativeUi.exe'
    $llpStableDiagnosticsPath = Join-Path $llpTempRoot 'stable-diagnostics.json'
    $llpStableDiagnosticsProcess = Start-Process `
        -FilePath $llpStableExecutable `
        -ArgumentList @('--diagnostics-file', $llpStableDiagnosticsPath) `
        -PassThru `
        -Wait `
        -WindowStyle Hidden
    Assert-Equal 0 $llpStableDiagnosticsProcess.ExitCode 'Stable diagnostics process exit code mismatch.'
    $llpStableDiagnostics = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpStableDiagnosticsPath | ConvertFrom-Json
    Assert-Equal '1.11.0' $llpStableDiagnostics.version 'Stable version mismatch.'
    Assert-Equal 'stable' $llpStableDiagnostics.channel 'Stable channel mismatch.'
    Assert-Equal 'Local\LightLaunchpad.NativeUi.SingleInstance.stable' $llpStableDiagnostics.singletonName 'Stable singleton name mismatch.'
    if ($llpStableDiagnostics.singletonName -eq $llpDiagnostics.singletonName) {
        throw 'Stable and development executables share a singleton name.'
    }

    $llpStableDevelopmentCase = 0
    foreach ($llpStableDevelopmentArguments in @(
        @('--material-probe-file', (Join-Path $llpTempRoot 'stable-material-probe.json')),
        @('--material-probe-ready-file', (Join-Path $llpTempRoot 'stable-material-ready.json')),
        @('--material-evidence'),
        @('--material-force-failure', 'duplication'),
        @('--test-monitor-device', '\\.\DISPLAY1'),
        @('--adaptive-test-dpi', '144'),
        @('--adaptive-test-text-scale', '125'),
        @('--adaptive-test-high-contrast', '1'),
        @('--adaptive-test-transparency', '0'),
        @('--adaptive-test-animations', '0'),
        @('--catalog-broker-probe-file', (Join-Path $llpTempRoot 'stable-catalog.json')),
        @('--catalog-broker-delay-ms', '100'),
        @('--shell-import-probe-file', (Join-Path $llpTempRoot 'stable-shell.json')),
        @('--shell-import-root', $llpTempRoot),
        @('--shell-import-destination', $llpTempRoot),
        @('--shell-import-repeat', '2'),
        @('--text-input-probe-file', (Join-Path $llpTempRoot 'stable-text.json')),
        @('--text-input-ready-file', (Join-Path $llpTempRoot 'stable-text-ready.json')),
        @('--spotlight-flow-probe-file', (Join-Path $llpTempRoot 'stable-spotlight.json')),
        @('--spotlight-flow-ready-file', (Join-Path $llpTempRoot 'stable-spotlight-ready.json')),
        @('--launchpad-flow-probe-file', (Join-Path $llpTempRoot 'stable-launchpad.json')),
        @('--launchpad-flow-ready-file', (Join-Path $llpTempRoot 'stable-launchpad-ready.json')),
        @('--organization-probe-file', (Join-Path $llpTempRoot 'stable-organization.json')),
        @('--organization-probe-root', $llpTempRoot),
        @('--region-undo-probe-file', (Join-Path $llpTempRoot 'stable-region.json')),
        @('--region-undo-probe-region', 'work'),
        @('--persistence-probe-file', (Join-Path $llpTempRoot 'stable-persistence.json')),
        @('--persistence-resave'),
        @('--startup-probe-file', (Join-Path $llpTempRoot 'stable-startup.json')),
        @('--startup-registry-subkey', 'Software\LightLaunchpadTest'),
        @('--exit-after-startup-probe'),
        @('--simulate-taskbar-created'),
        @('--spotlight-ownership-probe'),
        @('--spotlight-force-transparent-transition-failure'),
        @('--spotlight-force-hide-scene-race'),
        @('--spotlight-force-latest-hide-race', 'show-hide'),
        @('--spotlight-force-compose-hide-lifetime'),
        @('--spotlight-force-self-capture-evidence')
    )) {
        ++$llpStableDevelopmentCase
        $llpRejectedDiagnostics = Join-Path $llpTempRoot ("stable-development-rejected-$llpStableDevelopmentCase.json")
        $llpStableDevelopmentProcess = Start-Process `
            -FilePath $llpStableExecutable `
            -ArgumentList ([string[]]($llpStableDevelopmentArguments + @('--diagnostics-file', $llpRejectedDiagnostics))) `
            -PassThru `
            -Wait `
            -WindowStyle Hidden
        Assert-Equal 2 $llpStableDevelopmentProcess.ExitCode 'Stable build accepted a Development-only switch.'
        if (Test-Path -LiteralPath $llpRejectedDiagnostics) {
            throw 'Stable build processed diagnostics after a rejected Development-only switch.'
        }
    }
    if ((Test-Path -LiteralPath (Join-Path $llpTempRoot 'stable-material-probe.json')) -or
        (Test-Path -LiteralPath (Join-Path $llpTempRoot 'stable-material-ready.json'))) {
        throw 'Stable build created Development material evidence before rejecting its switch.'
    }

    Write-Output 'PASS ExecutableIdentity_SeparatesStableAndDevelopmentSingletonChannels'

    Initialize-NativeWindowInspector
    $llpUiMaterialProbePath = Join-Path $llpTempRoot 'identity-settings-material.json'
    $llpUiProcess = Start-Process -FilePath $llpExecutable -ArgumentList @(
        '--diagnostics-ui',
        '--material-probe-file', $llpUiMaterialProbePath,
        '--test-monitor-device', $llpTestMonitorDevice
    ) -PassThru -WindowStyle Hidden
    $llpSettingsWindow = [IntPtr]::Zero
    $llpDeadline = [DateTime]::UtcNow.AddSeconds(8)
    while ($llpSettingsWindow -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $llpDeadline) {
        Start-Sleep -Milliseconds 50
        $llpSettingsWindow = [LightLaunchpad.Tests.NativeWindows]::FindWindowForProcess($llpUiProcess.Id, 'LightLaunchpadNativeSettingsWindow')
    }
    if ($llpSettingsWindow -eq [IntPtr]::Zero) {
        throw 'The real NativeUi EXE did not open its settings window for diagnostics inspection.'
    }
    if (-not [LightLaunchpad.Tests.NativeWindows]::IsFullyContainedByMonitor($llpSettingsWindow, $llpTestMonitorDevice)) {
        throw 'The visible diagnostics settings window was not fully contained by DISPLAY1.'
    }

    $llpDiagnosticsControl = [LightLaunchpad.Tests.NativeWindows]::GetDlgItem($llpSettingsWindow, 3015)
    if ($llpDiagnosticsControl -eq [IntPtr]::Zero) {
        throw 'The settings window has no About/Diagnostics control.'
    }
    $llpDiagnosticsText = [LightLaunchpad.Tests.NativeWindows]::ReadWindowText($llpDiagnosticsControl)
    foreach ($llpExpectedText in @(
        "Version: 1.11.0-dev+$llpExpectedCommit",
        "Commit: $llpExpectedCommit",
        'Channel: development',
        "IPC class: LightLaunchpadNativeUiWindow.development.$llpExpectedCommit",
        "Executable: $([System.IO.Path]::GetFullPath($llpExecutable))",
        'Build ID:',
        'Renderer: not initialized',
        'DPI:',
        'Monitor:',
        'Startup:',
        'Hotkey: disabled in diagnostics UI'
    )) {
        Assert-Contains $llpExpectedText $llpDiagnosticsText 'Settings diagnostics text mismatch.'
    }

    Write-Output 'PASS ExecutableIdentity_ShowsAboutDiagnosticsInRealSettingsWindow'

    [System.IO.File]::WriteAllText($llpDirtyMarker, 'dirty-package-gate-test', [System.Text.UTF8Encoding]::new($false))
    $llpDirtyPackageRejected = $false
    try {
        & (Join-Path $llpRepoRoot 'tools\package-release.ps1') `
            -ProductVersion '1.11.0' `
            -OutputRoot $llpPackageOutputRelative | Out-Null
    }
    catch {
        if ($_.Exception.Message.IndexOf('dirty', [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
            $llpDirtyPackageRejected = $true
        }
        else {
            throw
        }
    }
    if (-not $llpDirtyPackageRejected) {
        throw 'Stable packaging did not reject a dirty repository.'
    }

    $llpPackageOutput = @(& (Join-Path $llpRepoRoot 'tools\package-release.ps1') `
        -ProductVersion '1.11.0' `
        -OutputRoot $llpPackageOutputRelative `
        -Development)
    $llpPackage = $llpPackageOutput | Where-Object { $_.PSObject.Properties.Name -contains 'PackageDirectory' } | Select-Object -Last 1
    if (-not $llpPackage) {
        throw 'Development packaging did not return package metadata.'
    }
    if (-not (Test-Path -LiteralPath $llpPackage.NativeUi -PathType Leaf)) {
        throw 'Development packaging did not produce the NativeUi EXE.'
    }
    if (-not (Test-Path -LiteralPath $llpPackage.Zip -PathType Leaf)) {
        throw 'Development packaging did not produce the ZIP.'
    }

    $llpHeadCommit = ((& git -C $llpRepoRoot rev-parse --short=12 HEAD) -join '').Trim()
    $llpPackagedVersion = (Get-Item -LiteralPath $llpPackage.NativeUi).VersionInfo.ProductVersion
    Assert-Contains "1.11.0-dev+$llpHeadCommit" $llpPackagedVersion 'Development package product version mismatch.'

    $llpPackagedDiagnosticsPath = Join-Path $llpTempRoot 'packaged-diagnostics.json'
    $llpPackagedDiagnosticsProcess = Start-Process `
        -FilePath $llpPackage.NativeUi `
        -ArgumentList @('--diagnostics-file', $llpPackagedDiagnosticsPath) `
        -PassThru `
        -Wait `
        -WindowStyle Hidden
    Assert-Equal 0 $llpPackagedDiagnosticsProcess.ExitCode 'Packaged diagnostics process exit code mismatch.'
    $llpPackagedDiagnostics = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpPackagedDiagnosticsPath | ConvertFrom-Json
    Assert-Equal 'development' $llpPackagedDiagnostics.channel 'Development package channel mismatch.'

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $llpArchive = [System.IO.Compression.ZipFile]::OpenRead([string]$llpPackage.Zip)
    try {
        $llpEntries = @($llpArchive.Entries | ForEach-Object { $_.FullName })
        Assert-Equal 1 $llpEntries.Count 'Development package ZIP entry count mismatch.'
        Assert-Equal 'LightLaunchpad.NativeUi.exe' $llpEntries[0] 'Development package ZIP content mismatch.'
    }
    finally {
        $llpArchive.Dispose()
    }

    Write-Output 'PASS PackageIdentity_RejectsDirtyStableAndLabelsExplicitDevelopmentPackage'
}
finally {
    if ($llpUiProcess -and -not $llpUiProcess.HasExited) {
        $llpMainWindow = [LightLaunchpad.Tests.NativeWindows]::FindWindowForProcess($llpUiProcess.Id, "LightLaunchpadNativeUiWindow.development.$llpExpectedCommit")
        [LightLaunchpad.Tests.NativeWindows]::CloseWindow($llpMainWindow)
        if (-not $llpUiProcess.WaitForExit(3000)) {
            Stop-Process -Id $llpUiProcess.Id -Force
            $llpUiProcess.WaitForExit()
        }
    }
    $llpResolvedTemp = [System.IO.Path]::GetFullPath($llpTempRoot)
    $llpExpectedTempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    $llpExpectedTempPrefix = $llpExpectedTempRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    if ($llpResolvedTemp.StartsWith($llpExpectedTempPrefix, [System.StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $llpResolvedTemp)) {
        $llpTempItem = Get-Item -LiteralPath $llpResolvedTemp -Force
        if (($llpTempItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to clean reparse-point identity test output $llpResolvedTemp."
        }
        Remove-Item -LiteralPath $llpResolvedTemp -Recurse -Force
    }
    if (Test-Path -LiteralPath $llpDirtyMarker -PathType Leaf) {
        Remove-Item -LiteralPath $llpDirtyMarker -Force
    }
    $llpExpectedReleaseRoot = [System.IO.Path]::GetFullPath((Join-Path $llpRepoRoot 'release'))
    if ($llpPackageOutputAbsolute.StartsWith($llpExpectedReleaseRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $llpPackageOutputAbsolute)) {
        $llpPackageOutputItem = Get-Item -LiteralPath $llpPackageOutputAbsolute -Force
        if (($llpPackageOutputItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to clean reparse-point package output $llpPackageOutputAbsolute."
        }
        Remove-Item -LiteralPath $llpPackageOutputAbsolute -Recurse -Force
    }
}
