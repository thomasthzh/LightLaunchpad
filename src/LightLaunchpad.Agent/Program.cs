using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;
using LightLaunchpad.Core.Activation;
using LightLaunchpad.Core.Hotkeys;
using LightLaunchpad.Core.Settings;

namespace LightLaunchpad.Agent;

internal static class Program
{
    private const string AppName = "LightLaunchpad";

    [STAThread]
    private static int Main()
    {
        using var mutex = new Mutex(true, "LightLaunchpad.Agent.SingleInstance", out var createdNew);
        if (!createdNew)
        {
            return 0;
        }

        var settings = LoadSettings();
        using var app = new AgentApplication(settings);
        return app.Run();
    }

    private static AppSettings LoadSettings()
    {
        var appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        var settingsPath = Path.Combine(appData, AppName, "settings.json");
        var userProfile = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        return new SettingsService(settingsPath, userProfile).Load();
    }
}

internal sealed class AgentApplication : IDisposable
{
    private const int HotkeyId = 0x4C41;
    private const uint WmHotkey = 0x0312;
    private const uint WmCommand = 0x0111;
    private const uint WmDestroy = 0x0002;
    private const uint WmTray = 0x8001;
    private const uint WmLButtonUp = 0x0202;
    private const uint WmRButtonUp = 0x0205;
    private const uint WmContextMenu = 0x007B;
    private const uint NifMessage = 0x00000001;
    private const uint NifIcon = 0x00000002;
    private const uint NifTip = 0x00000004;
    private const uint NimAdd = 0x00000000;
    private const uint NimDelete = 0x00000002;
    private const uint MfString = 0x00000000;
    private const uint MfSeparator = 0x00000800;
    private const uint TpmRightButton = 0x0002;
    private const int TrayOpenCommand = 1001;
    private const int TrayExitCommand = 1002;
    private const string WindowClassName = "LightLaunchpadAgentWindow";

    private readonly AppSettings _settings;
    private readonly HostedUiProcess _hostedUi = new();
    private readonly WindowProc _windowProc;
    private IntPtr _hwnd;
    private bool _disposed;
    private bool _trayCreated;

    public AgentApplication(AppSettings settings)
    {
        _settings = settings;
        _windowProc = WndProc;
    }

    public int Run()
    {
        CreateMessageWindow();
        RegisterHotkey();
        AddTrayIcon();

        while (GetMessage(out var message, IntPtr.Zero, 0, 0) > 0)
        {
            TranslateMessage(ref message);
            DispatchMessage(ref message);
        }

        return 0;
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _hostedUi.Dispose();
        if (_hwnd != IntPtr.Zero)
        {
            UnregisterHotKey(_hwnd, HotkeyId);
            RemoveTrayIcon();
            DestroyWindow(_hwnd);
            _hwnd = IntPtr.Zero;
        }
    }

    private void CreateMessageWindow()
    {
        var instance = GetModuleHandle(null);
        var wndClass = new WndClassEx
        {
            Size = (uint)Marshal.SizeOf<WndClassEx>(),
            Instance = instance,
            ClassName = WindowClassName,
            WindowProc = Marshal.GetFunctionPointerForDelegate(_windowProc)
        };

        var classAtom = RegisterClassEx(ref wndClass);
        if (classAtom == 0)
        {
            throw new InvalidOperationException($"Could not register LightLaunchpad agent window class. Win32={Marshal.GetLastWin32Error()}.");
        }

        _hwnd = CreateWindowEx(
            0,
            new IntPtr(classAtom),
            "LightLaunchpad Agent",
            0x80000000,
            0,
            0,
            0,
            0,
            IntPtr.Zero,
            IntPtr.Zero,
            instance,
            IntPtr.Zero);

        if (_hwnd == IntPtr.Zero)
        {
            throw new InvalidOperationException($"Could not create LightLaunchpad agent message window. Win32={Marshal.GetLastWin32Error()}.");
        }
    }

    private void RegisterHotkey()
    {
        try
        {
            var gesture = HotkeyGesture.Parse(_settings.Hotkey);
            if (!RegisterHotKey(_hwnd, HotkeyId, BuildModifiers(gesture), char.ToUpperInvariant(gesture.Key)))
            {
                ShowMessage($"Could not register hotkey '{_settings.Hotkey}'. You can still open LightLaunchpad from the tray icon.");
            }
        }
        catch (Exception ex)
        {
            ShowMessage($"Could not register hotkey '{_settings.Hotkey}': {ex.Message}");
        }
    }

    private void AddTrayIcon()
    {
        var icon = LoadAgentIcon();
        var data = CreateNotifyIconData(icon);
        _trayCreated = Shell_NotifyIcon(NimAdd, ref data);
    }

