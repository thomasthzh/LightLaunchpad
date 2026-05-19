using System.Windows.Media;
using System.Windows;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.App.ViewModels;

public sealed class LaunchItemViewModel : INotifyPropertyChanged
{
    private ImageSource? _icon;
    private bool _isSelected;
    private bool _isDragPlaceholder;
    private bool _iconLoadAttempted;

    public LaunchItemViewModel(
        LaunchItem item,
        ImageSource? icon,
        double iconSize,
        double appSpacing,
        string regionId,
        int order,
        bool iconLoadAttempted = false)
    {
        Item = item;
        _icon = icon;
        _iconLoadAttempted = iconLoadAttempted || icon is not null;
        IconSize = iconSize;
        AppMargin = new Thickness(appSpacing);
        RegionId = regionId;
        Order = order;
    }

    public LaunchItem Item { get; }

    public event PropertyChangedEventHandler? PropertyChanged;

    public string DisplayName => Item.DisplayName;

    public string SourcePath => Item.SourcePath;

    public string RegionId { get; internal set; }

    public int Order { get; internal set; }

    public ImageSource? Icon
    {
        get => _icon;
        private set
        {
            if (!ReferenceEquals(_icon, value))
            {
                _icon = value;
                OnPropertyChanged();
            }
        }
    }

    public double IconSize { get; }

    public Thickness AppMargin { get; }

    public double TileSize => IconSize + 78;

    public double LabelWidth => Math.Max(82, IconSize + 34);

    public bool IsSelected
    {
        get => _isSelected;
        set
        {
            if (_isSelected != value)
            {
                _isSelected = value;
                OnPropertyChanged();
            }
        }
    }

    public bool IsDragPlaceholder
    {
        get => _isDragPlaceholder;
        set
        {
            if (_isDragPlaceholder != value)
            {
                _isDragPlaceholder = value;
                OnPropertyChanged();
            }
        }
    }

    public bool IconLoadAttempted => _iconLoadAttempted;

    public void LoadIcon(Func<LaunchItem, ImageSource?> iconFactory)
    {
        if (_iconLoadAttempted)
        {
            return;
        }

        SetIcon(iconFactory(Item));
    }

    public void SetIcon(ImageSource? icon)
    {
        _iconLoadAttempted = true;
        Icon = icon;
    }

    public void ClearIcon()
    {
        _iconLoadAttempted = false;
        Icon = null;
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}