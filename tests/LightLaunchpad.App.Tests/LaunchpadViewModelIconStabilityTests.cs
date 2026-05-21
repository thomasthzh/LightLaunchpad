using System.IO;
using System.Windows.Media;
using LightLaunchpad.App.Services;
using LightLaunchpad.App.ViewModels;
using LightLaunchpad.Core.Layout;
using LightLaunchpad.Core.Settings;
using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.App.Tests;

public sealed class LaunchpadViewModelIconStabilityTests
{
    public static void LoadItems_PreservesLoadedIconForMatchingSourcePath()
    {
        var (viewModel, layout, launchItems) = CreateViewModel();
        var source = FindItem(viewModel, "Beta");
        var icon = CreateIcon();
        source.SetIcon(icon);

        viewModel.LoadItems(layout, launchItems);

        var refreshed = FindItem(viewModel, "Beta");
        TestAssert.Same(icon, refreshed.Icon!);
        TestAssert.True(refreshed.IconLoadAttempted);
    }

    public static void ApplyLoadedIcon_AppliesToReplacementItemAfterRefresh()
    {
        var (viewModel, layout, launchItems) = CreateViewModel();
        var staleItem = FindItem(viewModel, "Beta");
        var icon = CreateIcon();

        viewModel.LoadItems(layout, launchItems);
        viewModel.ApplyLoadedIcon(staleItem, icon);

        var current = FindItem(viewModel, "Beta");
        TestAssert.Same(icon, current.Icon!);
    }

    public static void SearchText_NoMatchesThenCleared_RestoresCachedIcons()
    {
        var (viewModel, _, _) = CreateViewModel();
        var source = FindItem(viewModel, "Beta");
        var icon = CreateIcon();
        source.SetIcon(icon);

        viewModel.SearchText = "Adasdasd";
        TestAssert.Equal(0, viewModel.FilteredItems.Count);

        viewModel.SearchText = string.Empty;

        var restored = FindItem(viewModel, "Beta");
        TestAssert.Same(icon, restored.Icon!);
        TestAssert.True(restored.IconLoadAttempted);
    }

    public static void SearchText_NoMatchesThenCleared_RequeuesMissingIcons()
    {
        var (viewModel, _, _) = CreateViewModel();

        viewModel.SearchText = "Adasdasd";
        TestAssert.Equal(0, viewModel.FilteredItems.Count);

        viewModel.SearchText = string.Empty;

        TestAssert.Equal(3, viewModel.GetMissingIconItems(10).Count);
    }

    public static void DragPlaceholderMethods_DoNotMutateVisibleCollections()
    {
        var (viewModel, _, _) = CreateViewModel();
        var source = FindItem(viewModel, "Beta");
        var beforeRegionItems = viewModel.Regions.Single().Items.ToList();
        var beforeActiveItems = viewModel.ActiveRegionItems.ToList();

        viewModel.BeginDragPlaceholder(source);
        AssertCollectionsUnchanged(viewModel, beforeRegionItems, beforeActiveItems);

        viewModel.MoveDragPlaceholder(source, LayoutService.UncategorizedRegionId, 0);
        AssertCollectionsUnchanged(viewModel, beforeRegionItems, beforeActiveItems);

        viewModel.EndDragPlaceholder(source);

        TestAssert.False(source.IsDragPlaceholder);
        AssertCollectionsUnchanged(viewModel, beforeRegionItems, beforeActiveItems);
    }

    private static (LaunchpadViewModel ViewModel, LaunchpadLayout Layout, IReadOnlyList<LaunchItem> Items) CreateViewModel()
    {
        var tempDir = Path.Combine(Path.GetTempPath(), "LightLaunchpadAppTests", Guid.NewGuid().ToString("N"));
        var settings = new AppSettings(
            LaunchpadFolder: tempDir,
            Hotkey: "Alt+D",
            StartWithWindows: false,
            IconSize: "Medium",
            ViewMode: "InlineRegions",
            IconQuality: "High");
        var viewModel = new LaunchpadViewModel(settings, new IconCacheService(Path.Combine(tempDir, "icons"), 56));
        var layout = new LaunchpadLayout(
            LaunchpadViewMode.InlineRegions,
            [new LaunchpadRegion(LayoutService.UncategorizedRegionId, "Uncategorized", 0)],
            [
                new LaunchpadLayoutItem(@"C:\Launchpad\Alpha.lnk", "Alpha", LayoutService.UncategorizedRegionId, 0),
                new LaunchpadLayoutItem(@"C:\Launchpad\Beta.lnk", "Beta", LayoutService.UncategorizedRegionId, 1),
                new LaunchpadLayoutItem(@"C:\Launchpad\Gamma.lnk", "Gamma", LayoutService.UncategorizedRegionId, 2)
            ]);
        var launchItems = layout.Items
            .Select(item => new LaunchItem(item.DisplayName, item.SourcePath, TargetPath: null, LaunchItemKind.Shortcut))
            .ToList();

        viewModel.LoadItems(layout, launchItems);
        return (viewModel, layout, launchItems);
    }

    private static LaunchItemViewModel FindItem(LaunchpadViewModel viewModel, string displayName)
    {
        return viewModel.Regions.Single().Items.Single(item => item.DisplayName == displayName);
    }

    private static ImageSource CreateIcon()
    {
        var icon = new DrawingImage();
        icon.Freeze();
        return icon;
    }

    private static void AssertCollectionsUnchanged(
        LaunchpadViewModel viewModel,
        IReadOnlyList<LaunchItemViewModel> beforeRegionItems,
        IReadOnlyList<LaunchItemViewModel> beforeActiveItems)
    {
        TestAssert.False(viewModel.Regions.Single().Items.Any(item => item.IsDragPlaceholder));
        TestAssert.SequenceSame(beforeRegionItems, viewModel.Regions.Single().Items);
        TestAssert.SequenceSame(beforeActiveItems, viewModel.ActiveRegionItems);
    }
}
