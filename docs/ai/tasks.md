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

- Status: `done`
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
- Completion notes: Completed with implementation commit `bc5ea3e`. NativeUi now pre-registers a `MOD_NOREPEAT` candidate hotkey under an alternate ID while the prior binding remains live, commits the swap only after startup registration and settings persistence succeed, and rolls back the exact prior startup command plus candidate registration on failure. Settings are read into a candidate object, failures keep prior in-memory/control/file state, and visible Diagnostics status reports conflict, write, startup, and success outcomes. Real-EXE tests prove conflict retention, successful ownership transfer, and unwritable-destination rollback across hotkey/startup/settings with no test registry residue. Post-commit transaction 3/3, AppHost 4/4, identity 5/5, Core 33/33, legacy App 74/74, native build/diagnostics, Run-value safety, and diff gates passed. Verified EXE: 561,152 bytes, SHA256 `6151535E799359BC6D3553619991ECD339A8FFA36CA6AECCF86562048A9DC53E`.

## Milestone B: Reliable Data And Instant Response

### LLP-005 - Standard Persistence And Folder-Scoped Layout

- Status: `done`
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
- Completion notes: Completed in implementation commit `a364883`. Production NativeUi now uses vendored nlohmann/json 3.12.0 with a pinned SHA256 and adjacent MIT license, schema-v2 validation and v1 compatibility, flushed temporary writes, atomic primary/backup replacement, corruption quarantine, backup recovery, normalized folder-scoped layout paths, duplicate stable-path rejection, and retained metadata that reattaches when a source returns. A real-EXE probe suite passes 7/7 checks for dependency integrity, escaped/Unicode/surrogate round trips, A/B folder isolation and restore, invalid fields/future schemas without mutation, missing-item reattachment, truncated-primary recovery without changing the last good backup, and clean first-run defaults. Post-commit settings 3/3, AppHost 4/4, identity 5/5, Core 33/33, legacy App 74/74, build/diagnostics, protected-process, Run-value, stable-install, and diff gates passed. Verified EXE: 1,379,840 bytes, SHA256 `60E2D4178C3F866EFB6C4E7649B0EAEC49BE98CF843D72C34B5ACF0758BFE95E`.

### LLP-006 - Catalog Snapshot And Shell STA Broker

- Status: `done`
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
- Completion notes: Completed in implementation commit `0bd788d`. Production NativeUi now publishes immutable catalog snapshots by stable item ID while a dedicated Shell STA owns scanning, shortcut/URL resolution, icons, launch/location, import, Recycle Bin, and queued layout persistence. Real-EXE tests prove a deliberately broken shortcut plus a 1.2 s slow refresh leaves cached content visible and accepts actual Alt+D-path input in 0 ms, repeated Start Menu import produces one `.lnk` then zero duplicates, executables are never copied, reparse points are skipped, and queued layout writes survive immediate shutdown. UI-thread Sleep animation was removed pending the compositor timeline task. Post-commit Catalog/Shell 3/3, persistence 7/7, settings 3/3, AppHost 4/4, identity 5/5, Core 33/33, and legacy App 74/74 gates passed. Verified EXE: 1,450,496 bytes, SHA256 `392664016AE776286DAECAB0EEB37A6F2B18C979B952D0BA7ABA07E7EAA49EE6`.

### LLP-007 - Native Search Index And Ranking

