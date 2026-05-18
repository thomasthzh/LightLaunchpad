using System.Collections.ObjectModel;

namespace LightLaunchpad.App.ViewModels;

public sealed class LaunchpadRegionViewModel
{
    public LaunchpadRegionViewModel(string id, string name)
    {
        Id = id;
        Name = name;
    }

    public string Id { get; }

    public string Name { get; }

    public ObservableCollection<LaunchItemViewModel> Items { get; } = [];
}
