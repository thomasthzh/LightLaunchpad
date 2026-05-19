using LightLaunchpad.Core.Settings;

namespace LightLaunchpad.Core.Tests;

public sealed class AppStoragePathsTests
{
    public static void GetIconCacheDirectory_UsesDocumentsLightLaunchpadIconsFolder()
    {
        var path = AppStoragePaths.GetIconCacheDirectory(@"C:\Users\Alice\Documents");

        TestAssert.Equal(@"C:\Users\Alice\Documents\LightLaunchpad\icons", path);
    }
}
