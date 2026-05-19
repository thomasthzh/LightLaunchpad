namespace LightLaunchpad.Core.Shortcuts;

public sealed class ShortcutRepository
{
    private readonly string _launchpadFolder;
    private readonly Func<string, string?>? _resolveShortcutTarget;

    public ShortcutRepository(string launchpadFolder, Func<string, string?>? resolveShortcutTarget = null)
    {
        _launchpadFolder = launchpadFolder;
        _resolveShortcutTarget = resolveShortcutTarget;
    }

    public IReadOnlyList<LaunchItem> LoadItems()
    {
        Directory.CreateDirectory(_launchpadFolder);

        return Directory.EnumerateFiles(_launchpadFolder)
            .Where(LaunchFileTypes.IsSupported)
            .OrderBy(Path.GetFileName, StringComparer.CurrentCultureIgnoreCase)
            .Select(CreateItem)
            .ToList();
    }

    private LaunchItem CreateItem(string path)
    {
        if (!LaunchFileTypes.TryGetKind(path, out var kind))
        {
            throw new NotSupportedException($"Unsupported launch file type: {path}");
        }

        string? targetPath = kind switch
        {
            LaunchItemKind.Shortcut => _resolveShortcutTarget?.Invoke(path),
            LaunchItemKind.Url => InternetShortcutFile.Read(path).Url,
            LaunchItemKind.Executable => path,
            _ => null
        };
        return new LaunchItem(Path.GetFileNameWithoutExtension(path), path, targetPath, kind);
    }
}
