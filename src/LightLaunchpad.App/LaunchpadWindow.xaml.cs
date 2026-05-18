using System.Windows;
using System.Windows.Input;
using System.Windows.Media.Animation;
using LightLaunchpad.App.Services;
using LightLaunchpad.App.ViewModels;

namespace LightLaunchpad.App;

public partial class LaunchpadWindow : Window
{
    private readonly LaunchpadViewModel _viewModel;
    private readonly LauncherService _launcherService;

    public LaunchpadWindow(LaunchpadViewModel viewModel, LauncherService launcherService)
    {
        _viewModel = viewModel;
        _launcherService = launcherService;
        DataContext = viewModel;
        InitializeComponent();
    }

    public void ShowLaunchpad()
    {
        Left = SystemParameters.VirtualScreenLeft;
        Top = SystemParameters.VirtualScreenTop;
        Width = SystemParameters.VirtualScreenWidth;
        Height = SystemParameters.VirtualScreenHeight;

        Opacity = 0;
        WindowScale.ScaleX = 0.98;
        WindowScale.ScaleY = 0.98;
        Show();
        Activate();
        FocusSearchBox();

        BeginAnimation(OpacityProperty, new DoubleAnimation(1, TimeSpan.FromMilliseconds(150)));
        WindowScale.BeginAnimation(System.Windows.Media.ScaleTransform.ScaleXProperty, new DoubleAnimation(1, TimeSpan.FromMilliseconds(150)));
        WindowScale.BeginAnimation(System.Windows.Media.ScaleTransform.ScaleYProperty, new DoubleAnimation(1, TimeSpan.FromMilliseconds(150)));
    }

    public void HideLaunchpad()
    {
        var animation = new DoubleAnimation(0, TimeSpan.FromMilliseconds(90));
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

    private void LaunchItem_Click(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { DataContext: LaunchItemViewModel item })
        {
            Launch(item);
        }
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
}