    private NotifyIconData CreateNotifyIconData(IntPtr icon)
    {
        return new NotifyIconData
        {
            Size = (uint)Marshal.SizeOf<NotifyIconData>(),
            WindowHandle = _hwnd,
            Id = 1,
            Flags = NifMessage | NifIcon | NifTip,
            CallbackMessage = WmTray,
            Icon = icon,
            Tip = "LightLaunchpad",
            Info = string.Empty,
            InfoTitle = string.Empty
        };
    }

    private void RemoveTrayIcon()
    {
        if (!_trayCreated)
        {
            return;
        }

        var data = CreateNotifyIconData(IntPtr.Zero);
        Shell_NotifyIcon(NimDelete, ref data);
        _trayCreated = false;
    }

    private IntPtr WndProc(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam)
    {
        if (message == WmHotkey && wParam.ToInt32() == HotkeyId)
        {
            _hostedUi.Show();
            return IntPtr.Zero;
        }

        if (message == WmCommand)
        {
            HandleCommand(wParam);
            return IntPtr.Zero;
        }

        if (message == WmTray)
        {
            var trayMessage = unchecked((uint)lParam.ToInt64());
            if (trayMessage == WmLButtonUp)
            {
                _hostedUi.Show();
                return IntPtr.Zero;
            }

            if (trayMessage == WmRButtonUp || trayMessage == WmContextMenu)
            {
                ShowTrayMenu();
                return IntPtr.Zero;
            }
        }

        if (message == WmDestroy)
        {
            PostQuitMessage(0);
            return IntPtr.Zero;
        }

        return DefWindowProc(hwnd, message, wParam, lParam);
    }

    private void HandleCommand(IntPtr wParam)
    {
        var command = wParam.ToInt32() & 0xffff;
        if (command == TrayOpenCommand)
        {
            _hostedUi.Show();
            return;
        }

        if (command == TrayExitCommand)
        {
            DestroyWindow(_hwnd);
        }
    }

    private void ShowTrayMenu()
    {
        var menu = CreatePopupMenu();
        if (menu == IntPtr.Zero)
        {
            return;
        }

        try
        {
            AppendMenu(menu, MfString, (UIntPtr)TrayOpenCommand, "Open launchpad");
            AppendMenu(menu, MfSeparator, UIntPtr.Zero, null);
            AppendMenu(menu, MfString, (UIntPtr)TrayExitCommand, "Exit");
            GetCursorPos(out var point);
            SetForegroundWindow(_hwnd);
            TrackPopupMenu(menu, TpmRightButton, point.X, point.Y, 0, _hwnd, IntPtr.Zero);
        }
        finally
        {
            DestroyMenu(menu);
        }
    }

    private static uint BuildModifiers(HotkeyGesture gesture)
    {
        var modifiers = 0u;
        if (gesture.Alt) modifiers |= 0x0001;
        if (gesture.Control) modifiers |= 0x0002;
        if (gesture.Shift) modifiers |= 0x0004;
        if (gesture.Windows) modifiers |= 0x0008;
        return modifiers;
    }

    private static IntPtr LoadAgentIcon()
    {
        var iconPath = Path.Combine(AppContext.BaseDirectory, "Assets", "LightLaunchpad.ico");
        if (File.Exists(iconPath))
        {
            var icon = LoadImage(IntPtr.Zero, iconPath, 1, 0, 0, 0x00000010 | 0x00000040 | 0x00008000);
            if (icon != IntPtr.Zero)
            {
                return icon;
            }
        }

        return LoadIcon(IntPtr.Zero, (IntPtr)32512);
    }

    private static void ShowMessage(string message)
    {
        MessageBox(IntPtr.Zero, message, "LightLaunchpad Agent", 0x00000040);
    }

