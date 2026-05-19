using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using LightLaunchpad.Core.Layout;
using WpfHorizontalAlignment = System.Windows.HorizontalAlignment;
using WpfImage = System.Windows.Controls.Image;
using WpfBrushes = System.Windows.Media.Brushes;
using WpfPoint = System.Windows.Point;
using WpfButton = System.Windows.Controls.Button;

namespace LightLaunchpad.App.Services;

/// <summary>
/// Drag service operating entirely in window-relative DIP coordinates.
/// For a fullscreen overlay window at virtual screen origin (0,0),
/// window DIPs ≈ screen DIPs.
/// </summary>
public sealed class DragService
{
    private const double Threshold = 10;
    private WpfPoint _startPos;
    private WpfPoint _clickOffset;
    private WpfPoint _screenOrigin;
    private Window? _adornerWindow;
    private double _mouseSensitivity = 1;

    public DragService(double mouseSensitivity = 1)
    {
        MouseSensitivity = mouseSensitivity;
    }

    public bool IsActive { get; private set; }

    public double MouseSensitivity
    {
        get => _mouseSensitivity;
        set => _mouseSensitivity = Math.Min(3, Math.Max(0.5, value <= 0 ? 1 : value));
    }

    public void BeginTrack(WpfPoint pos, WpfPoint clickOffset, WpfPoint screenOrigin)
    {
        _startPos = pos;
        _clickOffset = clickOffset;
        _screenOrigin = screenOrigin;
        IsActive = false;
    }

    public bool HasExceededThreshold(WpfPoint pos)
    {
        var dx = pos.X - _startPos.X;
        var dy = pos.Y - _startPos.Y;
        var threshold = Threshold / MouseSensitivity;
        return dx * dx + dy * dy >= threshold * threshold;
    }

    public void TryStart(
        WpfPoint pos,
        ImageSource? icon,
        string displayName,
        double tileSize,
        double iconSize,
        double labelWidth)
    {
        IsActive = true;
        ShowAdorner(icon, displayName, pos, tileSize, iconSize, labelWidth);
    }

    public void TryStart(WpfPoint pos, IReadOnlyList<DragPreviewItem> items)
    {
        if (items.Count == 0)
        {
            return;
        }

        IsActive = true;
        ShowAdorner(items, pos);
    }

    public void Move(WpfPoint pos)
    {
        if (_adornerWindow is { } w)
        {
            w.Left = _screenOrigin.X + pos.X - _clickOffset.X;
            w.Top = _screenOrigin.Y + pos.Y - _clickOffset.Y;
        }
    }

    public void End()
    {
        IsActive = false;
        HideAdorner();
    }

    public void Cancel()
    {
        IsActive = false;
        HideAdorner();
    }

    /// <summary>
    /// Find region at position. Accepts window-relative DIP coordinates.
    /// </summary>
    public static string? FindRegionId(UIElement windowContent, WpfPoint windowPos)
    {
        var result = VisualTreeHelper.HitTest(windowContent, windowPos);
        if (result?.VisualHit is null) return null;

        var element = result.VisualHit as DependencyObject;
        while (element is not null)
        {
            if (element is FrameworkElement { Tag: string regionId } && !string.IsNullOrWhiteSpace(regionId))
                return regionId;

            if (element is FrameworkElement fe && fe.DataContext is ViewModels.LaunchpadRegionViewModel region)
                return region.Id;

            element = VisualTreeHelper.GetParent(element);
        }

        return null;
    }

    /// <summary>
    /// Find insertion index at position. Accepts contentGrid-relative DIP coordinates.
    /// </summary>
    public static int FindInsertionIndex(
        FrameworkElement contentGrid,
        ViewModels.LaunchpadRegionViewModel region,
        WpfPoint contentPos,
        ViewModels.LaunchItemViewModel? excludeItem)
    {
        return FindInsertionIndex(
            contentGrid,
            region,
            contentPos,
            excludeItem is null ? [] : [excludeItem]);
    }

    public static int FindInsertionIndex(
        FrameworkElement contentGrid,
        ViewModels.LaunchpadRegionViewModel region,
        WpfPoint contentPos,
        IReadOnlyCollection<ViewModels.LaunchItemViewModel> excludeItems)
    {
        // Find all item buttons in this region
        var bounds = new List<DragTileBounds>();
        foreach (var item in region.Items)
        {
            if (excludeItems.Any(excluded => ReferenceEquals(excluded, item)) || item.IsDragPlaceholder) continue;
            var btn = FindVisualForItem(contentGrid, item);
            if (btn is null || btn.ActualWidth == 0) continue;
            try
            {
                var pos = btn.TransformToAncestor(contentGrid).Transform(new WpfPoint(0, 0));
                bounds.Add(new DragTileBounds(pos.X, pos.Y, btn.ActualWidth, btn.ActualHeight));
            }
            catch { /* element may not be in tree during layout updates */ }
        }

        return DragInsertionCalculator.FindInsertionIndex(bounds, contentPos.X, contentPos.Y);
    }

    private static FrameworkElement? FindVisualForItem(DependencyObject root, object targetDataContext)
    {
        FrameworkElement? fallback = null;
        var count = VisualTreeHelper.GetChildrenCount(root);
        for (var i = 0; i < count; i++)
        {
            var child = VisualTreeHelper.GetChild(root, i);
            if (child is WpfButton button && ReferenceEquals(button.DataContext, targetDataContext))
                return button;
            if (child is FrameworkElement fe && ReferenceEquals(fe.DataContext, targetDataContext))
                fallback ??= fe;
            var found = FindVisualForItem(child, targetDataContext);
            if (found is WpfButton) return found;
            fallback ??= found;
        }
        return fallback;
    }

