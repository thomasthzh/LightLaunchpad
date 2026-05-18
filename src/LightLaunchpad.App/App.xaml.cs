using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Threading;
using System.Windows;
using LightLaunchpad.App.Services;
using LightLaunchpad.App.ViewModels;
using LightLaunchpad.Core.Settings;
using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.App;

public partial class App : System.Windows.Application
{
    private const string AppName = "LightLaunchpad";
    private Mutex? _singleInstanceMutex;
    private AppSettings _settings = null!;
    private SettingsService _settingsService = null!;
    private ShortcutRepository _repository = null!;
    private IconService _iconService = null!;
    private LaunchpadViewModel _viewModel = null!;
    private LauncherService _launcherService = null!;
    private LaunchpadWindow _launchpadWindow = null!;
    private HotkeyService _hotkeyService = null!;
    private TrayService _trayService = null!;
    private ShortcutWatcher _shortcutWatcher = null!;

    protected override void OnStartup(StartupEventArgs e)
    {
        _singleInstanceMutex = new Mutex(true, "LightLaunchpad.SingleInstance", out var createdNew);
        if (!createdNew)
        {
            System.Windows.MessageBox.Show("LightLaunchpad is already running.", AppName, MessageBoxButton.OK, MessageBoxImage.Information);
            Shutdown();
            return;
        }

        base.OnStartup(e);
        LoadSettings();
        BuildServices();
        RefreshItems();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _shortcutWatcher?.Dispose();
        _hotkeyService?.Dispose();
        _trayService?.Dispose();
        _singleInstanceMutex?.Dispose();
        base.OnExit(e);
    }

    private void LoadSettings()
    {
        var appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        var settingsPath = Path.Combine(appData, AppName, "settings.json");
        var userProfile = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        _settingsService = new SettingsService(settingsPath, userProfile);
        _settings = _settingsService.Load();
    }

    private void BuildServices()
    {
        _repository = new ShortcutRepository(_settings.LaunchpadFolder);
        _iconService = new IconService();
        _launcherService = new LauncherService();
        _viewModel = new LaunchpadViewModel(_settings, item => _iconService.GetIcon(item.SourcePath));
        _launchpadWindow = new LaunchpadWindow(_viewModel, _launcherService);
        _hotkeyService = new HotkeyService(_launchpadWindow);
        _hotkeyService.Pressed += (_, _) => Dispatcher.Invoke(ToggleLaunchpad);
        _trayService = new TrayService(
            ToggleLaunchpad,
            OpenLaunchpadFolder,
            RefreshItems,
            OpenSettings,
            ImportVuiFiles,
            ExitApplication);

        RegisterHotkey();
        StartWatcher();
        StartupRegistrationService.SetEnabled(_settings.StartWithWindows);
    }

    private void RegisterHotkey()
    {
        try
        {
            if (!_hotkeyService.Register(_settings.Hotkey))
            {
                _trayService.ShowMessage(AppName, $"Could not register hotkey {_settings.Hotkey}. You can change it in Settings.");
            }
        }
        catch (Exception ex)
        {
            _trayService.ShowMessage(AppName, $"Invalid hotkey {_settings.Hotkey}: {ex.Message}");
        }
    }

    private void StartWatcher()
    {
        _shortcutWatcher?.Dispose();
        _shortcutWatcher = new ShortcutWatcher(_settings.LaunchpadFolder);
        _shortcutWatcher.Changed += (_, _) => Dispatcher.BeginInvoke(RefreshItems);
        _shortcutWatcher.Start();
    }

    private void ToggleLaunchpad()
    {
        if (_launchpadWindow.IsVisible)
        {
            _launchpadWindow.HideLaunchpad();
            return;
        }

        RefreshItems();
        _launchpadWindow.ShowLaunchpad();
    }

    private void RefreshItems()
    {
        if (!Dispatcher.CheckAccess())
        {
            Dispatcher.BeginInvoke(RefreshItems);
            return;
        }

        try
        {
            _viewModel.LoadItems(_repository.LoadItems());
        }
        catch (Exception ex)
        {
            _trayService.ShowMessage(AppName, $"Could not refresh shortcuts: {ex.Message}");
        }
    }

    private void OpenLaunchpadFolder()
    {
        Directory.CreateDirectory(_settings.LaunchpadFolder);
        Process.Start(new ProcessStartInfo
        {
            FileName = _settings.LaunchpadFolder,
            UseShellExecute = true
        });
    }

    private void OpenSettings()
    {
        var window = new SettingsWindow(_settings)
        {
            Owner = _launchpadWindow.IsVisible ? _launchpadWindow : null
        };

        if (window.ShowDialog() != true || window.UpdatedSettings is null)
        {
            return;
        }

        ApplySettings(window.UpdatedSettings);
    }

    private void ApplySettings(AppSettings settings)
    {
        _settings = settings;
        _settingsService.Save(_settings);
        _repository = new ShortcutRepository(_settings.LaunchpadFolder);
        _viewModel.UpdateSettings(_settings);
        StartWatcher();
        RegisterHotkey();
        StartupRegistrationService.SetEnabled(_settings.StartWithWindows);
        RefreshItems();
    }

    private void ImportVuiFiles()
    {
        try
        {
            var vuiFiles = Directory.EnumerateFiles(Environment.CurrentDirectory, "*.vui", SearchOption.TopDirectoryOnly).ToList();
            if (vuiFiles.Count == 0)
            {
                _trayService.ShowMessage(AppName, "No .vui files were found in the current directory.");
                return;
            }

            var importer = new VuiImportService(_settings.LaunchpadFolder, new ShellShortcutService());
            var summary = importer.Import(vuiFiles);
            RefreshItems();
            _trayService.ShowMessage(
                AppName,
                $"Imported {summary.ImportedCount} item(s). Skipped {summary.MissingCount} missing and {summary.UnsupportedCount} unsupported path(s).");
        }
        catch (Exception ex)
        {
            _trayService.ShowMessage(AppName, $"Could not import .vui files: {ex.Message}");
        }
    }

    private void ExitApplication()
    {
        Shutdown();
    }

    private static class StartupRegistrationService
    {
        private const string RunKeyPath = @"Software\Microsoft\Windows\CurrentVersion\Run";

        public static void SetEnabled(bool enabled)
        {
            using var key = Microsoft.Win32.Registry.CurrentUser.OpenSubKey(RunKeyPath, writable: true)
                ?? Microsoft.Win32.Registry.CurrentUser.CreateSubKey(RunKeyPath);
            if (key is null)
            {
                return;
            }

            if (!enabled)
            {
                key.DeleteValue(AppName, throwOnMissingValue: false);
                return;
            }

            var executablePath = Environment.ProcessPath ?? Assembly.GetExecutingAssembly().Location;
            key.SetValue(AppName, $"\"{executablePath}\"");
        }
    }
}