- Status: `done`
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
- Completion notes: Completed in implementation commit `b092424`. NativeUi now ships a production C++ `SearchIndex` with normalized English/Chinese matching, deterministic exact/prefix/boundary/contains/alias/pinyin/metadata/fuzzy ranking, stable IDs/focus, isolated explicit `path:` matching, and local frequency/recency/favorite history with tracking/visibility controls and history reset. A pinned MIT `mozillazg/pinyin-data` snapshot replaces the 129-character hand map; six representative names from the current launch folder improved from 0/6 complete-pinyin coverage to 6/6, with matching initials. History is loaded on the Shell STA, atomically persisted after successful Shell launch, and made read-only if a malformed or future schema cannot be loaded safely. Post-commit production search 7/7, Catalog/Shell 3/3, persistence 7/7, settings 3/3, AppHost 4/4, identity 5/5, Core 33/33, and legacy App 74/74 gates passed. The 300-item cached pinyin-query P95 was 90 microseconds. Verified EXE: 1,599,488 bytes, SHA256 `85AC30FF2FF79D5F5EB25D746FA453F3628CEC38C13AA8C8151F698C3A1E2312`.

## Milestone C: Apple-First Interaction Model

### LLP-008 - Complete Search Text And IME Controller

- Status: `done`
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
- Completion notes: Completed in implementation commit `d7f398f`. NativeUi now uses a production C++ UTF-16 `TextBuffer` with caret/anchor selection, surrogate-safe character and word movement/deletion, mouse positioning and drag selection, clipboard shortcuts, bounded undo/redo, inline IME composition/replacement, modern-result/legacy-character duplicate suppression, candidate placement at the drawn caret, and launcher-navigation suspension during composition. A native UI Automation provider exposes an editable search role/name/ID, Value and Text patterns, focus, document text, selection, and range selection. A real isolated EXE probe verified UIA discovery/operation, mouse selection, Ctrl+C/X/V/A/Z/Y, English and Chinese insertion at start/middle/end, visible rendering, and candidate position `(153,76)`; production C++ tests passed 7/7 with 2,000 edit+undo samples at 400 ns P95. Full post-commit gates passed. Verified EXE: 1,624,064 bytes, SHA256 `E7D92E1F4BEBA532CBD6432F31D2D65E0CBDCB3C48DC5CF0C21D22B3359A09F1`.

### LLP-009 - Spotlight State Machine And Keyboard Flow

- Status: `done`
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
- Completion notes: Completed in implementation commit `7bb1d0a`. NativeUi now uses a production `SpotlightState` that separates search, result, and auxiliary focus; opens without result preselection; implements Enter/Down/arrows/printable/Tab/Shift+Tab/progressive Esc semantics; preserves focused stable IDs across live Catalog reorder; and leaves no launch target when that ID disappears. Empty Spotlight is capped at 10 local recent/favorite items or shows a restrained disabled-state prompt. A real isolated NativeUi EXE drove both canonical Alt+D paths through successful Shell STA launches, verified 7 search-focused opens, inserted and removed live Catalog entries without focus substitution or unintended launch, and measured hotkey-to-input-ready at 62 ms P95. Product-design audit screenshots verified distinct empty, search, result, and auxiliary focus states. Full post-commit gates passed. Verified EXE: 1,637,888 bytes, SHA256 `968DC465474F5B81C87A1F46940C4A6D87CE6D7AA06FDBDBE5C90B4D192C0974`.

### LLP-010 - Launchpad Browse/Edit Separation

- Status: `done`
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
- Completion notes: Completed in implementation commit `689dc61`. NativeUi now opens Launchpad in explicit Browse mode, where single-click/Enter launch and persistent selection, marquee, item drag, and region drag are unavailable; Edit/Done (plus Ctrl+E) gates organization gestures and requires an explicit menu command to launch. A production `LaunchpadState`, shared `SceneGeometry` snapshot, and distinct normal/hover/keyboard/selected/multiselected/pressed/drag-source/drop-target states are used by D2D/GDI drawing and interaction consumers. Production geometry tests prove identical draw/hit/scroll slots at 100%, 150%, and 200% metrics. A Per-Monitor-V2 isolated real NativeUi EXE performed two exact browse launches while edit selection, Ctrl-selection, Enter, marquee, item drag, and region drag produced no launch; product-design audit accepted eight current-run state screenshots. The prohibited 1.16x/negative-offset focus backdrop sampling was removed. Full native, Core 33/33, and historical App 74/74 gates passed. Verified post-commit EXE: 1,650,176 bytes, SHA256 `C23EF857CE56875FEC0705AB0F7E51AC5B3357EDD927F2C6C3AE91450B9DFA69`.

