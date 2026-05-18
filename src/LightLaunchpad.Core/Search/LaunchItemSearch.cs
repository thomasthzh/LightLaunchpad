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
            .Select((item, index) => new SearchResult(item, index, Score(item.DisplayName, normalizedQuery)))
            .Where(result => result.Score >= 0)
            .OrderBy(result => result.Score)
            .ThenBy(result => result.Item.DisplayName, StringComparer.CurrentCultureIgnoreCase)
            .ThenBy(result => result.Index)
            .Select(result => result.Item);
    }

    private static int Score(string displayName, string query)
    {
        if (displayName.StartsWith(query, StringComparison.CurrentCultureIgnoreCase))
        {
            return 0;
        }

        if (displayName.Contains(query, StringComparison.CurrentCultureIgnoreCase))
        {
            return 1;
        }

        return -1;
    }

    private sealed record SearchResult(LaunchItem Item, int Index, int Score);
}
