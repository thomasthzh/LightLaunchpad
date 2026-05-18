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
        return JsonSerializer.Deserialize<AppSettings>(stream, JsonOptions)
            ?? AppSettings.CreateDefault(_userProfilePath);
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
