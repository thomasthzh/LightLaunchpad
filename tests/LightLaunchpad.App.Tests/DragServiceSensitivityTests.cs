using System.Windows;
using LightLaunchpad.App.Services;

namespace LightLaunchpad.App.Tests;

public sealed class DragServiceSensitivityTests
{
    public static void HasExceededThreshold_UsesMouseSensitivity()
    {
        var drag = new DragService(mouseSensitivity: 2);
        drag.BeginTrack(new Point(0, 0), new Point(0, 0), new Point(0, 0));

        TestAssert.True(drag.HasExceededThreshold(new Point(5, 0)));
    }

    public static void HasExceededThreshold_ClampsMouseSensitivity()
    {
        var drag = new DragService(mouseSensitivity: 0);
        drag.BeginTrack(new Point(0, 0), new Point(0, 0), new Point(0, 0));

        TestAssert.False(drag.HasExceededThreshold(new Point(9, 0)));
        TestAssert.True(drag.HasExceededThreshold(new Point(10, 0)));
    }
}