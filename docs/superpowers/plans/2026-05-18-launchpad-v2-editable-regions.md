# Launchpad V2 Editable Regions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add editable regions, two switchable layout modes, gear menu controls, Start Menu and NeeView imports, Alice icon assets, and performance improvements to LightLaunchpad.

**Architecture:** Keep layout, import, and search behavior in `LightLaunchpad.Core` with tests. Keep WPF interaction in `LightLaunchpad.App`, using one layout model rendered by two visual modes. Shift icon extraction to lazy loading and avoid full refresh on each hotkey open.

**Tech Stack:** C# 12, .NET 8, WPF, custom console tests, Windows Shell shortcuts, Win32 shell icon APIs.

---

## File Structure

- Create `src/LightLaunchpad.Core/Layout/LaunchpadLayout.cs`
- Create `src/LightLaunchpad.Core/Layout/LaunchpadRegion.cs`
- Create `src/LightLaunchpad.Core/Layout/LaunchpadLayoutItem.cs`
- Create `src/LightLaunchpad.Core/Layout/LayoutService.cs`
- Create `src/LightLaunchpad.Core/Layout/LaunchpadViewMode.cs`
- Create `src/LightLaunchpad.Core/Import/StartMenuImporter.cs`
- Modify `src/LightLaunchpad.Core/Settings/AppSettings.cs`
- Modify `src/LightLaunchpad.App/ViewModels/LaunchpadViewModel.cs`
- Modify `src/LightLaunchpad.App/ViewModels/LaunchItemViewModel.cs`
- Modify `src/LightLaunchpad.App/LaunchpadWindow.xaml`
- Modify `src/LightLaunchpad.App/LaunchpadWindow.xaml.cs`
- Modify `src/LightLaunchpad.App/App.xaml.cs`
- Modify `src/LightLaunchpad.App/Services/IconService.cs`
- Add `src/LightLaunchpad.App/Assets/Alice.gif`
- Add generated static icon files derived from `爱丽丝.gif`
- Add tests under `tests/LightLaunchpad.Core.Tests/`

## Task 1: Layout Model And Persistence

**Files:**
- Create: `src/LightLaunchpad.Core/Layout/LaunchpadViewMode.cs`
- Create: `src/LightLaunchpad.Core/Layout/LaunchpadRegion.cs`
- Create: `src/LightLaunchpad.Core/Layout/LaunchpadLayoutItem.cs`
- Create: `src/LightLaunchpad.Core/Layout/LaunchpadLayout.cs`
- Create: `src/LightLaunchpad.Core/Layout/LayoutService.cs`
- Test: `tests/LightLaunchpad.Core.Tests/LayoutServiceTests.cs`
- Modify: `tests/LightLaunchpad.Core.Tests/Program.cs`

- [ ] **Step 1: Write RED tests**

Test default layout creates `Uncategorized`; new folder items are added there; deleting a region moves items to `Uncategorized`; and view mode round-trips through JSON.

- [ ] **Step 2: Run tests to verify RED**

Run:

```powershell
& '.\.dotnet\dotnet.exe' run --project 'tests\LightLaunchpad.Core.Tests\LightLaunchpad.Core.Tests.csproj'
```

Expected: compile fails because layout types do not exist.

- [ ] **Step 3: Implement layout model and service**

Use JSON in `%APPDATA%\LightLaunchpad\layout.json`. Keep immutable-ish records for data and service methods for merge, rename, delete, and reorder.

- [ ] **Step 4: Run tests to verify GREEN and commit**

Run all tests. Commit with `feat: add launchpad layout persistence`.

## Task 2: Start Menu And NeeView Import

**Files:**
- Create: `src/LightLaunchpad.Core/Import/StartMenuImportCandidate.cs`
- Create: `src/LightLaunchpad.Core/Import/StartMenuImporter.cs`
- Test: `tests/LightLaunchpad.Core.Tests/StartMenuImporterTests.cs`
- Modify: `tests/LightLaunchpad.Core.Tests/Program.cs`
- Modify: `src/LightLaunchpad.App/App.xaml.cs`
- Modify: `src/LightLaunchpad.App/Services/TrayService.cs`

- [ ] **Step 1: Write RED tests**

Test importer reads `.lnk` and `.url` from recursive roots, maps parent folder names to region hints, and deduplicates by normalized path.

- [ ] **Step 2: Run tests to verify RED**

Expected: missing importer types.

- [ ] **Step 3: Implement importer**

Core importer returns candidates only. App layer copies shortcut files into the launchpad folder. NeeView import creates a shortcut to `C:\Users\thoma\AppData\Local\Microsoft\WindowsApps\NeeView.exe` if no existing NeeView shortcut is imported.

- [ ] **Step 4: Run tests and commit**

Run all tests and commit with `feat: import start menu apps`.

