using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.Core.Search;

public static class LaunchItemSearch
{
    public static IEnumerable<LaunchItem> Filter(IEnumerable<LaunchItem> items, string query)
    {
        var normalizedQuery = query.Trim();
        if (normalizedQuery.Length == 0)
        {
            return items;
        }

        return items
            .Select((item, index) => new SearchResult(item, index, Score(BuildSearchText(item.DisplayName), normalizedQuery)))
            .Where(result => result.Score >= 0)
            .OrderBy(result => result.Score)
            .ThenBy(result => result.Item.DisplayName, StringComparer.CurrentCultureIgnoreCase)
            .ThenBy(result => result.Index)
            .Select(result => result.Item);
    }

    private static string BuildSearchText(string displayName)
    {
        var (full, initials) = BuildPinyinText(displayName);
        return string.Concat(displayName, " ", full, " ", initials);
    }

    private static (string Full, string Initials) BuildPinyinText(string text)
    {
        var full = new System.Text.StringBuilder();
        var initials = new System.Text.StringBuilder();
        foreach (var ch in text)
        {
            var pinyin = KnownPinyinForChar(ch);
            if (pinyin.Length == 0)
            {
                continue;
            }

            full.Append(pinyin);
            initials.Append(pinyin[0]);
        }

        return (full.ToString(), initials.ToString());
    }

    private static string KnownPinyinForChar(char ch) => ch switch
    {
        '爱' => "ai",
        '百' => "bai",
        '宝' => "bao",
        '哔' => "bi",
        '度' => "du",
        '东' => "dong",
        '抖' => "dou",
        '飞' => "fei",
        '付' => "fu",
        '狗' => "gou",
        '红' => "hong",
        '乎' => "hu",
        '火' => "huo",
        '京' => "jing",
        '剪' => "jian",
        '克' => "ke",
        '酷' => "ku",
        '夸' => "kua",
        '哩' => "li",
        '乐' => "le",
        '美' => "mei",
        '盘' => "pan",
        '拼' => "pin",
        '企' => "qi",
        '奇' => "qi",
        '绒' => "rong",
        '书' => "shu",
        '搜' => "sou",
        '台' => "tai",
        '淘' => "tao",
        '腾' => "teng",
        '团' => "tuan",
        '网' => "wang",
        '微' => "wei",
        '信' => "xin",
        '小' => "xiao",
        '迅' => "xun",
        '讯' => "xun",
        '音' => "yin",
        '影' => "ying",
        '易' => "yi",
        '艺' => "yi",
        '云' => "yun",
        '业' => "ye",
        '支' => "zhi",
        '知' => "zhi",
        _ => string.Empty
    };

    private static int Score(string searchText, string query)
    {
        if (searchText.StartsWith(query, StringComparison.CurrentCultureIgnoreCase))
        {
            return 0;
        }

        if (searchText.Contains(query, StringComparison.CurrentCultureIgnoreCase))
        {
            return 1;
        }

        return -1;
    }

    private sealed record SearchResult(LaunchItem Item, int Index, int Score);
}
