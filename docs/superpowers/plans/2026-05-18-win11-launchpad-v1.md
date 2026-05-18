# Win11 Lightweight Launchpad V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a lightweight Windows 11 launchpad that opens with `Alt+D`, shows a searchable grid of shortcuts from `%USERPROFILE%\Launchpad`, launches selected items, and can import existing `.vui` path lists after the core app works.

**Architecture:** Create a .NET 8 WPF app plus a testable core library. Keep Win32, file-system, shortcut parsing, search, settings, and `.vui` import logic in small services, with the WPF layer depending on view models and service interfaces.

**Tech Stack:** C# 12, .NET 8, WPF, xUnit, Windows Forms tray icon, Win32 P/Invoke, COM ShellLink for `.lnk` creation/resolution.

---

## File Structure

- Create `LightLaunchpad.sln`: solution containing app, core library, and tests.
- Create `src/LightLaunchpad.Core/`: testable app logic.
- Create `src/LightLaunchpad.App/`: WPF shell, tray icon, hotkey, windows, resources.
- Create `tests/LightLaunchpad.Core.Tests/`: unit tests for search, settings, shortcut scanning, hotkey parsing, and `.vui` import.
- Modify `docs/superpowers/specs/2026-05-18-win11-launchpad-design.md` only if implementation reveals a real spec mismatch.

## Task 1: Repository And Build Skeleton

**Files:**
- Create: `global.json`
- Create: `.gitignore`
- Create: `LightLaunchpad.sln`
- Create: `src/LightLaunchpad.Core/LightLaunchpad.Core.csproj`
- Create: `src/LightLaunchpad.App/LightLaunchpad.App.csproj`
- Create: `tests/LightLaunchpad.Core.Tests/LightLaunchpad.Core.Tests.csproj`

- [ ] **Step 1: Check build tooling**

Run:

```powershell
dotnet --info
dotnet --list-sdks
```

Expected: if no SDK is installed, install a local .NET 8 SDK under `.dotnet` using Microsoft `dotnet-install.ps1`; do not use Python.

- [ ] **Step 2: Create project files**

Create SDK-style projects targeting `net8.0-windows`, with WPF enabled only in `LightLaunchpad.App`.

- [ ] **Step 3: Add a placeholder failing test**

Create an xUnit test that references a missing `LaunchItem` type:

```csharp
using LightLaunchpad.Core.Shortcuts;

namespace LightLaunchpad.Core.Tests;

public sealed class LaunchItemTests
{
    [Fact]
    public void LaunchItem_StoresDisplayNameAndSourcePath()
    {
        var item = new LaunchItem("Code", @"C:\Tools\Code.lnk", null, LaunchItemKind.Shortcut);

        Assert.Equal("Code", item.DisplayName);
        Assert.Equal(@"C:\Tools\Code.lnk", item.SourcePath);
        Assert.Equal(LaunchItemKind.Shortcut, item.Kind);
    }
}
```

- [ ] **Step 4: Verify RED**

Run:

```powershell
dotnet test tests/LightLaunchpad.Core.Tests/LightLaunchpad.Core.Tests.csproj
```

Expected: fails because `LaunchItem` does not exist.

- [ ] **Step 5: Implement minimal core model**

Create:

```csharp
namespace LightLaunchpad.Core.Shortcuts;

public enum LaunchItemKind
{
    Shortcut,
    Url,
    Executable
}

public sealed record LaunchItem(
    string DisplayName,
    string SourcePath,
    string? TargetPath,
    LaunchItemKind Kind);
```

- [ ] **Step 6: Verify GREEN and commit**

Run `dotnet test`, then commit skeleton and model.

## Task 2: Settings And Hotkey Parsing

**Files:**
- Create: `src/LightLaunchpad.Core/Settings/AppSettings.cs`
- Create: `src/LightLaunchpad.Core/Settings/SettingsService.cs`
- Create: `src/LightLaunchpad.Core/Hotkeys/HotkeyGesture.cs`
- Test: `tests/LightLaunchpad.Core.Tests/SettingsServiceTests.cs`
- Test: `tests/LightLaunchpad.Core.Tests/HotkeyGestureTests.cs`

- [ ] **Step 1: Write RED tests**

Test default settings:

```csharp
var settings = AppSettings.CreateDefault(@"C:\Users\me");
Assert.Equal(@"C:\Users\me\Launchpad", settings.LaunchpadFolder);
Assert.Equal("Alt+D", settings.Hotkey);
Assert.False(settings.StartWithWindows);
Assert.Equal("Medium", settings.IconSize);
```

Test hotkey parsing:

```csharp
var gesture = HotkeyGesture.Parse("Alt+D");
Assert.True(gesture.Alt);
Assert.Equal('D', gesture.Key);
Assert.Equal("Alt+D", gesture.ToString());
```

- [ ] **Step 2: Verify RED**

Run the two test files and confirm missing type failures.

- [ ] **Step 3: Implement minimal settings and parser**

Use JSON serialization with defaults. Support `Alt+<letter>` for V1.

- [ ] **Step 4: Verify GREEN and commit**

Run all tests and commit.

## Task 3: Shortcut Scanning And Search

