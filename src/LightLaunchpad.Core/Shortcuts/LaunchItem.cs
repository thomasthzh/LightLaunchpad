namespace LightLaunchpad.Core.Shortcuts;

public enum LaunchItemKind
{
    Shortcut,
    Url,
    Executable
}

public sealed record LaunchItem(
    string DisplayName,
    string SourcePath,
    string? TargetPath,
    LaunchItemKind Kind);
