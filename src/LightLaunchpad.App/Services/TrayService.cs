using System.IO;
using LightLaunchpad.Core.Settings;
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
        Action exit,
        string language)
    {
        _notifyIcon = new Forms.NotifyIcon
        {
            Icon = LoadTrayIcon(),
            Text = "LightLaunchpad",
            Visible = true,
            ContextMenuStrip = BuildMenu(openLaunchpad, openFolder, refresh, openSettings, exit, language)
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
        Action exit,
        string language)
    {
        var menu = new Forms.ContextMenuStrip();
        menu.Items.Add(Text(language, "Open launchpad", "打开启动台"), null, (_, _) => openLaunchpad());
        menu.Items.Add(Text(language, "Open launchpad folder", "打开启动台文件夹"), null, (_, _) => openFolder());
        menu.Items.Add(Text(language, "Refresh shortcuts", "刷新快捷方式"), null, (_, _) => refresh());
        menu.Items.Add(Text(language, "Settings", "设置"), null, (_, _) => openSettings());
        menu.Items.Add(new Forms.ToolStripSeparator());
        menu.Items.Add(Text(language, "Exit", "退出"), null, (_, _) => exit());
        return menu;
    }

    private static string Text(string language, string english, string chinese)
    {
        return AppLanguages.IsChinese(language) ? chinese : english;
    }
}