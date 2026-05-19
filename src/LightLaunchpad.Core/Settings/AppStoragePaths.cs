namespace LightLaunchpad.Core.Settings;

public static class AppStoragePaths
{
    public static string GetIconCacheDirectory(string documentsPath)
    {
        return Path.Combine(documentsPath, "LightLaunchpad", "icons");
    }
}
