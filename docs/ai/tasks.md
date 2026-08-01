# LightLaunchpad Apple-First Task Plan

## Execution Contract

- Execute exactly one task at a time in ID order unless a dependency makes it impossible.
- Before editing, mark one task `in_progress` and update `docs/ai/progress.md`.
- Use test-first development for behavior changes where practical.
- A task is `done` only after its verification evidence is recorded in `docs/ai/progress.md`.
- Keep the real NativeUi executable working after every task. Do not defer all integration until the end.
- Do not publish, overwrite `C:\TOOL`, change the user's startup entry, or create a GitHub Release unless the user explicitly requests that delivery action.

## Milestone A: Establish Product Truth

### LLP-001 - Canonical Development Baseline

- Status: `done`
- Depends on: none
- Purpose: make the PRD and AI execution docs the tracked source of truth without absorbing unrelated untracked work.
- Inspect: `git status`, branches, tags, `PRD.md`, `LLP_reconstruction/`, `LightLaunchpad.NativeUi.o`.
- Change: track `PRD.md` and `docs/ai/*`; create or use a `codex/` development branch; ignore generated object files if no existing policy covers them; leave `LLP_reconstruction/` untouched unless a later task deliberately mines it.
- Verify:
  - `git diff --check`
  - `dotnet run --project tests\LightLaunchpad.Core.Tests\LightLaunchpad.Core.Tests.csproj` reports `33/33 tests passed`.
  - `dotnet run --project tests\LightLaunchpad.App.Tests\LightLaunchpad.App.Tests.csproj` reports `74/74 tests passed` as a legacy baseline, not release confidence.
  - `powershell -ExecutionPolicy Bypass -File tools\build-native-ui.ps1 -OutputDirectory release\baseline-check` produces an EXE.
- Completion notes: Completed on `codex/apple-first-v1.1` with commit `d2da5c0`. Revalidated repository/runtime/startup identity; tracked PRD v1.1 and all `docs/ai/*`; ignored generated `*.o`; left `LLP_reconstruction/` untracked and unstaged. Fresh evidence: Core `33/33`, legacy App `74/74`, NativeUi build exit 0 (533,504 bytes, SHA256 `AEA2BD00973E89CA92666CF05D19F036336DC535677F1AE4E8DA2BA0948094C3`), and `git diff --cached --check` exit 0.

### LLP-002 - Executable Identity And Stable Install Contract

- Status: `done`
- Depends on: LLP-001
- Purpose: ensure source, running process, startup entry, install, TOOL copy, and release can be identified and compared.
- Likely files: `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.rc`, a new NativeUi manifest/version header, `tools/build-native-ui.ps1`, `tools/package-release.ps1`, diagnostics code extracted from `LightLaunchpad.NativeUi.cpp`.
- Change:
  - Embed product name, semantic version, file version, build commit, icon, and Per-Monitor V2 manifest.
  - Define `%LocalAppData%\Programs\LightLaunchpad\LightLaunchpad.NativeUi.exe` as the stable install path.
  - Make dev builds expose a visible `-dev+<commit>` identity and use a development singleton channel so they cannot silently activate an unrelated installed build.
  - Add an About/Diagnostics view showing executable path, version, commit, SHA256 or build ID, renderer, DPI, monitor, startup path, and hotkey state.
  - Make packaging reject dirty builds unless explicitly marked development.
- Verify:
  - Windows file properties show icon, product version, and file version.
  - Diagnostics values match `Get-Process`, `Get-FileHash`, and `git rev-parse --short HEAD`.
  - A development build and installed build cannot be confused or silently substituted.
