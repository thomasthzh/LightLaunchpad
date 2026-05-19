using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.Core.Tests;

public sealed class InternetShortcutFileTests
{
    public static void Read_ParsesSteamUrlIconFileAndIndex()
    {
        var tempDir = TestPaths.CreateTempDirectory();
        var iconPath = Path.Combine(tempDir, "steam-game.ico");
        var shortcutPath = Path.Combine(tempDir, "妹居物语 Demo.url");
        File.WriteAllText(shortcutPath, $"""
            [InternetShortcut]
            IDList=
            IconIndex=0
            URL=steam://rungameid/4027870
            IconFile="{iconPath}"
            """);

        var info = InternetShortcutFile.Read(shortcutPath);

        TestAssert.Equal("steam://rungameid/4027870", info.Url);
        TestAssert.Equal(iconPath, info.IconFile);
        TestAssert.Equal(0, info.IconIndex);
    }
}
