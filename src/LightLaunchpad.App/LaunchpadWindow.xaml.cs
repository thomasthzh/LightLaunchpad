using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using LightLaunchpad.App.Services;
using LightLaunchpad.App.ViewModels;
using LightLaunchpad.Core.Layout;

using WpfButton = System.Windows.Controls.Button;
using WpfTextBox = System.Windows.Controls.TextBox;
using WpfContextMenu = System.Windows.Controls.ContextMenu;

namespace LightLaunchpad.App;

public partial class LaunchpadWindow : Window
{
    private readonly LaunchpadViewModel _viewModel;
    private readonly LauncherService _launcherService;
    private readonly DragService _drag = new();
    private LaunchItemViewModel? _dragItem;
    private IReadOnlyList<LaunchItemViewModel> _dragItems = [];
    private bool _dragCompleted;
    private DropTarget? _lastDropTarget;

    // Rubber band selection state
    private bool _rubberBanding;
    private System.Windows.Point _rubberBandStart;

    public LaunchpadWindow(LaunchpadViewModel viewModel, LauncherService launcherService)
    {
        _viewModel = viewModel;
        _launcherService = launcherService;
        DataContext = viewModel;
        InitializeComponent();
        PreviewMouseMove += Window_PreviewMouseMove;
        PreviewMouseLeftButtonUp += Window_PreviewMouseLeftButtonUp;
        PreviewMouseLeftButtonDown += Window_PreviewMouseLeftButtonDown;
    }

    public event Action<LaunchpadViewMode>? ViewModeRequested;

    public event Action? ImportStartMenuRequested;

    public event Action? ImportVuiRequested;

    public event Action? OpenSettingsRequested;

    public event Action? ImportClicked;

    public event Action? HiddenCompleted;

    public event Action<string>? CreateRegionRequested;

    public event Action<string, string>? RenameRegionRequested;

    public event Action<string>? DeleteRegionRequested;

    public event Action<string, string>? RenameItemRequested;

    public event Action<string>? RemoveItemRequested;

    public event Action<string, string, int>? MoveItemRequested;

    public event Action<IReadOnlyList<string>, string, int>? MoveItemsRequested;

    public void ShowLaunchpad()
    {
        Left = SystemParameters.VirtualScreenLeft;
        Top = SystemParameters.VirtualScreenTop;
        Width = SystemParameters.VirtualScreenWidth;
        Height = SystemParameters.VirtualScreenHeight;

        Opacity = 0;
        WindowTranslate.Y = 10;
        Show();
        Activate();
        FocusSearchBox();

        BeginAnimation(OpacityProperty, new DoubleAnimation(1, TimeSpan.FromMilliseconds(110)));
        WindowTranslate.BeginAnimation(TranslateTransform.YProperty, new DoubleAnimation(0, TimeSpan.FromMilliseconds(130)));
    }

    public void HideLaunchpad()
    {
        var animation = new DoubleAnimation(0, TimeSpan.FromMilliseconds(70));
        animation.Completed += (_, _) =>
        {
            Hide();
            HiddenCompleted?.Invoke();
        };
        BeginAnimation(OpacityProperty, animation);
    }

    private void Window_Loaded(object sender, RoutedEventArgs e)
    {
        FocusSearchBox();
    }

    private void Window_PreviewKeyDown(object sender, System.Windows.Input.KeyEventArgs e)
    {
        if (e.Key == Key.Escape)
        {
            if (_drag.IsActive)
            {
                CancelDrag();
                e.Handled = true;
                return;
            }

            if (_rubberBanding)
            {
                CancelRubberBand();
                e.Handled = true;
                return;
            }

            HideLaunchpad();
            e.Handled = true;
            return;
        }

        if (e.Key == Key.Enter)
        {
            Launch(_viewModel.SelectedItem);
            e.Handled = true;
        }

        if (e.Key == Key.A && Keyboard.Modifiers == ModifierKeys.Control)
        {
            SelectAllItems();
            e.Handled = true;
        }
    }

