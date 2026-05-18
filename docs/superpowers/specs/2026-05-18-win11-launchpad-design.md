# Win11 Lightweight Launchpad Design

## Goal

Build a lightweight Windows 11 launchpad inspired by macOS Launchpad. Pressing `Alt+D` opens a smooth overlay with a search box at the top and a grid of shortcuts from a dedicated launchpad folder below it.

## Non-Goals

- No Python runtime, Python build scripts, or Python helper tools.
- No Electron or Chromium shell.
- No full-disk app search in the first version.
- No plugin system, web search, calculator, clipboard history, or command palette features.
- No complex drag-and-drop layout editor in the first version.
- No cloud sync or account system.

## Reference Projects

- WinLaunch: useful reference for the macOS Launchpad-like visual direction and overlay behavior.
- SimpleFolderLauncher: useful reference for the folder-as-launcher model.
- Flow Launcher, Wox, and Ueli: useful references for launcher search behavior, but their plugin-first scope is intentionally larger than this project.

The project should follow the simpler folder-based model rather than becoming a full Spotlight-style launcher.

## Recommended Technology

Use `C#`, `.NET 8`, and `WPF`.

Reasons:

- Good access to Win32 APIs for global hotkeys, shell shortcut resolution, file watching, tray icons, and process launching.
- Lower resource use than Electron.
- Faster development and better maintainability than raw C++/Win32 for this scope.
- Native Windows desktop deployment path.

The local machine currently has .NET desktop runtimes installed but no .NET SDK. Development requires installing the .NET 8 SDK or Visual Studio Build Tools with desktop workload support.

## User Experience

When the app starts, it runs quietly in the background and exposes a tray icon. Pressing `Alt+D` toggles the launchpad.

The launchpad is a borderless top-level overlay window:

- Search box at the top, focused automatically when opened.
- Shortcut grid below the search box.
- Icons and labels are centered and readable.
- `Esc` closes the overlay.
- `Enter` launches the currently selected result.
- Mouse click launches a shortcut.
- Search input filters visible items immediately.

The first version uses a restrained macOS-like feel without copying macOS exactly:

- A translucent dimmed background. Acrylic blur is optional and may fall back to a plain translucent background if it hurts performance or reliability.
- 120-180 ms fade and scale animation on open.
- Fast close animation or immediate close if needed for responsiveness.
- Reuse the overlay window instead of recreating it every time.

## Launchpad Folder

Default folder:

```text
%USERPROFILE%\Launchpad
```

The app creates the folder on first run if it does not exist.

Supported first-version item types:

- `.lnk`
- `.url`
- `.exe`

The tray menu includes:

- Open launchpad
- Open launchpad folder
- Refresh shortcuts
- Settings
- Exit

The app watches the launchpad folder with a file system watcher and updates its in-memory shortcut list when files are added, removed, or renamed. Recursive folders are not part of the first version unless explicitly enabled later.

## Existing VUI Import

The workspace contains existing `.vui` files that store launch paths in entries shaped like:

```text
d1("C:\Program%20Files\App\App.exe")
```

After the core launchpad is working, add a controlled one-time import path for these files. This is a migration helper, not part of the app's normal runtime scan loop.

Import behavior:

- Read selected `.vui` files without modifying them.
- Extract quoted values from `d<number>("...")` entries.
- Ignore empty strings, whitespace-only values, and `NULL`.
- Decode URL-style escapes such as `%20`.
- Trim accidental trailing spaces after decoding.
- Support imported paths that point to `.exe`, `.lnk`, or `.url`.
- Skip missing paths and report them in a small import summary.
- Deduplicate by normalized path.
- For existing `.lnk` and `.url` files, copy them into the launchpad folder when possible.
- For existing `.exe` files, create a `.lnk` shortcut in the launchpad folder using the executable file name as the display name.

The importer must not use Python. It should be implemented in C# using the same shortcut creation and path-normalization services used by the main app.

## Hotkey

Default global hotkey:

```text
Alt+D
```

Implementation should use the native Windows `RegisterHotKey` API, not a low-level keyboard hook.

If registration fails because another app owns the hotkey, the app should:

- Continue running.
- Show a tray notification or settings warning.
- Allow the user to set a different hotkey.

