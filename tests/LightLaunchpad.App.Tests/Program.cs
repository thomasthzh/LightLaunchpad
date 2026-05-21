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
                nameof(LaunchpadWindowXamlTests.LaunchpadWindow_RemovesVuiImportAndAddsRegionBatchMenus),
                LaunchpadWindowXamlTests.LaunchpadWindow_RemovesVuiImportAndAddsRegionBatchMenus),
            (
                nameof(LaunchpadWindowXamlTests.LaunchpadWindow_BindsAppSpacingAndHidesSpotlightScrollbarInCode),
                LaunchpadWindowXamlTests.LaunchpadWindow_BindsAppSpacingAndHidesSpotlightScrollbarInCode),
            (
                nameof(LaunchpadWindowXamlTests.LaunchpadWindow_SupportsRegionDragAndWheelSensitivity),
                LaunchpadWindowXamlTests.LaunchpadWindow_SupportsRegionDragAndWheelSensitivity),
            (
                nameof(SettingsWindowXamlTests.SettingsWindow_OffersLaunchpadAndSpotlightDisplayModes),
                SettingsWindowXamlTests.SettingsWindow_OffersLaunchpadAndSpotlightDisplayModes),
            (
                nameof(SettingsWindowXamlTests.SettingsWindow_OffersLanguageAndSpotlightTuning),
                SettingsWindowXamlTests.SettingsWindow_OffersLanguageAndSpotlightTuning),
            (
                nameof(TrayServiceSourceTests.TrayMenu_DoesNotExposeVuiImport),
                TrayServiceSourceTests.TrayMenu_DoesNotExposeVuiImport),
            (
                nameof(HostedAppModeSourceTests.AppSource_SupportsHostedUiModeWithoutStandaloneTrayOrHotkey),
                HostedAppModeSourceTests.AppSource_SupportsHostedUiModeWithoutStandaloneTrayOrHotkey),
            (
                nameof(HostedAppModeSourceTests.AgentProject_UsesLightweightNativeShellContracts),
                HostedAppModeSourceTests.AgentProject_UsesLightweightNativeShellContracts),
            (
                nameof(HostedAppModeSourceTests.NativeAgentSource_UsesWin32OnlyBackgroundContracts),
                HostedAppModeSourceTests.NativeAgentSource_UsesWin32OnlyBackgroundContracts),
            (
                nameof(HostedAppModeSourceTests.NativeAgentSource_LoadsTrayIconAndCleansHostedUiWithJobObject),
                HostedAppModeSourceTests.NativeAgentSource_LoadsTrayIconAndCleansHostedUiWithJobObject),
            (
                nameof(HostedAppModeSourceTests.PackageReleaseScript_BundlesUiManagedAgentAndNativeAgent),
                HostedAppModeSourceTests.PackageReleaseScript_BundlesUiManagedAgentAndNativeAgent),
            (
                nameof(HostedAppModeSourceTests.StartupRegistration_PrefersNativeAgentForLowMemoryRoute),
                HostedAppModeSourceTests.StartupRegistration_PrefersNativeAgentForLowMemoryRoute),
            (
                nameof(LaunchpadWindowPlacementTests.Calculate_UsesFullVirtualScreenForLaunchpadMode),
                LaunchpadWindowPlacementTests.Calculate_UsesFullVirtualScreenForLaunchpadMode),
            (
                nameof(LaunchpadWindowPlacementTests.Calculate_CentersSpotlightWindowInsideWorkArea),
                LaunchpadWindowPlacementTests.Calculate_CentersSpotlightWindowInsideWorkArea),
            (
                nameof(LaunchpadWindowPlacementTests.Calculate_UsesConfiguredSpotlightWindowSize),
                LaunchpadWindowPlacementTests.Calculate_UsesConfiguredSpotlightWindowSize),
            (
                nameof(LaunchpadWindowPlacementTests.Calculate_KeepsSmallSpotlightWindowWithinWorkAreaPadding),
                LaunchpadWindowPlacementTests.Calculate_KeepsSmallSpotlightWindowWithinWorkAreaPadding),
            (
                nameof(LaunchpadWindowPlacementTests.Calculate_DoesNotOverflowTinyWorkArea),
                LaunchpadWindowPlacementTests.Calculate_DoesNotOverflowTinyWorkArea),
            (
                nameof(WheelScrollCalculatorTests.CalculateOffset_UsesWheelSensitivity),
                WheelScrollCalculatorTests.CalculateOffset_UsesWheelSensitivity),
            (
                nameof(WheelScrollCalculatorTests.CalculateOffset_ClampsToScrollableRange),
                WheelScrollCalculatorTests.CalculateOffset_ClampsToScrollableRange),
            (
                nameof(LaunchpadRegionSelectionTests.ToggleRegionSelection_TracksSelectedRegions),
                LaunchpadRegionSelectionTests.ToggleRegionSelection_TracksSelectedRegions),
            (
                nameof(LaunchpadRegionSelectionTests.ClearSelection_ClearsItemsAndRegions),
                LaunchpadRegionSelectionTests.ClearSelection_ClearsItemsAndRegions),
            (
                nameof(LaunchpadRegionSelectionTests.LoadItems_HidesEmptyUncategorizedWhenAllAppsAreCategorized),
                LaunchpadRegionSelectionTests.LoadItems_HidesEmptyUncategorizedWhenAllAppsAreCategorized)
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
