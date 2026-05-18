using System.Windows.Media;
using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.App.ViewModels;

public sealed class LaunchItemViewModel
{
    public LaunchItemViewModel(LaunchItem item, ImageSource? icon, double iconSize, string regionId, int order)
    {
        Item = item;
        Icon = icon;
        IconSize = iconSize;
        RegionId = regionId;
        Order = order;
    }

    public LaunchItem Item { get; }

    public string DisplayName => Item.DisplayName;

    public string SourcePath => Item.SourcePath;

    public string RegionId { get; }

    public int Order { get; }

    public ImageSource? Icon { get; }

    public double IconSize { get; }

    public double TileSize => IconSize + 78;

    public double LabelWidth => Math.Max(82, IconSize + 34);
}
