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

    public static void Filter_MatchesChineseAppsByPinyinAndInitials()
    {
        var items = new[]
        {
            new LaunchItem("微信", @"C:\WeChat.lnk", null, LaunchItemKind.Shortcut),
            new LaunchItem("支付宝", @"C:\Alipay.lnk", null, LaunchItemKind.Shortcut),
            new LaunchItem("Visual Studio Code", @"C:\Code.lnk", null, LaunchItemKind.Shortcut)
        };

        var fullPinyinResults = LaunchItemSearch.Filter(items, "weixin").ToList();
        var initialResults = LaunchItemSearch.Filter(items, "zfb").ToList();

        TestAssert.SequenceEqual(new[] { "微信" }, fullPinyinResults.Select(item => item.DisplayName));
        TestAssert.SequenceEqual(new[] { "支付宝" }, initialResults.Select(item => item.DisplayName));
    }
}