The first version keeps `Alt+D` as the default because it is the requested behavior, while recognizing it can conflict with address-bar focus shortcuts in some apps.

## Search

First-version search is local and lightweight:

- Search over shortcut display names.
- Case-insensitive matching.
- Simple fuzzy matching is acceptable if it stays inexpensive.
- No background indexing service.
- No full Start Menu scan unless added as a later option.

Sorting:

1. Exact prefix matches.
2. Other name matches.
3. Original folder order or stable alphabetical order.

## Shortcut Model

Each launch item contains:

- Display name.
- Source path.
- Target path when resolvable.
- Icon image.
- Launch arguments when available from `.lnk`.
- Item type.

Shortcut loading should be separated from UI rendering so it can be tested independently.

Icon extraction should be cached in memory during the app session. Disk cache can be added later if profiling shows startup or refresh cost is too high.

## Settings

First-version settings:

- Launchpad folder path.
- Hotkey.
- Start with Windows.
- Icon size: small, medium, or large. Grid columns are calculated automatically from available window width.

Settings are stored as JSON in:

```text
%APPDATA%\LightLaunchpad\settings.json
```

Settings writes should be atomic enough to avoid corrupting the file during normal app shutdown.

## Architecture

Use small, focused components:

- `AppHost`: app startup, single-instance guard, service wiring, tray lifetime.
- `HotkeyService`: `RegisterHotKey`, unregister, hotkey changed handling.
- `ShortcutRepository`: scans the launchpad folder and produces launch items.
- `ShortcutWatcher`: observes folder changes and asks the repository to refresh.
- `IconService`: extracts and caches icons.
- `LauncherService`: safely starts selected items through Shell execution.
- `SettingsService`: loads and saves JSON settings.
- `LaunchpadViewModel`: search text, filtered items, selection, commands.
- `LaunchpadWindow`: overlay UI and animations.
- `SettingsWindow`: small settings editor.

The UI should depend on view models and services, not on direct file-system or Win32 calls.

## Error Handling

- Missing launchpad folder: create it.
- Unreadable shortcut: skip it and log a warning.
- Broken shortcut target: keep item visible if launch through Shell still works; otherwise show a non-blocking error on launch.
- Hotkey conflict: keep app alive and surface the issue in tray/settings.
- Icon extraction failure: use a default app icon.
- Launch failure: show a small non-blocking message and keep the overlay usable.

## Performance Targets

Idle:

- CPU should be effectively 0% when not opening, searching, or refreshing.
- No polling loops.
- File watching should be event-driven.

Memory:

- WPF first-version target: roughly 80-120 MB idle.
- If a future requirement demands dramatically lower idle memory, reconsider a C++/Win32 + Direct2D implementation.

Open behavior:

- Hotkey-to-visible response should feel immediate.
- Overlay window should be created once and reused.
- Animation should not block shortcut scanning or icon loading.

## Testing Strategy

Unit-level tests:

- Settings load/save defaults and custom values.
- Shortcut repository filters supported file types.
- Search ranking returns expected order.
- Hotkey parsing and formatting.

Manual verification:

- `Alt+D` opens and closes the launchpad.
- `Esc` closes the overlay.
- Typing filters results.
- `Enter` launches the selected result.
- Click launches the chosen item.
- Adding/removing files in `%USERPROFILE%\Launchpad` refreshes the grid.
- Broken shortcuts do not crash the app.
- Tray exit unregisters the hotkey.

Performance verification:

- Observe idle CPU after launch.
- Open/close repeatedly and confirm animation remains smooth.
- Test with 20, 100, and 300 shortcuts.

## First Milestone

The first milestone is a working local build that:

1. Starts a single tray-resident process.
2. Registers `Alt+D`.
3. Creates and scans `%USERPROFILE%\Launchpad`.
4. Displays `.lnk`, `.url`, and `.exe` files in a searchable grid.
5. Launches selected shortcuts.
6. Provides basic settings and tray commands.
7. Meets the lightweight idle behavior target.

## Later Options

Possible future additions after the first version is stable:

- Start Menu import button.
- Drag-and-drop shortcuts into the grid.
- Folder grouping and pages.
- Custom icon size, theme, and background style.
- Pinyin search for Chinese app names.
- Portable mode.
- Optional Everything integration.