    // --- Mouse down: start drag tracking or rubber band ---

    private void Window_PreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        _dragCompleted = false;
        var hit = e.OriginalSource as DependencyObject;

        // Check if click is on a launch item button
        var itemVm = FindDataContext<LaunchItemViewModel>(hit);
        if (itemVm is not null)
        {
            // Ctrl+Click toggles selection
            if (Keyboard.Modifiers == ModifierKeys.Control)
            {
                _viewModel.ToggleSelectItem(itemVm);
                e.Handled = true;
                return;
            }

            // Start drag tracking
            _dragItem = itemVm;
            var pos = e.GetPosition(this); // DIP coords, fullscreen at origin ≈ screen coords
            var itemPos = GetItemWindowPos(itemVm);
            var clickOffset = new System.Windows.Point(
                pos.X - itemPos.X,
                pos.Y - itemPos.Y);
            _drag.BeginTrack(pos, clickOffset, new System.Windows.Point(Left, Top));

            if (!itemVm.IsSelected)
                _viewModel.SelectItem(itemVm);
            return;
        }

        // Click on empty space: start rubber band
        if (IsOverInteractiveElement(hit)) return;

        _rubberBandStart = e.GetPosition(ContentGrid);
        _rubberBanding = true;
        SelectionRect.Width = 0;
        SelectionRect.Height = 0;
        SelectionRect.Visibility = Visibility.Visible;
        Canvas.SetLeft(SelectionRect, _rubberBandStart.X);
        Canvas.SetTop(SelectionRect, _rubberBandStart.Y);

        if (Keyboard.Modifiers != ModifierKeys.Control)
            _viewModel.ClearSelection();

