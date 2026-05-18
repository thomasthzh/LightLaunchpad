using System.Text.Json;
using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.Core.Layout;

public sealed class LayoutService
{
    public const string UncategorizedRegionId = "uncategorized";

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true
    };

    private readonly string _layoutPath;

    public LayoutService(string layoutPath)
    {
        _layoutPath = layoutPath;
    }

    public LaunchpadLayout Load()
    {
        if (!File.Exists(_layoutPath))
        {
            return CreateDefault();
        }

        using var stream = File.OpenRead(_layoutPath);
        return EnsureDefaultRegion(JsonSerializer.Deserialize<LaunchpadLayout>(stream, JsonOptions) ?? CreateDefault());
    }

    public void Save(LaunchpadLayout layout)
    {
        var directory = Path.GetDirectoryName(_layoutPath);
        if (!string.IsNullOrWhiteSpace(directory))
        {
            Directory.CreateDirectory(directory);
        }

        var json = JsonSerializer.Serialize(EnsureDefaultRegion(layout), JsonOptions);
        var tempPath = _layoutPath + ".tmp";
        File.WriteAllText(tempPath, json);

        if (File.Exists(_layoutPath))
        {
            File.Replace(tempPath, _layoutPath, null);
        }
        else
        {
            File.Move(tempPath, _layoutPath);
        }
    }

    public LaunchpadLayout MergeItems(LaunchpadLayout layout, IEnumerable<LaunchItem> currentItems)
    {
        var normalizedExisting = layout.Items
            .ToDictionary(item => NormalizePath(item.SourcePath), item => item, StringComparer.OrdinalIgnoreCase);
        var mergedItems = new List<LaunchpadLayoutItem>();
        var currentIndex = 0;

        foreach (var launchItem in currentItems)
        {
            var normalized = NormalizePath(launchItem.SourcePath);
            if (normalizedExisting.TryGetValue(normalized, out var existing))
            {
                mergedItems.Add(existing);
            }
            else
            {
                mergedItems.Add(new LaunchpadLayoutItem(
                    launchItem.SourcePath,
                    launchItem.DisplayName,
                    UncategorizedRegionId,
                    currentIndex));
            }

            currentIndex++;
        }

        return EnsureDefaultRegion(layout with { Items = RenumberByRegion(mergedItems) });
    }

    public LaunchpadLayout SetViewMode(LaunchpadLayout layout, LaunchpadViewMode viewMode)
    {
        return layout with { ViewMode = viewMode };
    }

    public LaunchpadLayout CreateRegion(LaunchpadLayout layout, string name)
    {
        var trimmedName = string.IsNullOrWhiteSpace(name) ? "New Region" : name.Trim();
        var idBase = Slugify(trimmedName);
        var id = idBase;
        var index = 2;
        while (layout.Regions.Any(region => string.Equals(region.Id, id, StringComparison.OrdinalIgnoreCase)))
        {
            id = $"{idBase}-{index}";
            index++;
        }

        var order = layout.Regions.Count == 0 ? 0 : layout.Regions.Max(region => region.Order) + 1;
        return layout with
        {
            Regions = layout.Regions.Append(new LaunchpadRegion(id, trimmedName, order)).ToList()
        };
    }

    public LaunchpadLayout RenameRegion(LaunchpadLayout layout, string regionId, string name)
    {
        if (regionId == UncategorizedRegionId || string.IsNullOrWhiteSpace(name))
        {
            return layout;
        }

        return layout with
        {
            Regions = layout.Regions
                .Select(region => region.Id == regionId ? region with { Name = name.Trim() } : region)
                .ToList()
        };
    }

    public LaunchpadLayout DeleteRegion(LaunchpadLayout layout, string regionId)
    {
        if (regionId == UncategorizedRegionId)
        {
            return EnsureDefaultRegion(layout);
        }

        var items = layout.Items
            .Select(item => item.RegionId == regionId ? item with { RegionId = UncategorizedRegionId } : item)
            .ToList();
        var regions = layout.Regions
            .Where(region => region.Id != regionId)
            .ToList();

        return EnsureDefaultRegion(layout with { Regions = regions, Items = RenumberByRegion(items) });
    }

    public LaunchpadLayout MoveItem(LaunchpadLayout layout, string sourcePath, string regionId, int order)
    {
        var targetRegion = layout.Regions.Any(region => region.Id == regionId) ? regionId : UncategorizedRegionId;
        var normalized = NormalizePath(sourcePath);
        var items = layout.Items
            .Select(item => NormalizePath(item.SourcePath) == normalized ? item with { RegionId = targetRegion, Order = order } : item)
            .ToList();

        return layout with { Items = RenumberByRegion(items) };
    }

    public LaunchpadLayout RenameItem(LaunchpadLayout layout, string sourcePath, string displayName)
    {
        if (string.IsNullOrWhiteSpace(displayName))
        {
            return layout;
        }

        var normalized = NormalizePath(sourcePath);
        return layout with
        {
            Items = layout.Items
                .Select(item => NormalizePath(item.SourcePath) == normalized ? item with { DisplayName = displayName.Trim() } : item)
                .ToList()
        };
    }

    public LaunchpadLayout RemoveItem(LaunchpadLayout layout, string sourcePath)
    {
        var normalized = NormalizePath(sourcePath);
        return layout with
        {
            Items = RenumberByRegion(layout.Items
                .Where(item => NormalizePath(item.SourcePath) != normalized)
                .ToList())
        };
    }

    private static LaunchpadLayout CreateDefault()
    {
        return new LaunchpadLayout(
            LaunchpadViewMode.InlineRegions,
            [new LaunchpadRegion(UncategorizedRegionId, "Uncategorized", 0)],
            []);
    }

    private static LaunchpadLayout EnsureDefaultRegion(LaunchpadLayout layout)
    {
        if (layout.Regions.Any(region => region.Id == UncategorizedRegionId))
        {
            return layout;
        }

        var regions = new List<LaunchpadRegion>
        {
            new(UncategorizedRegionId, "Uncategorized", 0)
        };
        regions.AddRange(layout.Regions.Select(region => region with { Order = region.Order + 1 }));
        return layout with { Regions = regions };
    }

    private static List<LaunchpadLayoutItem> RenumberByRegion(IReadOnlyList<LaunchpadLayoutItem> items)
    {
        return items
            .GroupBy(item => item.RegionId)
            .SelectMany(group => group
                .OrderBy(item => item.Order)
                .ThenBy(item => item.DisplayName, StringComparer.CurrentCultureIgnoreCase)
                .Select((item, index) => item with { Order = index }))
            .ToList();
    }

    private static string NormalizePath(string path)
    {
        try
        {
            return Path.GetFullPath(path);
        }
        catch (Exception)
        {
            return path.Trim();
        }
    }

    private static string Slugify(string value)
    {
        var chars = value
            .Trim()
            .ToLowerInvariant()
            .Select(character => char.IsLetterOrDigit(character) ? character : '-')
            .ToArray();
        var slug = new string(chars).Trim('-');
        return string.IsNullOrWhiteSpace(slug) ? "region" : slug;
    }
}
