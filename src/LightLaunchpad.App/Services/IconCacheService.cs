using System.Globalization;
using System.IO;
using System.Security.Cryptography;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace LightLaunchpad.App.Services;

public sealed class IconCacheService
{
    private readonly string _cacheDir;
    private readonly int _iconSize;
    private readonly Dictionary<string, ImageSource?> _memoryCache = new(StringComparer.OrdinalIgnoreCase);
    private readonly object _cacheLock = new();

    public IconCacheService(string cacheDir, int iconSize)
    {
        _cacheDir = cacheDir;
        _iconSize = iconSize;
        Directory.CreateDirectory(_cacheDir);
    }

    public ImageSource? GetIcon(string sourcePath, string? targetPath, bool isShortcut)
    {
        var iconSource = ResolveSource(sourcePath, targetPath, isShortcut);
        var cacheKey = $"{iconSource}|{_iconSize}";

        lock (_cacheLock)
        {
            if (_memoryCache.TryGetValue(cacheKey, out var memCached))
            {
                return memCached;
            }
        }

        var fileName = HashPath($"{iconSource}|{_iconSize}|v2") + ".png";
        var cachedPath = Path.Combine(_cacheDir, fileName);

        ImageSource? icon;
        if (File.Exists(cachedPath))
        {
            icon = LoadPng(cachedPath);
        }
        else
        {
            icon = ExtractAndCache(iconSource, cachedPath);
        }

        lock (_cacheLock)
        {
            _memoryCache[cacheKey] = icon;
        }
        return icon;
    }

    public void EnsureCached(string sourcePath, string? targetPath, bool isShortcut)
    {
        var iconSource = ResolveSource(sourcePath, targetPath, isShortcut);
        var fileName = HashPath($"{iconSource}|{_iconSize}|v2") + ".png";
        var cachedPath = Path.Combine(_cacheDir, fileName);

        if (File.Exists(cachedPath))
        {
            return;
        }

        ExtractAndCache(iconSource, cachedPath);
    }

    public void Cleanup(IEnumerable<string> activeSourcePaths, IEnumerable<string?> activeTargetPaths, IEnumerable<bool> activeIsShortcuts)
    {
        var validFiles = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var (source, target, isLnk) in activeSourcePaths.Zip(activeTargetPaths, activeIsShortcuts))
        {
            var iconSource = ResolveSource(source, target, isLnk);
            validFiles.Add(HashPath($"{iconSource}|{_iconSize}|v2") + ".png");
        }

        foreach (var file in Directory.EnumerateFiles(_cacheDir, "*.png"))
        {
            if (!validFiles.Contains(Path.GetFileName(file)))
            {
                try { File.Delete(file); } catch { /* ignore */ }
            }
        }
    }

    public void DeleteIcon(string sourcePath, string? targetPath, bool isShortcut)
    {
        var iconSource = ResolveSource(sourcePath, targetPath, isShortcut);
        var cacheKey = $"{iconSource}|{_iconSize}";
        var cachedPath = Path.Combine(_cacheDir, HashPath($"{iconSource}|{_iconSize}|v2") + ".png");

        lock (_cacheLock)
        {
            _memoryCache.Remove(cacheKey);
        }

        try
        {
            if (File.Exists(cachedPath))
            {
                File.Delete(cachedPath);
            }
        }
        catch
        {
            // ignore cache deletion failures
        }
    }

    public void ClearMemoryCache()
    {
        lock (_cacheLock)
        {
            _memoryCache.Clear();
        }
    }

    private ImageSource? ExtractAndCache(string iconSource, string cachedPath)
    {
        var raw = ShellIconExtractor.ExtractRaw(iconSource);
        if (raw is null) return null;

        var icon = IconUpScaler.UpscaleIfNeeded(raw, _iconSize)
            ?? ShellIconExtractor.Extract(iconSource, _iconSize);
        if (icon is null) return null;

        SavePng(icon, cachedPath);
        return icon;
    }

    private static string ResolveSource(string sourcePath, string? targetPath, bool isShortcut)
    {
        if (isShortcut && !string.IsNullOrWhiteSpace(targetPath)
            && (File.Exists(targetPath) || Directory.Exists(targetPath)))
        {
            return targetPath;
        }
        return sourcePath;
    }

    private static string HashPath(string path)
    {
        var hash = SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(path.ToUpperInvariant()));
        return Convert.ToHexString(hash)[..16];
    }

    private static ImageSource? LoadPng(string path)
    {
        try
        {
            var bitmap = new BitmapImage();
            bitmap.BeginInit();
            bitmap.UriSource = new Uri(path, UriKind.Absolute);
            bitmap.CacheOption = BitmapCacheOption.OnLoad;
            bitmap.CreateOptions = BitmapCreateOptions.IgnoreImageCache;
            bitmap.EndInit();
            bitmap.Freeze();
            return bitmap;
        }
        catch
        {
            return null;
        }
    }

    private static void SavePng(ImageSource source, string path)
    {
        try
        {
            var encoder = new PngBitmapEncoder();
            encoder.Frames.Add(BitmapFrame.Create((BitmapSource)source));
            using var stream = File.Create(path);
            encoder.Save(stream);
        }
        catch
        {
            // ignore save failures
        }
    }
}
