using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media.Animation;
using LightLaunchpad.App.Services;
using LightLaunchpad.App.ViewModels;
using LightLaunchpad.Core.Layout;

namespace LightLaunchpad.App;

public partial class LaunchpadWindow : Window
{
    private const string DragFormat = "LightLaunchpad.LaunchItemSourcePath";
    private readonly LaunchpadViewModel _viewModel;
    private readonly LauncherService _launcherService;

    public LaunchpadWindow(LaunchpadViewModel viewModel, LauncherService launcherService)
    {
        _viewModel = viewModel;
        _launcherService = launcherService;
        DataContext = viewModel;
        InitializeComponent();
    }

    public event Action<LaunchpadViewMode>? ViewModeRequested;

    public event Action? ImportStartMenuRequested;

    public event Action? ImportVuiRequested;

    public event Action? OpenSettingsRequested;

    public event Action<string>? CreateRegionRequested;

    public event Action<string, string>? RenameRegionRequested;

    public event Action<string>? DeleteRegionRequested;

    public event Action<string, string>? RenameItemRequested;

    public event Action<string>? RemoveItemRequested;

    public event Action<string, string, int>? MoveItemRequested;

    public void ShowLaunchpad()
    {
        Left = SystemParameters.VirtualScreenLeft;
        Top = SystemParameters.VirtualScreenTop;
        Width = SystemParameters.VirtualScreenWidth;
        Height = SystemParameters.VirtualScreenHeight;

        Opacity = 0;
        WindowTranslate.Y = 10;
        Show();
        Activate();
        FocusSearchBox();

        BeginAnimation(OpacityProperty, new DoubleAnimation(1, TimeSpan.FromMilliseconds(110)));
        WindowTranslate.BeginAnimation(System.Windows.Media.TranslateTransform.YProperty, new DoubleAnimation(0, TimeSpan.FromMilliseconds(130)));
    }

    public void HideLaunchpad()
    {
        var animation = new DoubleAnimation(0, TimeSpan.FromMilliseconds(70));
        animation.Completed += (_, _) => Hide();
        BeginAnimation(OpacityProperty, animation);
    }

    private void Window_Loaded(object sender, RoutedEventArgs e)
    {
        FocusSearchBox();
    }

    private void Window_PreviewKeyDown(object sender, System.Windows.Input.KeyEventArgs e)
    {
        if (e.Key == Key.Escape)
        {
            HideLaunchpad();
            e.Handled = true;
            return;
        }

        if (e.Key == Key.Enter)
        {
            Launch(_viewModel.SelectedItem);
            e.Handled = true;
        }
    }

    private void GearButton_Click(object sender, RoutedEventArgs e)
    {
        if (sender is System.Windows.Controls.Button button && button.ContextMenu is not null)
        {
            button.ContextMenu.PlacementTarget = button;
            button.ContextMenu.IsOpen = true;
        }
    }

    private void SetInlineRegions_Click(object sender, RoutedEventArgs e)
    {
        ViewModeRequested?.Invoke(LaunchpadViewMode.InlineRegions);
    }

    private void SetRegionTabs_Click(object sender, RoutedEventArgs e)
    {
        ViewModeRequested?.Invoke(LaunchpadViewMode.RegionTabs);
    }

    private void ImportStartMenu_Click(object sender, RoutedEventArgs e)
    {
        ImportStartMenuRequested?.Invoke();
    }

    private void ImportVui_Click(object sender, RoutedEventArgs e)
    {
        ImportVuiRequested?.Invoke();
    }

    private void OpenSettings_Click(object sender, RoutedEventArgs e)
    {
        OpenSettingsRequested?.Invoke();
    }

