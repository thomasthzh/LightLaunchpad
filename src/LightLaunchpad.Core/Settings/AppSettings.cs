namespace LightLaunchpad.Core.Settings;

public sealed record AppSettings(
    string LaunchpadFolder,
    string Hotkey,
    bool StartWithWindows,
    string IconSize,
    string ViewMode,
    string IconQuality,
    string DisplayMode = LaunchpadDisplayModes.Launchpad,
    string Language = AppLanguages.English,
    double SpotlightWidth = AppSettingLimits.DefaultSpotlightWidth,
    double SpotlightHeight = AppSettingLimits.DefaultSpotlightHeight,
    double AppSpacing = AppSettingLimits.DefaultAppSpacing,
    double WheelSensitivity = AppSettingLimits.DefaultWheelSensitivity)
{
    public static AppSettings CreateDefault(string userProfilePath)
    {
        return new AppSettings(
            Path.Combine(userProfilePath, "Launchpad"),
            "Alt+D",
            StartWithWindows: false,
            IconSize: "Medium",
            ViewMode: "InlineRegions",
            IconQuality: "High",
            DisplayMode: LaunchpadDisplayModes.Launchpad,
            Language: AppLanguages.English,
            SpotlightWidth: AppSettingLimits.DefaultSpotlightWidth,
            SpotlightHeight: AppSettingLimits.DefaultSpotlightHeight,
            AppSpacing: AppSettingLimits.DefaultAppSpacing,
            WheelSensitivity: AppSettingLimits.DefaultWheelSensitivity);
    }
}