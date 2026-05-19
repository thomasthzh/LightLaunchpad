using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace LightLaunchpad.App.Services;

public static class IconUpScaler
{
    private const int MinSizeThreshold = 48;
    private const double MinVisibleRatio = 0.58;
    private const byte AlphaThreshold = 8;

    public static BitmapSource? UpscaleIfNeeded(BitmapSource source, int targetSize)
    {
        var normalized = EnsurePbgra32(source);
        var visibleBounds = FindVisibleBounds(normalized);
        if (visibleBounds is null)
        {
            return HighQualityScale(normalized, targetSize);
        }

        var visibleRatio = Math.Max(
            (double)visibleBounds.Value.Width / normalized.PixelWidth,
            (double)visibleBounds.Value.Height / normalized.PixelHeight);
        var canvasIsSmall = normalized.PixelWidth < MinSizeThreshold || normalized.PixelHeight < MinSizeThreshold;
        if (!canvasIsSmall && visibleRatio >= MinVisibleRatio)
        {
            return null;
        }

        var cropped = new CroppedBitmap(normalized, ExpandBounds(visibleBounds.Value, normalized.PixelWidth, normalized.PixelHeight));
        cropped.Freeze();
        return HighQualityScale(cropped, targetSize);
    }

    private static BitmapSource EnsurePbgra32(BitmapSource source)
    {
        if (source.Format == PixelFormats.Pbgra32)
        {
            return source;
        }

        var converted = new FormatConvertedBitmap(source, PixelFormats.Pbgra32, null, 0);
        converted.Freeze();
        return converted;
    }

    private static Int32Rect? FindVisibleBounds(BitmapSource source)
    {
        var width = source.PixelWidth;
        var height = source.PixelHeight;
        var stride = width * 4;
        var pixels = new byte[stride * height];
        source.CopyPixels(pixels, stride, 0);

        var minX = width;
        var minY = height;
        var maxX = -1;
        var maxY = -1;

        for (var y = 0; y < height; y++)
        {
            var row = y * stride;
            for (var x = 0; x < width; x++)
            {
                if (pixels[row + (x * 4) + 3] <= AlphaThreshold)
                {
                    continue;
                }

                minX = Math.Min(minX, x);
                minY = Math.Min(minY, y);
                maxX = Math.Max(maxX, x);
                maxY = Math.Max(maxY, y);
            }
        }

        return maxX < minX || maxY < minY
            ? null
            : new Int32Rect(minX, minY, maxX - minX + 1, maxY - minY + 1);
    }

    private static Int32Rect ExpandBounds(Int32Rect bounds, int imageWidth, int imageHeight)
    {
        var padding = Math.Max(1, (int)Math.Ceiling(Math.Max(bounds.Width, bounds.Height) * 0.08));
        var x = Math.Max(0, bounds.X - padding);
        var y = Math.Max(0, bounds.Y - padding);
        var right = Math.Min(imageWidth, bounds.X + bounds.Width + padding);
        var bottom = Math.Min(imageHeight, bounds.Y + bounds.Height + padding);
        return new Int32Rect(x, y, right - x, bottom - y);
    }

    private static BitmapSource HighQualityScale(BitmapSource source, int targetSize)
    {
        var result = new RenderTargetBitmap(targetSize, targetSize, 96, 96, PixelFormats.Pbgra32);
        var visual = new DrawingVisual();
        RenderOptions.SetBitmapScalingMode(visual, BitmapScalingMode.HighQuality);
        using (var dc = visual.RenderOpen())
        {
            var scale = Math.Min((double)targetSize / source.PixelWidth, (double)targetSize / source.PixelHeight) * 0.94;
            var width = source.PixelWidth * scale;
            var height = source.PixelHeight * scale;
            dc.DrawImage(source, new Rect((targetSize - width) / 2, (targetSize - height) / 2, width, height));
        }
        result.Render(visual);
        result.Freeze();
        return result;
    }
}
