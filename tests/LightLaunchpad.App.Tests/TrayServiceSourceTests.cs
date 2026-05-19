using System.IO;

namespace LightLaunchpad.App.Tests;

public sealed class TrayServiceSourceTests
{
    public static void TrayMenu_DoesNotExposeVuiImport()
    {
        var source = File.ReadAllText(FindTrayService());

        TestAssert.DoesNotContain("Import .vui files", source);
        TestAssert.DoesNotContain("importVui", source);
    }

    private static string FindTrayService()
    {
        var current = new DirectoryInfo(Environment.CurrentDirectory);
        while (current is not null)
        {
            var candidate = Path.Combine(current.FullName, "src", "LightLaunchpad.App", "Services", "TrayService.cs");
            if (File.Exists(candidate))
            {
                return candidate;
            }

            current = current.Parent;
        }

        throw new FileNotFoundException("Could not find TrayService.cs from the test working directory.");
    }
}