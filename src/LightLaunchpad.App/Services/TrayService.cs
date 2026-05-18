using System.IO;
using Forms = System.Windows.Forms;

namespace LightLaunchpad.App.Services;

public sealed class TrayService : IDisposable
{
    private readonly Forms.NotifyIcon _notifyIcon;

    public TrayService(
        Action openLaunchpad,
        Action openFolder,
        Action refresh,
        Action openSettings,
        Action importVui,
        Action exit)
    {
        _notifyIcon = new Forms.NotifyIcon
        {
            Icon = LoadTrayIcon(),
            Text = "LightLaunchpad",
            Visible = true,
            ContextMenuStrip = BuildMenu(openLaunchpad, openFolder, refresh, openSettings, importVui, exit)
        };
        _notifyIcon.DoubleClick += (_, _) => openLaunchpad();
    }

    private static System.Drawing.Icon LoadTrayIcon()
    {
        var iconPath = Path.Combine(AppContext.BaseDirectory, "Assets", "Alice.ico");
        return File.Exists(iconPath)
            ? new System.Drawing.Icon(iconPath)
            : System.Drawing.SystemIcons.Application;
    }

    public void ShowMessage(string title, string message)
    {
        _notifyIcon.BalloonTipTitle = title;
        _notifyIcon.BalloonTipText = message;
        _notifyIcon.ShowBalloonTip(3000);
    }

    public void Dispose()
    {
        _notifyIcon.Visible = false;
        _notifyIcon.Dispose();
    }

    private static Forms.ContextMenuStrip BuildMenu(
        Action openLaunchpad,
        Action openFolder,
        Action refresh,
        Action openSettings,
        Action importVui,
        Action exit)
    {
        var menu = new Forms.ContextMenuStrip();
        menu.Items.Add("Open launchpad", null, (_, _) => openLaunchpad());
        menu.Items.Add("Open launchpad folder", null, (_, _) => openFolder());
        menu.Items.Add("Refresh shortcuts", null, (_, _) => refresh());
        menu.Items.Add("Import .vui files", null, (_, _) => importVui());
        menu.Items.Add("Settings", null, (_, _) => openSettings());
        menu.Items.Add(new Forms.ToolStripSeparator());
        menu.Items.Add("Exit", null, (_, _) => exit());
        return menu;
    }
}
