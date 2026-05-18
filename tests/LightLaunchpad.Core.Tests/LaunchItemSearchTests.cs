using LightLaunchpad.Core.Search;
using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.Core.Tests;

public sealed class LaunchItemSearchTests
{
    public static void Filter_RanksPrefixMatchesBeforeContainsMatches()
    {
        var items = new[]
        {
            new LaunchItem("Visual Studio Code", @"C:\Code.lnk", null, LaunchItemKind.Shortcut),
            new LaunchItem("Chrome", @"C:\Chrome.lnk", null, LaunchItemKind.Shortcut),
            new LaunchItem("Code Switch", @"C:\Switch.exe", null, LaunchItemKind.Executable)
        };

        var results = LaunchItemSearch.Filter(items, "code").ToList();

        TestAssert.SequenceEqual(new[] { "Code Switch", "Visual Studio Code" }, results.Select(item => item.DisplayName));
    }

    public static void Filter_ReturnsAllItemsWhenQueryIsBlank()
    {
        var items = new[]
        {
            new LaunchItem("B", @"C:\B.lnk", null, LaunchItemKind.Shortcut),
            new LaunchItem("A", @"C:\A.lnk", null, LaunchItemKind.Shortcut)
        };

        var results = LaunchItemSearch.Filter(items, " ").ToList();

        TestAssert.SequenceEqual(new[] { "B", "A" }, results.Select(item => item.DisplayName));
    }
}
