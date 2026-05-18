namespace LightLaunchpad.Core.Tests;

internal static class TestPaths
{
    public static string CreateTempDirectory()
    {
        var path = Path.Combine(Path.GetTempPath(), "LightLaunchpad.Tests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(path);
        return path;
    }
}
