namespace LightLaunchpad.Core.Tests;

internal static class Program
{
    private static int Main()
    {
        var tests = new (string Name, Action Run)[]
        {
            (nameof(LaunchItemTests.LaunchItem_StoresDisplayNameAndSourcePath), LaunchItemTests.LaunchItem_StoresDisplayNameAndSourcePath)
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
