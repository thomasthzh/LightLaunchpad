using System.IO;
using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.App.Services;

public static class IconSourceResolver
{
    public static IReadOnlyList<string> ResolveIconSources(
        string sourcePath,
        string? targetPath,
        LaunchItemKind kind)
    {
        var sources = new List<string>();

        if (kind == LaunchItemKind.Url)
        {
            var info = InternetShortcutFile.Read(sourcePath);
            AddIfUsablePath(sources, info.IconFile);
            AddIfUsablePath(sources, sourcePath, requireExists: false);
            return sources;
        }

        if (kind == LaunchItemKind.Shortcut)
        {
            AddIfUsablePath(sources, targetPath);
            AddIfUsablePath(sources, sourcePath, requireExists: false);
            return sources;
        }

        AddIfUsablePath(sources, sourcePath, requireExists: false);
        return sources;
    }

    private static void AddIfUsablePath(ICollection<string> sources, string? path, bool requireExists = true)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            return;
        }

        var cleaned = path.Trim().Trim('"');
        if (requireExists && !File.Exists(cleaned) && !Directory.Exists(cleaned))
        {
            return;
        }

        if (!sources.Contains(cleaned, StringComparer.OrdinalIgnoreCase))
        {
            sources.Add(cleaned);
        }
    }
}
