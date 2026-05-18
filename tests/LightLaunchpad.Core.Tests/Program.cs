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
            (nameof(HotkeyGestureTests.Parse_ReadsAltLetterGesture), HotkeyGestureTests.Parse_ReadsAltLetterGesture),
            (nameof(HotkeyGestureTests.Parse_NormalizesWhitespaceAndCase), HotkeyGestureTests.Parse_NormalizesWhitespaceAndCase),
            (nameof(ShortcutRepositoryTests.LoadItems_CreatesFolderWhenMissing), ShortcutRepositoryTests.LoadItems_CreatesFolderWhenMissing),
            (nameof(ShortcutRepositoryTests.LoadItems_IncludesSupportedFilesAndSkipsUnsupportedFiles), ShortcutRepositoryTests.LoadItems_IncludesSupportedFilesAndSkipsUnsupportedFiles),
            (nameof(LaunchItemSearchTests.Filter_RanksPrefixMatchesBeforeContainsMatches), LaunchItemSearchTests.Filter_RanksPrefixMatchesBeforeContainsMatches),
            (nameof(LaunchItemSearchTests.Filter_ReturnsAllItemsWhenQueryIsBlank), LaunchItemSearchTests.Filter_ReturnsAllItemsWhenQueryIsBlank)
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
