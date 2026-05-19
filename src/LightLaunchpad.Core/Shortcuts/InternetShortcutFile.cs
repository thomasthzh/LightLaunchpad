namespace LightLaunchpad.Core.Shortcuts;

public sealed record InternetShortcutInfo(
    string? Url,
    string? IconFile,
    int IconIndex);

public static class InternetShortcutFile
{
    public static InternetShortcutInfo Read(string path)
    {
        string? url = null;
        string? iconFile = null;
        var iconIndex = 0;

        try
        {
            foreach (var line in File.ReadLines(path))
            {
                var separator = line.IndexOf('=');
                if (separator <= 0)
                {
                    continue;
                }

                var key = line[..separator].Trim();
                var value = CleanValue(line[(separator + 1)..]);

                if (key.Equals("URL", StringComparison.OrdinalIgnoreCase))
                {
                    url = value;
                    continue;
                }

                if (key.Equals("IconFile", StringComparison.OrdinalIgnoreCase))
                {
                    iconFile = string.IsNullOrWhiteSpace(value)
                        ? null
                        : Environment.ExpandEnvironmentVariables(value);
                    continue;
                }

                if (key.Equals("IconIndex", StringComparison.OrdinalIgnoreCase)
                    && int.TryParse(value, out var parsedIndex))
                {
                    iconIndex = parsedIndex;
                }
            }
        }
        catch
        {
            // Ignore unreadable internet shortcuts and let callers fall back.
        }

        return new InternetShortcutInfo(url, iconFile, iconIndex);
    }

    private static string CleanValue(string value)
    {
        return value.Trim().Trim('"');
    }
}
