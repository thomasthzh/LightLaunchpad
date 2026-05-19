using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace LightLaunchpad.App.Services;

public static class ShellIconExtractor
{
    private const uint ShgfiSysIconIndex = 0x000004000;
    private const int ShilJumbo = 0x4;
    private const int IldTransparent = 0x1;
    private static readonly Guid ImageListId = new("46EB5926-582E-4017-9FDF-E8998DAA0950");

    public static ImageSource? Extract(string path, int targetSize)
    {
        var raw = ExtractJumboIcon(path) ?? ExtractStandardIcon(path);
        if (raw is null)
        {
            return null;
        }

        if (raw is BitmapSource bmp && (bmp.PixelWidth != targetSize || bmp.PixelHeight != targetSize))
        {
            return Resize(bmp, targetSize);
        }

        return raw;
    }

    public static BitmapSource? ExtractRaw(string path)
    {
        var icon = ExtractJumboIcon(path) ?? ExtractStandardIcon(path);
        return icon is BitmapSource bmp ? bmp : null;
    }

    private static ImageSource? ExtractStandardIcon(string path)
    {
        var info = new ShFileInfo();
        const uint flags = 0x000000100 | 0x000000000; // SHGFI_ICON | SHGFI_LARGEICON
        var result = SHGetFileInfo(path, 0, ref info, (uint)Marshal.SizeOf<ShFileInfo>(), flags);
        if (result == IntPtr.Zero || info.IconHandle == IntPtr.Zero)
        {
            return null;
        }

        try
        {
            var image = Imaging.CreateBitmapSourceFromHIcon(
                info.IconHandle, Int32Rect.Empty, BitmapSizeOptions.FromEmptyOptions());
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

        var iid = ImageListId;
        var hr = SHGetImageList(ShilJumbo, ref iid, out var imageList);
        if (hr != 0 || imageList is null)
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
                    iconHandle, Int32Rect.Empty, BitmapSizeOptions.FromEmptyOptions());
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

    private static BitmapSource Resize(BitmapSource source, int size)
    {
        var scaled = new TransformedBitmap(source,
            new ScaleTransform(
                (double)size / source.PixelWidth,
                (double)size / source.PixelHeight));
        var result = new RenderTargetBitmap(size, size, 96, 96, PixelFormats.Pbgra32);
        var visual = new DrawingVisual();
        using (var dc = visual.RenderOpen())
        {
            dc.DrawImage(scaled, new Rect(0, 0, size, size));
        }
        result.Render(visual);
        result.Freeze();
        return result;
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr SHGetFileInfo(
        string path, uint fileAttributes,
        ref ShFileInfo fileInfo, uint fileInfoSize, uint flags);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool DestroyIcon(IntPtr icon);

    [DllImport("shell32.dll", EntryPoint = "#727")]
    private static extern int SHGetImageList(int imageList, ref Guid riid,
        [MarshalAs(UnmanagedType.Interface)] out IImageList? ppv);

    [ComImport]
    [Guid("46EB5926-582E-4017-9FDF-E8998DAA0950")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IImageList
    {
        [PreserveSig] int Add(IntPtr hbmImage, IntPtr hbmMask, ref int pi);
        [PreserveSig] int ReplaceIcon(int i, IntPtr hicon, ref int pi);
        [PreserveSig] int SetOverlayImage(int iImage, int iOverlay);
        [PreserveSig] int Replace(int i, IntPtr hbmImage, IntPtr hbmMask);
        [PreserveSig] int AddMasked(IntPtr hbmImage, int crMask, ref int pi);
        [PreserveSig] int Draw(IntPtr pimldp);
        [PreserveSig] int Remove(int i);
        [PreserveSig] int GetIcon(int i, int flags, out IntPtr picon);
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
