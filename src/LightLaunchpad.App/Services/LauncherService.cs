using System.Diagnostics;
using System.IO;
using System.Windows;
using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.App.Services;

public sealed class LauncherService
{
    public void Launch(LaunchItem item)
    {
        try
        {
            switch (item.Kind)
            {
                case LaunchItemKind.Executable:
                    LaunchExecutable(item);
                    break;
                case LaunchItemKind.Shortcut:
                    LaunchShortcut(item);
                    break;
                case LaunchItemKind.Url:
                    LaunchUrl(item);
                    break;
            }
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

    private static void LaunchExecutable(LaunchItem item)
    {
        var dir = Path.GetDirectoryName(item.SourcePath);
        Process.Start(new ProcessStartInfo
        {
            FileName = item.SourcePath,
            WorkingDirectory = dir ?? string.Empty,
            UseShellExecute = true
        });
    }

    private static void LaunchShortcut(LaunchItem item)
    {
        var target = item.TargetPath;

        if (!string.IsNullOrWhiteSpace(target) && File.Exists(target))
        {
            var dir = Path.GetDirectoryName(target);
            Process.Start(new ProcessStartInfo
            {
                FileName = target,
                WorkingDirectory = dir ?? string.Empty,
                UseShellExecute = true
            });
            return;
        }

        if (!string.IsNullOrWhiteSpace(target))
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = target,
                UseShellExecute = true
            });
            return;
        }

        Process.Start(new ProcessStartInfo
        {
            FileName = item.SourcePath,
            UseShellExecute = true
        });
    }

    private static void LaunchUrl(LaunchItem item)
    {
        var url = item.TargetPath;

        if (!string.IsNullOrWhiteSpace(url))
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = url,
                UseShellExecute = true
            });
            return;
        }

        Process.Start(new ProcessStartInfo
        {
            FileName = item.SourcePath,
            UseShellExecute = true
        });
    }
}
