using System.Windows;
using System.Windows.Controls;
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
            ((ComboBoxItem)DisplayModeComboBox.SelectedItem).Tag?.ToString() ?? LaunchpadDisplayModes.Launchpad);
        DialogResult = true;
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
}