### LLP-011 - Favorites, Safe Delete, And Undo

- Status: `done`
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
- Completion notes: Completed in implementation commit `707510a`. Launchpad now manages persisted local favorites with Fluent icon badges, commits layout only for exact Shell-confirmed recycle successes, retains failed items/selections, and exposes an 8-second action-scoped Undo surface. Shell STA deletion captures Recycle Bin PIDLs from `PostDeleteItem` and restores to the exact parent/name without overwriting collisions; partial failure, collision retry, source preservation, and exact item/region order are proven by isolated real NativeUi EXEs. Restart persistence/ranking, visual feedback, full NativeUi gates, Core 33/33, and historical App 74/74 passed. Verified post-commit EXE: 1,694,720 bytes, SHA256 `38B8FFEEF0CABC7FE0A365C98CB82158D359A2AC9D4F026E2AF4E7BDD4DEA8EA`.

## Milestone D: Adaptive Material And Motion

### LLP-012 - DPI, Monitor, Accessibility, And Scene Geometry

- Status: `done`
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
- Completion notes: Completed in implementation commit `b222b1b`. NativeUi now applies Per-Monitor-V2 at process/window boundaries, follows the foreground monitor with cursor/primary fallback, handles DPI/display/theme/accessibility changes, and derives main, settings, modal, draw, hit-test, navigation, feedback, and Undo geometry from shared DIP snapshots. High contrast/reduced transparency use opaque adaptive palettes, reduced motion removes reflow interpolation, and organization status raises UIA plus WinEvent notifications without stealing search focus. Production C++ tests passed 3/3 adaptive-policy and 7/7 geometry checks; isolated real EXEs passed the actual-monitor plus 96/144/192-DPI and 100/150/200%-text matrix 4/4 with UIA, announcement, launch-hit, settings-reflow, and 12 screenshot artifacts. Full NativeUi gates, Core 33/33, and historical App 74/74 passed. The current host exposes one physical display, so cross-physical-monitor capture could not be fabricated; foreground placement is exercised on the real monitor and foreground/cursor/primary selection is covered directly. Verified post-commit EXE: 1,730,560 bytes, SHA256 `3B18E57FCFA0CA391CC4F51C59E5974E041EA87EA0207C320C074D153F13A1C2`.

### LLP-013 - Unified Adaptive Material Renderer

