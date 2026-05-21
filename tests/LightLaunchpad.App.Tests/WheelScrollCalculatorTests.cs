using LightLaunchpad.App.Services;

namespace LightLaunchpad.App.Tests;

public sealed class WheelScrollCalculatorTests
{
    public static void CalculateOffset_UsesWheelSensitivity()
    {
        var offset = WheelScrollCalculator.CalculateOffset(
            currentOffset: 500,
            wheelDelta: 120,
            wheelSensitivity: 2,
            scrollableHeight: 1000);

        TestAssert.Equal(260d, offset);
    }

    public static void CalculateOffset_ClampsToScrollableRange()
    {
        TestAssert.Equal(0d, WheelScrollCalculator.CalculateOffset(20, 120, 3, 1000));
        TestAssert.Equal(1000d, WheelScrollCalculator.CalculateOffset(990, -120, 3, 1000));
    }
}