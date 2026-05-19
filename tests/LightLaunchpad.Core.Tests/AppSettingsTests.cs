using LightLaunchpad.Core.Settings;

namespace LightLaunchpad.Core.Tests;

public sealed class AppSettingsTests
{
    public static void CreateDefault_UsesUserLaunchpadFolderAndAltD()
    {
        var settings = AppSettings.CreateDefault(@"C:\Users\me");

        TestAssert.Equal(@"C:\Users\me\Launchpad", settings.LaunchpadFolder);
        TestAssert.Equal("Alt+D", settings.Hotkey);
        TestAssert.False(settings.StartWithWindows);
        TestAssert.Equal("Medium", settings.IconSize);
        TestAssert.Equal("InlineRegions", settings.ViewMode);
        TestAssert.Equal("High", settings.IconQuality);
        TestAssert.Equal("Launchpad", settings.DisplayMode);
    }

    public static void SettingsService_LoadsDefaultsWhenFileDoesNotExist()
    {
        var tempRoot = TestPaths.CreateTempDirectory();
        var settingsPath = Path.Combine(tempRoot, "settings.json");
        var service = new SettingsService(settingsPath, @"C:\Users\me");

        var settings = service.Load();

        TestAssert.Equal(@"C:\Users\me\Launchpad", settings.LaunchpadFolder);
        TestAssert.Equal("Alt+D", settings.Hotkey);
    }

    public static void SettingsService_SavesAndLoadsCustomSettings()
    {
        var tempRoot = TestPaths.CreateTempDirectory();
        var settingsPath = Path.Combine(tempRoot, "settings.json");
        var service = new SettingsService(settingsPath, @"C:\Users\me");
        var expected = new AppSettings(@"D:\Launchpad", "Alt+L", true, "Large", "RegionTabs", "High", "Spotlight");

        service.Save(expected);
        var actual = service.Load();

        TestAssert.Equal(expected, actual);
    }

    public static void SettingsService_DefaultsMissingDisplayModeToLaunchpad()
    {
        var tempRoot = TestPaths.CreateTempDirectory();
        var settingsPath = Path.Combine(tempRoot, "settings.json");
        File.WriteAllText(
            settingsPath,
            """
            {
              "LaunchpadFolder": "D:\\Launchpad",
              "Hotkey": "Alt+L",
              "StartWithWindows": true,
              "IconSize": "Small",
              "ViewMode": "InlineRegions",
              "IconQuality": "High"
            }
            """);
        var service = new SettingsService(settingsPath, @"C:\Users\me");

        var settings = service.Load();

        TestAssert.Equal("Launchpad", settings.DisplayMode);
    }
}