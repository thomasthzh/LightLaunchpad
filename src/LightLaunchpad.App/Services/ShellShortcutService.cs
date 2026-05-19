using System.IO;
using System.Runtime.InteropServices;

namespace LightLaunchpad.App.Services;

public sealed class ShellShortcutService
{
    public static string? ResolveTarget(string shortcutPath)
    {
        try
        {
            var shellType = Type.GetTypeFromProgID("WScript.Shell");
            if (shellType is null) return null;

            object? shell = null;
            object? shortcut = null;
            try
            {
                shell = Activator.CreateInstance(shellType);
                if (shell is null) return null;

                shortcut = shellType.InvokeMember(
                    "CreateShortcut",
                    System.Reflection.BindingFlags.InvokeMethod,
                    null, shell, [shortcutPath]);
                if (shortcut is null) return null;

                var shortcutType = shortcut.GetType();
                return shortcutType.InvokeMember(
                    "TargetPath",
                    System.Reflection.BindingFlags.GetProperty,
                    null, shortcut, null) as string;
            }
            finally
            {
                if (shortcut is not null && Marshal.IsComObject(shortcut))
                    Marshal.FinalReleaseComObject(shortcut);
                if (shell is not null && Marshal.IsComObject(shell))
                    Marshal.FinalReleaseComObject(shell);
            }
        }
        catch
        {
            return null;
        }
    }

    public void CreateShortcut(string shortcutPath, string targetPath)
    {
        var shellType = Type.GetTypeFromProgID("WScript.Shell")
            ?? throw new InvalidOperationException("Windows Script Host is not available.");
        object? shell = null;
        object? shortcut = null;

        try
        {
            shell = Activator.CreateInstance(shellType);
            if (shell is null)
            {
                throw new InvalidOperationException("Could not create WScript.Shell.");
            }

            shortcut = shellType.InvokeMember(
                "CreateShortcut",
                System.Reflection.BindingFlags.InvokeMethod,
                null,
                shell,
                [shortcutPath]);

            if (shortcut is null)
            {
                throw new InvalidOperationException("Could not create shell shortcut.");
            }

            var shortcutType = shortcut.GetType();
            shortcutType.InvokeMember("TargetPath", System.Reflection.BindingFlags.SetProperty, null, shortcut, [targetPath]);
            shortcutType.InvokeMember("WorkingDirectory", System.Reflection.BindingFlags.SetProperty, null, shortcut, [Path.GetDirectoryName(targetPath) ?? string.Empty]);
            shortcutType.InvokeMember("Save", System.Reflection.BindingFlags.InvokeMethod, null, shortcut, []);
        }
        finally
        {
            if (shortcut is not null && Marshal.IsComObject(shortcut))
            {
                Marshal.FinalReleaseComObject(shortcut);
            }

            if (shell is not null && Marshal.IsComObject(shell))
            {
                Marshal.FinalReleaseComObject(shell);
            }
        }
    }
}
