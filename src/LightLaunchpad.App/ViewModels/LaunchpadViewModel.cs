using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Media;
using LightLaunchpad.App.Services;
using LightLaunchpad.Core.Layout;
using LightLaunchpad.Core.Search;
using LightLaunchpad.Core.Settings;
using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.App.ViewModels;

public sealed class LaunchpadViewModel : INotifyPropertyChanged
{
    private readonly List<LaunchItem> _items = [];
    private IconCacheService _iconCache;
    private LaunchpadLayout _layout = new(LaunchpadViewMode.InlineRegions, [], []);
    private string _searchText = string.Empty;
    private double _iconSizePx = 56;
    private double _appSpacing = AppSettingLimits.DefaultAppSpacing;
    private LaunchpadViewMode _viewMode = LaunchpadViewMode.InlineRegions;
    private string _activeRegionId = LayoutService.UncategorizedRegionId;
    private LaunchItemViewModel? _selectedItem;

    public LaunchpadViewModel(AppSettings settings, IconCacheService iconCache)
    {
        _iconSizePx = ResolveIconSizePx(settings.IconSize);
        _appSpacing = AppSettingLimits.NormalizeAppSpacing(settings.AppSpacing);
        _viewMode = Enum.TryParse<LaunchpadViewMode>(settings.ViewMode, out var mode)
            ? mode
            : LaunchpadViewMode.InlineRegions;
        _iconCache = iconCache;
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public ObservableCollection<LaunchItemViewModel> FilteredItems { get; } = [];

    public ObservableCollection<LaunchpadRegionViewModel> Regions { get; } = [];

    public ObservableCollection<LaunchItemViewModel> ActiveRegionItems { get; } = [];

    public IReadOnlyList<LaunchpadRegionViewModel> SelectedRegions =>
        Regions.Where(region => region.IsSelected).ToList();

    public LaunchpadViewMode ViewMode
    {
        get => _viewMode;
        private set
        {
            if (_viewMode == value) return;
            _viewMode = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(InlineRegionsVisibility));
            OnPropertyChanged(nameof(RegionTabsVisibility));
        }
    }

    public Visibility InlineRegionsVisibility => ViewMode == LaunchpadViewMode.InlineRegions
        ? Visibility.Visible : Visibility.Collapsed;

    public Visibility RegionTabsVisibility => ViewMode == LaunchpadViewMode.RegionTabs
        ? Visibility.Visible : Visibility.Collapsed;

    public string ActiveRegionId
    {
        get => _activeRegionId;
        set
        {
            if (_activeRegionId == value) return;
            _activeRegionId = value;
            OnPropertyChanged();
            RefreshActiveRegion();
        }
    }

    public LaunchItemViewModel? SelectedItem
    {
        get => _selectedItem;
        private set
        {
            if (!ReferenceEquals(_selectedItem, value))
            {
                _selectedItem = value;
                OnPropertyChanged();
            }
        }
    }

    public string SearchText
    {
        get => _searchText;
        set
        {
            if (_searchText == value) return;
            _searchText = value;
            OnPropertyChanged();
            RefreshFilter();
        }
    }

    public void UpdateSettings(AppSettings settings, IconCacheService iconCache)
    {
        _iconSizePx = ResolveIconSizePx(settings.IconSize);
        _appSpacing = AppSettingLimits.NormalizeAppSpacing(settings.AppSpacing);
        _iconCache = iconCache;
        if (Enum.TryParse<LaunchpadViewMode>(settings.ViewMode, out var mode))
        {
            ViewMode = mode;
        }
        RefreshFilter();
    }

    public void LoadItems(LaunchpadLayout layout, IEnumerable<LaunchItem> items)
    {
        _layout = layout;
        ViewMode = layout.ViewMode;
        _items.Clear();
        _items.AddRange(items);
        if (!_layout.Regions.Any(region => region.Id == ActiveRegionId))
        {
            ActiveRegionId = _layout.Regions.OrderBy(region => region.Order).FirstOrDefault()?.Id
                ?? LayoutService.UncategorizedRegionId;
        }
        RefreshFilter();
    }

