using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Media;
using LightLaunchpad.Core.Layout;
using LightLaunchpad.Core.Search;
using LightLaunchpad.Core.Settings;
using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.App.ViewModels;

public sealed class LaunchpadViewModel : INotifyPropertyChanged
{
    private readonly Func<LaunchItem, ImageSource?> _iconFactory;
    private readonly List<LaunchItem> _items = [];
    private LaunchpadLayout _layout = new(LaunchpadViewMode.InlineRegions, [], []);
    private string _searchText = string.Empty;
    private string _iconSize = "Medium";
    private LaunchpadViewMode _viewMode = LaunchpadViewMode.InlineRegions;
    private string _activeRegionId = LayoutService.UncategorizedRegionId;
    private LaunchItemViewModel? _selectedItem;

    public LaunchpadViewModel(AppSettings settings, Func<LaunchItem, ImageSource?> iconFactory)
    {
        _iconSize = settings.IconSize;
        _viewMode = Enum.TryParse<LaunchpadViewMode>(settings.ViewMode, out var mode)
            ? mode
            : LaunchpadViewMode.InlineRegions;
        _iconFactory = iconFactory;
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public ObservableCollection<LaunchItemViewModel> FilteredItems { get; } = [];

    public ObservableCollection<LaunchpadRegionViewModel> Regions { get; } = [];

    public ObservableCollection<LaunchItemViewModel> ActiveRegionItems { get; } = [];

    public LaunchpadViewMode ViewMode
    {
        get => _viewMode;
        private set
        {
            if (_viewMode == value)
            {
                return;
            }

            _viewMode = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(InlineRegionsVisibility));
            OnPropertyChanged(nameof(RegionTabsVisibility));
        }
    }

    public Visibility InlineRegionsVisibility => ViewMode == LaunchpadViewMode.InlineRegions
        ? Visibility.Visible
        : Visibility.Collapsed;

    public Visibility RegionTabsVisibility => ViewMode == LaunchpadViewMode.RegionTabs
        ? Visibility.Visible
        : Visibility.Collapsed;

    public string ActiveRegionId
    {
        get => _activeRegionId;
        set
        {
            if (_activeRegionId == value)
            {
                return;
            }

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
            if (_searchText == value)
            {
                return;
            }

            _searchText = value;
            OnPropertyChanged();
            RefreshFilter();
        }
    }

    public void UpdateSettings(AppSettings settings)
    {
        _iconSize = settings.IconSize;
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
        FilteredItems.Clear();

        foreach (var item in LaunchItemSearch.Filter(_items, SearchText))
        {
            var layoutItem = FindLayoutItem(item);
            FilteredItems.Add(new LaunchItemViewModel(
                item with { DisplayName = layoutItem?.DisplayName ?? item.DisplayName },
                _iconFactory(item),
                ResolveIconSize(),
                layoutItem?.RegionId ?? LayoutService.UncategorizedRegionId,
                layoutItem?.Order ?? 0));
        }

        SelectedItem = FilteredItems.FirstOrDefault();
        RefreshRegions();
        RefreshActiveRegion();
    }

    private void RefreshRegions()
    {
        Regions.Clear();
        var filteredByRegion = FilteredItems.GroupBy(item => item.RegionId).ToDictionary(group => group.Key, group => group.ToList());

        foreach (var region in _layout.Regions.OrderBy(region => region.Order))
        {
            var regionViewModel = new LaunchpadRegionViewModel(region.Id, region.Name);
            if (filteredByRegion.TryGetValue(region.Id, out var regionItems))
            {
                foreach (var item in regionItems.OrderBy(item => item.Order).ThenBy(item => item.DisplayName, StringComparer.CurrentCultureIgnoreCase))
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
        if (active is null)
        {
            return;
        }

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
        try
        {
            return Path.GetFullPath(path);
        }
        catch (Exception)
        {
            return path.Trim();
        }
    }

    private double ResolveIconSize()
    {
        return _iconSize.ToUpperInvariant() switch
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
}
