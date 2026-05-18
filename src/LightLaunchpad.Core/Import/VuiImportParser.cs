using System.Text.RegularExpressions;

namespace LightLaunchpad.Core.Import;

public static partial class VuiImportParser
{
    public static IReadOnlyList<VuiImportCandidate> Parse(string content)
    {
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var results = new List<VuiImportCandidate>();

        foreach (Match match in EntryRegex().Matches(content))
        {
            var rawValue = match.Groups["path"].Value;
            var decoded = Uri.UnescapeDataString(rawValue).Trim();

            if (string.IsNullOrWhiteSpace(decoded) ||
                string.Equals(decoded, "NULL", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            var normalized = NormalizePath(decoded);
            if (seen.Add(normalized))
            {
                results.Add(new VuiImportCandidate(decoded));
            }
        }

        return results;
    }

    private static string NormalizePath(string path)
    {
        try
        {
            return System.IO.Path.GetFullPath(path);
        }
        catch (ArgumentException)
        {
            return path.Trim();
        }
        catch (NotSupportedException)
        {
            return path.Trim();
        }
    }

    [GeneratedRegex("""d\d+\("(?<path>[^"]*)"\)""", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant)]
    private static partial Regex EntryRegex();
}
