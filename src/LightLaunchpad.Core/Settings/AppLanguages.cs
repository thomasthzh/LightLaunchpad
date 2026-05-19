namespace LightLaunchpad.Core.Settings;

public static class AppLanguages
{
    public const string English = "English";
    public const string Chinese = "Chinese";

    public static string Normalize(string? value)
    {
        if (string.Equals(value, Chinese, StringComparison.OrdinalIgnoreCase)
            || value == "中文"
            || value == "中")
        {
            return Chinese;
        }

        return English;
    }

    public static bool IsChinese(string? value)
    {
        return string.Equals(Normalize(value), Chinese, StringComparison.OrdinalIgnoreCase);
    }
}