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
}
