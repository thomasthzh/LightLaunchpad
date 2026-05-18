using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.Core.Tests;

public sealed class LaunchItemTests
{
    public static void LaunchItem_StoresDisplayNameAndSourcePath()
    {
        var item = new LaunchItem("Code", @"C:\Tools\Code.lnk", null, LaunchItemKind.Shortcut);

        TestAssert.Equal("Code", item.DisplayName);
        TestAssert.Equal(@"C:\Tools\Code.lnk", item.SourcePath);
        TestAssert.Equal(LaunchItemKind.Shortcut, item.Kind);
    }
}
