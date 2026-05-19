namespace LightLaunchpad.Core.Settings;

public static class LaunchpadDisplayModes
{
    public const string Launchpad = "Launchpad";
    public const string Spotlight = "Spotlight";

    public static string Normalize(string? value)
    {
        if (string.Equals(value, Spotlight, StringComparison.OrdinalIgnoreCase) || value == "聚焦")
        {
            return Spotlight;
        }

        return Launchpad;
    }

    public static bool IsSpotlight(string? value)
    {
        return string.Equals(Normalize(value), Spotlight, StringComparison.OrdinalIgnoreCase);
    }
}