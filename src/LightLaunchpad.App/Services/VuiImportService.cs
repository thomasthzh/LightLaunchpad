using System.IO;
using LightLaunchpad.Core.Import;

namespace LightLaunchpad.App.Services;

public sealed class VuiImportService
{
    private static readonly HashSet<string> SupportedExtensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".exe",
        ".lnk",
        ".url"
    };

    private readonly string _launchpadFolder;
    private readonly ShellShortcutService _shortcutService;

    public VuiImportService(string launchpadFolder, ShellShortcutService shortcutService)
    {
        _launchpadFolder = launchpadFolder;
        _shortcutService = shortcutService;
    }

    public VuiImportSummary Import(IEnumerable<string> vuiFiles)
    {
        Directory.CreateDirectory(_launchpadFolder);
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var summary = new VuiImportSummary();

        foreach (var vuiFile in vuiFiles)
        {
            var content = File.ReadAllText(vuiFile);
            foreach (var candidate in VuiImportParser.Parse(content))
            {
                var normalized = Normalize(candidate.Path);
                if (!seen.Add(normalized))
                {
                    summary.DuplicateCount++;
                    continue;
                }

                if (!File.Exists(candidate.Path))
                {
                    summary.MissingCount++;
                    continue;
                }

                var extension = Path.GetExtension(candidate.Path);
                if (!SupportedExtensions.Contains(extension))
                {
                    summary.UnsupportedCount++;
                    continue;
                }

                ImportPath(candidate.Path, extension);
                summary.ImportedCount++;
            }
        }

        return summary;
    }

    private void ImportPath(string sourcePath, string extension)
    {
        if (extension.Equals(".exe", StringComparison.OrdinalIgnoreCase))
        {
            var shortcutPath = GetUniqueDestination(Path.GetFileNameWithoutExtension(sourcePath), ".lnk");
            _shortcutService.CreateShortcut(shortcutPath, sourcePath);
            return;
        }

        var destinationPath = GetUniqueDestination(Path.GetFileNameWithoutExtension(sourcePath), extension);
        if (!string.Equals(Normalize(sourcePath), Normalize(destinationPath), StringComparison.OrdinalIgnoreCase))
        {
            File.Copy(sourcePath, destinationPath, overwrite: false);
        }
    }

    private string GetUniqueDestination(string baseName, string extension)
    {
        var safeName = string.Join("_", baseName.Split(Path.GetInvalidFileNameChars(), StringSplitOptions.RemoveEmptyEntries)).Trim();
        if (string.IsNullOrWhiteSpace(safeName))
        {
            safeName = "Imported";
        }

        var candidate = Path.Combine(_launchpadFolder, safeName + extension);
        var index = 2;
        while (File.Exists(candidate))
        {
            candidate = Path.Combine(_launchpadFolder, $"{safeName} ({index}){extension}");
            index++;
        }

        return candidate;
    }

    private static string Normalize(string path)
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

public sealed class VuiImportSummary
{
    public int ImportedCount { get; set; }

    public int MissingCount { get; set; }

    public int UnsupportedCount { get; set; }

    public int DuplicateCount { get; set; }
}
