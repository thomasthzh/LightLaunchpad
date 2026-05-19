using System.Windows;

namespace LightLaunchpad.App;

public sealed class HotkeySinkWindow : Window
{
    public HotkeySinkWindow()
    {
        Width = 0;
        Height = 0;
        WindowStyle = WindowStyle.None;
        ResizeMode = ResizeMode.NoResize;
        ShowInTaskbar = false;
        ShowActivated = false;
        Visibility = Visibility.Hidden;
    }
}