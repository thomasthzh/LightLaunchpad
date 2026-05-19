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
        File.WriteAllText(Path.Combine(launchpadFolder, "ClickOnce.appref-ms"), "");
        File.WriteAllText(Path.Combine(launchpadFolder, "Control.cpl"), "");
        File.WriteAllText(Path.Combine(launchpadFolder, "Script.cmd"), "");
        File.WriteAllText(Path.Combine(launchpadFolder, "Tool.exe"), "");
        File.WriteAllText(Path.Combine(launchpadFolder, "Notes.txt"), "");
        Directory.CreateDirectory(Path.Combine(launchpadFolder, "Nested"));

        var repository = new ShortcutRepository(launchpadFolder);

        var items = repository.LoadItems();

        TestAssert.SequenceEqual(new[] { "ClickOnce", "Code", "Control", "Docs", "Script", "Tool" }, items.Select(item => item.DisplayName));
        TestAssert.SequenceEqual(
            new[]
            {
                LaunchItemKind.Executable,
                LaunchItemKind.Shortcut,
                LaunchItemKind.Executable,
                LaunchItemKind.Url,
                LaunchItemKind.Executable,
                LaunchItemKind.Executable
            },
            items.Select(item => item.Kind));
    }
}