    private void ShowAdorner(
        ImageSource? icon,
        string displayName,
        WpfPoint pos,
        double tileSize,
        double iconSize,
        double labelWidth)
    {
        HideAdorner();

        var panel = new StackPanel
        {
            HorizontalAlignment = WpfHorizontalAlignment.Center,
            Opacity = 0.85
        };

        if (icon is not null)
        {
            panel.Children.Add(new WpfImage
            {
                Source = icon,
                Width = iconSize,
                Height = iconSize,
                HorizontalAlignment = WpfHorizontalAlignment.Center
            });
        }

        panel.Children.Add(new TextBlock
        {
            Text = displayName,
            FontSize = 11,
            Foreground = WpfBrushes.White,
            TextAlignment = TextAlignment.Center,
            Width = labelWidth,
            TextTrimming = TextTrimming.CharacterEllipsis,
            HorizontalAlignment = WpfHorizontalAlignment.Center,
            Margin = new Thickness(0, 4, 0, 0)
        });

        var preview = new Border
        {
            Width = tileSize,
            Height = tileSize,
            Background = WpfBrushes.Transparent,
            Child = panel
        };

        _adornerWindow = new Window
        {
            Content = preview,
            SizeToContent = SizeToContent.WidthAndHeight,
            WindowStyle = WindowStyle.None,
            AllowsTransparency = true,
            Background = WpfBrushes.Transparent,
            ShowInTaskbar = false,
            Topmost = true,
            ShowActivated = false,
            Left = _screenOrigin.X + pos.X - _clickOffset.X,
            Top = _screenOrigin.Y + pos.Y - _clickOffset.Y,
            IsHitTestVisible = false
        };
        _adornerWindow.Show();
    }

    private void ShowAdorner(IReadOnlyList<DragPreviewItem> items, WpfPoint pos)
    {
        HideAdorner();

        var first = items[0];
        FrameworkElement preview = items.Count == 1
            ? CreateSinglePreview(first)
            : CreateGroupPreview(items);

        _adornerWindow = new Window
        {
            Content = preview,
            SizeToContent = SizeToContent.WidthAndHeight,
            WindowStyle = WindowStyle.None,
            AllowsTransparency = true,
            Background = WpfBrushes.Transparent,
            ShowInTaskbar = false,
            Topmost = true,
            ShowActivated = false,
            Left = _screenOrigin.X + pos.X - _clickOffset.X,
            Top = _screenOrigin.Y + pos.Y - _clickOffset.Y,
            IsHitTestVisible = false
        };
        _adornerWindow.Show();
    }

    private static FrameworkElement CreateSinglePreview(DragPreviewItem item)
    {
        var panel = new StackPanel
        {
            HorizontalAlignment = WpfHorizontalAlignment.Center,
            Opacity = 0.85
        };

        if (item.Icon is not null)
        {
            panel.Children.Add(new WpfImage
            {
                Source = item.Icon,
                Width = item.IconSize,
                Height = item.IconSize,
                HorizontalAlignment = WpfHorizontalAlignment.Center
            });
        }

        panel.Children.Add(new TextBlock
        {
            Text = item.DisplayName,
            FontSize = 11,
            Foreground = WpfBrushes.White,
            TextAlignment = TextAlignment.Center,
            Width = item.LabelWidth,
            TextTrimming = TextTrimming.CharacterEllipsis,
            HorizontalAlignment = WpfHorizontalAlignment.Center,
            Margin = new Thickness(0, 4, 0, 0)
        });

        return new Border
        {
            Width = item.TileSize,
            Height = item.TileSize,
            Background = WpfBrushes.Transparent,
            Child = panel
        };
    }

    private static FrameworkElement CreateGroupPreview(IReadOnlyList<DragPreviewItem> items)
    {
        var first = items[0];
        var canvas = new Canvas
        {
            Width = first.TileSize + 30,
            Height = first.TileSize + 30,
            Opacity = 0.9
        };
        var previewItems = items.Take(4).Reverse().ToList();

        for (var index = 0; index < previewItems.Count; index++)
        {
            var item = previewItems[index];
            var tile = new Border
            {
                Width = first.TileSize,
                Height = first.TileSize,
                Background = WpfBrushes.Transparent,
                Child = item.Icon is null
                    ? null
                    : new WpfImage
                    {
                        Source = item.Icon,
                        Width = item.IconSize,
                        Height = item.IconSize,
                        HorizontalAlignment = WpfHorizontalAlignment.Center,
                        VerticalAlignment = VerticalAlignment.Center
                    }
            };
            Canvas.SetLeft(tile, index * 8);
            Canvas.SetTop(tile, index * 8);
            canvas.Children.Add(tile);
        }

        var badge = new Border
        {
            MinWidth = 28,
            Height = 24,
            CornerRadius = new CornerRadius(12),
            Background = new SolidColorBrush(System.Windows.Media.Color.FromArgb(230, 91, 155, 213)),
            Child = new TextBlock
            {
                Text = items.Count.ToString(),
                Foreground = WpfBrushes.White,
                FontWeight = FontWeights.SemiBold,
                TextAlignment = TextAlignment.Center,
                VerticalAlignment = VerticalAlignment.Center,
                Margin = new Thickness(8, 0, 8, 0)
            }
        };
        Canvas.SetLeft(badge, first.TileSize - 8);
        Canvas.SetTop(badge, 4);
        canvas.Children.Add(badge);

        return canvas;
    }

    private void HideAdorner()
    {
        if (_adornerWindow is { } w)
        {
            _adornerWindow = null;
            try { w.Close(); } catch { /* ignore */ }
        }
    }
}

public sealed record DragPreviewItem(
    ImageSource? Icon,
    string DisplayName,
    double TileSize,
    double IconSize,
    double LabelWidth);