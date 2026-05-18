using System.Windows;
using System.Windows.Controls;
using LightLaunchpad.Core.Settings;

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

        foreach (ComboBoxItem item in IconSizeComboBox.Items)
        {
            if (string.Equals(item.Content?.ToString(), settings.IconSize, StringComparison.OrdinalIgnoreCase))
            {
                IconSizeComboBox.SelectedItem = item;
                break;
            }
        }

        IconSizeComboBox.SelectedIndex = IconSizeComboBox.SelectedIndex < 0 ? 1 : IconSizeComboBox.SelectedIndex;
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
            _settings.IconQuality);
        DialogResult = true;
    }
}