    private void RegionTab_Click(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { Tag: string regionId })
        {
            _viewModel.ActiveRegionId = regionId;
        }
    }

    private void CreateRegion_Click(object sender, RoutedEventArgs e)
    {
        var name = Prompt("Create region", "Region name:", "New Region");
        if (!string.IsNullOrWhiteSpace(name))
        {
            CreateRegionRequested?.Invoke(name);
        }
    }

    private void RenameRegion_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as MenuItem)?.CommandParameter is not LaunchpadRegionViewModel region)
        {
            return;
        }

        var name = Prompt("Rename region", "Region name:", region.Name);
        if (!string.IsNullOrWhiteSpace(name))
        {
            RenameRegionRequested?.Invoke(region.Id, name);
        }
    }

    private void DeleteRegion_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as MenuItem)?.CommandParameter is not LaunchpadRegionViewModel region)
        {
            return;
        }

        if (System.Windows.MessageBox.Show(
                $"Delete region '{region.Name}'? Apps will move to Uncategorized.",
                "LightLaunchpad",
                MessageBoxButton.YesNo,
                MessageBoxImage.Question) == MessageBoxResult.Yes)
        {
            DeleteRegionRequested?.Invoke(region.Id);
        }
    }

    private void LaunchItem_Click(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { DataContext: LaunchItemViewModel item })
        {
            Launch(item);
        }
    }

    private void LaunchMenuItem_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as MenuItem)?.CommandParameter is LaunchItemViewModel item)
        {
            Launch(item);
        }
    }

    private void RenameItem_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as MenuItem)?.CommandParameter is not LaunchItemViewModel item)
        {
            return;
        }

        var name = Prompt("Rename app", "Display name:", item.DisplayName);
        if (!string.IsNullOrWhiteSpace(name))
        {
            RenameItemRequested?.Invoke(item.SourcePath, name);
        }
    }

    private void RemoveItem_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as MenuItem)?.CommandParameter is not LaunchItemViewModel item)
        {
            return;
        }

        if (System.Windows.MessageBox.Show(
                $"Remove '{item.DisplayName}' from the launchpad? This does not uninstall the app.",
                "LightLaunchpad",
                MessageBoxButton.YesNo,
                MessageBoxImage.Question) == MessageBoxResult.Yes)
        {
            RemoveItemRequested?.Invoke(item.SourcePath);
        }
    }

    private void OpenItemLocation_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as MenuItem)?.CommandParameter is not LaunchItemViewModel item)
        {
            return;
        }

        var directory = Path.GetDirectoryName(item.SourcePath);
        if (!string.IsNullOrWhiteSpace(directory) && Directory.Exists(directory))
        {
            Process.Start(new ProcessStartInfo { FileName = directory, UseShellExecute = true });
        }
    }

    private void LaunchItem_PreviewMouseMove(object sender, System.Windows.Input.MouseEventArgs e)
    {
        if (e.LeftButton != MouseButtonState.Pressed ||
            sender is not FrameworkElement { DataContext: LaunchItemViewModel item })
        {
            return;
        }

        System.Windows.DragDrop.DoDragDrop((DependencyObject)sender, new System.Windows.DataObject(DragFormat, item.SourcePath), System.Windows.DragDropEffects.Move);
    }

    private void Region_Drop(object sender, System.Windows.DragEventArgs e)
    {
        if (!e.Data.GetDataPresent(DragFormat))
        {
            return;
        }

        var sourcePath = (string)e.Data.GetData(DragFormat);
        var regionId = ResolveRegionId(sender);
        if (!string.IsNullOrWhiteSpace(regionId))
        {
            MoveItemRequested?.Invoke(sourcePath, regionId, int.MaxValue);
        }
    }

    private void LaunchItem_Drop(object sender, System.Windows.DragEventArgs e)
    {
        if (!e.Data.GetDataPresent(DragFormat) ||
            sender is not FrameworkElement { DataContext: LaunchItemViewModel targetItem })
        {
            return;
        }

        var sourcePath = (string)e.Data.GetData(DragFormat);
        MoveItemRequested?.Invoke(sourcePath, targetItem.RegionId, targetItem.Order);
        e.Handled = true;
    }

    private static string ResolveRegionId(object sender)
    {
        return sender switch
        {
            FrameworkElement { DataContext: LaunchpadRegionViewModel region } => region.Id,
            FrameworkElement { Tag: string regionId } => regionId,
            _ => string.Empty
        };
    }

    private void Launch(LaunchItemViewModel? item)
    {
        if (item is null)
        {
            return;
        }

        _launcherService.Launch(item.Item);
        HideLaunchpad();
    }

    private void FocusSearchBox()
    {
        SearchBox.Focus();
        Keyboard.Focus(SearchBox);
        SearchBox.SelectAll();
    }

    private string? Prompt(string title, string label, string initialValue)
    {
        var dialog = new TextInputDialog(title, label, initialValue)
        {
            Owner = this
        };
        return dialog.ShowDialog() == true ? dialog.Value : null;
    }
}
