using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Windows.Media;
using LightLaunchpad.Core.Search;
using LightLaunchpad.Core.Settings;
using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.App.ViewModels;

public sealed class LaunchpadViewModel : INotifyPropertyChanged
{
    private readonly Func<LaunchItem, ImageSource?> _iconFactory;
    private readonly List<LaunchItem> _items = [];
    private string _searchText = string.Empty;
    private string _iconSize = "Medium";
    private LaunchItemViewModel? _selectedItem;

    public LaunchpadViewModel(AppSettings settings, Func<LaunchItem, ImageSource?> iconFactory)
    {
        _iconSize = settings.IconSize;
        _iconFactory = iconFactory;
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public ObservableCollection<LaunchItemViewModel> FilteredItems { get; } = [];

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
        RefreshFilter();
    }

    public void LoadItems(IEnumerable<LaunchItem> items)
    {
        _items.Clear();
        _items.AddRange(items);
        RefreshFilter();
    }

    private void RefreshFilter()
    {
        FilteredItems.Clear();

        foreach (var item in LaunchItemSearch.Filter(_items, SearchText))
        {
            FilteredItems.Add(new LaunchItemViewModel(item, _iconFactory(item), ResolveIconSize()));
        }

        SelectedItem = FilteredItems.FirstOrDefault();
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
