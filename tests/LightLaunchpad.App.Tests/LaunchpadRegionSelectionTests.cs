using System.IO;
using LightLaunchpad.App.Services;
using LightLaunchpad.App.ViewModels;
using LightLaunchpad.Core.Layout;
using LightLaunchpad.Core.Settings;

namespace LightLaunchpad.App.Tests;

public sealed class LaunchpadRegionSelectionTests
{
    public static void ToggleRegionSelection_TracksSelectedRegions()
    {
        var viewModel = CreateViewModel();
        var first = viewModel.Regions[0];
        var second = viewModel.Regions[1];

        viewModel.ToggleSelectRegion(first);
        viewModel.ToggleSelectRegion(second);

        TestAssert.SequenceEqual(new[] { first.Id, second.Id }, viewModel.SelectedRegions.Select(region => region.Id));
        TestAssert.True(first.IsSelected);
        TestAssert.True(second.IsSelected);
    }

    public static void ClearSelection_ClearsItemsAndRegions()
    {
        var viewModel = CreateViewModel();
        var region = viewModel.Regions[0];
        viewModel.ToggleSelectRegion(region);

        viewModel.ClearSelection();

        TestAssert.False(region.IsSelected);
        TestAssert.Equal(0, viewModel.SelectedRegions.Count);
    }

    public static void LoadItems_HidesEmptyUncategorizedWhenAllAppsAreCategorized()
    {
        var tempDir = Path.Combine(Path.GetTempPath(), "LightLaunchpadAppTests", Guid.NewGuid().ToString("N"));
        var settings = new AppSettings(tempDir, "Alt+D", false, "Medium", "InlineRegions", "High");
        var viewModel = new LaunchpadViewModel(settings, new IconCacheService(Path.Combine(tempDir, "icons"), 56));
        var layout = new LaunchpadLayout(
            LaunchpadViewMode.InlineRegions,
            [
                new LaunchpadRegion(LayoutService.UncategorizedRegionId, "Uncategorized", 0),
                new LaunchpadRegion("tools", "Tools", 1)
            ],
            [
                new LaunchpadLayoutItem(@"C:\Launchpad\Code.lnk", "Code", "tools", 0)
            ]);
        var items = new[]
        {
            new LightLaunchpad.Core.Shortcuts.LaunchItem(
                "Code",
                @"C:\Launchpad\Code.lnk",
                null,
                LightLaunchpad.Core.Shortcuts.LaunchItemKind.Shortcut)
        };

        viewModel.LoadItems(layout, items);

        TestAssert.SequenceEqual(new[] { "tools" }, viewModel.Regions.Select(region => region.Id));
    }

    private static LaunchpadViewModel CreateViewModel()
    {
        var tempDir = Path.Combine(Path.GetTempPath(), "LightLaunchpadAppTests", Guid.NewGuid().ToString("N"));
        var settings = new AppSettings(tempDir, "Alt+D", false, "Medium", "InlineRegions", "High");
        var viewModel = new LaunchpadViewModel(settings, new IconCacheService(Path.Combine(tempDir, "icons"), 56));
        var layout = new LaunchpadLayout(
            LaunchpadViewMode.InlineRegions,
            [
                new LaunchpadRegion("tools", "Tools", 0),
                new LaunchpadRegion("games", "Games", 1)
            ],
            []);

        viewModel.LoadItems(layout, []);
        return viewModel;
    }
}