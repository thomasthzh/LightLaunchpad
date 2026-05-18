namespace LightLaunchpad.Core.Import;

public sealed record StartMenuImportCandidate(
    string SourcePath,
    string DisplayName,
    string RegionHint);
