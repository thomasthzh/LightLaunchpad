using LightLaunchpad.Core.Layout;

namespace LightLaunchpad.Core.Tests;

public sealed class DragTargetSmootherTests
{
    public static void TryAccept_WaitsForStableCandidateBeforeAccepting()
    {
        var smoother = new DragTargetSmoother(settleFrames: 2, minMoveDistance: 0);

        var firstAccepted = smoother.TryAccept("apps", 1, 10, 10, out _);
        var secondAccepted = smoother.TryAccept("apps", 1, 11, 10, out var target);

        TestAssert.False(firstAccepted);
        TestAssert.True(secondAccepted);
        TestAssert.Equal("apps", target.RegionId);
        TestAssert.Equal(1, target.Index);
    }

    public static void TryAccept_IgnoresTinyMovesAfterAcceptedTarget()
    {
        var smoother = new DragTargetSmoother(settleFrames: 1, minMoveDistance: 18);

        TestAssert.True(smoother.TryAccept("apps", 1, 10, 10, out _));
        var acceptedTinyMove = smoother.TryAccept("apps", 2, 18, 12, out _);

        TestAssert.False(acceptedTinyMove);
    }
}
