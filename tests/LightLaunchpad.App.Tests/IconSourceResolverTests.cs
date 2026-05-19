using System.IO;
using LightLaunchpad.App.Services;
using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.App.Tests;

public sealed class IconSourceResolverTests
{
    public static void ResolveIconSources_PrefersSteamUrlIconFile()
    {
        var tempDir = CreateTempDirectory();
        var iconPath = Path.Combine(tempDir, "steam-game.ico");
        var shortcutPath = Path.Combine(tempDir, "卡片魔王：只剩个头！.url");
        File.WriteAllText(iconPath, "");
        File.WriteAllText(shortcutPath, $"""
            [InternetShortcut]
            URL=steam://rungameid/3720420
            IconFile={iconPath}
            IconIndex=0
            """);

        var sources = IconSourceResolver.ResolveIconSources(
            shortcutPath,
            "steam://rungameid/3720420",
            LaunchItemKind.Url);

        TestAssert.SequenceEqual(new[] { iconPath, shortcutPath }, sources);
    }

    public static void ResolveIconSources_FallsBackToUrlFileWhenIconFileIsMissing()
    {
        var tempDir = CreateTempDirectory();
        var shortcutPath = Path.Combine(tempDir, "Steam Game.url");
        File.WriteAllText(shortcutPath, $"""
            [InternetShortcut]
            URL=steam://rungameid/123
            IconFile={Path.Combine(tempDir, "missing.ico")}
            """);

        var sources = IconSourceResolver.ResolveIconSources(
            shortcutPath,
            "steam://rungameid/123",
            LaunchItemKind.Url);

        TestAssert.SequenceEqual(new[] { shortcutPath }, sources);
    }

    private static string CreateTempDirectory()
    {
        var path = Path.Combine(Path.GetTempPath(), "LightLaunchpad.App.Tests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(path);
        return path;
    }
}
