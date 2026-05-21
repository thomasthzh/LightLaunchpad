using LightLaunchpad.Core.Activation;

namespace LightLaunchpad.Core.Tests;

public sealed class LaunchpadActivationContextTests
{
    public static void Parse_ReadsHostedUiFlagsAndNamedEvents()
    {
        var context = LaunchpadActivationContext.Parse([
            "--hosted-ui",
            "--show-event=Local\\LightLaunchpad_Show",
            "--exit-event=\"Local\\LightLaunchpad_Exit\"",
            "--show-immediately",
            "--exit-on-hide"
        ]);

        TestAssert.True(context.IsHostedUi);
        TestAssert.Equal("Local\\LightLaunchpad_Show", context.ShowEventName);
        TestAssert.Equal("Local\\LightLaunchpad_Exit", context.ExitEventName);
        TestAssert.True(context.ShowImmediately);
        TestAssert.True(context.ExitOnHide);
    }

    public static void CreateHostedUiArguments_QuotesEventNamesForProcessLaunch()
    {
        var args = LaunchpadActivationContext.CreateHostedUiArguments(
            "Local\\LightLaunchpad Agent Show",
            "Local\\LightLaunchpad Agent Exit",
            showImmediately: true,
            exitOnHide: true);

        TestAssert.SequenceEqual(
            [
                "--hosted-ui",
                "--show-event=\"Local\\LightLaunchpad Agent Show\"",
                "--exit-event=\"Local\\LightLaunchpad Agent Exit\"",
                "--show-immediately",
                "--exit-on-hide"
            ],
            args);
    }
}