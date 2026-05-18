namespace LightLaunchpad.Core.Import;

public static class StartMenuImporter
{
    private static readonly HashSet<string> SupportedExtensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".lnk",
        ".url"
    };

    public static IReadOnlyList<StartMenuImportCandidate> Discover(IEnumerable<string> roots)
    {
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var candidates = new List<StartMenuImportCandidate>();

        foreach (var root in roots.Where(Directory.Exists).Distinct(StringComparer.OrdinalIgnoreCase))
        {
            foreach (var file in Directory.EnumerateFiles(root, "*.*", SearchOption.AllDirectories)
                         .Where(path => SupportedExtensions.Contains(Path.GetExtension(path))))
            {
                var normalized = NormalizePath(file);
                if (!seen.Add(normalized))
                {
                    continue;
                }

                candidates.Add(new StartMenuImportCandidate(
                    file,
                    Path.GetFileNameWithoutExtension(file),
                    ResolveRegionHint(root, file)));
            }
        }

        return candidates
            .OrderBy(candidate => candidate.DisplayName, StringComparer.CurrentCultureIgnoreCase)
            .ToList();
    }

    private static string ResolveRegionHint(string root, string file)
    {
        var parent = Directory.GetParent(file);
        if (parent is null)
        {
            return "Uncategorized";
        }

        var rootFullPath = NormalizePath(root).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        var parentFullPath = NormalizePath(parent.FullName).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        return string.Equals(rootFullPath, parentFullPath, StringComparison.OrdinalIgnoreCase)
            ? "Uncategorized"
            : parent.Name;
    }

    private static string NormalizePath(string path)
    {
        try
        {
            return Path.GetFullPath(path);
        }
        catch (Exception)
        {
            return path.Trim();
        }
    }
}
