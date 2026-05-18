using System.Windows;

namespace LightLaunchpad.App;

public partial class TextInputDialog : Window
{
    public TextInputDialog(string title, string label, string initialValue)
    {
        InitializeComponent();
        Title = title;
        LabelTextBlock.Text = label;
        ValueTextBox.Text = initialValue;
        ValueTextBox.SelectAll();
        ValueTextBox.Focus();
    }

    public string Value => ValueTextBox.Text.Trim();

    private void Ok_Click(object sender, RoutedEventArgs e)
    {
        DialogResult = true;
    }
}
