namespace LightLaunchpad.Core.Settings;

public sealed record AppSettings(
    string LaunchpadFolder,
    string Hotkey,
    bool StartWithWindows,
    string IconSize,
    string ViewMode,
    string IconQuality)
{
    public static AppSettings CreateDefault(string userProfilePath)
    {
        return new AppSettings(
            Path.Combine(userProfilePath, "Launchpad"),
            "Alt+D",
            StartWithWindows: false,
            IconSize: "Medium",
            ViewMode: "InlineRegions",
            IconQuality: "Balanced");
    }
}
