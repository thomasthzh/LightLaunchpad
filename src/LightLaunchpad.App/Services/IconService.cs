using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace LightLaunchpad.App.Services;

public sealed class IconService
{
    private const uint ShgfiIcon = 0x000000100;
    private const uint ShgfiLargeIcon = 0x000000000;
    private readonly Dictionary<string, ImageSource?> _cache = new(StringComparer.OrdinalIgnoreCase);

    public ImageSource? GetIcon(string path)
    {
        if (_cache.TryGetValue(path, out var cached))
        {
            return cached;
        }

        var icon = ExtractIcon(path);
        _cache[path] = icon;
        return icon;
    }

    private static ImageSource? ExtractIcon(string path)
    {
        var info = new ShFileInfo();
        var result = SHGetFileInfo(path, 0, ref info, (uint)Marshal.SizeOf<ShFileInfo>(), ShgfiIcon | ShgfiLargeIcon);
        if (result == IntPtr.Zero || info.IconHandle == IntPtr.Zero)
        {
            return null;
        }

        try
        {
            var image = Imaging.CreateBitmapSourceFromHIcon(
                info.IconHandle,
                Int32Rect.Empty,
                BitmapSizeOptions.FromEmptyOptions());
            image.Freeze();
            return image;
        }
        finally
        {
            DestroyIcon(info.IconHandle);
        }
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr SHGetFileInfo(
        string path,
        uint fileAttributes,
        ref ShFileInfo fileInfo,
        uint fileInfoSize,
        uint flags);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool DestroyIcon(IntPtr icon);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct ShFileInfo
    {
        public IntPtr IconHandle;
        public int IconIndex;
        public uint Attributes;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
        public string DisplayName;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 80)]
        public string TypeName;
    }
}
