using System.IO;

namespace LightLaunchpad.App.Tests;

public sealed class LaunchpadWindowXamlTests
{
    public static void AppTileTemplate_BindsSelectedStateToVisibleFeedback()
    {
        var xaml = File.ReadAllText(FindLaunchpadWindowXaml());

        TestAssert.Contains("Binding=\"{Binding IsSelected}\"", xaml);
        TestAssert.Contains("x:Name=\"SelectedBadge\"", xaml);
        TestAssert.Contains("Property=\"BorderBrush\" Value=\"#CC7DBEFF\"", xaml);
        TestAssert.Contains("Property=\"Visibility\" Value=\"Visible\"", xaml);
    }

    public static void AppTileTemplate_LaunchesItemsOnlyFromDoubleClick()
    {
        var xaml = File.ReadAllText(FindLaunchpadWindowXaml());

        TestAssert.Contains("MouseDoubleClick=\"LaunchItem_DoubleClick\"", xaml);
        TestAssert.DoesNotContain("Click=\"LaunchItem_Click\"", xaml);
    }

    public static void LaunchpadWindow_HasNamedFocusSurfaceForSpotlightSizing()
    {
        var xaml = File.ReadAllText(FindLaunchpadWindowXaml());

        TestAssert.Contains("x:Name=\"FocusSurface\"", xaml);
        TestAssert.Contains("Background=\"Transparent\"", xaml);
        TestAssert.Contains("HorizontalScrollBarVisibility=\"Disabled\"", xaml);
        TestAssert.Contains("WrapPanel HorizontalAlignment=\"Center\"", xaml);
    }

    private static string FindLaunchpadWindowXaml()
    {
        var current = new DirectoryInfo(Environment.CurrentDirectory);
        while (current is not null)
        {
            var candidate = Path.Combine(current.FullName, "src", "LightLaunchpad.App", "LaunchpadWindow.xaml");
            if (File.Exists(candidate))
            {
                return candidate;
            }

            current = current.Parent;
        }

        throw new FileNotFoundException("Could not find LaunchpadWindow.xaml from the test working directory.");
    }
}