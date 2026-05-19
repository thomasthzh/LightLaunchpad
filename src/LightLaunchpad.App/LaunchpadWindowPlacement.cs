using LightLaunchpad.Core.Settings;

namespace LightLaunchpad.App;

public readonly record struct ScreenBounds(double Left, double Top, double Width, double Height);

public readonly record struct WindowBounds(double Left, double Top, double Width, double Height);

public static class LaunchpadWindowPlacement
{
    private const double SpotlightMaxWidth = 980;
    private const double SpotlightMaxHeight = 720;
    private const double SpotlightPadding = 80;
    private const double SpotlightMinimumWidth = 520;
    private const double SpotlightMinimumHeight = 420;

    public static WindowBounds Calculate(string? displayMode, ScreenBounds virtualScreen, ScreenBounds workArea)
    {
        if (!LaunchpadDisplayModes.IsSpotlight(displayMode))
        {
            return new WindowBounds(
                virtualScreen.Left,
                virtualScreen.Top,
                virtualScreen.Width,
                virtualScreen.Height);
        }

        var width = Math.Min(
            workArea.Width,
            Math.Min(SpotlightMaxWidth, Math.Max(SpotlightMinimumWidth, workArea.Width - SpotlightPadding)));
        var height = Math.Min(
            workArea.Height,
            Math.Min(SpotlightMaxHeight, Math.Max(SpotlightMinimumHeight, workArea.Height - SpotlightPadding)));
        var left = workArea.Left + (workArea.Width - width) / 2;
        var top = workArea.Top + (workArea.Height - height) / 2;

        return new WindowBounds(left, top, width, height);
    }
}