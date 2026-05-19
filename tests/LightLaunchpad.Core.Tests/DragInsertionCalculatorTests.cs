using LightLaunchpad.Core.Layout;

namespace LightLaunchpad.Core.Tests;

public sealed class DragInsertionCalculatorTests
{
    public static void FindInsertionIndex_UsesTileHorizontalMidline()
    {
        var tiles = new[]
        {
            new DragTileBounds(0, 0, 100, 100),
            new DragTileBounds(100, 0, 100, 100)
        };

        TestAssert.Equal(0, DragInsertionCalculator.FindInsertionIndex(tiles, 49, 50));
        TestAssert.Equal(1, DragInsertionCalculator.FindInsertionIndex(tiles, 51, 50));
        TestAssert.Equal(1, DragInsertionCalculator.FindInsertionIndex(tiles, 149, 50));
        TestAssert.Equal(2, DragInsertionCalculator.FindInsertionIndex(tiles, 151, 50));
    }

    public static void FindInsertionIndex_UsesMatchingRowWhenPointerIsInBlankArea()
    {
        var tiles = new[]
        {
            new DragTileBounds(0, 0, 100, 100),
            new DragTileBounds(120, 0, 100, 100),
            new DragTileBounds(0, 130, 100, 100),
            new DragTileBounds(120, 130, 100, 100)
        };

        TestAssert.Equal(2, DragInsertionCalculator.FindInsertionIndex(tiles, 260, 40));
        TestAssert.Equal(2, DragInsertionCalculator.FindInsertionIndex(tiles, 10, 170));
        TestAssert.Equal(3, DragInsertionCalculator.FindInsertionIndex(tiles, 80, 170));
        TestAssert.Equal(4, DragInsertionCalculator.FindInsertionIndex(tiles, 260, 170));
    }
}
