using LightLaunchpad.Core.Hotkeys;

namespace LightLaunchpad.Core.Tests;

public sealed class HotkeyGestureTests
{
    public static void Parse_ReadsAltLetterGesture()
    {
        var gesture = HotkeyGesture.Parse("Alt+D");

        TestAssert.True(gesture.Alt);
        TestAssert.False(gesture.Control);
        TestAssert.False(gesture.Shift);
        TestAssert.False(gesture.Windows);
        TestAssert.Equal('D', gesture.Key);
        TestAssert.Equal("Alt+D", gesture.ToString());
    }

    public static void Parse_NormalizesWhitespaceAndCase()
    {
        var gesture = HotkeyGesture.Parse(" alt + l ");

        TestAssert.True(gesture.Alt);
        TestAssert.Equal('L', gesture.Key);
        TestAssert.Equal("Alt+L", gesture.ToString());
    }
}
