using LightLaunchpad.Core.Layout;
using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.Core.Tests;

public sealed class LayoutServiceTests
{
    public static void Load_CreatesDefaultLayoutWhenMissing()
    {
        var path = Path.Combine(TestPaths.CreateTempDirectory(), "layout.json");
        var service = new LayoutService(path);

        var layout = service.Load();

        TestAssert.Equal(LaunchpadViewMode.InlineRegions, layout.ViewMode);
        TestAssert.Equal(LayoutService.UncategorizedRegionId, layout.Regions.Single().Id);
        TestAssert.Equal("Uncategorized", layout.Regions.Single().Name);
    }

    public static void MergeItems_AddsNewFolderItemsToUncategorized()
    {
        var path = Path.Combine(TestPaths.CreateTempDirectory(), "layout.json");
        var service = new LayoutService(path);
        var items = new[]
        {
            new LaunchItem("Code", @"C:\Launchpad\Code.lnk", null, LaunchItemKind.Shortcut),
            new LaunchItem("Chrome", @"C:\Launchpad\Chrome.lnk", null, LaunchItemKind.Shortcut)
        };

        var layout = service.MergeItems(service.Load(), items);

        TestAssert.SequenceEqual(new[] { "Code", "Chrome" }, layout.Items.Select(item => item.DisplayName));
        TestAssert.True(layout.Items.All(item => item.RegionId == LayoutService.UncategorizedRegionId));
        TestAssert.SequenceEqual(new[] { 0, 1 }, layout.Items.Select(item => item.Order));
    }

    public static void DeleteRegion_MovesItemsToUncategorized()
    {
        var path = Path.Combine(TestPaths.CreateTempDirectory(), "layout.json");
        var service = new LayoutService(path);
        var layout = new LaunchpadLayout(
            LaunchpadViewMode.RegionTabs,
            new List<LaunchpadRegion>
            {
                new(LayoutService.UncategorizedRegionId, "Uncategorized", 0),
                new("games", "Games", 1)
            },
            new List<LaunchpadLayoutItem>
            {
                new(@"C:\Launchpad\Game.lnk", "Game", "games", 0)
            });

        var updated = service.DeleteRegion(layout, "games");

        TestAssert.Equal(1, updated.Regions.Count);
        TestAssert.Equal(LayoutService.UncategorizedRegionId, updated.Items.Single().RegionId);
    }

    public static void SaveAndLoad_RoundTripsViewMode()
    {
        var path = Path.Combine(TestPaths.CreateTempDirectory(), "layout.json");
        var service = new LayoutService(path);
        var layout = service.Load() with { ViewMode = LaunchpadViewMode.RegionTabs };

        service.Save(layout);
        var loaded = service.Load();

        TestAssert.Equal(LaunchpadViewMode.RegionTabs, loaded.ViewMode);
    }

    public static void MoveItem_ReordersWithinRegionAtRequestedIndex()
    {
        var path = Path.Combine(TestPaths.CreateTempDirectory(), "layout.json");
        var service = new LayoutService(path);
        var layout = new LaunchpadLayout(
            LaunchpadViewMode.InlineRegions,
            new List<LaunchpadRegion>
            {
                new(LayoutService.UncategorizedRegionId, "Uncategorized", 0)
            },
            new List<LaunchpadLayoutItem>
            {
                new(@"C:\Launchpad\Alpha.lnk", "Alpha", LayoutService.UncategorizedRegionId, 0),
                new(@"C:\Launchpad\Beta.lnk", "Beta", LayoutService.UncategorizedRegionId, 1),
                new(@"C:\Launchpad\Gamma.lnk", "Gamma", LayoutService.UncategorizedRegionId, 2)
            });

        var updated = service.MoveItem(layout, @"C:\Launchpad\Gamma.lnk", LayoutService.UncategorizedRegionId, 0);

        TestAssert.SequenceEqual(
            new[] { "Gamma", "Alpha", "Beta" },
            updated.Items.OrderBy(item => item.Order).Select(item => item.DisplayName));
        TestAssert.SequenceEqual(new[] { 0, 1, 2 }, updated.Items.OrderBy(item => item.Order).Select(item => item.Order));
    }

