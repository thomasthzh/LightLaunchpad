using System.Globalization;
using System.IO;
using System.Security.Cryptography;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using LightLaunchpad.Core.Shortcuts;

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

    public ImageSource? GetIcon(string sourcePath, string? targetPath, LaunchItemKind kind)
    {
        var iconSources = IconSourceResolver.ResolveIconSources(sourcePath, targetPath, kind);
        var cacheKey = $"{string.Join("|", iconSources)}|{_iconSize}";

        lock (_cacheLock)
        {
            if (_memoryCache.TryGetValue(cacheKey, out var memCached))
            {
                return memCached;
            }
        }

        var fileName = HashPath($"{cacheKey}|v3") + ".png";
        var cachedPath = Path.Combine(_cacheDir, fileName);

        ImageSource? icon;
        if (File.Exists(cachedPath))
        {
            icon = LoadPng(cachedPath);
        }
        else
        {
            icon = ExtractAndCache(iconSources, cachedPath);
        }

        lock (_cacheLock)
        {
            _memoryCache[cacheKey] = icon;
        }
        return icon;
    }

    public void EnsureCached(string sourcePath, string? targetPath, LaunchItemKind kind)
    {
        var iconSources = IconSourceResolver.ResolveIconSources(sourcePath, targetPath, kind);
        var cacheKey = $"{string.Join("|", iconSources)}|{_iconSize}";
        var fileName = HashPath($"{cacheKey}|v3") + ".png";
        var cachedPath = Path.Combine(_cacheDir, fileName);

        if (File.Exists(cachedPath))
        {
            return;
        }

        ExtractAndCache(iconSources, cachedPath);
    }

    public void Cleanup(IEnumerable<string> activeSourcePaths, IEnumerable<string?> activeTargetPaths, IEnumerable<LaunchItemKind> activeKinds)
    {
        var validFiles = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var (source, target, kind) in activeSourcePaths.Zip(activeTargetPaths, activeKinds))
        {
            var iconSources = IconSourceResolver.ResolveIconSources(source, target, kind);
            var cacheKey = $"{string.Join("|", iconSources)}|{_iconSize}";
            validFiles.Add(HashPath($"{cacheKey}|v3") + ".png");
        }

        foreach (var file in Directory.EnumerateFiles(_cacheDir, "*.png"))
        {
            if (!validFiles.Contains(Path.GetFileName(file)))
            {
                try { File.Delete(file); } catch { /* ignore */ }
            }
        }
    }

    public void DeleteIcon(string sourcePath, string? targetPath, LaunchItemKind kind)
    {
        var iconSources = IconSourceResolver.ResolveIconSources(sourcePath, targetPath, kind);
        var cacheKey = $"{string.Join("|", iconSources)}|{_iconSize}";
        var cachedPath = Path.Combine(_cacheDir, HashPath($"{cacheKey}|v3") + ".png");

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

    private ImageSource? ExtractAndCache(IReadOnlyList<string> iconSources, string cachedPath)
    {
        foreach (var iconSource in iconSources)
        {
            var raw = ShellIconExtractor.ExtractRaw(iconSource);
            if (raw is null) continue;

            var icon = IconUpScaler.UpscaleIfNeeded(raw, _iconSize)
                ?? ShellIconExtractor.Extract(iconSource, _iconSize);
            if (icon is null) continue;

            SavePng(icon, cachedPath);
            return icon;
        }

        return null;
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
