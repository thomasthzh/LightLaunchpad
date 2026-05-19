namespace LightLaunchpad.Core.Tests;

internal static class Program
{
    private static int Main()
    {
        var tests = new (string Name, Action Run)[]
        {
            (nameof(LaunchItemTests.LaunchItem_StoresDisplayNameAndSourcePath), LaunchItemTests.LaunchItem_StoresDisplayNameAndSourcePath),
            (nameof(AppSettingsTests.CreateDefault_UsesUserLaunchpadFolderAndAltD), AppSettingsTests.CreateDefault_UsesUserLaunchpadFolderAndAltD),
            (nameof(AppSettingsTests.SettingsService_LoadsDefaultsWhenFileDoesNotExist), AppSettingsTests.SettingsService_LoadsDefaultsWhenFileDoesNotExist),
            (nameof(AppSettingsTests.SettingsService_SavesAndLoadsCustomSettings), AppSettingsTests.SettingsService_SavesAndLoadsCustomSettings),
            (nameof(AppSettingsTests.SettingsService_DefaultsMissingDisplayModeToLaunchpad), AppSettingsTests.SettingsService_DefaultsMissingDisplayModeToLaunchpad),
            (nameof(AppStoragePathsTests.GetIconCacheDirectory_UsesDocumentsLightLaunchpadIconsFolder), AppStoragePathsTests.GetIconCacheDirectory_UsesDocumentsLightLaunchpadIconsFolder),
            (nameof(HotkeyGestureTests.Parse_ReadsAltLetterGesture), HotkeyGestureTests.Parse_ReadsAltLetterGesture),
            (nameof(HotkeyGestureTests.Parse_NormalizesWhitespaceAndCase), HotkeyGestureTests.Parse_NormalizesWhitespaceAndCase),
            (nameof(ShortcutRepositoryTests.LoadItems_CreatesFolderWhenMissing), ShortcutRepositoryTests.LoadItems_CreatesFolderWhenMissing),
            (nameof(ShortcutRepositoryTests.LoadItems_IncludesSupportedFilesAndSkipsUnsupportedFiles), ShortcutRepositoryTests.LoadItems_IncludesSupportedFilesAndSkipsUnsupportedFiles),
            (nameof(InternetShortcutFileTests.Read_ParsesSteamUrlIconFileAndIndex), InternetShortcutFileTests.Read_ParsesSteamUrlIconFileAndIndex),
            (nameof(LaunchItemSearchTests.Filter_RanksPrefixMatchesBeforeContainsMatches), LaunchItemSearchTests.Filter_RanksPrefixMatchesBeforeContainsMatches),
            (nameof(LaunchItemSearchTests.Filter_ReturnsAllItemsWhenQueryIsBlank), LaunchItemSearchTests.Filter_ReturnsAllItemsWhenQueryIsBlank),
            (nameof(VuiImportParserTests.Parse_ExtractsDecodedPathsAndSkipsEmptyValues), VuiImportParserTests.Parse_ExtractsDecodedPathsAndSkipsEmptyValues),
            (nameof(VuiImportParserTests.Parse_DeduplicatesNormalizedPaths), VuiImportParserTests.Parse_DeduplicatesNormalizedPaths),
            (nameof(LayoutServiceTests.Load_CreatesDefaultLayoutWhenMissing), LayoutServiceTests.Load_CreatesDefaultLayoutWhenMissing),
            (nameof(LayoutServiceTests.MergeItems_AddsNewFolderItemsToUncategorized), LayoutServiceTests.MergeItems_AddsNewFolderItemsToUncategorized),
            (nameof(LayoutServiceTests.DeleteRegion_MovesItemsToUncategorized), LayoutServiceTests.DeleteRegion_MovesItemsToUncategorized),
            (nameof(LayoutServiceTests.SaveAndLoad_RoundTripsViewMode), LayoutServiceTests.SaveAndLoad_RoundTripsViewMode),
            (nameof(LayoutServiceTests.MoveItem_ReordersWithinRegionAtRequestedIndex), LayoutServiceTests.MoveItem_ReordersWithinRegionAtRequestedIndex),
            (nameof(LayoutServiceTests.MoveItem_MovesBetweenRegionsAtRequestedIndex), LayoutServiceTests.MoveItem_MovesBetweenRegionsAtRequestedIndex),
            (nameof(LayoutServiceTests.MoveItems_MovesSelectionAsContiguousGroupUsingIndexAmongRemainingItems), LayoutServiceTests.MoveItems_MovesSelectionAsContiguousGroupUsingIndexAmongRemainingItems),
            (nameof(DragTargetSmootherTests.TryAccept_WaitsForStableCandidateBeforeAccepting), DragTargetSmootherTests.TryAccept_WaitsForStableCandidateBeforeAccepting),
            (nameof(DragTargetSmootherTests.TryAccept_IgnoresTinyMovesAfterAcceptedTarget), DragTargetSmootherTests.TryAccept_IgnoresTinyMovesAfterAcceptedTarget),
            (nameof(DragInsertionCalculatorTests.FindInsertionIndex_UsesTileHorizontalMidline), DragInsertionCalculatorTests.FindInsertionIndex_UsesTileHorizontalMidline),
            (nameof(DragInsertionCalculatorTests.FindInsertionIndex_UsesMatchingRowWhenPointerIsInBlankArea), DragInsertionCalculatorTests.FindInsertionIndex_UsesMatchingRowWhenPointerIsInBlankArea),
            (nameof(StartMenuImporterTests.Discover_ReadsShortcutsRecursivelyWithRegionHints), StartMenuImporterTests.Discover_ReadsShortcutsRecursivelyWithRegionHints),
            (nameof(StartMenuImporterTests.Discover_DeduplicatesByNormalizedSourcePath), StartMenuImporterTests.Discover_DeduplicatesByNormalizedSourcePath)
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