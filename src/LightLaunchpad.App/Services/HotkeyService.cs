using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using LightLaunchpad.Core.Hotkeys;

namespace LightLaunchpad.App.Services;

public sealed class HotkeyService : IDisposable
{
    private const int WmHotkey = 0x0312;
    private const int HotkeyId = 0x4C44;
    private readonly Window _window;
    private HwndSource? _source;
    private bool _registered;

    public HotkeyService(Window window)
    {
        _window = window;
    }

    public event EventHandler? Pressed;

    public bool Register(string gestureText)
    {
        Unregister();

        var gesture = HotkeyGesture.Parse(gestureText);
        var handle = new WindowInteropHelper(_window).EnsureHandle();
        _source = HwndSource.FromHwnd(handle);
        _source?.AddHook(WndProc);

        _registered = RegisterHotKey(handle, HotkeyId, BuildModifiers(gesture), char.ToUpperInvariant(gesture.Key));
        return _registered;
    }

    public void Unregister()
    {
        var handle = new WindowInteropHelper(_window).Handle;
        if (_registered && handle != IntPtr.Zero)
        {
            UnregisterHotKey(handle, HotkeyId);
            _registered = false;
        }

        _source?.RemoveHook(WndProc);
        _source = null;
    }

    public void Dispose()
    {
        Unregister();
    }

    private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (msg == WmHotkey && wParam.ToInt32() == HotkeyId)
        {
            Pressed?.Invoke(this, EventArgs.Empty);
            handled = true;
        }

        return IntPtr.Zero;
    }

    private static uint BuildModifiers(HotkeyGesture gesture)
    {
        var modifiers = 0u;
        if (gesture.Alt)
        {
            modifiers |= 0x0001;
        }

        if (gesture.Control)
        {
            modifiers |= 0x0002;
        }

        if (gesture.Shift)
        {
            modifiers |= 0x0004;
        }

        if (gesture.Windows)
        {
            modifiers |= 0x0008;
        }

        return modifiers;
    }

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool RegisterHotKey(IntPtr hWnd, int id, uint fsModifiers, int vk);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool UnregisterHotKey(IntPtr hWnd, int id);
}