    private delegate IntPtr WindowProc(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct WndClassEx
    {
        public uint Size;
        public uint Style;
        public IntPtr WindowProc;
        public int ClassExtra;
        public int WindowExtra;
        public IntPtr Instance;
        public IntPtr Icon;
        public IntPtr Cursor;
        public IntPtr Background;
        public string? MenuName;
        public string ClassName;
        public IntPtr SmallIcon;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Message
    {
        public IntPtr Hwnd;
        public uint Msg;
        public IntPtr WParam;
        public IntPtr LParam;
        public uint Time;
        public Point Point;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Point
    {
        public int X;
        public int Y;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct NotifyIconData
    {
        public uint Size;
        public IntPtr WindowHandle;
        public uint Id;
        public uint Flags;
        public uint CallbackMessage;
        public IntPtr Icon;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string Tip;
        public uint State;
        public uint StateMask;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string Info;
        public uint TimeoutOrVersion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string InfoTitle;
        public uint InfoFlags;
        public Guid GuidItem;
        public IntPtr BalloonIcon;
    }

    [DllImport("user32.dll", SetLastError = true)]
    private static extern ushort RegisterClassEx(ref WndClassEx wndClass);

    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr CreateWindowEx(
        uint extendedStyle,
        IntPtr className,
        string windowName,
        uint style,
        int x,
        int y,
        int width,
        int height,
        IntPtr parent,
        IntPtr menu,
        IntPtr instance,
        IntPtr param);

    [DllImport("user32.dll")]
    private static extern IntPtr DefWindowProc(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool DestroyWindow(IntPtr hwnd);

    [DllImport("user32.dll")]
    private static extern sbyte GetMessage(out Message message, IntPtr hwnd, uint minFilter, uint maxFilter);

    [DllImport("user32.dll")]
    private static extern bool TranslateMessage(ref Message message);

    [DllImport("user32.dll")]
    private static extern IntPtr DispatchMessage(ref Message message);

    [DllImport("user32.dll")]
    private static extern void PostQuitMessage(int exitCode);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool RegisterHotKey(IntPtr hwnd, int id, uint modifiers, int virtualKey);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool UnregisterHotKey(IntPtr hwnd, int id);

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern bool Shell_NotifyIcon(uint message, ref NotifyIconData data);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr GetModuleHandle(string? moduleName);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr LoadImage(IntPtr instance, string name, uint type, int cx, int cy, uint flags);

    [DllImport("user32.dll")]
    private static extern IntPtr LoadIcon(IntPtr instance, IntPtr iconName);

    [DllImport("user32.dll")]
    private static extern IntPtr CreatePopupMenu();

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern bool AppendMenu(IntPtr menu, uint flags, UIntPtr idNewItem, string? newItem);

    [DllImport("user32.dll")]
    private static extern bool DestroyMenu(IntPtr menu);

    [DllImport("user32.dll")]
    private static extern bool TrackPopupMenu(IntPtr menu, uint flags, int x, int y, int reserved, IntPtr hwnd, IntPtr rect);

    [DllImport("user32.dll")]
    private static extern bool GetCursorPos(out Point point);

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hwnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int MessageBox(IntPtr hwnd, string text, string caption, uint type);
}

internal sealed class HostedUiProcess : IDisposable
{
    private EventWaitHandle? _showEvent;
    private EventWaitHandle? _exitEvent;
    private Process? _process;
    private string? _showEventName;
    private string? _exitEventName;

    public void Show()
    {
        if (IsRunning())
        {
            _showEvent?.Set();
            return;
        }

        Reset();
        _showEventName = BuildEventName("Show");
        _exitEventName = BuildEventName("Exit");
        _showEvent = new EventWaitHandle(false, EventResetMode.AutoReset, _showEventName);
        _exitEvent = new EventWaitHandle(false, EventResetMode.AutoReset, _exitEventName);

        var uiExecutable = ResolveUiExecutable();
        if (uiExecutable is null)
        {
            MessageBox(IntPtr.Zero, "Could not find LightLaunchpad.App.exe.", "LightLaunchpad Agent", 0x00000010);
            return;
        }

        var args = LaunchpadActivationContext.CreateHostedUiArguments(
            _showEventName,
            _exitEventName,
            showImmediately: true,
            exitOnHide: true);
        var startInfo = new ProcessStartInfo(uiExecutable)
        {
            Arguments = string.Join(" ", args),
            WorkingDirectory = Path.GetDirectoryName(uiExecutable) ?? AppContext.BaseDirectory,
            UseShellExecute = false
        };

        _process = Process.Start(startInfo);
        _showEvent.Set();
    }

    public void Dispose()
    {
        if (IsRunning())
        {
            _exitEvent?.Set();
            if (!_process!.WaitForExit(2000))
            {
                _process.Kill(entireProcessTree: true);
            }
        }

        Reset();
    }

    private bool IsRunning()
    {
        try
        {
            return _process is not null && !_process.HasExited;
        }
        catch
        {
            return false;
        }
    }

    private void Reset()
    {
        _showEvent?.Dispose();
        _exitEvent?.Dispose();
        _process?.Dispose();
        _showEvent = null;
        _exitEvent = null;
        _process = null;
        _showEventName = null;
        _exitEventName = null;
    }

    private static string BuildEventName(string suffix)
    {
        return $"Local\\LightLaunchpadAgent_{Environment.ProcessId}_{Guid.NewGuid():N}_{suffix}";
    }

    private static string? ResolveUiExecutable()
    {
        var direct = Path.Combine(AppContext.BaseDirectory, "LightLaunchpad.App.exe");
        if (File.Exists(direct))
        {
            return direct;
        }

        var current = new DirectoryInfo(AppContext.BaseDirectory);
        while (current is not null)
        {
            foreach (var configuration in new[] { "Debug", "Release" })
            {
                var candidate = Path.Combine(
                    current.FullName,
                    "src",
                    "LightLaunchpad.App",
                    "bin",
                    configuration,
                    "net8.0-windows",
                    "LightLaunchpad.App.exe");
                if (File.Exists(candidate))
                {
                    return candidate;
                }
            }

            current = current.Parent;
        }

        return null;
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int MessageBox(IntPtr hwnd, string text, string caption, uint type);
}
