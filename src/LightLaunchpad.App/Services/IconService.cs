using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace LightLaunchpad.App.Services;

public sealed class IconService
{
    private const uint ShgfiIcon = 0x000000100;
    private const uint ShgfiSysIconIndex = 0x000004000;
    private const uint ShgfiLargeIcon = 0x000000000;
    private const int ShilJumbo = 0x4;
    private const int IldTransparent = 0x1;
    private static readonly Guid ImageListId = new("46EB5926-582E-4017-9FDF-E8998DAA0950");
    private readonly Dictionary<string, ImageSource?> _cache = new(StringComparer.OrdinalIgnoreCase);
    private readonly bool _useJumboIcons;

    public IconService(string iconQuality)
    {
        _useJumboIcons = string.Equals(iconQuality, "High", StringComparison.OrdinalIgnoreCase);
    }

    public ImageSource? GetIcon(string path)
    {
        if (_cache.TryGetValue(path, out var cached))
        {
            return cached;
        }

        var icon = ExtractIcon(path, _useJumboIcons);
        _cache[path] = icon;
        return icon;
    }

    private static ImageSource? ExtractIcon(string path, bool useJumboIcons)
    {
        if (useJumboIcons)
        {
            var jumbo = ExtractJumboIcon(path);
            if (jumbo is not null)
            {
                return jumbo;
            }
        }

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

    private static ImageSource? ExtractJumboIcon(string path)
    {
        var info = new ShFileInfo();
        var result = SHGetFileInfo(path, 0, ref info, (uint)Marshal.SizeOf<ShFileInfo>(), ShgfiSysIconIndex);
        if (result == IntPtr.Zero)
        {
            return null;
        }

        var imageListId = ImageListId;
        var imageListResult = SHGetImageList(ShilJumbo, ref imageListId, out var imageList);
        if (imageListResult != 0 || imageList is null)
        {
            return null;
        }

        try
        {
            var iconResult = imageList.GetIcon(info.IconIndex, IldTransparent, out var iconHandle);
            if (iconResult != 0 || iconHandle == IntPtr.Zero)
            {
                return null;
            }

            try
            {
                var image = Imaging.CreateBitmapSourceFromHIcon(
                    iconHandle,
                    Int32Rect.Empty,
                    BitmapSizeOptions.FromEmptyOptions());
                image.Freeze();
                return image;
            }
            finally
            {
                DestroyIcon(iconHandle);
            }
        }
        finally
        {
            Marshal.ReleaseComObject(imageList);
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

    [DllImport("shell32.dll", EntryPoint = "#727")]
    private static extern int SHGetImageList(int imageList, ref Guid riid, [MarshalAs(UnmanagedType.Interface)] out IImageList? ppv);

    [ComImport]
    [Guid("46EB5926-582E-4017-9FDF-E8998DAA0950")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IImageList
    {
        [PreserveSig]
        int Add(IntPtr hbmImage, IntPtr hbmMask, ref int pi);

        [PreserveSig]
        int ReplaceIcon(int i, IntPtr hicon, ref int pi);

        [PreserveSig]
        int SetOverlayImage(int iImage, int iOverlay);

        [PreserveSig]
        int Replace(int i, IntPtr hbmImage, IntPtr hbmMask);

        [PreserveSig]
        int AddMasked(IntPtr hbmImage, int crMask, ref int pi);

        [PreserveSig]
        int Draw(IntPtr pimldp);

        [PreserveSig]
        int Remove(int i);

        [PreserveSig]
        int GetIcon(int i, int flags, out IntPtr picon);
    }

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
