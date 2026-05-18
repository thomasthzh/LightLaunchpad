using System.IO;
using LightLaunchpad.Core.Import;

namespace LightLaunchpad.App.Services;

public sealed class StartMenuImportService
{
    private readonly string _launchpadFolder;

    public StartMenuImportService(string launchpadFolder)
    {
        _launchpadFolder = launchpadFolder;
    }

    public StartMenuImportSummary Import(IEnumerable<StartMenuImportCandidate> candidates)
    {
        Directory.CreateDirectory(_launchpadFolder);
        var summary = new StartMenuImportSummary();

        foreach (var candidate in candidates)
        {
            if (!File.Exists(candidate.SourcePath))
            {
                summary.MissingCount++;
                continue;
            }

            var destination = GetUniqueDestination(candidate.DisplayName, Path.GetExtension(candidate.SourcePath));
            File.Copy(candidate.SourcePath, destination, overwrite: false);
            summary.ImportedItems.Add(new ImportedStartMenuItem(destination, candidate.RegionHint));
        }

        return summary;
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
}

public sealed class StartMenuImportSummary
{
    public List<ImportedStartMenuItem> ImportedItems { get; } = [];

    public int MissingCount { get; set; }
}

public sealed record ImportedStartMenuItem(string SourcePath, string RegionHint);
