using System.IO;

namespace LightLaunchpad.App.Tests;

public sealed class SettingsWindowXamlTests
{
    public static void SettingsWindow_OffersLaunchpadAndSpotlightDisplayModes()
    {
        var xaml = File.ReadAllText(FindSettingsWindowXaml());

        TestAssert.Contains("x:Name=\"DisplayModeComboBox\"", xaml);
        TestAssert.Contains("Tag=\"Launchpad\" Content=\"启动台\"", xaml);
        TestAssert.Contains("Tag=\"Spotlight\" Content=\"聚焦\"", xaml);
    }

    private static string FindSettingsWindowXaml()
    {
        var current = new DirectoryInfo(Environment.CurrentDirectory);
        while (current is not null)
        {
            var candidate = Path.Combine(current.FullName, "src", "LightLaunchpad.App", "SettingsWindow.xaml");
            if (File.Exists(candidate))
            {
                return candidate;
            }

            current = current.Parent;
        }

        throw new FileNotFoundException("Could not find SettingsWindow.xaml from the test working directory.");
    }
}