using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Threading;
using System.Windows;
using LightLaunchpad.App.Services;
using LightLaunchpad.App.ViewModels;
using LightLaunchpad.Core.Import;
using LightLaunchpad.Core.Layout;
using LightLaunchpad.Core.Settings;
using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.App;

public partial class App : System.Windows.Application
{
    private const string AppName = "LightLaunchpad";
    private Mutex? _singleInstanceMutex;
    private AppSettings _settings = null!;
    private SettingsService _settingsService = null!;
    private LayoutService _layoutService = null!;
    private LaunchpadLayout _layout = null!;
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
        _layoutService = new LayoutService(Path.Combine(appData, AppName, "layout.json"));
        _settings = _settingsService.Load();
        _layout = _layoutService.SetViewMode(
            _layoutService.Load(),
            Enum.TryParse<LaunchpadViewMode>(_settings.ViewMode, out var mode) ? mode : LaunchpadViewMode.InlineRegions);
    }

    private void BuildServices()
    {
        _repository = new ShortcutRepository(_settings.LaunchpadFolder);
        _iconService = new IconService();
        _launcherService = new LauncherService();
        _viewModel = new LaunchpadViewModel(_settings, item => _iconService.GetIcon(item.SourcePath));
        _launchpadWindow = new LaunchpadWindow(_viewModel, _launcherService);
        _launchpadWindow.ViewModeRequested += ApplyViewMode;
        _launchpadWindow.ImportStartMenuRequested += ImportStartMenuApps;
        _launchpadWindow.ImportVuiRequested += ImportVuiFiles;
        _launchpadWindow.OpenSettingsRequested += OpenSettings;
        _launchpadWindow.CreateRegionRequested += CreateRegion;
        _launchpadWindow.RenameRegionRequested += RenameRegion;
        _launchpadWindow.DeleteRegionRequested += DeleteRegion;
        _launchpadWindow.RenameItemRequested += RenameItem;
        _launchpadWindow.RemoveItemRequested += RemoveItem;
        _launchpadWindow.MoveItemRequested += MoveItem;
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
            var items = _repository.LoadItems();
            _layout = _layoutService.MergeItems(_layout, items);
            _layoutService.Save(_layout);
            _viewModel.LoadItems(_layout, items);
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
        _layout = _layoutService.SetViewMode(
            _layout,
            Enum.TryParse<LaunchpadViewMode>(_settings.ViewMode, out var mode) ? mode : _layout.ViewMode);
        _layoutService.Save(_layout);
        StartWatcher();
        RegisterHotkey();
        StartupRegistrationService.SetEnabled(_settings.StartWithWindows);
        RefreshItems();
    }

    private void ApplyViewMode(LaunchpadViewMode viewMode)
    {
        _layout = _layoutService.SetViewMode(_layout, viewMode);
        _layoutService.Save(_layout);
        _settings = _settings with { ViewMode = viewMode.ToString() };
        _settingsService.Save(_settings);
        _viewModel.UpdateSettings(_settings);
        RefreshItems();
    }

    private void CreateRegion(string name)
    {
        _layout = _layoutService.CreateRegion(_layout, name);
        _layoutService.Save(_layout);
        RefreshItems();
    }

    private void RenameRegion(string regionId, string name)
    {
        _layout = _layoutService.RenameRegion(_layout, regionId, name);
        _layoutService.Save(_layout);
        RefreshItems();
    }

    private void DeleteRegion(string regionId)
    {
        _layout = _layoutService.DeleteRegion(_layout, regionId);
        _layoutService.Save(_layout);
        RefreshItems();
    }

    private void RenameItem(string sourcePath, string name)
    {
        _layout = _layoutService.RenameItem(_layout, sourcePath, name);
        _layoutService.Save(_layout);
        RefreshItems();
    }

    private void RemoveItem(string sourcePath)
    {
        _layout = _layoutService.RemoveItem(_layout, sourcePath);
        _layoutService.Save(_layout);

        try
        {
            if (File.Exists(sourcePath))
            {
                File.Delete(sourcePath);
            }
        }
        catch (Exception ex)
        {
            _trayService.ShowMessage(AppName, $"Removed from layout but could not delete shortcut: {ex.Message}");
        }

        RefreshItems();
    }

    private void MoveItem(string sourcePath, string regionId, int order)
    {
        _layout = _layoutService.MoveItem(_layout, sourcePath, regionId, order);
        _layoutService.Save(_layout);
        RefreshItems();
    }

    private void ImportVuiFiles()
    {
        try
        {
            var vuiFiles = FindVuiFiles();
            if (vuiFiles.Count == 0)
            {
                _trayService.ShowMessage(AppName, "No .vui files were found near the app or current directory.");
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

    private void ImportStartMenuApps()
    {
        try
        {
            var roots = new[]
            {
                Environment.GetFolderPath(Environment.SpecialFolder.StartMenu),
                Environment.GetFolderPath(Environment.SpecialFolder.CommonStartMenu)
            }.Where(path => !string.IsNullOrWhiteSpace(path) && Directory.Exists(path));

            var candidates = StartMenuImporter.Discover(roots);
            var importer = new StartMenuImportService(_settings.LaunchpadFolder);
            var summary = importer.Import(candidates);
            EnsureNeeViewShortcut(summary);
            _layout = _layoutService.MergeItems(_layout, _repository.LoadItems());
            ApplyRegionHints(summary.ImportedItems);
            RefreshItems();
            _trayService.ShowMessage(AppName, $"Imported {summary.ImportedItems.Count} Start Menu item(s).");
        }
        catch (Exception ex)
        {
            _trayService.ShowMessage(AppName, $"Could not import Start Menu apps: {ex.Message}");
        }
    }

    private void EnsureNeeViewShortcut(StartMenuImportSummary summary)
    {
        if (summary.ImportedItems.Any(item => Path.GetFileNameWithoutExtension(item.SourcePath).Contains("NeeView", StringComparison.OrdinalIgnoreCase)))
        {
            return;
        }

        var neeViewPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            @"Microsoft\WindowsApps\NeeView.exe");
        if (!File.Exists(neeViewPath))
        {
            return;
        }

        var shortcutPath = Path.Combine(_settings.LaunchpadFolder, "NeeView.lnk");
        var index = 2;
        while (File.Exists(shortcutPath))
        {
            shortcutPath = Path.Combine(_settings.LaunchpadFolder, $"NeeView ({index}).lnk");
            index++;
        }

        new ShellShortcutService().CreateShortcut(shortcutPath, neeViewPath);
        summary.ImportedItems.Add(new ImportedStartMenuItem(shortcutPath, "Apps"));
    }

    private void ApplyRegionHints(IEnumerable<ImportedStartMenuItem> importedItems)
    {
        foreach (var item in importedItems)
        {
            var regionName = string.IsNullOrWhiteSpace(item.RegionHint) ? "Uncategorized" : item.RegionHint;
            var region = _layout.Regions.FirstOrDefault(region => string.Equals(region.Name, regionName, StringComparison.OrdinalIgnoreCase));
            if (region is null)
            {
                _layout = _layoutService.CreateRegion(_layout, regionName);
                region = _layout.Regions.FirstOrDefault(existing => string.Equals(existing.Name, regionName, StringComparison.OrdinalIgnoreCase));
            }

            if (region is not null)
            {
                _layout = _layoutService.MoveItem(_layout, item.SourcePath, region.Id, int.MaxValue);
            }
        }

        _layoutService.Save(_layout);
    }

    private static List<string> FindVuiFiles()
    {
        var roots = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        AddDirectoryAndParents(Environment.CurrentDirectory, roots);
        AddDirectoryAndParents(AppContext.BaseDirectory, roots);

        return roots
            .Where(Directory.Exists)
            .SelectMany(root => Directory.EnumerateFiles(root, "*.vui", SearchOption.TopDirectoryOnly))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    private static void AddDirectoryAndParents(string directory, ISet<string> roots)
    {
        var current = new DirectoryInfo(directory);
        for (var depth = 0; current is not null && depth < 8; depth++)
        {
            roots.Add(current.FullName);
            current = current.Parent;
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

            var assemblyPath = Assembly.GetEntryAssembly()?.Location ?? Assembly.GetExecutingAssembly().Location;
            var appHostPath = Path.ChangeExtension(assemblyPath, ".exe");
            var executablePath = File.Exists(appHostPath)
                ? appHostPath
                : Environment.ProcessPath ?? assemblyPath;
            key.SetValue(AppName, $"\"{executablePath}\"");
        }
    }
}
