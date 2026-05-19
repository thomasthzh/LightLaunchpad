namespace LightLaunchpad.App.Tests;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        var tests = new (string Name, Action Run)[]
        {
            (
                nameof(InteractiveElementHitTestTests.IsOverInteractiveElement_TreatsScrollBarChromeAsInteractive),
                InteractiveElementHitTestTests.IsOverInteractiveElement_TreatsScrollBarChromeAsInteractive),
            (
                nameof(InteractiveElementHitTestTests.IsOverInteractiveElement_IgnoresPlainLayoutSurfaces),
                InteractiveElementHitTestTests.IsOverInteractiveElement_IgnoresPlainLayoutSurfaces),
            (
                nameof(IconSourceResolverTests.ResolveIconSources_PrefersSteamUrlIconFile),
                IconSourceResolverTests.ResolveIconSources_PrefersSteamUrlIconFile),
            (
                nameof(IconSourceResolverTests.ResolveIconSources_FallsBackToUrlFileWhenIconFileIsMissing),
                IconSourceResolverTests.ResolveIconSources_FallsBackToUrlFileWhenIconFileIsMissing),
            (
                nameof(LaunchpadViewModelIconStabilityTests.LoadItems_PreservesLoadedIconForMatchingSourcePath),
                LaunchpadViewModelIconStabilityTests.LoadItems_PreservesLoadedIconForMatchingSourcePath),
            (
                nameof(LaunchpadViewModelIconStabilityTests.ApplyLoadedIcon_AppliesToReplacementItemAfterRefresh),
                LaunchpadViewModelIconStabilityTests.ApplyLoadedIcon_AppliesToReplacementItemAfterRefresh),
            (
                nameof(LaunchpadViewModelIconStabilityTests.DragPlaceholderMethods_DoNotMutateVisibleCollections),
                LaunchpadViewModelIconStabilityTests.DragPlaceholderMethods_DoNotMutateVisibleCollections),
            (
                nameof(LaunchpadWindowXamlTests.AppTileTemplate_BindsSelectedStateToVisibleFeedback),
                LaunchpadWindowXamlTests.AppTileTemplate_BindsSelectedStateToVisibleFeedback),
            (
                nameof(LaunchpadWindowXamlTests.AppTileTemplate_LaunchesItemsOnlyFromDoubleClick),
                LaunchpadWindowXamlTests.AppTileTemplate_LaunchesItemsOnlyFromDoubleClick),
            (
                nameof(LaunchpadWindowXamlTests.LaunchpadWindow_HasNamedFocusSurfaceForSpotlightSizing),
                LaunchpadWindowXamlTests.LaunchpadWindow_HasNamedFocusSurfaceForSpotlightSizing),
            (
                nameof(SettingsWindowXamlTests.SettingsWindow_OffersLaunchpadAndSpotlightDisplayModes),
                SettingsWindowXamlTests.SettingsWindow_OffersLaunchpadAndSpotlightDisplayModes),
            (
                nameof(LaunchpadWindowPlacementTests.Calculate_UsesFullVirtualScreenForLaunchpadMode),
                LaunchpadWindowPlacementTests.Calculate_UsesFullVirtualScreenForLaunchpadMode),
            (
                nameof(LaunchpadWindowPlacementTests.Calculate_CentersSpotlightWindowInsideWorkArea),
                LaunchpadWindowPlacementTests.Calculate_CentersSpotlightWindowInsideWorkArea),
            (
                nameof(LaunchpadWindowPlacementTests.Calculate_KeepsSmallSpotlightWindowWithinWorkAreaPadding),
                LaunchpadWindowPlacementTests.Calculate_KeepsSmallSpotlightWindowWithinWorkAreaPadding),
            (
                nameof(LaunchpadWindowPlacementTests.Calculate_DoesNotOverflowTinyWorkArea),
                LaunchpadWindowPlacementTests.Calculate_DoesNotOverflowTinyWorkArea)
        };

        var failed = 0;
        foreach (var test in tests)
        {
            try
            {
                test.Run();
                Console.WriteLine($"PASS {test.Name}");
            }
            catch (Exception ex)
            {
                failed++;
                Console.Error.WriteLine($"FAIL {test.Name}");
                Console.Error.WriteLine(ex.Message);
            }
        }

        Console.WriteLine($"{tests.Length - failed}/{tests.Length} tests passed");
        return failed == 0 ? 0 : 1;
    }
}