    private void RefreshFilter()
    {
        var existingBySource = FilteredItems
            .GroupBy(item => Normalize(item.SourcePath), StringComparer.OrdinalIgnoreCase)
            .ToDictionary(group => group.Key, group => group.First(), StringComparer.OrdinalIgnoreCase);
        var selectedSource = SelectedItem is not null ? Normalize(SelectedItem.SourcePath) : null;

        FilteredItems.Clear();

        foreach (var item in LaunchItemSearch.Filter(_items, SearchText))
        {
            var layoutItem = FindLayoutItem(item);
            existingBySource.TryGetValue(Normalize(item.SourcePath), out var existing);
            var itemViewModel = new LaunchItemViewModel(
                item with { DisplayName = layoutItem?.DisplayName ?? item.DisplayName },
                existing?.Icon,
                _iconSizePx,
                _appSpacing,
                layoutItem?.RegionId ?? LayoutService.UncategorizedRegionId,
                layoutItem?.Order ?? 0,
                existing?.IconLoadAttempted ?? false)
            {
                IsSelected = existing?.IsSelected ?? false
            };
            FilteredItems.Add(itemViewModel);
        }

        SelectedItem = selectedSource is not null
            ? FilteredItems.FirstOrDefault(item =>
                string.Equals(Normalize(item.SourcePath), selectedSource, StringComparison.OrdinalIgnoreCase))
                ?? FilteredItems.FirstOrDefault()
            : FilteredItems.FirstOrDefault();
        RefreshRegions();
        RefreshActiveRegion();
    }

    public int LoadMissingIcons(int limit)
    {
        var loaded = 0;
        foreach (var item in FilteredItems.Where(item => item.Icon is null).Take(limit))
        {
            item.LoadIcon(launchItem => _iconCache.GetIcon(
                launchItem.SourcePath,
                launchItem.TargetPath,
                launchItem.Kind));
            loaded++;
        }

        return loaded;
    }

    public IReadOnlyList<LaunchItemViewModel> GetMissingIconItems(int limit)
    {
        return FilteredItems
            .Where(item => item.Icon is null && !item.IconLoadAttempted)
            .Take(limit)
            .ToList();
    }

    public ImageSource? LoadIconFor(LaunchItemViewModel item)
    {
        return _iconCache.GetIcon(
            item.Item.SourcePath,
            item.Item.TargetPath,
            item.Item.Kind);
    }

    public void ApplyLoadedIcon(LaunchItemViewModel item, ImageSource? icon)
    {
        var target = FilteredItems.Contains(item)
            ? item
            : FilteredItems.FirstOrDefault(current =>
                string.Equals(Normalize(current.SourcePath), Normalize(item.SourcePath), StringComparison.OrdinalIgnoreCase));

        target?.SetIcon(icon);
    }

    public LaunchItem? FindItemBySourcePath(string sourcePath)
    {
        var normalized = Normalize(sourcePath);
        return _items.FirstOrDefault(item =>
            string.Equals(Normalize(item.SourcePath), normalized, StringComparison.OrdinalIgnoreCase));
    }

    public void ClearLoadedIcons()
    {
        foreach (var item in FilteredItems)
        {
            item.ClearIcon();
        }

        _iconCache.ClearMemoryCache();
    }

    private void RefreshRegions()
    {
        var selectedRegionIds = SelectedRegions
            .Select(region => region.Id)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        Regions.Clear();
        var filteredByRegion = FilteredItems.GroupBy(item => item.RegionId)
            .ToDictionary(g => g.Key, g => g.ToList());

        foreach (var region in _layout.Regions.OrderBy(region => region.Order))
        {
            var regionViewModel = new LaunchpadRegionViewModel(region.Id, region.Name)
            {
                IsSelected = selectedRegionIds.Contains(region.Id)
            };
            if (filteredByRegion.TryGetValue(region.Id, out var regionItems))
            {
                foreach (var item in regionItems.OrderBy(item => item.Order)
                    .ThenBy(item => item.DisplayName, StringComparer.CurrentCultureIgnoreCase))
                {
                    regionViewModel.Items.Add(item);
                }
            }
            if (regionViewModel.Items.Count > 0 || string.IsNullOrWhiteSpace(SearchText))
            {
                Regions.Add(regionViewModel);
            }
        }
    }