- Status: `done`
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
- Historical Task 3 checkpoint: LLP-013 was still in progress. Design `0cf5a51` and the 10-task/87-step plan `3d70d18` were approved; the user selected Subagent-Driven execution, 12 controlled backgrounds plus the real desktop, continuous live capture, and restrained cool white-blue material. The pre-implementation Windows PowerShell 5.1/WinLibs baseline passed all 16 NativeUi suites in 364.94 seconds with protected runtime/install/startup hashes and all six historical `lightlaunchpad-*` temp roots unchanged. Task 1 completed through plan correction `03df6b8`, pure boundary/tests `d54f106`, durable RED evidence `ece7c5b`, and hardening `659b0a9`; focused gates passed MaterialRenderer 5/5 and AdaptiveEnvironment 3/3 with independent reviews green. Task 2 completed through immutable scene/icon ownership `cde84d2`, legacy AND/XOR alpha correction `7be8d13`, and modern premultiplied-alpha/caller-handle proof `db24ecf`; final reviews were green at MaterialRenderer 8/8 and AdaptiveEnvironment 3/3. Task 3 completed through production bootstrap `38308d6` and concurrency/toolchain hardening `4f5b044`. NativeUi gained the exact pimpl facade, one no-poll renderer thread, thread-owned COM/D3D11/DXGI/D2D1/DWrite/DComp initialization, stable first-failure fallback, and a real four-stage feature-level probe; every normal and clean-source build included the module and a Strawberry-first PATH deterministically selected complete WinLibs 16.1. Independent specification review passed with zero findings and code-quality review was Ready Yes with 0 Critical/Important/Minor. Serial gates passed MaterialRenderer 11/11 (feature level 0xb000; 1,000 immediate lifetimes and 500 Show/Hide lifetimes), AdaptiveEnvironment 3/3, ExecutableIdentity 6/6, and AppHost 4/4. The retained Development EXE was 1,749,504 bytes, SHA256 `BF44E7700FE06227E0F48A2CE11E5A10A277DFD5CA1D0DB539A1C60F612488BB`, and exited diagnostics mode 0 with commit `4f5b044c6c1e`; no host display/capture was claimed at that checkpoint.
- Current LLP-013 checkpoint (supersedes the Task 3-only final state above): Task 4 is complete through probe-scope correction `6044bf0`, live capture `f6fcfb9`, acquisition/hidden-evidence correction `d7c72ae`, and quality hardening `c4bc9b0`. The renderer thread now owns the exact-monitor DXGI 1.2 duplication lifecycle, effective self-capture exclusion, 32 ms first-frame deadline with waits capped at 16 ms, ReleaseFrame-before-reacquire, one immediate rebuild for access loss/device removal, nonblocking Hide, no hidden polling, and one stable fallback. Development-only real-EXE probes remain outside normal Spotlight host wiring; Stable rejects them before COM/singleton/window creation. Final direct production gates pass MaterialRenderer 24/24 and AdaptiveEnvironment 3/3; the real Lifecycle gate passes forced fallback, responsive UIA input, fresh hidden counters, zero Stable window events, and actual-monitor `high-fidelity/None` capture with 3 acquired frames and 0 rebuilds. The retained current-commit Development EXE is 1,761,280 bytes, SHA256 `7C08124DC2EFE790990668A7CA424059959646D91AB57958369A08D6916D19DA`, identity `development:c4bc9b024104`, and contains no test runner symbol. Independent final specification review is PASS with zero findings and code-quality review is Ready Yes with 0 Critical/Important/Minor. Task 4 is capture-lifecycle infrastructure, so visual screenshot/pixel evidence remains N/A until Tasks 5, 6, and 8. Task 5 three-layer DirectComposition rendering is the next gate; no formal install, startup, `C:\TOOL`, push, tag, or release mutation has occurred.
- Current LLP-013 checkpoint (supersedes the Task 4 checkpoint above): Task 5 is complete through three-layer composition `1a2421f`, source-flag plan correction `d0cbe69`, optical/atlas/local-guard/resource hardening through `5d1b2b9`, `0c593a2`, and `a8cb096`, focused-transform removal `8b44ca1`, and final rotated GPU sampling/actual-operation failure proof `7a43555`. One target/root owns exactly three ordered premultiplied material/overlay/content surfaces; only material sees one live desktop source, content remains crisp, search refraction is bounded/clamped/no-bias, focus has fill/rim/shadow only, radial clarity and 16x12 spatial local guards are active, required resource failures stop before commit/present, and capture timeouts do not present. Final direct production gates pass MaterialRenderer 29/29, AdaptiveEnvironment 3/3, and AdaptiveWindowBehavior 4/4. The final independent exact-HEAD real EXE is 1,783,808 bytes, SHA256 `891FFAEEF49467C305EFB50EA6E505406415AB1A854CE234954D48F7638D7134`, and reports `high-fidelity/live/None`, acquired 20, presented 20, rebuilds 0; production builds contain no test injection markers. Independent specification review is PASS with zero findings and code-quality review is Ready Yes with 0 Critical/Important/Minor. This remains probe-only composition: normal Spotlight host integration, legacy-lens removal, canonical launch flows, screenshot/pixel/contrast evidence, and final performance budgets are not claimed until Tasks 6-9. Task 6 is the next gate; no formal install, startup, `C:\TOOL`, push, tag, or release mutation has occurred.
- Current LLP-013 checkpoint (supersedes the Task 5 checkpoint above): Task 6 is complete through normal host routing `5ce5bbc`, complete immutable Spotlight scenes `df00638`, and the final atomic lifecycle/recovery correction `cf8489b`. Normal Spotlight now submits latest-only immutable scenes to the single DComp renderer, validates rather than legacy-paints while high fidelity owns the window, retains one aligned no-displacement fallback, and has no nested/magnifying legacy functions. The final correction proves direct/DComp ownership handoff, current-activation/current-scene acknowledgement, nonblocking Hide, latest-request-wins across Hide cleanup, renderer-owned scene lifetime, ordinary-path zero identity-string copies, real Alt+9 registration and triggering, visible Settings mode transitions, pre-clear failure recovery without glass-on-glass, self-capture-free Launchpad-to-Spotlight fallback, Settings HWND destruction, real blur-close, and Alt+9 reopen. All visible evidence is hard-targeted to `\\.\DISPLAY1`; normal and forced Host canonical flows pass with 78/78 ms input-ready P95, MaterialRenderer passes 29/29, Lifecycle passes with Stable window events zero and production test symbols absent, and independent final specification and quality reviews both report C0/I0/M0 / Ready Yes. The retained exact-HEAD Development EXE is 1,838,080 bytes at `artifacts/task6-postcommit-cf8489b/development/LightLaunchpad.NativeUi.exe`, SHA256 `58D18F79938B5FEE9DE19B19E42A9FFFB73500C60ACFBDC1039391F7FDC190AD`; diagnostics identify full commit `cf8489be500dfff6cf0d360206c84d4d4a459f4f`, and a real DISPLAY1 probe reports `live/high-fidelity`, acquired 3, presented 3, first frame 1.5428 ms. Task 7 recovery/adaptive routing/diagnostics/evidence freeze is now the sole next gate; Task 8 visual-background acceptance and Tasks 9-10 performance/final gates are not claimed. No formal install, startup, `C:\TOOL`, push, tag, or release mutation has occurred.
- Current LLP-013 checkpoint (supersedes the Task 6 checkpoint above): Task 7 is complete in implementation commit `a9032341540a34622e0a998db7396a0b0ac1f5e2` (`feat: harden material recovery and evidence`). High contrast and disabled transparency select a live solid surface without capture or affinity; reduced motion keeps high-fidelity capture with identity sampling. DXGI access loss/device removal/reset from acquisition, composition resources, BeginDraw, capture textures, Commit, and Wait consume one rebuild per visible epoch; a failed rebuild or second loss remains in truthful fallback until a new show, display change, or real `EVENT_SYSTEM_DESKTOPSWITCH`. Development evidence freezes the actual last committed GPU desktop texture and scene exactly once after clearing affinity, rejects an unpresented candidate by distinct hash/frame identity, and never resumes; Stable rejects all Development switches before files, COM, singleton, or HWND work. Diagnostics now own adapter/output/DPI/reduced-motion/P95/freeze values, publish only from renderer status/shutdown, and avoid repeated owned string/vector copies on steady capture steps. Default SpotlightFlow retains the full canonical launch regression while `-EvidenceOnly` owns terminal freeze evidence. Final gates pass MaterialRenderer 43/43, Recovery, and all six ExecutableIdentity tests; real Recovery reports string/vector copy deltas 0, distinct candidate hash, exact baseline/freeze hash, one extra frozen presentation, WDA_NONE, and terminal state. Independent final specification review is C0/I0/M0 and quality review is Ready Yes C0/I0/M0. The retained exact-HEAD Development EXE is 1,863,168 bytes at `artifacts/task7-postcommit-a903234/development/LightLaunchpad.NativeUi.exe`, SHA256 `3E89E445E6EE8AE44114266C5753637459C9B13C557291E8CBDDA90B64FC7BAD`, with diagnostics commit `a9032341540a34622e0a998db7396a0b0ac1f5e2` and channel `development`. Task 8 deterministic 12-background plus real-desktop pixel/live-refresh evidence is the sole next gate; Tasks 9-10 and LLP-014 remain unclaimed. No formal install, startup, `C:\TOOL`, push, tag, or release mutation has occurred.
- Completion notes: Completed on `codex/apple-first-v1.1` through the scoped design/plan and task checkpoints recorded below, with final commits `a903234` (recovery/evidence), `aff89f3` (Task 7 durable state), `abd3b70` (12+1 material pixels), and `1dabee0` (performance/lifecycle gates). The retained manifest has 12 controlled backgrounds plus the real desktop on `\\.\DISPLAY1`; controlled minima are text `6.363:1` and focus `3.204:1`, all 13 meet `4.5:1`/`3:1`, maximum edge displacement is `1.5 DIP`, and duplicate/scale/leak checks are zero. The final 30-open/600-frame/20-focus real-EXE run reports hotkey P95 `94 ms`, frame P95 `8.486 ms`, focus P95 `30.827 ms`, hidden CPU `0%`, stationary hidden counters, and retained growth `14,012,416` bytes after the first full graphics teardown. Absolute hidden Private Bytes are truthfully recorded as `120,479,744` bytes on the NVIDIA adapter and remain above the aspirational 15 MB target because of user-mode driver commitment. NativeUi 18/18, MaterialRenderer 43/43, Core 33/33, App 74/74, affected real flows, syntax, and diff gates passed. Precommit EXE: 1,866,240 bytes, SHA256 `FF74AEE1392F85807BE60822972E4EE36E6DECA217B83E1F35C564FE957DB71F`, development/dirty identity, Per-Monitor V2, non-stable path. The protected runtime/startup/TOOL identities are unchanged; no install, startup, `C:\TOOL`, push, tag, or release mutation occurred.