- Completion notes: Completed with implementation commit `a9037bb`. Real development and clean-fixture stable EXEs expose distinct semantic versions, commit/channel/build IDs, and singleton names; PE VERSIONINFO/icon/Per-Monitor V2 manifest and the Native settings About/Diagnostics surface are verified. Dirty stable packaging is rejected unless `-Development` is explicit. Post-commit NativeUi identity behavior 5/5, Core 33/33, legacy App 74/74, Native build/diagnostics smoke, screenshot/UI Automation audit, package contents, and diff checks passed. Verified EXE: 545,792 bytes, SHA256 `65198E9CB78B7F94A4A978E48F7A3999D5966F892A6C844F3D63FFF2F560A768`.

### LLP-003 - Shell-First Startup And Singleton Activation

- Status: `done`
- Depends on: LLP-002
- Purpose: tray, hotkey, and second-instance activation must work before catalog I/O.
- Likely files: extract `AppHost`, `Diagnostics`, and IPC modules from `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp`; update build script inputs.
- Change:
  - Create the hidden message window, diagnostics, tray, and hotkey before scanning launch folders or extracting icons.
  - Replace silent mutex exit with IPC that activates the existing instance and reports version/channel mismatch.
  - Add bounded tray retry and `TaskbarCreated` recovery without registry promotion.
  - Record startup stage timings and failures in a bounded local log.
- Verify:
  - Simulate a slow/unavailable launch folder; tray and settings still appear promptly.
  - Start the EXE twice; one process remains and the first instance becomes active.
  - Restart Explorer; tray menu and settings recover.
- Completion notes: Completed with implementation commit `33a139e`. The real NativeUi EXE now creates its channel-specific hidden window and shell integrations without catalog I/O, uses bounded `WM_COPYDATA` singleton activation with explicit mismatch reporting, retries tray registration at most eight times, restores on `TaskbarCreated`, and writes a 256 KiB-bounded JSONL startup log. Real-EXE tests covered an unavailable UNC catalog, same-build activation/one-process ownership, stable 1.11.0-vs-1.12.0 mismatch, direct recovery simulation, and an actual Explorer restart with tray/settings recovery. Post-commit AppHost behavior 4/4, identity behavior 5/5, Core 33/33, legacy App 74/74, native build/diagnostics, runtime safety, and diff gates passed. Verified EXE: 556,544 bytes, SHA256 `FE560F080969DFC355BCB226D339529892A9816EF1802EBB4D196EB2C030C93D`.

### LLP-004 - Transactional Hotkey And Settings Writes

- Status: `in_progress`
- Depends on: LLP-003
- Purpose: a failed settings change must never destroy the working launch path.
- Likely files: `AppHost`, `SettingsStore`, Native settings UI, hotkey parser/registrar.
- Change:
  - Validate and register a candidate hotkey before committing it; on conflict, retain the old registration and persisted value.
  - Add `MOD_NOREPEAT` where supported and actionable conflict feedback.
  - Treat settings, startup registration, and persistence as fallible operations with rollback.
  - Preserve default `Alt+D` and configurable reset-on-open behavior.
- Verify:
  - Register a known conflicting hotkey; old hotkey continues to work and the setting shows the reason.
  - Save to an unwritable destination; UI retains the prior value and reports failure.
- Completion notes: Started immediately after LLP-003 verification and documentation. Next RED targets transactional hotkey replacement and settings write rollback/failure feedback against production NativeUi behavior.

## Milestone B: Reliable Data And Instant Response

### LLP-005 - Standard Persistence And Folder-Scoped Layout

- Status: `pending`
- Depends on: LLP-004
- Purpose: preserve user organization through corruption, temporary file loss, and folder changes.
- Likely files: new `Persistence`/`SettingsStore`/`LayoutStore` modules; migration code for `%AppData%\LightLaunchpad\settings.json` and `layout.json`.
- Change:
  - Replace hand-written JSON scanning with a real parser available to the selected C++ toolchain.
  - Add schema versions, validation, `.bak`, temporary write, atomic replace, corruption quarantine, and recovery diagnostics.
  - Namespace layout metadata by normalized launchpad-folder identity.
  - Retain metadata for temporarily missing items and reattach it when the stable source path returns.
