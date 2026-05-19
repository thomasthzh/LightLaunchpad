using LightLaunchpad.Core.Import;

namespace LightLaunchpad.Core.Tests;

public sealed class StartMenuImporterTests
{
    public static void Discover_ReadsShortcutsRecursivelyWithRegionHints()
    {
        var root = TestPaths.CreateTempDirectory();
        var tools = Path.Combine(root, "Tools");
        Directory.CreateDirectory(tools);
        File.WriteAllText(Path.Combine(tools, "ClickOnce.appref-ms"), "");
        File.WriteAllText(Path.Combine(tools, "NeeView.lnk"), "");
        File.WriteAllText(Path.Combine(root, "Website.url"), "");
        File.WriteAllText(Path.Combine(root, "Ignore.txt"), "");

        var candidates = StartMenuImporter.Discover([root]).ToList();

        TestAssert.SequenceEqual(new[] { "ClickOnce", "NeeView", "Website" }, candidates.Select(candidate => candidate.DisplayName));
        TestAssert.SequenceEqual(new[] { "Tools", "Tools", "Uncategorized" }, candidates.Select(candidate => candidate.RegionHint));
    }

    public static void Discover_DeduplicatesByNormalizedSourcePath()
    {
        var root = TestPaths.CreateTempDirectory();
        var shortcut = Path.Combine(root, "App.lnk");
        File.WriteAllText(shortcut, "");

        var candidates = StartMenuImporter.Discover([root, root]).ToList();

        TestAssert.Equal(1, candidates.Count);
        TestAssert.Equal(shortcut, candidates.Single().SourcePath);
    }
}