### LLP-014 - Continuous Focus And Nonblocking Motion

- Status: `complete`
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
- Completion notes: Completed on 2026-09-22 in the current working tree. `MotionTimeline.{h,cpp}` owns QPC-sampled visibility/focus/press channels with normal durations 170/140/100/72 ms and reduced-motion durations 90/90/80/60 ms; reduced motion removes offset and scale. `MaterialRenderer.{h,cpp}` accepts latest-only renderer-thread motion requests and applies the high-fidelity root offset without UI-thread sleeps; layered fallback uses deferred alpha updates on the 16 ms timer. NativeUi reverses close from the current frame, keeps input focusable from the opening call, preserves query text on rapid Alt+D, and adds press/focus feedback without a second material source. Normal and forced-fallback real `SpotlightFlowBehavior` pass on `\\.\DISPLAY1`; `MotionTimeline` passes 4/4; `MaterialRenderer` passes 43/43; `AdaptiveWindowBehavior` passes 4/4 including reduced motion; `ExecutableIdentity` passes 6/6 after clean-source coverage was updated for the new module; Core/App pass 33/33 and 74/74. Final real performance reports hotkey-to-input-ready P95 94 ms, frame P95 8.759 ms, focus P95 31.93 ms, hidden CPU 0%, stationary hidden counters, and retained hidden Private Bytes growth 39,022,592 bytes after 30 warm opens on this driver. The absolute/retained memory measurement remains recorded without relabeling; hidden CPU and capture polling are zero. Final Development EXE: `artifacts\\native-ui-llp014-dev\\LightLaunchpad.NativeUi.exe`, 1,871,360 bytes, SHA256 `770F2C21A63578E8E4C6950FC48A1701AA5B6EDAE8873EE7405415E5A6320EF2`. Protected runtime path/hash and HKCU Run remain unchanged; no install, `C:\TOOL`, push, tag, or release mutation occurred.

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
