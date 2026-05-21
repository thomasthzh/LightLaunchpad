using System.IO;

namespace LightLaunchpad.App.Tests;

public sealed class HostedAppModeSourceTests
{
    public static void AppSource_SupportsHostedUiModeWithoutStandaloneTrayOrHotkey()
    {
        var code = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.App", "App.xaml.cs"));

        TestAssert.Contains("LaunchpadActivationContext.Parse", code);
        TestAssert.Contains("BuildHostedServices", code);
        TestAssert.Contains("StartHostedActivation", code);
        TestAssert.Contains("ExitOnHide", code);
        TestAssert.Contains("BuildStandaloneServices", code);
    }

    public static void AgentProject_UsesLightweightNativeShellContracts()
    {
        var project = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.Agent", "LightLaunchpad.Agent.csproj"));
        var program = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.Agent", "Program.cs"));

        TestAssert.DoesNotContain("<UseWPF>true</UseWPF>", project);
        TestAssert.DoesNotContain("<UseWindowsForms>true</UseWindowsForms>", project);
        TestAssert.Contains("RegisterHotKey", program);
        TestAssert.Contains("Shell_NotifyIcon", program);
        TestAssert.Contains("LaunchpadActivationContext.CreateHostedUiArguments", program);
    }

    public static void NativeAgentSource_UsesWin32OnlyBackgroundContracts()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeAgent", "LightLaunchpad.NativeAgent.cpp"));

        TestAssert.Contains("wWinMain", source);
        TestAssert.Contains("RegisterHotKey", source);
        TestAssert.Contains("Shell_NotifyIconW", source);
        TestAssert.Contains("CreateEventW", source);
        TestAssert.Contains("CreateProcessW", source);
        TestAssert.Contains("GetMessageW", source);
        TestAssert.DoesNotContain("System.Text.Json", source);
    }

    public static void NativeAgentSource_LoadsTrayIconAndCleansHostedUiWithJobObject()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeAgent", "LightLaunchpad.NativeAgent.cpp"));
        var script = File.ReadAllText(FindRepoFile("tools", "build-native-agent.ps1"));

        TestAssert.Contains("Assets\\\\Alice.ico", source);
        TestAssert.Contains("LoadImageW", source);
        TestAssert.Contains("CreateJobObjectW", source);
        TestAssert.Contains("JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE", source);
        TestAssert.Contains("AssignProcessToJobObject", source);
        TestAssert.Contains("param(", script);
        TestAssert.Contains("LightLaunchpad.App\\Assets\\Alice.ico", script);
        TestAssert.Contains("GetTempPath", script);
        TestAssert.Contains("lightlaunchpad-native-", script);
    }

    public static void PackageReleaseScript_ExcludesAgentColdStartPackage()
    {
        var script = File.ReadAllText(FindRepoFile("tools", "package-release.ps1"));

        TestAssert.Contains("LightLaunchpad-nativeui-$Runtime-$Version", script);
        TestAssert.Contains("src\\LightLaunchpad.App\\LightLaunchpad.App.csproj", script);
        TestAssert.Contains("Compress-Archive", script);
        TestAssert.Contains("Package output must stay under the release directory.", script);
        TestAssert.Contains("-join [Environment]::NewLine", script);
        TestAssert.Contains("dotnet publish failed", script);
        TestAssert.Contains("Native UI build failed", script);
        TestAssert.DoesNotContain("LightLaunchpad-native-agent-$Runtime-$Version", script);
        TestAssert.DoesNotContain("build-native-agent.ps1", script);
        TestAssert.DoesNotContain("LightLaunchpad.Agent.csproj", script);
    }

    public static void StartupRegistration_StaysOnSingleProcessAppForResponsiveRoute()
    {
        var code = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.App", "App.xaml.cs"));

        TestAssert.Contains("ResolveStartupExecutablePath", code);
        TestAssert.Contains("AppContext.BaseDirectory", code);
        TestAssert.Contains("Environment.ProcessPath", code);
        TestAssert.DoesNotContain("Assembly.GetEntryAssembly()?.Location", code);
        TestAssert.DoesNotContain("LightLaunchpad.NativeAgent.exe", code);
        TestAssert.DoesNotContain("LightLaunchpad.Agent.exe", code);
    }

    public static void LaunchpadWindow_DoesNotClearSearchTextWhenShown()
    {
        var code = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.App", "LaunchpadWindow.xaml.cs"));

        TestAssert.Contains("FocusSearchBox", code);
        TestAssert.Contains("SearchBox.SelectAll", code);
        TestAssert.DoesNotContain("SearchBox.Clear", code);
        TestAssert.DoesNotContain("SearchText = string.Empty", code);
    }

    public static void NativeUiSource_UsesWin32WindowNotWpfColdStart()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("wWinMain", source);
        TestAssert.Contains("RegisterHotKey", source);
        TestAssert.Contains("Shell_NotifyIconW", source);
        TestAssert.Contains("WM_PAINT", source);
        TestAssert.Contains("CreateCompatibleDC", source);
        TestAssert.Contains("ShellExecuteW", source);
        TestAssert.DoesNotContain("LightLaunchpad.App.exe", source);
        TestAssert.DoesNotContain("CreateProcessW", source);
    }

    public static void NativeUiSource_ReadsExistingSettingsLayoutAndSearches()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("settings.json", source);
        TestAssert.Contains("layout.json", source);
        TestAssert.Contains("LaunchpadFolder", source);
        TestAssert.Contains("DisplayName", source);
        TestAssert.Contains("SourcePath", source);
        TestAssert.Contains("RegionId", source);
        TestAssert.Contains("Search", source);
    }

    public static void NativeUiBuildScript_ProducesNativeUiExecutable()
    {
        var script = File.ReadAllText(FindRepoFile("tools", "build-native-ui.ps1"));

        TestAssert.Contains("LightLaunchpad.NativeUi.cpp", script);
        TestAssert.Contains("LightLaunchpad.NativeUi.exe", script);
        TestAssert.Contains("GetTempPath", script);
        TestAssert.Contains("-mwindows", script);
        TestAssert.Contains("-static", script);
        TestAssert.Contains("-static-libgcc", script);
        TestAssert.Contains("-static-libstdc++", script);
    }

    public static void PackageReleaseScript_IncludesNativeUiPrimaryExecutable()
    {
        var script = File.ReadAllText(FindRepoFile("tools", "package-release.ps1"));

        TestAssert.Contains("LightLaunchpad-nativeui-$Runtime-$Version", script);
        TestAssert.Contains("build-native-ui.ps1", script);
        TestAssert.Contains("LightLaunchpad.NativeUi.exe", script);
        TestAssert.Contains("LightLaunchpad.App.exe", script);
    }

    private static string FindRepoFile(params string[] parts)
    {
        var current = new DirectoryInfo(Environment.CurrentDirectory);
        while (current is not null)
        {
            var candidateParts = new[] { current.FullName }.Concat(parts).ToArray();
            var candidate = Path.Combine(candidateParts);
            if (File.Exists(candidate))
            {
                return candidate;
            }

            current = current.Parent;
        }

        throw new FileNotFoundException($"Could not find {Path.Combine(parts)} from the test working directory.");
    }
}
