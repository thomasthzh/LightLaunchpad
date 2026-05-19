namespace LightLaunchpad.Core.Settings;

public static class AppSettingLimits
{
    public const double DefaultSpotlightWidth = 980;
    public const double DefaultSpotlightHeight = 720;
    public const double DefaultAppSpacing = 8;
    public const double DefaultMouseSensitivity = 1;
    public const double MinSpotlightWidth = 520;
    public const double MinSpotlightHeight = 420;
    public const double MaxSpotlightWidth = 1600;
    public const double MaxSpotlightHeight = 1000;

    public static double NormalizeSpotlightWidth(double value)
    {
        return ClampOrDefault(value, MinSpotlightWidth, MaxSpotlightWidth, DefaultSpotlightWidth);
    }

    public static double NormalizeSpotlightHeight(double value)
    {
        return ClampOrDefault(value, MinSpotlightHeight, MaxSpotlightHeight, DefaultSpotlightHeight);
    }

    public static double NormalizeAppSpacing(double value)
    {
        if (double.IsNaN(value) || double.IsInfinity(value))
        {
            return DefaultAppSpacing;
        }

        return Math.Min(28, Math.Max(0, value));
    }

    public static double NormalizeMouseSensitivity(double value)
    {
        return ClampOrDefault(value, 0.5, 3, DefaultMouseSensitivity);
    }

    private static double ClampOrDefault(double value, double min, double max, double fallback)
    {
        if (double.IsNaN(value) || double.IsInfinity(value) || value <= 0)
        {
            return fallback;
        }

        return Math.Min(max, Math.Max(min, value));
    }
}