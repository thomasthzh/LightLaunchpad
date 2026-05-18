using System.Diagnostics;
using System.Windows;
using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.App.Services;

public sealed class LauncherService
{
    public void Launch(LaunchItem item)
    {
        try
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = item.SourcePath,
                UseShellExecute = true
            });
        }
        catch (Exception ex)
        {
            System.Windows.MessageBox.Show(
                $"Could not launch {item.DisplayName}.\n\n{ex.Message}",
                "LightLaunchpad",
                MessageBoxButton.OK,
                MessageBoxImage.Warning);
        }
    }
}
