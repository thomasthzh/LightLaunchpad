using System.Windows.Media;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.App.ViewModels;

public sealed class LaunchItemViewModel : INotifyPropertyChanged
{
    private ImageSource? _icon;

    public LaunchItemViewModel(LaunchItem item, ImageSource? icon, double iconSize, string regionId, int order)
    {
        Item = item;
        _icon = icon;
        IconSize = iconSize;
        RegionId = regionId;
        Order = order;
    }

    public LaunchItem Item { get; }

    public event PropertyChangedEventHandler? PropertyChanged;

    public string DisplayName => Item.DisplayName;

    public string SourcePath => Item.SourcePath;

    public string RegionId { get; }

    public int Order { get; }

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

    public double TileSize => IconSize + 78;

    public double LabelWidth => Math.Max(82, IconSize + 34);

    public void LoadIcon(Func<LaunchItem, ImageSource?> iconFactory)
    {
        Icon ??= iconFactory(Item);
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}
