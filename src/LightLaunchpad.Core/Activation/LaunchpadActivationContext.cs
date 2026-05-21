namespace LightLaunchpad.Core.Activation;

public sealed record LaunchpadActivationContext(
    bool IsHostedUi,
    string? ShowEventName,
    string? ExitEventName,
    bool ShowImmediately,
    bool ExitOnHide)
{
    public static LaunchpadActivationContext Standalone { get; } = new(
        IsHostedUi: false,
        ShowEventName: null,
        ExitEventName: null,
        ShowImmediately: false,
        ExitOnHide: false);

    public static LaunchpadActivationContext Parse(IEnumerable<string> args)
    {
        var isHostedUi = false;
        var showImmediately = false;
        var exitOnHide = false;
        string? showEventName = null;
        string? exitEventName = null;

        foreach (var arg in args)
        {
            if (arg.Equals("--hosted-ui", StringComparison.OrdinalIgnoreCase))
            {
                isHostedUi = true;
                continue;
            }

            if (arg.Equals("--show-immediately", StringComparison.OrdinalIgnoreCase))
            {
                showImmediately = true;
                continue;
            }

            if (arg.Equals("--exit-on-hide", StringComparison.OrdinalIgnoreCase))
            {
                exitOnHide = true;
                continue;
            }

            if (TryReadValue(arg, "--show-event", out var value))
            {
                showEventName = value;
                continue;
            }

            if (TryReadValue(arg, "--exit-event", out value))
            {
                exitEventName = value;
            }
        }

        return new LaunchpadActivationContext(
            isHostedUi,
            showEventName,
            exitEventName,
            showImmediately,
            exitOnHide);
    }

    public static IReadOnlyList<string> CreateHostedUiArguments(
        string showEventName,
        string exitEventName,
        bool showImmediately,
        bool exitOnHide)
    {
        var args = new List<string>
        {
            "--hosted-ui",
            $"--show-event=\"{showEventName}\"",
            $"--exit-event=\"{exitEventName}\""
        };

        if (showImmediately)
        {
            args.Add("--show-immediately");
        }

        if (exitOnHide)
        {
            args.Add("--exit-on-hide");
        }

        return args;
    }

    private static bool TryReadValue(string candidate, string key, out string? value)
    {
        value = null;
        if (!candidate.StartsWith(key, StringComparison.OrdinalIgnoreCase)
            || candidate.Length <= key.Length
            || candidate[key.Length] != '=')
        {
            return false;
        }

        value = candidate[(key.Length + 1)..].Trim().Trim('"');
        return !string.IsNullOrWhiteSpace(value);
    }
}