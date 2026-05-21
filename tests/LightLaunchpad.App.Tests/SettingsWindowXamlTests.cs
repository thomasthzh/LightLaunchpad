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

    public static void SettingsWindow_OffersLanguageAndSpotlightTuning()
    {
        var xaml = File.ReadAllText(FindSettingsWindowXaml());

        TestAssert.Contains("x:Name=\"LanguageComboBox\"", xaml);
        TestAssert.Contains("Tag=\"English\" Content=\"English\"", xaml);
        TestAssert.Contains("Tag=\"Chinese\" Content=\"中文\"", xaml);
        TestAssert.Contains("x:Name=\"SpotlightWidthTextBox\"", xaml);
        TestAssert.Contains("x:Name=\"SpotlightHeightTextBox\"", xaml);
        TestAssert.Contains("x:Name=\"AppSpacingTextBox\"", xaml);
        TestAssert.Contains("x:Name=\"WheelSensitivityTextBox\"", xaml);
        TestAssert.DoesNotContain("MouseSensitivityTextBox", xaml);
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