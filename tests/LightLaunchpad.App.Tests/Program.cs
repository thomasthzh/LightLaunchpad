namespace LightLaunchpad.App.Tests;

internal static class Program
{
    private static int Main()
    {
        var tests = new (string Name, Action Run)[]
        {
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
                LaunchpadWindowXamlTests.AppTileTemplate_LaunchesItemsOnlyFromDoubleClick)
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
