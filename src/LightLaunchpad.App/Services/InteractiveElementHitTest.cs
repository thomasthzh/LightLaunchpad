using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;

using WpfButtonBase = System.Windows.Controls.Primitives.ButtonBase;
using WpfTextBox = System.Windows.Controls.TextBox;

namespace LightLaunchpad.App.Services;

public static class InteractiveElementHitTest
{
    public static bool IsOverInteractiveElement(DependencyObject? hit)
    {
        while (hit is not null)
        {
            if (hit is WpfTextBox or WpfButtonBase or ContextMenu or RangeBase or Thumb or Track)
                return true;

            if (hit is FrameworkElement { Name: "SearchBox" })
                return true;

            hit = VisualTreeHelper.GetParent(hit);
        }

        return false;
    }
}
