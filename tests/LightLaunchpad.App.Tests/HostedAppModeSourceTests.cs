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

    public static void PackageReleaseScript_BundlesUiManagedAgentAndNativeAgent()
    {
        var script = File.ReadAllText(FindRepoFile("tools", "package-release.ps1"));

        TestAssert.Contains("LightLaunchpad-native-agent-$Runtime-$Version", script);
        TestAssert.Contains("src\\LightLaunchpad.App\\LightLaunchpad.App.csproj", script);
        TestAssert.Contains("src\\LightLaunchpad.Agent\\LightLaunchpad.Agent.csproj", script);
        TestAssert.Contains("build-native-agent.ps1", script);
        TestAssert.Contains("LightLaunchpad.NativeAgent.exe", script);
        TestAssert.Contains("Compress-Archive", script);
        TestAssert.Contains("Package output must stay under the release directory.", script);
        TestAssert.Contains("-join [Environment]::NewLine", script);
    }

    public static void StartupRegistration_PrefersNativeAgentForLowMemoryRoute()
    {
        var code = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.App", "App.xaml.cs"));

        TestAssert.Contains("ResolveStartupExecutablePath", code);
        TestAssert.Contains("AppContext.BaseDirectory", code);
        TestAssert.Contains("LightLaunchpad.NativeAgent.exe", code);
        TestAssert.Contains("LightLaunchpad.Agent.exe", code);
        TestAssert.Contains("Environment.ProcessPath", code);
        TestAssert.DoesNotContain("Assembly.GetEntryAssembly()?.Location", code);
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
