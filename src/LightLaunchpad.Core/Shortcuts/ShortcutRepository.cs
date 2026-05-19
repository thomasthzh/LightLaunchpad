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
            .Where(IsSupported)
            .OrderBy(Path.GetFileName, StringComparer.CurrentCultureIgnoreCase)
            .Select(CreateItem)
            .ToList();
    }

    private static bool IsSupported(string path)
    {
        return SupportedExtensions.ContainsKey(Path.GetExtension(path));
    }

    private LaunchItem CreateItem(string path)
    {
        var extension = Path.GetExtension(path);
        var kind = SupportedExtensions[extension];
        string? targetPath = kind switch
        {
            LaunchItemKind.Shortcut => _resolveShortcutTarget?.Invoke(path),
            LaunchItemKind.Url => ParseUrlFile(path),
            LaunchItemKind.Executable => path,
            _ => null
        };
        return new LaunchItem(Path.GetFileNameWithoutExtension(path), path, targetPath, kind);
    }

    internal static string? ParseUrlFile(string path)
    {
        try
        {
            foreach (var line in File.ReadLines(path))
            {
                if (line.StartsWith("URL=", StringComparison.OrdinalIgnoreCase))
                {
                    return line.Substring(4).Trim();
                }
            }
        }
        catch
        {
            // ignore unreadable files
        }

        return null;
    }
}
