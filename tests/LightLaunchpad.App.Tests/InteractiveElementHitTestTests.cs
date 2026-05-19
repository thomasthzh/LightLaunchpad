using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using LightLaunchpad.App.Services;

namespace LightLaunchpad.App.Tests;

public sealed class InteractiveElementHitTestTests
{
    public static void IsOverInteractiveElement_TreatsScrollBarChromeAsInteractive()
    {
        TestAssert.True(InteractiveElementHitTest.IsOverInteractiveElement(new ScrollBar()));
        TestAssert.True(InteractiveElementHitTest.IsOverInteractiveElement(new Thumb()));
        TestAssert.True(InteractiveElementHitTest.IsOverInteractiveElement(new Track()));
        TestAssert.True(InteractiveElementHitTest.IsOverInteractiveElement(new RepeatButton()));
    }

    public static void IsOverInteractiveElement_IgnoresPlainLayoutSurfaces()
    {
        TestAssert.False(InteractiveElementHitTest.IsOverInteractiveElement(null));
        TestAssert.False(InteractiveElementHitTest.IsOverInteractiveElement(new Border()));
    }
}