        Mouse.Capture(ContentGrid, CaptureMode.Element);
        e.Handled = true;
    }

    // --- Mouse move: drag or rubber band ---

    private void Window_PreviewMouseMove(object sender, System.Windows.Input.MouseEventArgs e)
    {
        if (e.LeftButton != MouseButtonState.Pressed) return;

        var windowPos = e.GetPosition(this);
        var contentPos = e.GetPosition(ContentGrid);

        // Drag logic
        if (_dragItem is not null)
        {
            if (_drag.IsActive)
            {
                _drag.Move(windowPos);
                UpdateDragTarget(windowPos, contentPos);
                e.Handled = true;
                return;
            }

            if (_drag.HasExceededThreshold(windowPos))
            {
                if (_rubberBanding) CancelRubberBand();

                _dragItems = ResolveDragItems(_dragItem);
                _lastDropTarget = null;

                Mouse.Capture(ContentGrid, CaptureMode.SubTree);
                _drag.TryStart(windowPos, CreatePreviewItems(_dragItems));
                UpdateDragTarget(windowPos, contentPos);
                e.Handled = true;
                return;
            }
            return;
        }

        // Rubber band logic
        if (_rubberBanding)
        {
            var current = e.GetPosition(ContentGrid);
            var x = Math.Min(current.X, _rubberBandStart.X);
            var y = Math.Min(current.Y, _rubberBandStart.Y);
            var w = Math.Abs(current.X - _rubberBandStart.X);
            var h = Math.Abs(current.Y - _rubberBandStart.Y);

            Canvas.SetLeft(SelectionRect, x);
            Canvas.SetTop(SelectionRect, y);
            SelectionRect.Width = w;
            SelectionRect.Height = h;

            var rect = new Rect(x, y, w, h);
            SelectItemsInRect(rect);
            e.Handled = true;
        }
    }

    // --- Mouse up: end drag or rubber band ---

    private void Window_PreviewMouseLeftButtonUp(object sender, MouseButtonEventArgs e)
    {
        // End drag
        if (_drag.IsActive)
        {
            var windowPos = e.GetPosition(this);
            var contentPos = e.GetPosition(ContentGrid);
            var dropTarget = ResolveDropTarget(windowPos, contentPos) ?? _lastDropTarget;

            _drag.End();
            Mouse.Capture(null);

            if (dropTarget is { } acceptedTarget && _dragItems.Count > 0)
            {
                var sourcePaths = _dragItems.Select(item => item.SourcePath).ToList();
                if (sourcePaths.Count == 1)
                {
                    MoveItemRequested?.Invoke(sourcePaths[0], acceptedTarget.RegionId, acceptedTarget.InsertIndex);
                }
                else
                {
                    MoveItemsRequested?.Invoke(sourcePaths, acceptedTarget.RegionId, acceptedTarget.InsertIndex);
                }
            }

            _dragItem = null;
            _dragItems = [];
            _lastDropTarget = null;
            _dragCompleted = true;
            HideDropIndicator();
            e.Handled = true;
            return;
        }

        _dragItem = null;
        _dragItems = [];

        // End rubber band
        if (_rubberBanding)
        {
            SelectionRect.Visibility = Visibility.Collapsed;
            _rubberBanding = false;
            Mouse.Capture(null);
            e.Handled = true;
        }
    }

    // --- Drag cancel ---

    private void CancelDrag()
    {
        _drag.Cancel();
        Mouse.Capture(null);
        _dragItem = null;
        _dragItems = [];
        _lastDropTarget = null;
        HideDropIndicator();
    }

    // --- Drop indicator ---

    private void UpdateDragTarget(System.Windows.Point windowPos, System.Windows.Point contentPos)
    {
        var dropTarget = ResolveDropTarget(windowPos, contentPos);
        if (dropTarget is null)
        {
            HideDropIndicator();
            _lastDropTarget = null;
            return;
        }

        _lastDropTarget = dropTarget;
        var targetRegion = _viewModel.Regions.FirstOrDefault(region => region.Id == dropTarget.Value.RegionId);
        if (targetRegion is null) return;
        UpdateDropIndicator(targetRegion, dropTarget.Value.InsertIndex);
    }

    private DropTarget? ResolveDropTarget(System.Windows.Point windowPos, System.Windows.Point contentPos)
    {
        if (_dragItems.Count == 0 || Content is not UIElement content)
        {
            return null;
        }

        var targetRegion = ResolveTargetRegion(content, windowPos, contentPos);
        if (targetRegion is null)
        {
            return null;
        }

        var insertIndex = DragService.FindInsertionIndex(ContentGrid, targetRegion, contentPos, _dragItems);
        return new DropTarget(targetRegion.Id, insertIndex);
    }

    private LaunchpadRegionViewModel? ResolveTargetRegion(
        UIElement content,
        System.Windows.Point windowPos,
        System.Windows.Point contentPos)
    {
        var hitRegionId = DragService.FindRegionId(content, windowPos);
        if (!string.IsNullOrWhiteSpace(hitRegionId))
        {
            var hitRegion = _viewModel.Regions.FirstOrDefault(region => region.Id == hitRegionId);
            if (hitRegion is not null)
            {
                return hitRegion;
            }
        }

        foreach (var region in _viewModel.Regions)
        {
            var panel = FindRegionPanel(region.Id);
            if (panel is null)
            {
                continue;
            }

            try
            {
                var pos = panel.TransformToAncestor(ContentGrid).Transform(new System.Windows.Point(0, 0));
                var height = Math.Max(panel.ActualHeight, 96);
                var top = pos.Y - 14;
                var bottom = pos.Y + height + 14;
                if (contentPos.Y >= top && contentPos.Y <= bottom)
                {
                    return region;
                }
            }
            catch
            {
                // Ignore transient layout misses during first arrange.
            }
        }

        if (ViewModeIsTabs())
        {
            return _viewModel.Regions.FirstOrDefault(region => region.Id == _viewModel.ActiveRegionId)
                ?? _viewModel.Regions.FirstOrDefault();
        }

        return null;
    }

    private bool ViewModeIsTabs()
    {
        return _viewModel.RegionTabsVisibility == Visibility.Visible;
    }

    private void UpdateDropIndicator(LaunchpadRegionViewModel targetRegion, int insertIdx)
    {
        if (_dragItems.Count == 0) return;

        var visibleItems = targetRegion.Items
            .Where(item => !_dragItems.Any(dragged => ReferenceEquals(dragged, item)) && !item.IsDragPlaceholder)
            .ToList();
        System.Windows.Point indicatorPos;
        double indicatorHeight = 0;

        if (visibleItems.Count == 0)
        {
            var regionPanel = FindRegionPanel(targetRegion.Id);
            if (regionPanel is not null)
            {
                try
                {
                    var pos = regionPanel.TransformToAncestor(ContentGrid).Transform(new System.Windows.Point(0, 0));
                    indicatorPos = new System.Windows.Point(pos.X + 12, pos.Y + 30);
                    indicatorHeight = 80;
                }
                catch { HideDropIndicator(); return; }
            }
            else { HideDropIndicator(); return; }
        }
        else if (insertIdx < visibleItems.Count)
        {
            var targetItem = visibleItems[insertIdx];
            var btn = FindVisualForItem(ContentGrid, targetItem);
            if (btn is null) { HideDropIndicator(); return; }
            try
            {
                var pos = btn.TransformToAncestor(ContentGrid).Transform(new System.Windows.Point(0, 0));
                indicatorPos = new System.Windows.Point(pos.X - 6, pos.Y);
                indicatorHeight = btn.ActualHeight;
            }
            catch { HideDropIndicator(); return; }
        }
        else
        {
            var lastItem = visibleItems[^1];
            var btn = FindVisualForItem(ContentGrid, lastItem);
            if (btn is null) { HideDropIndicator(); return; }
            try
            {
                var pos = btn.TransformToAncestor(ContentGrid).Transform(new System.Windows.Point(0, 0));
                indicatorPos = new System.Windows.Point(pos.X + btn.ActualWidth + 4, pos.Y);
                indicatorHeight = btn.ActualHeight;
            }
            catch { HideDropIndicator(); return; }
        }

        DropIndicator.Visibility = Visibility.Visible;
        Canvas.SetLeft(DropIndicator, indicatorPos.X);
        Canvas.SetTop(DropIndicator, indicatorPos.Y);
        DropIndicator.Height = Math.Max(indicatorHeight, 20);
    }

    private void HideDropIndicator()
    {
        DropIndicator.Visibility = Visibility.Collapsed;
    }

    // --- Rubber band selection helpers ---

    private void SelectItemsInRect(Rect rect)
    {
        foreach (var region in _viewModel.Regions)
        {
            foreach (var item in region.Items)
            {
                var btn = FindVisualForItem(ContentGrid, item);
                if (btn is null) continue;

                try
                {
                    var pos = btn.TransformToAncestor(ContentGrid).Transform(new System.Windows.Point(0, 0));
                    var itemRect = new Rect(pos.X, pos.Y, btn.ActualWidth, btn.ActualHeight);

                    if (rect.IntersectsWith(itemRect))
                        item.IsSelected = true;
                    else if (Keyboard.Modifiers != ModifierKeys.Control)
                        item.IsSelected = false;
                }
                catch
                {
                    // Element may not be in tree
                }
            }
        }
    }

    private static FrameworkElement? FindVisualForItem(DependencyObject root, LaunchItemViewModel target)
    {
        FrameworkElement? fallback = null;
        var count = VisualTreeHelper.GetChildrenCount(root);
        for (var i = 0; i < count; i++)
        {
            var child = VisualTreeHelper.GetChild(root, i);
            if (child is WpfButton button && ReferenceEquals(button.DataContext, target))
                return button;
            if (child is FrameworkElement fe && ReferenceEquals(fe.DataContext, target))
                fallback ??= fe;
            var found = FindVisualForItem(child, target);
            if (found is WpfButton) return found;
            fallback ??= found;
        }
        return fallback;
    }

    private FrameworkElement? FindRegionPanel(string regionId)
    {
        return FindRegionPanelRecursive(ContentGrid, regionId);
    }

    private static FrameworkElement? FindRegionPanelRecursive(DependencyObject root, string regionId)
    {
        var count = VisualTreeHelper.GetChildrenCount(root);
        for (var i = 0; i < count; i++)
        {
            var child = VisualTreeHelper.GetChild(root, i);
            if (child is FrameworkElement feRegion && feRegion.Tag is string tag && tag == regionId)
                return feRegion;
            var found = FindRegionPanelRecursive(child, regionId);
            if (found is not null) return found;
        }
        return null;
    }

    private void CancelRubberBand()
    {
        SelectionRect.Visibility = Visibility.Collapsed;
        _rubberBanding = false;
        Mouse.Capture(null);
    }

    // --- Helpers ---

    private IReadOnlyList<LaunchItemViewModel> ResolveDragItems(LaunchItemViewModel primary)
    {
        if (!primary.IsSelected)
        {
            return [primary];
        }

        var selected = _viewModel.SelectedItems.ToHashSet();
        var ordered = _viewModel.Regions
            .SelectMany(region => region.Items)
            .Where(item => selected.Contains(item))
            .ToList();
        return ordered.Count == 0 ? [primary] : ordered;
    }

    private static IReadOnlyList<DragPreviewItem> CreatePreviewItems(IEnumerable<LaunchItemViewModel> items)
    {
        return items
            .Select(item => new DragPreviewItem(
                item.Icon,
                item.DisplayName,
                item.TileSize,
                item.IconSize,
                item.LabelWidth))
            .ToList();
    }

    private System.Windows.Point GetItemWindowPos(LaunchItemViewModel item)
    {
        var btn = FindVisualForItem(ContentGrid, item);
        if (btn is null) return new System.Windows.Point(0, 0);
        try
        {
            return btn.TransformToAncestor(this).Transform(new System.Windows.Point(0, 0));
        }
        catch
        {
            return new System.Windows.Point(0, 0);
        }
    }

    // --- Selection operations ---

    private void SelectAllItems()
    {
        foreach (var item in _viewModel.FilteredItems)
            item.IsSelected = true;
    }

    private void SelectAll_Click(object sender, RoutedEventArgs e) => SelectAllItems();

    private void ClearSelection_Click(object sender, RoutedEventArgs e) => _viewModel.ClearSelection();

    private void BatchRemove_Click(object sender, RoutedEventArgs e)
    {
        var selected = _viewModel.SelectedItems;
        if (selected.Count == 0) return;

        var names = string.Join(", ", selected.Take(5).Select(i => i.DisplayName));
        if (selected.Count > 5) names += $" and {selected.Count - 5} more";

        if (System.Windows.MessageBox.Show(
                $"Remove {selected.Count} item(s) from the launchpad?\n{names}",
                "LightLaunchpad", MessageBoxButton.YesNo, MessageBoxImage.Question) == MessageBoxResult.Yes)
        {
            foreach (var item in selected.ToList())
                RemoveItemRequested?.Invoke(item.SourcePath);
        }
    }

    // --- Hit-test helpers ---

    private static T? FindDataContext<T>(DependencyObject? obj) where T : class
    {
        while (obj is not null)
        {
            if (obj is FrameworkElement fe && fe.DataContext is T typed)
                return typed;
            obj = VisualTreeHelper.GetParent(obj);
        }
        return null;
    }

    private static bool IsOverInteractiveElement(DependencyObject? hit)
    {
        while (hit is not null)
        {
            if (hit is WpfTextBox or WpfButton or WpfContextMenu)
                return true;
            if (hit is FrameworkElement { Name: "SearchBox" })
                return true;
            hit = VisualTreeHelper.GetParent(hit);
        }
        return false;
    }

    // --- Gear / menu ---

    private void GearButton_Click(object sender, RoutedEventArgs e)
    {
        if (sender is WpfButton button && button.ContextMenu is not null)
        {
            button.ContextMenu.PlacementTarget = button;
            button.ContextMenu.IsOpen = true;
        }
    }

    private void ImportButton_Click(object sender, RoutedEventArgs e) => ImportClicked?.Invoke();

    private void SetInlineRegions_Click(object sender, RoutedEventArgs e) => ViewModeRequested?.Invoke(LaunchpadViewMode.InlineRegions);

    private void SetRegionTabs_Click(object sender, RoutedEventArgs e) => ViewModeRequested?.Invoke(LaunchpadViewMode.RegionTabs);

    private void ImportStartMenu_Click(object sender, RoutedEventArgs e) => ImportStartMenuRequested?.Invoke();

    private void ImportVui_Click(object sender, RoutedEventArgs e) => ImportVuiRequested?.Invoke();

    private void OpenSettings_Click(object sender, RoutedEventArgs e) => OpenSettingsRequested?.Invoke();

    private void RegionTab_Click(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { Tag: string regionId })
            _viewModel.ActiveRegionId = regionId;
    }

    private void CreateRegion_Click(object sender, RoutedEventArgs e)
    {
        var name = Prompt("Create region", "Region name:", "New Region");
        if (!string.IsNullOrWhiteSpace(name))
            CreateRegionRequested?.Invoke(name);
    }

    private void RenameRegion_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as MenuItem)?.CommandParameter is not LaunchpadRegionViewModel region) return;
        var name = Prompt("Rename region", "Region name:", region.Name);
        if (!string.IsNullOrWhiteSpace(name))
            RenameRegionRequested?.Invoke(region.Id, name);
    }

    private void DeleteRegion_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as MenuItem)?.CommandParameter is not LaunchpadRegionViewModel region) return;
        if (System.Windows.MessageBox.Show(
                $"Delete region '{region.Name}'? Apps will move to Uncategorized.",
                "LightLaunchpad", MessageBoxButton.YesNo, MessageBoxImage.Question) == MessageBoxResult.Yes)
        {
            DeleteRegionRequested?.Invoke(region.Id);
        }
    }

    private void LaunchItem_DoubleClick(object sender, MouseButtonEventArgs e)
    {
        if (_drag.IsActive || _dragCompleted) return;
        if (sender is FrameworkElement { DataContext: LaunchItemViewModel item })
        {
            Launch(item);
            e.Handled = true;
        }
    }

    private void LaunchMenuItem_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as MenuItem)?.CommandParameter is LaunchItemViewModel item)
            Launch(item);
    }

    private void RenameItem_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as MenuItem)?.CommandParameter is not LaunchItemViewModel item) return;
        var name = Prompt("Rename app", "Display name:", item.DisplayName);
        if (!string.IsNullOrWhiteSpace(name))
            RenameItemRequested?.Invoke(item.SourcePath, name);
    }

    private void RemoveItem_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as MenuItem)?.CommandParameter is not LaunchItemViewModel item) return;
        if (System.Windows.MessageBox.Show(
                $"Remove '{item.DisplayName}' from the launchpad? This does not uninstall the app.",
                "LightLaunchpad", MessageBoxButton.YesNo, MessageBoxImage.Question) == MessageBoxResult.Yes)
        {
            RemoveItemRequested?.Invoke(item.SourcePath);
        }
    }

    private void OpenItemLocation_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as MenuItem)?.CommandParameter is not LaunchItemViewModel item) return;
        var directory = Path.GetDirectoryName(item.SourcePath);
        if (!string.IsNullOrWhiteSpace(directory) && Directory.Exists(directory))
            Process.Start(new ProcessStartInfo { FileName = directory, UseShellExecute = true });
    }

    private void Launch(LaunchItemViewModel? item)
    {
        if (item is null) return;
        _launcherService.Launch(item.Item);
        HideLaunchpad();
    }

    private void FocusSearchBox()
    {
        SearchBox.Focus();
        Keyboard.Focus(SearchBox);
        SearchBox.SelectAll();
    }

    private string? Prompt(string title, string label, string initialValue)
    {
        var dialog = new TextInputDialog(title, label, initialValue) { Owner = this };
        return dialog.ShowDialog() == true ? dialog.Value : null;
    }

    private readonly record struct DropTarget(string RegionId, int InsertIndex);
}