**Files:**
- Create: `src/LightLaunchpad.Core/Shortcuts/ShortcutRepository.cs`
- Create: `src/LightLaunchpad.Core/Search/LaunchItemSearch.cs`
- Test: `tests/LightLaunchpad.Core.Tests/ShortcutRepositoryTests.cs`
- Test: `tests/LightLaunchpad.Core.Tests/LaunchItemSearchTests.cs`

- [ ] **Step 1: Write RED tests**

Test repository includes `.lnk`, `.url`, and `.exe`, skips unsupported files, and uses file names without extension as display names.

Test search ranking:

```csharp
var items = new[]
{
    new LaunchItem("Visual Studio Code", @"C:\Code.lnk", null, LaunchItemKind.Shortcut),
    new LaunchItem("Chrome", @"C:\Chrome.lnk", null, LaunchItemKind.Shortcut),
    new LaunchItem("Code Switch", @"C:\Switch.exe", null, LaunchItemKind.Executable)
};

var results = LaunchItemSearch.Filter(items, "code").ToList();

Assert.Equal(new[] { "Code Switch", "Visual Studio Code" }, results.Select(x => x.DisplayName));
```

- [ ] **Step 2: Verify RED**

Run targeted tests and confirm missing service failures.

- [ ] **Step 3: Implement repository and search**

Repository should create missing launchpad folder and scan only the top level. Search should be case-insensitive and rank prefix matches before contains matches.

- [ ] **Step 4: Verify GREEN and commit**

Run all tests and commit.

## Task 4: VUI Import

**Files:**
- Create: `src/LightLaunchpad.Core/Import/VuiImportParser.cs`
- Create: `src/LightLaunchpad.Core/Import/VuiImportCandidate.cs`
- Test: `tests/LightLaunchpad.Core.Tests/VuiImportParserTests.cs`

- [ ] **Step 1: Write RED tests**

Test that the parser extracts `d<number>("...")` values, decodes `%20`, trims trailing spaces, skips empty and `NULL`, and deduplicates normalized paths.

- [ ] **Step 2: Verify RED**

Run parser tests and confirm missing parser failures.

- [ ] **Step 3: Implement parser**

Use C# regular expressions and `Uri.UnescapeDataString`. Do not use Python.

- [ ] **Step 4: Verify GREEN and commit**

Run all tests and commit.

## Task 5: WPF Shell, Tray, Hotkey, And Launching

**Files:**
- Create: `src/LightLaunchpad.App/App.xaml`
- Create: `src/LightLaunchpad.App/App.xaml.cs`
- Create: `src/LightLaunchpad.App/LaunchpadWindow.xaml`
- Create: `src/LightLaunchpad.App/LaunchpadWindow.xaml.cs`
- Create: `src/LightLaunchpad.App/SettingsWindow.xaml`
- Create: `src/LightLaunchpad.App/SettingsWindow.xaml.cs`
- Create: `src/LightLaunchpad.App/ViewModels/LaunchpadViewModel.cs`
- Create: `src/LightLaunchpad.App/Services/HotkeyService.cs`
- Create: `src/LightLaunchpad.App/Services/TrayService.cs`
- Create: `src/LightLaunchpad.App/Services/LauncherService.cs`
- Create: `src/LightLaunchpad.App/Services/ShortcutWatcher.cs`

- [ ] **Step 1: Implement UI shell**

Create a borderless centered overlay with top search box and grid, using `ItemsControl` plus `WrapPanel`. Use a translucent dimmed background and a short fade/scale open animation.

- [ ] **Step 2: Implement tray commands**

Tray menu includes open launchpad, open launchpad folder, refresh shortcuts, settings, and exit.

- [ ] **Step 3: Implement global hotkey**

Register `Alt+D` via Win32 `RegisterHotKey`, unregister on shutdown, and toggle the launchpad.

- [ ] **Step 4: Implement launching**

Launch items with `ProcessStartInfo { UseShellExecute = true }`.

- [ ] **Step 5: Verify app build**

Run:

```powershell
dotnet build src/LightLaunchpad.App/LightLaunchpad.App.csproj
```

Expected: build succeeds with no compile errors.

## Task 6: Integration, Import Command, And Manual Checks

**Files:**
- Modify: `src/LightLaunchpad.App/Services/TrayService.cs`
- Create or modify: app services needed to copy `.lnk` and `.url` imports or create `.lnk` for `.exe`.

- [ ] **Step 1: Add import command**

Add a tray menu command to import `.vui` files from the project root when they exist, read-only.

- [ ] **Step 2: Build and test**

Run:

```powershell
dotnet test
dotnet build src/LightLaunchpad.App/LightLaunchpad.App.csproj
```

- [ ] **Step 3: Manual smoke test**

Run the app, create sample shortcuts in `%USERPROFILE%\Launchpad`, test open/search/launch/close behavior, then test `.vui` import summary.

- [ ] **Step 4: Commit final implementation**

Commit after tests and build pass.

## Self-Review

- Spec coverage: hotkey, folder scanning, search, launch, settings, tray, lightweight behavior, and `.vui` import all have tasks.
- Placeholder scan: no unresolved placeholder markers are intentionally left.
- Type consistency: `LaunchItem`, `LaunchItemKind`, `HotkeyGesture`, `AppSettings`, and `VuiImportParser` names are consistent across tasks.
