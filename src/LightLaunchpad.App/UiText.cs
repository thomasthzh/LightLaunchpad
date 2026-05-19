using LightLaunchpad.Core.Settings;

namespace LightLaunchpad.App;

public static class UiText
{
    public static string Pick(string? language, string english, string chinese)
    {
        return AppLanguages.IsChinese(language) ? chinese : english;
    }
}