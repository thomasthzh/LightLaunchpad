using LightLaunchpad.Core.Settings;

namespace LightLaunchpad.App;

public readonly record struct ScreenBounds(double Left, double Top, double Width, double Height);

public readonly record struct WindowBounds(double Left, double Top, double Width, double Height);

public readonly record struct SpotlightWindowOptions(double Width, double Height);

public static class LaunchpadWindowPlacement
{
    private const double SpotlightPadding = 80;

    public static WindowBounds Calculate(string? displayMode, ScreenBounds virtualScreen, ScreenBounds workArea)
    {
        return Calculate(
            displayMode,
            virtualScreen,
            workArea,
            new SpotlightWindowOptions(
                AppSettingLimits.DefaultSpotlightWidth,
                AppSettingLimits.DefaultSpotlightHeight));
    }

    public static WindowBounds Calculate(
        string? displayMode,
        ScreenBounds virtualScreen,
        ScreenBounds workArea,
        SpotlightWindowOptions options)
    {
        if (!LaunchpadDisplayModes.IsSpotlight(displayMode))
        {
            return new WindowBounds(
                virtualScreen.Left,
                virtualScreen.Top,
                virtualScreen.Width,
                virtualScreen.Height);
        }

        var requestedWidth = AppSettingLimits.NormalizeSpotlightWidth(options.Width);
        var requestedHeight = AppSettingLimits.NormalizeSpotlightHeight(options.Height);
        var availableWidth = workArea.Width < AppSettingLimits.MinSpotlightWidth + SpotlightPadding
            ? workArea.Width
            : Math.Max(0, workArea.Width - SpotlightPadding);
        var availableHeight = workArea.Height < AppSettingLimits.MinSpotlightHeight + SpotlightPadding
            ? workArea.Height
            : Math.Max(0, workArea.Height - SpotlightPadding);
        var width = Math.Min(
            workArea.Width,
            Math.Min(requestedWidth, availableWidth));
        var height = Math.Min(
            workArea.Height,
            Math.Min(requestedHeight, availableHeight));
        var left = workArea.Left + (workArea.Width - width) / 2;
        var top = workArea.Top + (workArea.Height - height) / 2;

        return new WindowBounds(left, top, width, height);
    }
}