namespace LightLaunchpad.Core.Shortcuts;

public sealed class ShortcutRepository
{
    private static readonly Dictionary<string, LaunchItemKind> SupportedExtensions = new(StringComparer.OrdinalIgnoreCase)
    {
        [".lnk"] = LaunchItemKind.Shortcut,
        [".url"] = LaunchItemKind.Url,
        [".exe"] = LaunchItemKind.Executable
    };

    private readonly string _launchpadFolder;

    public ShortcutRepository(string launchpadFolder)
    {
        _launchpadFolder = launchpadFolder;
    }

    public IReadOnlyList<LaunchItem> LoadItems()
    {
        Directory.CreateDirectory(_launchpadFolder);

        return Directory.EnumerateFiles(_launchpadFolder)
            .Where(IsSupported)
            .OrderBy(Path.GetFileName, StringComparer.CurrentCultureIgnoreCase)
            .Select(CreateItem)
            .ToList();
    }

    private static bool IsSupported(string path)
    {
        return SupportedExtensions.ContainsKey(Path.GetExtension(path));
    }

    private static LaunchItem CreateItem(string path)
    {
        var extension = Path.GetExtension(path);
        var kind = SupportedExtensions[extension];
        return new LaunchItem(Path.GetFileNameWithoutExtension(path), path, null, kind);
    }
}
