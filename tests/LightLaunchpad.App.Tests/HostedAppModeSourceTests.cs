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