    public static void MoveItem_MovesBetweenRegionsAtRequestedIndex()
    {
        var path = Path.Combine(TestPaths.CreateTempDirectory(), "layout.json");
        var service = new LayoutService(path);
        var layout = new LaunchpadLayout(
            LaunchpadViewMode.InlineRegions,
            new List<LaunchpadRegion>
            {
                new(LayoutService.UncategorizedRegionId, "Uncategorized", 0),
                new("tools", "Tools", 1)
            },
            new List<LaunchpadLayoutItem>
            {
                new(@"C:\Launchpad\Alpha.lnk", "Alpha", LayoutService.UncategorizedRegionId, 0),
                new(@"C:\Launchpad\Beta.lnk", "Beta", "tools", 0),
                new(@"C:\Launchpad\Gamma.lnk", "Gamma", "tools", 1)
            });

        var updated = service.MoveItem(layout, @"C:\Launchpad\Alpha.lnk", "tools", 1);
        var tools = updated.Items
            .Where(item => item.RegionId == "tools")
            .OrderBy(item => item.Order)
            .Select(item => item.DisplayName);

        TestAssert.SequenceEqual(new[] { "Beta", "Alpha", "Gamma" }, tools);
    }

    public static void MoveItems_MovesSelectionAsContiguousGroupUsingIndexAmongRemainingItems()
    {
        var path = Path.Combine(TestPaths.CreateTempDirectory(), "layout.json");
        var service = new LayoutService(path);
        var layout = new LaunchpadLayout(
            LaunchpadViewMode.InlineRegions,
            new List<LaunchpadRegion>
            {
                new(LayoutService.UncategorizedRegionId, "Uncategorized", 0)
            },
            new List<LaunchpadLayoutItem>
            {
                new(@"C:\Launchpad\Alpha.lnk", "Alpha", LayoutService.UncategorizedRegionId, 0),
                new(@"C:\Launchpad\Beta.lnk", "Beta", LayoutService.UncategorizedRegionId, 1),
                new(@"C:\Launchpad\Gamma.lnk", "Gamma", LayoutService.UncategorizedRegionId, 2),
                new(@"C:\Launchpad\Delta.lnk", "Delta", LayoutService.UncategorizedRegionId, 3)
            });

        var updated = service.MoveItems(
            layout,
            [@"C:\Launchpad\Beta.lnk", @"C:\Launchpad\Gamma.lnk"],
            LayoutService.UncategorizedRegionId,
            2);

        TestAssert.SequenceEqual(
            new[] { "Alpha", "Delta", "Beta", "Gamma" },
            updated.Items.OrderBy(item => item.Order).Select(item => item.DisplayName));
    }

    public static void MoveRegion_ReordersRegionsAroundTarget()
    {
        var path = Path.Combine(TestPaths.CreateTempDirectory(), "layout.json");
        var service = new LayoutService(path);
        var layout = new LaunchpadLayout(
            LaunchpadViewMode.InlineRegions,
            new List<LaunchpadRegion>
            {
                new(LayoutService.UncategorizedRegionId, "Uncategorized", 0),
                new("tools", "Tools", 1),
                new("games", "Games", 2),
                new("media", "Media", 3)
            },
            []);

        var updated = service.MoveRegion(layout, "media", "tools", insertAfterTarget: false);

        TestAssert.SequenceEqual(
            new[] { LayoutService.UncategorizedRegionId, "media", "tools", "games" },
            updated.Regions.OrderBy(region => region.Order).Select(region => region.Id));
        TestAssert.SequenceEqual(new[] { 0, 1, 2, 3 }, updated.Regions.OrderBy(region => region.Order).Select(region => region.Order));
    }
}