## Task 3: Gear Menu And View Mode Toggle

**Files:**
- Modify: `src/LightLaunchpad.App/LaunchpadWindow.xaml`
- Modify: `src/LightLaunchpad.App/LaunchpadWindow.xaml.cs`
- Modify: `src/LightLaunchpad.App/ViewModels/LaunchpadViewModel.cs`
- Modify: `src/LightLaunchpad.App/App.xaml.cs`
- Modify: `src/LightLaunchpad.Core/Settings/AppSettings.cs`
- Test: settings/layout tests as needed

- [ ] **Step 1: Write RED tests for settings**

Test `AppSettings.CreateDefault` includes default view mode `InlineRegions` and icon quality `Balanced`.

- [ ] **Step 2: Implement settings fields**

Add `ViewMode` and `IconQuality`. Keep backward compatibility when old settings JSON lacks these fields.

- [ ] **Step 3: Implement WPF gear button**

Place a small top-right gear button. Menu items switch Inline Regions and Region Tabs, trigger imports, open settings, and change icon quality/size.

- [ ] **Step 4: Build and commit**

Run tests and build. Commit with `feat: add launchpad gear menu`.

## Task 4: Editable Regions And Right-Click Menus

**Files:**
- Modify: `src/LightLaunchpad.App/LaunchpadWindow.xaml`
- Modify: `src/LightLaunchpad.App/LaunchpadWindow.xaml.cs`
- Modify: `src/LightLaunchpad.App/ViewModels/LaunchpadViewModel.cs`
- Create: simple input dialog if needed
- Modify: `src/LightLaunchpad.Core/Layout/LayoutService.cs`

- [ ] **Step 1: Add RED layout tests**

Test rename region, remove app from layout, move app to another region, and reorder within region.

- [ ] **Step 2: Implement core layout operations**

Add methods to mutate layout safely and save after changes.

- [ ] **Step 3: Add WPF context menus**

Right-click empty space creates region. Right-click region title/tab renames or deletes. Right-click app launches, renames, removes from launchpad, or opens location.

- [ ] **Step 4: Add drag/drop**

Drag app tiles to reorder and drag into another region. Persist after drop.

- [ ] **Step 5: Verify and commit**

Run tests and build. Commit with `feat: add editable regions`.

## Task 5: Icon Asset And Icon Quality

**Files:**
- Copy: `爱丽丝.gif` to `src/LightLaunchpad.App/Assets/Alice.gif`
- Generate: `src/LightLaunchpad.App/Assets/Alice.ico`
- Modify: `src/LightLaunchpad.App/LightLaunchpad.App.csproj`
- Modify: `src/LightLaunchpad.App/Services/TrayService.cs`
- Modify: `src/LightLaunchpad.App/Services/IconService.cs`

- [ ] **Step 1: Generate static icon from GIF**

Use .NET or PowerShell imaging tools, not Python. Extract first frame from `爱丽丝.gif` and create `.ico`.

- [ ] **Step 2: Set app/tray icon**

Use `ApplicationIcon` for the executable and tray icon. Keep GIF available as app asset.

- [ ] **Step 3: Improve icon extraction**

Prefer larger icon extraction when available. Keep fallback stable.

- [ ] **Step 4: Build and commit**

Run build and commit with `feat: add alice icon and sharper app icons`.

## Task 6: Performance Pass

**Files:**
- Modify: `src/LightLaunchpad.App/App.xaml.cs`
- Modify: `src/LightLaunchpad.App/ViewModels/LaunchpadViewModel.cs`
- Modify: `src/LightLaunchpad.App/Services/IconService.cs`
- Modify: `src/LightLaunchpad.App/LaunchpadWindow.xaml.cs`

- [ ] **Step 1: Capture baseline**

Run the current app and record startup working set at 0.5s and 3s, plus hotkey/open responsiveness by manual observation.

- [ ] **Step 2: Avoid eager refresh on every open**

Only refresh on startup, import, settings change, or watcher dirty flag.

- [ ] **Step 3: Lazy-load icons**

Show placeholder first. Load visible/all icons after the overlay becomes visible using dispatcher background priority.

- [ ] **Step 4: Simplify animation**

Use opacity and slight vertical translate. Avoid scale on the whole content if it feels stiff.

- [ ] **Step 5: Verify and commit**

Run tests, build, startup smoke test, and record new memory numbers. Commit with `perf: reduce launchpad startup work`.

## Self-Review

- Spec coverage: both view modes, gear switch, regions, drag/drop, right-click, Alice icon, performance, Start Menu import, and NeeView import are covered.
- Placeholder scan: no unresolved placeholder markers are intentionally left.
- Type consistency: `LaunchpadLayout`, `LaunchpadRegion`, `LaunchpadLayoutItem`, `LaunchpadViewMode`, and importer names are consistent.
