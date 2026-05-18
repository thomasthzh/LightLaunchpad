namespace LightLaunchpad.Core.Layout;

public sealed record LaunchpadLayoutItem(
    string SourcePath,
    string DisplayName,
    string RegionId,
    int Order);
