using System.Text.Json;

namespace LightLaunchpad.Core.Settings;

public sealed class SettingsService
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true
    };

    private readonly string _settingsPath;
    private readonly string _userProfilePath;

    public SettingsService(string settingsPath, string userProfilePath)
    {
        _settingsPath = settingsPath;
        _userProfilePath = userProfilePath;
    }

    public AppSettings Load()
    {
        if (!File.Exists(_settingsPath))
        {
            return AppSettings.CreateDefault(_userProfilePath);
        }

        using var stream = File.OpenRead(_settingsPath);
        var loaded = JsonSerializer.Deserialize<AppSettings>(stream, JsonOptions);
        if (loaded is null)
        {
            return AppSettings.CreateDefault(_userProfilePath);
        }

        var defaults = AppSettings.CreateDefault(_userProfilePath);
        return new AppSettings(
            string.IsNullOrWhiteSpace(loaded.LaunchpadFolder) ? defaults.LaunchpadFolder : loaded.LaunchpadFolder,
            string.IsNullOrWhiteSpace(loaded.Hotkey) ? defaults.Hotkey : loaded.Hotkey,
            loaded.StartWithWindows,
            string.IsNullOrWhiteSpace(loaded.IconSize) ? defaults.IconSize : loaded.IconSize,
            string.IsNullOrWhiteSpace(loaded.ViewMode) ? defaults.ViewMode : loaded.ViewMode,
            string.IsNullOrWhiteSpace(loaded.IconQuality) ? defaults.IconQuality : loaded.IconQuality,
            LaunchpadDisplayModes.Normalize(loaded.DisplayMode),
            AppLanguages.Normalize(loaded.Language),
            AppSettingLimits.NormalizeSpotlightWidth(loaded.SpotlightWidth),
            AppSettingLimits.NormalizeSpotlightHeight(loaded.SpotlightHeight),
            AppSettingLimits.NormalizeAppSpacing(loaded.AppSpacing),
            AppSettingLimits.NormalizeMouseSensitivity(loaded.MouseSensitivity));
    }

    public void Save(AppSettings settings)
    {
        var directory = Path.GetDirectoryName(_settingsPath);
        if (!string.IsNullOrWhiteSpace(directory))
        {
            Directory.CreateDirectory(directory);
        }

        var tempPath = _settingsPath + ".tmp";
        var json = JsonSerializer.Serialize(settings, JsonOptions);
        File.WriteAllText(tempPath, json);

        if (File.Exists(_settingsPath))
        {
            File.Replace(tempPath, _settingsPath, null);
        }
        else
        {
            File.Move(tempPath, _settingsPath);
        }
    }
}