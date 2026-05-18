using LightLaunchpad.Core.Import;

namespace LightLaunchpad.Core.Tests;

public sealed class VuiImportParserTests
{
    public static void Parse_ExtractsDecodedPathsAndSkipsEmptyValues()
    {
        const string content = """
            ##Inventory
            Object{d1("C:\Program%20Files\App\App.exe"),d2(""),d3("NULL"),d4("C:\Tools\Tool.lnk%20")}
            ###
            """;

        var results = VuiImportParser.Parse(content).ToList();

        TestAssert.SequenceEqual(
            new[] { @"C:\Program Files\App\App.exe", @"C:\Tools\Tool.lnk" },
            results.Select(result => result.Path));
    }

    public static void Parse_DeduplicatesNormalizedPaths()
    {
        const string content = """
            d1("C:\Tools\App.exe"),d2("c:\tools\app.exe"),d3("C:\Tools\Other.url")
            """;

        var results = VuiImportParser.Parse(content).ToList();

        TestAssert.SequenceEqual(
            new[] { @"C:\Tools\App.exe", @"C:\Tools\Other.url" },
            results.Select(result => result.Path));
    }
}