- Verify:
  - Strings containing quotes, braces, Unicode, and surrogate pairs round-trip.
  - Truncated JSON recovers from backup without overwriting the last good file.
  - Switching launch folders cannot display old-folder items; switching back restores their layout.
- Completion notes: Not started.

### LLP-006 - Catalog Snapshot And Shell STA Broker

- Status: `pending`
- Depends on: LLP-005
- Purpose: show immediately and refresh safely without Shell or file I/O on the UI/render thread.
- Likely files: new `Catalog`, `ShellBroker`, immutable snapshot types, icon cache, import logic, `ShowNativeUi` call sites.
- Change:
  - Show the last successful immutable catalog/scene snapshot first.
  - Move directory scanning, `.lnk` COM work, URL resolution, icon extraction, launch, file location, import, and Recycle Bin operations to a dedicated Shell STA worker.
  - Publish catalog changes back to the UI thread by stable item ID.
  - Import executables as shortcuts rather than copying binaries; deduplicate across runs by normalized target/source identity; guard reparse points and cancellation.
- Verify:
  - A slow or broken shortcut cannot freeze typing or animation.
  - Repeated Start Menu import creates no `(2)`/`(3)` duplicates for the same target.
  - Alt+D uses cached content while refresh proceeds in the background.
- Completion notes: Not started.

### LLP-007 - Native Search Index And Ranking

- Status: `pending`
- Depends on: LLP-006
- Purpose: make the shipped C++ search implementation meet the PRD instead of relying on the tested C# Core.
- Likely files: new `SearchIndex`, production C++ unit-test target, catalog snapshot integration.
- Change:
  - Implement normalized Chinese/English search, reliable full-pinyin and initials support, deterministic scoring, and stable item IDs.
  - Separate display name, aliases, target/source subtitle, and path; do not let common folder path text dominate normal matching.
  - Add local recency, frequency, and favorite tie-breaking with privacy controls and history reset.
- Verify:
  - Production C++ tests cover prefix, word boundary, contains, pinyin, initials, typo/fuzzy, duplicate names, stable focus, and ranking history.
  - Current launch folder names achieve materially better pinyin coverage than the hand-written map baseline.
- Completion notes: Not started.

## Milestone C: Apple-First Interaction Model

### LLP-008 - Complete Search Text And IME Controller

- Status: `pending`
- Depends on: LLP-007
- Purpose: make search behave like a real text field while retaining custom visual rendering.
- Likely files: new `InputController`, `TextBuffer`, IME bridge, clipboard commands, UI Automation provider, Native window message handling.
- Change:
  - Implement caret insertion, selection, mouse positioning, `Home`, `End`, `Delete`, word deletion, clipboard copy/cut/paste, and standard `Ctrl+A` while search owns focus.
  - Position the IME composition/candidate UI at the drawn caret.
  - Suspend launcher navigation commands during composition.
  - Expose search value, role, focus, selection, and text-edit actions through UI Automation.
- Verify:
  - English and Chinese IME input works at start, middle, and end of text with no duplicate commits.
  - Clipboard, selection, caret navigation, and undoable text edits behave consistently.
  - A UI Automation inspector can discover and operate the search field.
- Completion notes: Not started.

### LLP-009 - Spotlight State Machine And Keyboard Flow

- Status: `pending`
- Depends on: LLP-008
- Purpose: make the lazy-user launch path deterministic and fast.
- Likely files: `InputController`, `InteractionState`, Spotlight controller, search result scene builder.
- Change:
  - Open with search as the only focus; do not paint the first result as selected.
  - `Enter` in search launches the first result. `Down` enters results. Arrow keys navigate results. Printable input returns to search.
  - `Tab` and `Shift+Tab` traverse search, results, and auxiliary controls.
  - `Esc` progressively cancels transient UI, clears a query, then closes.
  - Empty query shows 8-12 recent/favorite items when enabled and a restrained empty state when disabled.
