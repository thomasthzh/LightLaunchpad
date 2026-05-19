namespace LightLaunchpad.Core.Shortcuts;

public static class LaunchFileTypes
{
    private static readonly Dictionary<string, LaunchItemKind> ExtensionKinds = new(StringComparer.OrdinalIgnoreCase)
    {
        [".lnk"] = LaunchItemKind.Shortcut,
        [".url"] = LaunchItemKind.Url,
        [".exe"] = LaunchItemKind.Executable,
        [".com"] = LaunchItemKind.Executable,
        [".bat"] = LaunchItemKind.Executable,
        [".cmd"] = LaunchItemKind.Executable,
        [".ps1"] = LaunchItemKind.Executable,
        [".vbs"] = LaunchItemKind.Executable,
        [".vbe"] = LaunchItemKind.Executable,
        [".js"] = LaunchItemKind.Executable,
        [".jse"] = LaunchItemKind.Executable,
        [".wsf"] = LaunchItemKind.Executable,
        [".msc"] = LaunchItemKind.Executable,
        [".cpl"] = LaunchItemKind.Executable,
        [".msi"] = LaunchItemKind.Executable,
        [".msp"] = LaunchItemKind.Executable,
        [".scr"] = LaunchItemKind.Executable,
        [".appref-ms"] = LaunchItemKind.Executable,
        [".application"] = LaunchItemKind.Executable
    };

    public static IReadOnlyCollection<string> SupportedExtensions => ExtensionKinds.Keys;

    public static bool IsSupported(string path) => TryGetKind(path, out _);

    public static bool TryGetKind(string path, out LaunchItemKind kind)
    {
        return ExtensionKinds.TryGetValue(Path.GetExtension(path), out kind);
    }
}
