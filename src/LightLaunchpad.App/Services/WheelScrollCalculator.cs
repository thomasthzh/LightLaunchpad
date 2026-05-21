using LightLaunchpad.Core.Settings;

namespace LightLaunchpad.App.Services;

public static class WheelScrollCalculator
{
    public static double CalculateOffset(
        double currentOffset,
        int wheelDelta,
        double wheelSensitivity,
        double scrollableHeight)
    {
        var sensitivity = AppSettingLimits.NormalizeWheelSensitivity(wheelSensitivity);
        var next = currentOffset - (wheelDelta * sensitivity);
        return Math.Min(Math.Max(next, 0), Math.Max(scrollableHeight, 0));
    }
}