- Verify:
  - Automate both canonical keyboard paths from Alt+D through successful Shell launch.
  - Result refresh preserves focus by stable ID and never launches an unintended item.
- Completion notes: Not started.

### LLP-010 - Launchpad Browse/Edit Separation

- Status: `pending`
- Depends on: LLP-009
- Purpose: remove the launch-versus-selection conflict.
- Likely files: `InteractionState`, Launchpad controller, context menus, region/drag layout code.
- Change:
  - Browse mode: single click and Enter launch.
  - Explicit edit mode: click selects; Ctrl-click, marquee, multiselect, region drag, and item drag become available; launch requires an explicit launch command.
  - Separate visual states for hover, keyboard focus, selected, multiselected, pressed, drag source, and drop target.
  - Use one shared geometry model for drawing, hit testing, keyboard navigation, scrolling, marquee, and drop targets.
- Verify:
  - Browse mode cannot accidentally select or rearrange.
  - Edit mode cannot accidentally launch from a selection click.
  - Hit targets match drawn tiles at 100%, 150%, and 200% DPI.
- Completion notes: Not started.

### LLP-011 - Favorites, Safe Delete, And Undo

- Status: `pending`
- Depends on: LLP-010
- Purpose: make organization direct and forgiving.
- Likely files: `InteractionState`, `Catalog`, `ShellBroker`, persistence, context menus, transient feedback surface.
- Change:
  - Add favorite/unfavorite in Launchpad and persist it locally.
  - Move launchpad files to Recycle Bin, commit layout only after success, and expose time-limited undo.
  - Preserve items and selections on failure and provide concise actionable feedback.
  - Ensure region deletion only moves items to Uncategorized and can be reversed during the same action window.
- Verify:
  - Delete, undo, failed delete, batch partial failure, favorite ranking, and restart persistence all pass production behavior tests.
- Completion notes: Not started.

## Milestone D: Adaptive Material And Motion

### LLP-012 - DPI, Monitor, Accessibility, And Scene Geometry

- Status: `pending`
- Depends on: LLP-011
- Purpose: create one correct coordinate and accessibility foundation before lens work.
- Likely files: manifest/resource, `AppHost`, layout geometry, `Renderer`, settings window, UI Automation providers.
- Change:
  - Enable Per-Monitor V2, handle `WM_DPICHANGED`, display changes, system theme, high contrast, reduced transparency, and reduced motion.
  - Position Spotlight on the foreground app monitor, falling back to the mouse monitor.
  - Express layout in DIP and generate draw/hit-test/navigation rectangles from one scene layout.
  - Ensure text and controls remain usable at 100%, 150%, and 200% DPI and with increased text size.
- Verify:
  - Multi-monitor and three-DPI matrix screenshots show no clipping, hit-test drift, or wrong-screen placement.
  - High contrast and reduced transparency use readable solid/adaptive surfaces.
- Completion notes: Not started.

### LLP-013 - Unified Adaptive Material Renderer

- Status: `pending`
- Depends on: LLP-012
- Purpose: replace screenshot-offset effects with a continuous Apple-inspired material system.
- Likely files: new D3D11/D2D/DirectComposition renderer modules, scene snapshot types, fallback renderer, build link settings.
- Change:
  - Build one continuous adaptive main glass layer with luminance-aware tint, blur, edge light, shadow, and contrast protection.
  - Remove nested glass captures and all 1.16x selected-tile background magnification.
  - Keep search center aligned; constrain visible center displacement to 0.5 DIP and edge displacement to 1.5 DIP without sampling outside the control bounds.
  - Keep text and icons above optical material with no second image.
  - Provide a no-displacement fallback with the same hierarchy.
- Verify:
  - Compare reference and current screenshots on at least 12 varied desktop backgrounds at matching states and viewport sizes.
  - Pixel checks show no upward ghost, duplicated text/icon, rectangular leak, or out-of-bounds refraction.
  - Normal text contrast is at least 4.5:1 and focus boundaries target at least 3:1.
