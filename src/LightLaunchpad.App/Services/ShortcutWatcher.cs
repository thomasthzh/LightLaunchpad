using System.IO;

namespace LightLaunchpad.App.Services;

public sealed class ShortcutWatcher : IDisposable
{
    private readonly FileSystemWatcher _watcher;

    public ShortcutWatcher(string folder)
    {
        Directory.CreateDirectory(folder);
        _watcher = new FileSystemWatcher(folder)
        {
            IncludeSubdirectories = false,
            NotifyFilter = NotifyFilters.FileName | NotifyFilters.LastWrite
        };
        _watcher.Created += OnChanged;
        _watcher.Deleted += OnChanged;
        _watcher.Renamed += OnRenamed;
        _watcher.Changed += OnChanged;
    }

    public event EventHandler? Changed;

    public void Start()
    {
        _watcher.EnableRaisingEvents = true;
    }

    public void Dispose()
    {
        _watcher.Dispose();
    }

    private void OnChanged(object sender, FileSystemEventArgs e)
    {
        Changed?.Invoke(this, EventArgs.Empty);
    }

    private void OnRenamed(object sender, RenamedEventArgs e)
    {
        Changed?.Invoke(this, EventArgs.Empty);
    }
}
