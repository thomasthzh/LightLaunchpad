using System.Windows;
using System.Windows.Controls;
using System.Globalization;
using LightLaunchpad.Core.Settings;

using WpfComboBox = System.Windows.Controls.ComboBox;

namespace LightLaunchpad.App;

public partial class SettingsWindow : Window
{
    private readonly AppSettings _settings;

    public SettingsWindow(AppSettings settings)
    {
        _settings = settings;
        InitializeComponent();
        FolderTextBox.Text = settings.LaunchpadFolder;
        HotkeyTextBox.Text = settings.Hotkey;
        StartWithWindowsCheckBox.IsChecked = settings.StartWithWindows;

        SelectByContent(IconSizeComboBox, settings.IconSize);
        IconSizeComboBox.SelectedIndex = IconSizeComboBox.SelectedIndex < 0 ? 1 : IconSizeComboBox.SelectedIndex;
        SelectByTag(DisplayModeComboBox, LaunchpadDisplayModes.Normalize(settings.DisplayMode));
        DisplayModeComboBox.SelectedIndex = DisplayModeComboBox.SelectedIndex < 0 ? 0 : DisplayModeComboBox.SelectedIndex;
        SelectByTag(LanguageComboBox, AppLanguages.Normalize(settings.Language));
        LanguageComboBox.SelectedIndex = LanguageComboBox.SelectedIndex < 0 ? 0 : LanguageComboBox.SelectedIndex;
        SpotlightWidthTextBox.Text = FormatNumber(settings.SpotlightWidth);
        SpotlightHeightTextBox.Text = FormatNumber(settings.SpotlightHeight);
        AppSpacingTextBox.Text = FormatNumber(settings.AppSpacing);
        MouseSensitivityTextBox.Text = FormatNumber(settings.MouseSensitivity);
        ApplyLanguage(SelectedLanguage);
    }

    public AppSettings? UpdatedSettings { get; private set; }

    private void Save_Click(object sender, RoutedEventArgs e)
    {
        UpdatedSettings = new AppSettings(
            FolderTextBox.Text.Trim(),
            HotkeyTextBox.Text.Trim(),
            StartWithWindowsCheckBox.IsChecked == true,
            ((ComboBoxItem)IconSizeComboBox.SelectedItem).Content?.ToString() ?? "Medium",
            _settings.ViewMode,
            _settings.IconQuality,
            ((ComboBoxItem)DisplayModeComboBox.SelectedItem).Tag?.ToString() ?? LaunchpadDisplayModes.Launchpad,
            SelectedLanguage,
            AppSettingLimits.NormalizeSpotlightWidth(ParseDouble(SpotlightWidthTextBox.Text, _settings.SpotlightWidth)),
            AppSettingLimits.NormalizeSpotlightHeight(ParseDouble(SpotlightHeightTextBox.Text, _settings.SpotlightHeight)),
            AppSettingLimits.NormalizeAppSpacing(ParseDouble(AppSpacingTextBox.Text, _settings.AppSpacing)),
            AppSettingLimits.NormalizeMouseSensitivity(ParseDouble(MouseSensitivityTextBox.Text, _settings.MouseSensitivity)));
        DialogResult = true;
    }

    private string SelectedLanguage =>
        ((ComboBoxItem?)LanguageComboBox.SelectedItem)?.Tag?.ToString() ?? AppLanguages.English;

    private void LanguageComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (!IsInitialized)
        {
            return;
        }

        ApplyLanguage(SelectedLanguage);
    }

    private void ApplyLanguage(string language)
    {
        Title = UiText.Pick(language, "LightLaunchpad Settings", "LightLaunchpad 设置");
        FolderLabel.Text = UiText.Pick(language, "Launchpad folder", "启动台文件夹");
        HotkeyLabel.Text = UiText.Pick(language, "Hotkey", "快捷键");
        IconSizeLabel.Text = UiText.Pick(language, "Icon size", "图标大小");
        DisplayModeLabel.Text = UiText.Pick(language, "Open mode", "打开方式");
        LanguageLabel.Text = UiText.Pick(language, "Language", "语言");
        SpotlightSizeLabel.Text = UiText.Pick(language, "Spotlight size", "聚焦窗口大小");
        AppSpacingLabel.Text = UiText.Pick(language, "App spacing", "APP 间距");
        MouseSensitivityLabel.Text = UiText.Pick(language, "Mouse sensitivity", "鼠标灵敏度");
        StartWithWindowsCheckBox.Content = UiText.Pick(language, "Start with Windows", "随 Windows 启动");
        CancelButton.Content = UiText.Pick(language, "Cancel", "取消");
        SaveButton.Content = UiText.Pick(language, "Save", "保存");

        SetComboContent(DisplayModeComboBox, LaunchpadDisplayModes.Launchpad, UiText.Pick(language, "Launchpad", "启动台"));
        SetComboContent(DisplayModeComboBox, LaunchpadDisplayModes.Spotlight, UiText.Pick(language, "Spotlight", "聚焦"));
    }

    private static void SelectByContent(WpfComboBox comboBox, string value)
    {
        foreach (ComboBoxItem item in comboBox.Items)
        {
            if (string.Equals(item.Content?.ToString(), value, StringComparison.OrdinalIgnoreCase))
            {
                comboBox.SelectedItem = item;
                return;
            }
        }
    }

    private static void SelectByTag(WpfComboBox comboBox, string value)
    {
        foreach (ComboBoxItem item in comboBox.Items)
        {
            if (string.Equals(item.Tag?.ToString(), value, StringComparison.OrdinalIgnoreCase))
            {
                comboBox.SelectedItem = item;
                return;
            }
        }
    }

    private static void SetComboContent(WpfComboBox comboBox, string tag, string content)
    {
        foreach (ComboBoxItem item in comboBox.Items)
        {
            if (string.Equals(item.Tag?.ToString(), tag, StringComparison.OrdinalIgnoreCase))
            {
                item.Content = content;
                return;
            }
        }
    }

    private static string FormatNumber(double value)
    {
        return value.ToString("0.##", CultureInfo.InvariantCulture);
    }

    private static double ParseDouble(string value, double fallback)
    {
        return double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var parsed)
            ? parsed
            : fallback;
    }
}