- Completion notes: Not started.

### LLP-014 - Continuous Focus And Nonblocking Motion

- Status: `pending`
- Depends on: LLP-013
- Purpose: deliver elegant motion that reinforces hierarchy without delaying work.
- Likely files: animation clock, DirectComposition visual properties, `InteractionState`, reduced-motion policy, performance telemetry.
- Change:
  - Replace all UI-thread `Sleep` animation with a QPC/time-based or compositor-driven clock.
  - Implement open 140-180 ms, close 120-160 ms, focus 80-120 ms, and press 60-90 ms motion.
  - Keep input active from the first visible frame.
  - Focus uses luminance, edge response, and elevation; scale is 1.0 preferred and capped at 1.015.
  - Reduced motion removes scale, spring, parallax, and dynamic refraction and uses at most 100 ms fades.
- Verify:
  - Capture frame-time traces; 60 Hz P95 is at most 16.7 ms and focus feedback P95 is at most 50 ms.
  - Repeated Alt+D during animation never deadlocks, queues stale motion, or loses text.
- Completion notes: Not started.

## Milestone E: Settings, Verification, And Cleanup

### LLP-015 - Settings Information Architecture

- Status: `pending`
- Depends on: LLP-014
- Purpose: make configuration quiet, comprehensible, and nonessential to first use.
- Likely files: Native settings window/controller, `SettingsStore`, diagnostics view.
- Change:
  - Group settings into General, Search & Hotkey, Appearance, Apps & Layout, and Advanced.
  - Use browse controls, toggles, segmented choices, steppers/validated numeric fields, and immediate validated persistence.
  - Include recent/favorite visibility, history clear, reset search, reduced motion, startup, and hotkey conflict status.
  - Restore the last settings page and expose settings from Launchpad as well as tray.
- Verify:
  - All settings are keyboard accessible, fit at supported DPI, persist immediately only after validation, and recover from write failure.
- Completion notes: Not started.

### LLP-016 - Production Behavior Tests, CI, And Release Gate

- Status: `pending`
- Depends on: LLP-015
- Purpose: make green tests mean the shipped executable works.
- Likely files: new C++ unit/integration test target, EXE smoke harness, pixel baselines, performance scripts, `.github/workflows/*`, build/package scripts.
- Change:
  - Compile production C++ search, persistence, geometry, state, and settings modules into tests.
  - Add real EXE tests for tray, singleton IPC, hotkey conflict, IME, launch failure, Recycle Bin, DPI, monitor placement, Explorer restart, and corrupted configuration.
  - Add pixel-regression and performance-budget checks.
  - Make package creation depend on all gates and verify PE architecture, version, ZIP contents, static dependencies, clean commit, and startup smoke.
- Verify:
  - Clean clone/build passes locally and in CI.
  - A deliberately broken tray, hotkey, version, visual baseline, or package dependency causes the release gate to fail.
- Completion notes: Not started.

### LLP-017 - Retire Legacy Product Paths

- Status: `pending`
- Depends on: LLP-016
- Purpose: leave one product, one test surface, and one release owner.
- Likely files: `LightLaunchpad.sln`, `src/LightLaunchpad.App`, `src/LightLaunchpad.Agent`, `src/LightLaunchpad.NativeAgent`, retired tests, README, project structure docs.
- Change:
  - Remove retired projects from the active solution, CI, test runner, package scripts, startup assumptions, and documentation.
  - Preserve only explicitly useful migration fixtures or historical source in an archive strategy agreed with the user; do not ship it.
  - Remove source-string tests that assert superseded WPF, Agent, Tab completion, double-click launch, tray registry mutation, or old glass constants.
- Verify:
  - Repository-wide search finds no active build, startup, package, or test dependency on WPF/Agent/NativeAgent/Assist.
  - Clean NativeUi build, production tests, smoke tests, and package gates pass.
- Completion notes: Not started.