    private void RefreshActiveRegion()
    {
        ActiveRegionItems.Clear();
        var active = Regions.FirstOrDefault(region => region.Id == ActiveRegionId) ?? Regions.FirstOrDefault();
        if (active is null) return;
        foreach (var item in active.Items)
        {
            ActiveRegionItems.Add(item);
        }
    }

    private LaunchpadLayoutItem? FindLayoutItem(LaunchItem item)
    {
        return _layout.Items.FirstOrDefault(layoutItem =>
            string.Equals(Normalize(layoutItem.SourcePath), Normalize(item.SourcePath), StringComparison.OrdinalIgnoreCase));
    }

    private static string Normalize(string path)
    {
        try { return Path.GetFullPath(path); }
        catch { return path.Trim(); }
    }

    private static double ResolveIconSizePx(string iconSize)
    {
        return iconSize.ToUpperInvariant() switch
        {
            "SMALL" => 42,
            "LARGE" => 72,
            _ => 56
        };
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }

    // --- Selection ---

    public IReadOnlyList<LaunchItemViewModel> SelectedItems =>
        FilteredItems.Where(i => i.IsSelected).ToList();

    public void SelectItem(LaunchItemViewModel item, bool addToSelection = false)
    {
        if (!addToSelection) ClearSelection();
        item.IsSelected = true;
        SelectedItem = item;
    }

    public void ToggleSelectItem(LaunchItemViewModel item)
    {
        item.IsSelected = !item.IsSelected;
        if (item.IsSelected) SelectedItem = item;
    }

    public void SelectRange(LaunchItemViewModel from, LaunchItemViewModel to)
    {
        var items = FilteredItems.ToList();
        var startIdx = items.IndexOf(from);
        var endIdx = items.IndexOf(to);
        if (startIdx < 0 || endIdx < 0) return;
        if (startIdx > endIdx) (startIdx, endIdx) = (endIdx, startIdx);
        for (var i = startIdx; i <= endIdx; i++)
        {
            items[i].IsSelected = true;
        }
        SelectedItem = to;
    }

    public void SelectInRect(System.Windows.Rect rect)
    {
        // Items are in FilteredItems; their visual positions are managed by the view
        // The view will call this with items that are within the rect
    }

    public void ClearSelection()
    {
        foreach (var item in FilteredItems)
        {
            item.IsSelected = false;
        }

        ClearRegionSelection();
    }

    public void SelectRegion(LaunchpadRegionViewModel region, bool addToSelection = false)
    {
        if (!addToSelection)
        {
            ClearRegionSelection();
        }

        region.IsSelected = true;
    }

    public void ToggleSelectRegion(LaunchpadRegionViewModel region)
    {
        region.IsSelected = !region.IsSelected;
    }

    public void ClearRegionSelection()
    {
        foreach (var region in Regions)
        {
            region.IsSelected = false;
        }
    }

    // --- Drag support ---

    public (string RegionId, int Index) BeginDragPlaceholder(LaunchItemViewModel item)
    {
        var region = Regions.FirstOrDefault(region => region.Items.Contains(item));
        var index = region?.Items.IndexOf(item) ?? Math.Max(0, item.Order);
        var regionId = region?.Id ?? item.RegionId;

        item.IsDragPlaceholder = false;
        return (regionId, index);
    }

    public void MoveDragPlaceholder(LaunchItemViewModel item, string regionId, int insertIndex)
    {
        item.IsDragPlaceholder = false;
    }

    public void EndDragPlaceholder(LaunchItemViewModel item)
    {
        item.IsDragPlaceholder = false;
    }

    public void RestoreDragPlaceholder(LaunchItemViewModel item, string regionId, int insertIndex)
    {
        item.IsDragPlaceholder = false;
    }

    public void RemoveItemFromDisplay(LaunchItem item)
    {
        _items.Remove(item);
        RefreshFilter();
    }

    public void AddItemToDisplay(LaunchItem item)
    {
        if (!_items.Contains(item))
            _items.Add(item);
        RefreshFilter();
    }
}