using System.Windows.Media;
using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.App.ViewModels;

public sealed class LaunchItemViewModel
{
    public LaunchItemViewModel(LaunchItem item, ImageSource? icon, double iconSize)
    {
        Item = item;
        Icon = icon;
        IconSize = iconSize;
    }

    public LaunchItem Item { get; }

    public string DisplayName => Item.DisplayName;

    public ImageSource? Icon { get; }

    public double IconSize { get; }

    public double TileSize => IconSize + 78;

    public double LabelWidth => Math.Max(82, IconSize + 34);
}
