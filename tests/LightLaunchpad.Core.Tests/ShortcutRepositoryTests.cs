using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.Core.Tests;

public sealed class ShortcutRepositoryTests
{
    public static void LoadItems_CreatesFolderWhenMissing()
    {
        var tempRoot = TestPaths.CreateTempDirectory();
        var launchpadFolder = Path.Combine(tempRoot, "Launchpad");
        var repository = new ShortcutRepository(launchpadFolder);

        var items = repository.LoadItems();

        TestAssert.True(Directory.Exists(launchpadFolder));
        TestAssert.Equal(0, items.Count);
    }

    public static void LoadItems_IncludesSupportedFilesAndSkipsUnsupportedFiles()
    {
        var launchpadFolder = TestPaths.CreateTempDirectory();
        File.WriteAllText(Path.Combine(launchpadFolder, "Code.lnk"), "");
        File.WriteAllText(Path.Combine(launchpadFolder, "Docs.url"), "");
        File.WriteAllText(Path.Combine(launchpadFolder, "Tool.exe"), "");
        File.WriteAllText(Path.Combine(launchpadFolder, "Notes.txt"), "");
        Directory.CreateDirectory(Path.Combine(launchpadFolder, "Nested"));

        var repository = new ShortcutRepository(launchpadFolder);

        var items = repository.LoadItems();

        TestAssert.SequenceEqual(new[] { "Code", "Docs", "Tool" }, items.Select(item => item.DisplayName));
        TestAssert.SequenceEqual(
            new[] { LaunchItemKind.Shortcut, LaunchItemKind.Url, LaunchItemKind.Executable },
            items.Select(item => item.Kind));
    }
}
