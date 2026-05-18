namespace LightLaunchpad.Core.Layout;

public sealed record LaunchpadLayout(
    LaunchpadViewMode ViewMode,
    List<LaunchpadRegion> Regions,
    List<LaunchpadLayoutItem> Items);
