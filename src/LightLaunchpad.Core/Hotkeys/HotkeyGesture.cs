namespace LightLaunchpad.Core.Hotkeys;

public sealed record HotkeyGesture(
    bool Alt,
    bool Control,
    bool Shift,
    bool Windows,
    char Key)
{
    public static HotkeyGesture Parse(string value)
    {
        var parts = value
            .Split('+', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Select(part => part.ToUpperInvariant())
            .ToArray();

        if (parts.Length < 2)
        {
            throw new FormatException("Hotkey must include at least one modifier and one key.");
        }

        var keyPart = parts[^1];
        if (keyPart.Length != 1 || !char.IsLetterOrDigit(keyPart[0]))
        {
            throw new FormatException("Hotkey key must be a single letter or digit.");
        }

        return new HotkeyGesture(
            Alt: parts[..^1].Contains("ALT"),
            Control: parts[..^1].Contains("CTRL") || parts[..^1].Contains("CONTROL"),
            Shift: parts[..^1].Contains("SHIFT"),
            Windows: parts[..^1].Contains("WIN") || parts[..^1].Contains("WINDOWS"),
            Key: keyPart[0]);
    }

    public override string ToString()
    {
        var modifiers = new List<string>();
        if (Control)
        {
            modifiers.Add("Ctrl");
        }

        if (Alt)
        {
            modifiers.Add("Alt");
        }

        if (Shift)
        {
            modifiers.Add("Shift");
        }

        if (Windows)
        {
            modifiers.Add("Win");
        }

        modifiers.Add(Key.ToString());
        return string.Join("+", modifiers);
    }
}
