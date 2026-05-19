namespace LightLaunchpad.Core.Layout;

public sealed class DragTargetSmoother
{
    private readonly int _settleFrames;
    private readonly double _minMoveDistance;
    private string? _lastAcceptedRegionId;
    private int _lastAcceptedIndex = -1;
    private string? _candidateRegionId;
    private int _candidateIndex = -1;
    private int _candidateFrames;
    private double _lastAcceptedX;
    private double _lastAcceptedY;

    public DragTargetSmoother(int settleFrames = 2, double minMoveDistance = 18)
    {
        _settleFrames = Math.Max(1, settleFrames);
        _minMoveDistance = Math.Max(0, minMoveDistance);
    }

    public bool TryAccept(string regionId, int index, double x, double y, out DragTarget target)
    {
        target = default;

        if (_lastAcceptedRegionId == regionId && _lastAcceptedIndex == index)
        {
            _lastAcceptedX = x;
            _lastAcceptedY = y;
            ResetCandidate();
            return false;
        }

        var movedEnough = _lastAcceptedRegionId is null
            || DistanceSquared(x, y, _lastAcceptedX, _lastAcceptedY) >= _minMoveDistance * _minMoveDistance;
        if (!movedEnough)
        {
            ResetCandidate();
            return false;
        }

        if (_candidateRegionId == regionId && _candidateIndex == index)
        {
            _candidateFrames++;
        }
        else
        {
            _candidateRegionId = regionId;
            _candidateIndex = index;
            _candidateFrames = 1;
        }

        if (_candidateFrames < _settleFrames)
        {
            return false;
        }

        _lastAcceptedRegionId = regionId;
        _lastAcceptedIndex = index;
        _lastAcceptedX = x;
        _lastAcceptedY = y;
        target = new DragTarget(regionId, index);
        ResetCandidate();
        return true;
    }

    public void Reset()
    {
        _lastAcceptedRegionId = null;
        _lastAcceptedIndex = -1;
        _lastAcceptedX = 0;
        _lastAcceptedY = 0;
        ResetCandidate();
    }

    private void ResetCandidate()
    {
        _candidateRegionId = null;
        _candidateIndex = -1;
        _candidateFrames = 0;
    }

    private static double DistanceSquared(double x1, double y1, double x2, double y2)
    {
        var dx = x1 - x2;
        var dy = y1 - y2;
        return dx * dx + dy * dy;
    }
}

public readonly record struct DragTarget(string RegionId, int Index);
