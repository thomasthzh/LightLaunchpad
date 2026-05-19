using LightLaunchpad.App;

namespace LightLaunchpad.App.Tests;

public sealed class LaunchpadWindowPlacementTests
{
    public static void Calculate_UsesFullVirtualScreenForLaunchpadMode()
    {
        var virtualScreen = new ScreenBounds(-1200, 0, 3120, 1080);
        var workArea = new ScreenBounds(0, 0, 1920, 1040);

        var bounds = LaunchpadWindowPlacement.Calculate("Launchpad", virtualScreen, workArea);

        TestAssert.Equal(-1200d, bounds.Left);
        TestAssert.Equal(0d, bounds.Top);
        TestAssert.Equal(3120d, bounds.Width);
        TestAssert.Equal(1080d, bounds.Height);
    }

    public static void Calculate_CentersSpotlightWindowInsideWorkArea()
    {
        var virtualScreen = new ScreenBounds(0, 0, 1920, 1080);
        var workArea = new ScreenBounds(0, 0, 1440, 900);

        var bounds = LaunchpadWindowPlacement.Calculate("Spotlight", virtualScreen, workArea);

        TestAssert.Equal(980d, bounds.Width);
        TestAssert.Equal(720d, bounds.Height);
        TestAssert.Equal(230d, bounds.Left);
        TestAssert.Equal(90d, bounds.Top);
    }

    public static void Calculate_UsesConfiguredSpotlightWindowSize()
    {
        var virtualScreen = new ScreenBounds(0, 0, 1920, 1080);
        var workArea = new ScreenBounds(0, 0, 1440, 900);
        var options = new SpotlightWindowOptions(840, 640);

        var bounds = LaunchpadWindowPlacement.Calculate("Spotlight", virtualScreen, workArea, options);

        TestAssert.Equal(840d, bounds.Width);
        TestAssert.Equal(640d, bounds.Height);
        TestAssert.Equal(300d, bounds.Left);
        TestAssert.Equal(130d, bounds.Top);
    }

    public static void Calculate_KeepsSmallSpotlightWindowWithinWorkAreaPadding()
    {
        var virtualScreen = new ScreenBounds(0, 0, 900, 700);
        var workArea = new ScreenBounds(20, 30, 760, 520);

        var bounds = LaunchpadWindowPlacement.Calculate("Spotlight", virtualScreen, workArea);

        TestAssert.Equal(680d, bounds.Width);
        TestAssert.Equal(440d, bounds.Height);
        TestAssert.Equal(60d, bounds.Left);
        TestAssert.Equal(70d, bounds.Top);
    }

    public static void Calculate_DoesNotOverflowTinyWorkArea()
    {
        var virtualScreen = new ScreenBounds(0, 0, 500, 360);
        var workArea = new ScreenBounds(10, 20, 480, 320);

        var bounds = LaunchpadWindowPlacement.Calculate("Spotlight", virtualScreen, workArea);

        TestAssert.Equal(480d, bounds.Width);
        TestAssert.Equal(320d, bounds.Height);
        TestAssert.Equal(10d, bounds.Left);
        TestAssert.Equal(20d, bounds.Top);
    }
}