# LightLaunchpad Development Progress

## Handoff Snapshot

- Updated: 2026-08-09 18:46:44 +08:00, Asia/Hong_Kong
- Workspace: `C:\Users\thoma\Desktop\启动台`
- Branch: `codex/apple-first-v1.1`
- HEAD: `d54f106`
- Remote main: `origin/main@33e91c2`
- Active task: `LLP-013 - Unified Adaptive Material Renderer` (`in_progress`)
- Completed tasks: `LLP-001`, `LLP-002`, `LLP-003`, `LLP-004`, `LLP-005`, `LLP-006`, `LLP-007`, `LLP-008`, `LLP-009`, `LLP-010`, `LLP-011`, `LLP-012`
- Next task after verification: `LLP-014 - Continuous Focus And Nonblocking Motion`
- Goal status: active; LLP-013 remains the only task in progress.
- Current gate: LLP-013 Task 1 pure material policies are committed at `d54f106`; specification and code-quality review remain pending before Task 1 can close.

## LLP-013 Task 1 Implementation Evidence

Status at 2026-08-09 18:46 +08:00: implementation committed; LLP-013 remains `in_progress`, and Task 1 review is not yet closed.

Changed files in scope:

- `src/LightLaunchpad.NativeUi/MaterialRenderer.{h,cpp}` - pure output-crop rotation, physical-pixel/DIP conversion, bounded search sampling, adaptive material tone, and one-rebuild lifecycle policies without Win32/GPU ownership.
- `tests/LightLaunchpad.NativeUi.Tests/MaterialRenderer.Tests.{cpp,ps1}` - direct production C++ policy checks and the isolated static WinLibs-linked runner.

TDD and verification evidence:

- RED ran under Windows PowerShell 5.1 with `C:\Users\thoma\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin` prepended to `PATH`: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\LightLaunchpad.NativeUi.Tests\MaterialRenderer.Tests.ps1`. It exited 1 with exact diagnostics `fatal error: MaterialRenderer.h: No such file or directory` and `fatal error: src\LightLaunchpad.NativeUi\MaterialRenderer.cpp: No such file or directory`; `lightlaunchpad-material-renderer-tests-*` roots remained 0 -> 0.
- The plan's identity-crop example was corrected in `03df6b8` so the required partial crop clamps to the selected output bottom (`700`, not the out-of-bounds `840`). Pure implementation and tests were committed in `d54f106` (`test: lock LLP-013 material policies`).
- Fresh GREEN runs reported `MaterialRenderer` 5/5 and `AdaptiveEnvironment` 3/3 under the same WinLibs-first Windows PowerShell 5.1 environment. Material and adaptive temporary roots both remained 0 -> 0; `git diff --cached --check` passed before the implementation commit.
- Task 1 is pure, nonvisual policy work, so screenshot and runtime-performance evidence are not applicable at this checkpoint; real-renderer visual/performance gates remain assigned to later LLP-013 tasks.

Next step:

- Complete Task 1 specification and code-quality review. Do not start Task 2 or mark LLP-013 complete until both reviews close.

## LLP-013 Implementation Plan Checkpoint

Status at 2026-08-09 15:49 +08:00: implementation plan complete; production implementation not started.

Changed files in scope:

- `docs/superpowers/plans/2026-08-09-llp-013-live-material-renderer.md` - exact 10-task, 87-step RED/GREEN plan covering pure coordinate/material/lifecycle policy, immutable scenes, renderer-thread ownership, DDA capture, D3D11/D2D/DComp composition, host/fallback integration, recovery/adaptive behavior, 12-background pixel evidence, performance, full regression, and durable task transition.
- `.learnings/ERRORS.md` - records and resolves the stale-PID safety-check assumption; protected runtime identity is now verified by normalized exact path, process count, hash, and startup command while PID remains observational.
- `docs/ai/{tasks,progress}.md` - records this plan checkpoint without changing the sole `in_progress` task.

Plan verification and technical baseline:

- Required-content self-review passed 18/18 checks; the plan has 10 task sections, 87 checkbox steps, 72 balanced code fences, and zero unfinished markers.
- Every approved design section maps to an implementation/test task: ownership, three-layer composition, bounded material/displacement, live capture, one fallback, one rebuild, adaptive routing, diagnostics/performance, development-only freeze evidence, and 12 controlled backgrounds plus the real desktop.
- MinGW-w64 reports `x86_64-w64-mingw32`, exposes `dcomp.h`, and resolves `d3d11`, `dxgi`, and `dcomp` link libraries. The plan therefore retains the single approved R1 route without an alternate backend.
- The plan names exact source/test/tool files, compiler/link commands, RED/GREEN expectations, commit boundaries, full NativeUi/Core/App gates, product-design audit, pixel/contrast thresholds, and post-commit real-EXE identity checks.
- `git diff --cached --check` passed; staged scope was exactly the plan and error-learning file. Plan commit: `3d70d18a8b317b4811ba63adcce2a6663f716e85` (`docs: plan LLP-013 live material implementation`).

Safety baseline and correction:

- The prior PID `22560` was no longer present. Read-only enumeration found PID `29376`, created 2026-08-04 23:42:39, running the exact protected `artifacts\native-ui-final-fix\LightLaunchpad.NativeUi.exe` path. No process was started, stopped, or replaced.
- Protected EXE SHA256 remains `EBD9363316F45B8449CDA57218A8349C039EFDB6001FC030B69648436C47A0BD`; HKCU Run remains the exact quoted protected path; `%LocalAppData%\Programs\LightLaunchpad\LightLaunchpad.NativeUi.exe` remains absent; `C:\TOOL` EXE SHA256 remains `2E44A11A27C96787EA85873B28CE782FCE9257A8918D10D4F619A27D50679C1A`.
- `LLP_reconstruction/` remains untracked/unstaged and ignored `LightLaunchpad.NativeUi.o` was not adopted. No production C++, EXE build, stable install, startup, `C:\TOOL`, push, tag, or release mutation occurred.

Next step:

- Choose Subagent-Driven Execution (recommended by `writing-plans`) or Inline Execution. Then start Task 1 with the missing `MaterialRenderer` production C++ RED test; LLP-013 remains the only project task in progress.

## LLP-013 Design Checkpoint

Status at 2026-08-09 14:58 +08:00: design approved; implementation not started.

Changed files in scope:

- `docs/superpowers/specs/2026-08-09-llp-013-live-material-renderer-design.md` - approved live-material architecture, lifecycle, single fallback, adaptive policy, performance budgets, and production-test evidence design.
- `.gitignore` - excludes persistent `.superpowers/` visual-companion session artifacts.

Approved decisions and evidence:

- Browser events and terminal confirmation agree on `1C + 2A`: continuously updating live desktop background with restrained cool white-blue material.
- The user approved R1: one DXGI Desktop Duplication, D3D11, Direct2D DeviceContext, and DirectComposition path. Windows.Graphics.Capture and system Desktop Acrylic are explicitly rejected.
- `WDA_EXCLUDEFROMCAPTURE` is limited to the visible high-fidelity session to prevent recursive self-capture. The approved consequence is that Spotlight normally does not appear in public screenshots or screen sharing while live capture is active.
- The spec keeps one latest-only immutable scene slot and one PRD-required no-displacement fallback. It adds no polling while hidden, alternate capture backend, generalized graphics framework, package runtime, or speculative recovery chain.
- Visual acceptance uses 12 reproducible controlled backgrounds as the primary set plus the current real desktop. A development-only freeze/stop-capture evidence mode allows real-EXE onscreen screenshots without recursive feedback; stable builds reject the switch.

Verification and scope:

- Spec self-review passed seven required-content checks with zero placeholders; `git diff --cached --check` passed before commit and the staged scope contained only the spec and `.gitignore`.
- Design commit: `0cf5a5114d77d65ba4c30f16eb83c654fac36063` (`docs: approve LLP-013 live material design`).
- No production C++, NativeUi build, installation, startup entry, `C:\TOOL`, push, tag, or release change occurred in this checkpoint.
- `LLP_reconstruction/` remains untracked and unstaged.

Next step:

- User reviews the committed written spec. After approval, invoke only `writing-plans`, write the implementation plan, and then begin LLP-013 with production C++ RED tests.

## LLP-012 Execution Evidence

Status at 2026-08-02 12:29 +08:00: complete.

Changed files in scope:

- `src/LightLaunchpad.NativeUi/AdaptiveEnvironment.{h,cpp}` - portable foreground/cursor/primary monitor selection, system-preference policy, adaptive solid palettes, reduced-motion decisions, text-scale clamping, and WCAG contrast calculations.
- `src/LightLaunchpad.NativeUi/SceneGeometry.{h,cpp}` - DPI/text-aware main scene, settings columns/rows, transient controls, and native text-input modal geometry shared by drawing, hit testing, navigation, scrolling, and tests.
- `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp` - explicit Per-Monitor-V2 initialization; foreground-monitor positioning; `WM_DPICHANGED`, display, theme, high-contrast, transparency, motion, and text-scale handling; DPI-aware icon/font/resource lifetime; adaptive solid rendering; settings reflow; modal reflow; and development-only real-window snapshots.
- `src/LightLaunchpad.NativeUi/SearchAccessibility.{h,cpp}` - non-focus-stealing UIA notification plus `EVENT_SYSTEM_ALERT` fallback for organization status.
- `tools/build-native-ui.ps1` and `tests/LightLaunchpad.NativeUi.Tests/{AppHost,ExecutableIdentity}.Tests.ps1` - compile/copy the adaptive production module in normal and clean-source builds.
- `tests/LightLaunchpad.NativeUi.Tests/AdaptiveEnvironment.Tests.{cpp,ps1}` - direct production policy, fallback, reduced-motion, text-scale, and contrast checks.
- `tests/LightLaunchpad.NativeUi.Tests/AdaptiveWindowBehavior.Tests.ps1` - isolated real EXE matrix with actual and forced DPI/preferences, UIA Value/focus, cross-process WinEvent announcement, production draw-hit launch, settings UIA/reflow, transition snapshots, and screenshot capture.
- `tests/LightLaunchpad.NativeUi.Tests/{SceneGeometry.Tests.cpp,TextInputBehavior.Tests.ps1,LaunchpadFlowBehavior.Tests.ps1}` - seven shared-geometry checks and real input/Launchpad flows driven by geometry published by the production EXE instead of legacy pixel constants.
- `tests/LightLaunchpad.App.Tests/HostedAppModeSourceTests.cs` - updates one historical string baseline for the required solid-surface backdrop gate; it remains non-authoritative for visual behavior.
- `docs/ai/{tasks,progress}.md` - records verified completion and activates LLP-013.

TDD, systematic-debugging, and visual-audit evidence:

- RED began with a missing `AdaptiveEnvironment` production boundary. GREEN is 3/3 and proves foreground preference, cursor/primary fallback, opaque high-contrast/reduced-transparency surfaces, contrast ratios, reduced motion, and bounded text scaling.
- RED geometry checks expanded from 3 to 7 and now cover main chrome at 96/144/192 DPI, increased-text tile/control expansion, 200% settings columns/rows, and every native text-input modal control/target.
- First real screenshots exposed a second-line dash at 150% and clipped `Adaptive Target` at 200%. Accessible tile geometry now expands from the shared font metrics; recaptured frames show both lines completely.
- Settings audit exposed clipped labels and diagnostic/button overlap. Shared settings metrics now reflow columns, rows, checkboxes, diagnostics, scrolling, and actions; 200% top/bottom recaptures show complete labels and separated Save/Apply/Cancel controls.
- A blank Save label in one bottom capture was isolated to immediate post-scroll `PrintWindow` timing. The evidence driver now waits for child repaint after scrolling; the recapture renders all three actions.
- Full regression found two pre-LLP-012 test drivers still sending historical coordinates. Text input and Launchpad probes now consume exact `searchText`, tile, edit, Undo, content, and region bounds published by the real EXE. Mouse caret, Browse launch, Edit selection, delete/Undo, marquee, item drag, and region drag returned GREEN without weakening production hit testing.
- The real WinEvent listener received the organization status alert while UIA reported that keyboard focus remained on the search edit. The rendered high-contrast case uses black/white/yellow system-derived surfaces with no transparent glass retained.

Fresh verification against implementation commit `b222b1b483d9`:

- Full NativeUi isolated-process gate: all 16 PowerShell suites passed. Direct production totals include adaptive environment 3/3, scene geometry 7/7, interaction 7/7, organization state 4/4, search 7/7, text input 7/7, persistence 7/7, settings transactions 3/3, AppHost 4/4, and executable identity 5/5; all real behavior suites passed.
- Real Spotlight: 7 opens, 2 exact Shell launches, input-ready samples `0,0,0,0,0,0,15` ms and P95 15 ms. Real Launchpad: 2 launches, 3 edit-selection presses, maximum selection 3, exact favorite/delete/Undo/restart persistence.
- Search: 300-item rebuild 400 microseconds and cached pinyin-query P95 86 microseconds. Text edit/undo P95: 200 ns. Catalog input response remained 0 ms during a 1,875 ms STA refresh.
- Adaptive real-window matrix: actual monitor `dpi=144`, then forced `96/100%`, `144/150%` solid/reduced-motion, and `192/200%` high-contrast all passed 4/4; window sizes were 1470x1080, 980x720, 1470x1080, and 1960x1440 respectively.
- Core tests: 33/33 passed. Historical App/source suite: 74/74 passed after its obsolete acrylic-only string was updated; it was not used to prove DPI, UIA, hit testing, high contrast, or rendering.
- Post-commit affected gates passed again: adaptive environment 3/3, scene geometry 7/7, real UIA/IME input, real Launchpad organization flow, and adaptive real-window matrix 4/4.
- Post-commit build/diagnostics: `artifacts\native-ui-llp012-postcommit\LightLaunchpad.NativeUi.exe`, 1,730,560 bytes, SHA256 `3B18E57FCFA0CA391CC4F51C59E5974E041EA87EA0207C320C074D153F13A1C2`, development identity `b222b1b483d9`, Per-Monitor-V2, non-stable executable path, and protected startup path matched.

Visual, monitor, safety, and evidence limits:

- `product-design:audit` inspected current-run original-resolution main and settings frames under `artifacts\native-ui-llp012-adaptive-evidence`: four main cases plus top/bottom settings captures at actual, 100%, 150%, and 200% conditions. They show no text/control clipping, solid fallback leaks, search/content overlap, tile-label loss, or action overlap after the fixes above.
- The current host exposes only `DISPLAY1`, so a second physical monitor screenshot cannot truthfully be produced. The real foreground-window monitor path passed on that display; the production chooser directly proves foreground, cursor fallback, and primary fallback, while synthetic `WM_DPICHANGED` and real `WM_DISPLAYCHANGE` paths prove reposition/rebuild behavior. Installing a display driver or changing the user's display topology was outside ordinary-development safety scope.
- Protected PID 22560 still runs `artifacts\native-ui-final-fix\LightLaunchpad.NativeUi.exe` with unchanged SHA256 `EBD9363316F45B8449CDA57218A8349C039EFDB6001FC030B69648436C47A0BD`; HKCU Run remains its exact quoted path, stable install is absent, and the `C:\TOOL` EXE remains SHA256 `2E44A11A27C96787EA85873B28CE782FCE9257A8918D10D4F619A27D50679C1A`.
- `LLP_reconstruction/` remains untracked/unstaged and ignored `LightLaunchpad.NativeUi.o` remains excluded. No stable-install, startup, `C:\TOOL`, push, tag, or release mutation occurred.
- Scoped implementation commit: `b222b1b` (`feat: complete LLP-012 adaptive DPI foundation`).
- Task transition: LLP-012 marked `done`; LLP-013 marked as the sole `in_progress` task.

## LLP-011 Execution Evidence

Status at 2026-08-02 10:48 +08:00: complete.

Changed files in scope:

- `src/LightLaunchpad.NativeUi/OrganizationState.{h,cpp}` - production pending-delete correlation, exact layout snapshots, one-action undo ownership, 8,000 ms deadlines, in-flight Shell completion protection, partial restore retry, and region snapshots.
- `src/LightLaunchpad.NativeUi/ShellBroker.{h,cpp}` - `IFileOperation` recycle/restore requests on the dedicated STA, `FOFX_RECYCLEONDELETE`/`FOFX_ADDUNDORECORD`, `PostDeleteItem` PIDL capture, exact parent/name restore, collision refusal, retryable records, and bounded record cleanup.
- `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp` - favorite/unfavorite context actions and local persistence, Fluent favorite badges, Shell-result-before-layout delete commit, failed-selection preservation, concise transient feedback with an 8-second Undo target, exact insertion-order restore, reversible region deletion, and real-EXE organization/region probes.
- `tools/build-native-ui.ps1` and `tests/LightLaunchpad.NativeUi.Tests/{AppHost,ExecutableIdentity}.Tests.ps1` - compile/copy the new production organization module in normal and clean-source builds.
- `tests/LightLaunchpad.NativeUi.Tests/OrganizationState.Tests.{cpp,ps1}` - four direct production-state tests for success, expiry, in-flight completion, failed/partial batches, retryable partial restore, and region snapshots.
- `tests/LightLaunchpad.NativeUi.Tests/OrganizationShellBehavior.Tests.ps1` - isolated real NativeUi EXEs for 2-success/1-failure recycle, exact-content restore, collision refusal/retry, and delete-region-to-Uncategorized/exact undo.
- `tests/LightLaunchpad.NativeUi.Tests/LaunchpadFlowBehavior.Tests.ps1` - real context command, two-item favorite persistence, batch recycle, clickable Undo, exact order assertion, restart proof, and current-run visual evidence.
- `tests/LightLaunchpad.App.Tests/HostedAppModeSourceTests.cs` - updates one historical source boundary from the retired direct `affectedPaths` spelling to the organization transaction/restore boundary; it remains non-authoritative for Shell and UI behavior.
- `docs/ai/{tasks,progress}.md` - records verified completion and activates LLP-012.

TDD and systematic-debugging evidence:

- Initial RED could not compile because `OrganizationState` did not exist. GREEN is 4/4 and proves that layout never changes before a matched Shell completion, a full failure preserves every item, a partial batch removes only confirmed paths, and region snapshots expire after exactly 8,000 ms.
- A delayed-restore test exposed that a user-invoked Undo could expire while Shell STA work was in flight. `BeginRecycleUndo` now freezes the accepted action until completion; partial completion opens a fresh bounded retry window.
- The first collision probe correctly refused overwrite but then lost its retry record because cleanup equated “original path exists” with “restore succeeded.” Cleanup now uses only explicit restored paths; the real EXE rejects the collision, preserves its content, and restores the original `charlie` payload after the collision is removed.
- Visual audit found Alpha/Beta/Gamma returning as Alpha/Gamma/Beta. Successful delete had renumbered the retained sibling, causing order collisions on undo. Restore now reinserts each snapshot at its original region index; the real-EXE ready snapshot and recaptured frame both prove Alpha/Beta/Gamma.
- A new fully failed delete originally left the older UI undo token visible even though ShellBroker had replaced it. A RED regression now requires new delete completion to retire the prior action; GREEN keeps UI and Shell token ownership synchronized.
- One parallel verification run left two interrupted temporary flow probes and caused the clipboard test to paste an empty value. Their command lines were verified under GUID-scoped `%TEMP%` roots, only those two test PIDs were terminated, and the input probe passed when rerun alone. The protected production PID was never touched.

Fresh verification against implementation commit `707510a8d024`:

- `OrganizationState.Tests.ps1` - 4/4 passed.
- `OrganizationShellBehavior.Tests.ps1` - real EXE: batch removed 2, failed 1, restored 2; collision failed 1 without overwrite; retry restored 1 with exact content. Region delete moved only to Uncategorized, preserved all source files, and restored exact metadata, placement, and order.
- `LaunchpadFlowBehavior.Tests.ps1` - 2 Shell launches, 3 edit-selection presses, max selection 2; two favorites persisted, exact delete/undo order restored, and restart catalog republished all 3 items with both favorites.
- `SearchIndex.Tests.ps1` - 7/7 passed; 300-item rebuild 391 microseconds, cached pinyin-query P95 94 microseconds; favorite tie-break, empty-state inclusion, history reset preservation, and JSON restart round trip passed.
- `InteractionState.Tests.ps1` 7/7; `SceneGeometry.Tests.ps1` 3/3; `TextInput.Tests.ps1` 7/7 with 200 ns P95; real UIA/clipboard/IME behavior passed with value `中a文b末`, caret 5, candidate `(153,76)`.
- `SpotlightFlowBehavior.Tests.ps1` - 7 opens, 2 exact launches, 63 ms input-ready P95.
- `CatalogShellBroker.Tests.ps1` - input response 0 ms during a 1,859 ms refresh, import 1 then 0, one skipped reparse point, and future history preserved.
- `Persistence.Tests.ps1` 7/7; `SettingsTransactions.Tests.ps1` 3/3; `AppHost.Tests.ps1` 4/4 (`shellReadyMs=16`, `probeExitMs=368`, `settingsWindowMs=169`, `logBytes=1277`); `ExecutableIdentity.Tests.ps1` 5/5.
- Core Release 33/33 passed. Historical App Release 74/74 passed after updating the one obsolete source-boundary assertion; it was not used to prove recycle, favorite, interaction, or visual correctness.
- Post-commit build/diagnostics: `artifacts\native-ui-llp011-postcommit\LightLaunchpad.NativeUi.exe`, 1,694,720 bytes, SHA256 `38B8FFEEF0CABC7FE0A365C98CB82158D359A2AC9D4F026E2AF4E7BDD4DEA8EA`, development identity `707510a8d024`, Per-Monitor-V2 manifest, non-stable path, and protected startup path all matched.

Visual, safety, and evidence limits:

- `product-design:audit` preflight found no saved user context and accepted four current-run, original-resolution frames under `artifacts\native-ui-llp011-visual-evidence`: `08-edit-favorites-saved.png`, `09-edit-recycle-undo.png`, `10-edit-undo-restored.png`, and `11-restart-favorites-persisted.png`. They show distinct Fluent favorite badges, concise bottom feedback, a separated Undo action, exact selection/order restoration, and restart persistence.
- Visible hierarchy and action discoverability are acceptable for LLP-011. Screenshots alone do not prove UIA live announcements, keyboard focus on the transient action, contrast ratios, target size in DIP, high contrast, increased text size, or multi-monitor DPI transitions; these are explicitly activated as LLP-012 work rather than claimed complete.
- Protected PID 22560 still runs `artifacts\native-ui-final-fix\LightLaunchpad.NativeUi.exe` with SHA256 `EBD9363316F45B8449CDA57218A8349C039EFDB6001FC030B69648436C47A0BD`; HKCU Run remains its exact quoted path, stable install is absent, and the `C:\TOOL` EXE remains SHA256 `2E44A11A27C96787EA85873B28CE782FCE9257A8918D10D4F619A27D50679C1A`.
- `LLP_reconstruction/` remains untracked/unstaged and ignored `LightLaunchpad.NativeUi.o` remains excluded. No stable-install, startup, `C:\TOOL`, push, tag, or release mutation occurred.
- Scoped implementation commit: `707510a` (`feat(native-ui): add safe organization undo`).
- Task transition: LLP-011 marked `done`; LLP-012 marked as the sole `in_progress` task.

## LLP-010 Execution Evidence

Status at 2026-08-02 09:59 +08:00: complete.

Changed files in scope:

- `src/LightLaunchpad.NativeUi/InteractionState.{h,cpp}` - production `LaunchpadState` with explicit Browse/Edit intent, launch-vs-selection actions, Enter behavior, and capability guards for selection, marquee, item drag, and region drag.
- `src/LightLaunchpad.NativeUi/SceneGeometry.{h,cpp}` - one portable scene snapshot for item/region content and viewport rectangles, columns, scrolling, empty regions, hit lookup, and intersection.
- `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp` - integrates Edit/Done and Ctrl+E, Browse single-click press/release launch, edit-only selection/context/organization paths, keyboard focus ownership, shared geometry across D2D/GDI drawing and interaction consumers, eight tile visual states, and an isolated real-EXE Launchpad probe. The obsolete 1.16x, -4 px focused-tile backdrop sample is now 1.0x with zero displacement.
- `tools/build-native-ui.ps1` and `tests/LightLaunchpad.NativeUi.Tests/{AppHost,ExecutableIdentity}.Tests.ps1` - compile/copy `SceneGeometry` in normal and clean-source builds.
- `tests/LightLaunchpad.NativeUi.Tests/InteractionState.Tests.cpp` - direct production Browse/Edit semantics, extended from 5 to 7 named checks.
- `tests/LightLaunchpad.NativeUi.Tests/SceneGeometry.Tests.{cpp,ps1}` - exact draw/hit/scroll geometry at 100%, 150%, and 200% metrics, searched layout, empty regions, and bounds lookup.
- `tests/LightLaunchpad.NativeUi.Tests/LaunchpadFlowBehavior.Tests.ps1` - Per-Monitor-V2 isolated real NativeUi EXE, Shell launch markers, Browse/Edit gesture probe, keyboard/mouse paths, and eight screenshot states.
- `tests/LightLaunchpad.App.Tests/HostedAppModeSourceTests.cs` - updates historical regression strings for the new visual-state/shared-preview structure and bans the retired 1.16x focus sampling; this suite is not visual or interaction authority.
- `docs/ai/{tasks,progress}.md` - records verified completion and activates LLP-011.

TDD and systematic-debugging evidence:

- RED: production Launchpad tests initially lacked explicit Browse/Edit types. GREEN proves Browse click/Enter launch intent while all edit gestures are rejected, and Edit selection/toggle succeeds while Enter cannot accidentally launch.
- RED: `SceneGeometry` did not exist. GREEN uses its exact item/region slots for rendering, hit testing, keyboard columns, scrolling, marquee intersection, and drop computation; all three scale fixtures match draw and hit rectangles byte-for-byte.
- The first real-EXE mouse run missed its visible tile even though the hit cache contained two entries. Probe evidence showed sent `(108,216)` arrived as `(162,324)`, exactly 1.5x, identifying cross-process DPI virtualization in the PowerShell driver. The driver now enters Per-Monitor-V2 before creating or addressing windows, after which the full flow passes.
- The real flow performs two successful Browse Shell launches. A Browse drag is explicitly rejected; Edit click, Ctrl-click, Enter, marquee, multi-item drag, and region drag produce no extra launch; `maxSelectedItems=2`, mode transitions are exactly Edit then Done, and Browse retains zero persistent selections.
- Product-design audit expanded a two-item drag fixture to three items so keyboard focus and a real unselected drop target could be inspected, rather than inferred from code. The final drag frame shows a green target and a separate `2 apps` source badge.
- Static prohibition review found legacy focused-tile backdrop sampling at 1.16x/-4 px. A regression assertion failed RED, production moved to 1.0x/zero displacement, and App 74/74 plus both real Spotlight/Launchpad flows returned GREEN. The remaining `Sleep(25)` is bounded secondary-instance IPC polling, not UI animation; ShellBroker's test-only delay runs on its worker.
- Three historical App source assertions expected the retired boolean-selection and render-loop spellings. They were updated to follow the new enum/resolver, unified background, and cached `hideTileForDrag` structure; production code was not regressed to satisfy strings.

Fresh verification against implementation commit `689dc61619fd`:

- `tests\LightLaunchpad.NativeUi.Tests\InteractionState.Tests.ps1` - 7/7 passed.
- `tests\LightLaunchpad.NativeUi.Tests\SceneGeometry.Tests.ps1` - 3/3 passed, including exact 100/150/200% draw-hit-scroll correspondence.
- `tests\LightLaunchpad.NativeUi.Tests\LaunchpadFlowBehavior.Tests.ps1` - real EXE passed two browse launches and all edit-only/no-accidental-launch checks; `launches=2`, `editSelections=3` (two selection presses plus drag start), `maxSelected=2`.
- `tests\LightLaunchpad.NativeUi.Tests\SpotlightFlowBehavior.Tests.ps1` - canonical paths remained green; 7 opens, 2 launches, input-ready samples `46,46,47,47,47,47,63` ms, P95 63 ms.
- `tests\LightLaunchpad.NativeUi.Tests\TextInput.Tests.ps1` - 7/7 passed; 2,000 production edit+undo samples measured 200 ns P95.
- `tests\LightLaunchpad.NativeUi.Tests\TextInputBehavior.Tests.ps1` - real UIA/keyboard/clipboard/IME flow passed; final value `中a文b末`, caret 5, candidate point `(153,76)`.
- `tests\LightLaunchpad.NativeUi.Tests\SearchIndex.Tests.ps1` - 7/7 passed; 300-item rebuild 352 microseconds and cached pinyin-query P95 88 microseconds.
- `tests\LightLaunchpad.NativeUi.Tests\CatalogShellBroker.Tests.ps1` - 3/3 passed; input response 0 ms during a 1,890 ms refresh, generation 2, import counts 1 then 0, skipped reparse points 1, and future history unchanged.
- `tests\LightLaunchpad.NativeUi.Tests\Persistence.Tests.ps1` - 7/7 passed.
- `tests\LightLaunchpad.NativeUi.Tests\SettingsTransactions.Tests.ps1` - 3/3 passed.
- `tests\LightLaunchpad.NativeUi.Tests\AppHost.Tests.ps1` - 4/4 passed; `shellReadyMs=32`, `probeExitMs=375`, `settingsWindowMs=160`, `logBytes=1279`.
- `tests\LightLaunchpad.NativeUi.Tests\ExecutableIdentity.Tests.ps1` - 5/5 passed.
- `dotnet run --project tests\LightLaunchpad.Core.Tests\LightLaunchpad.Core.Tests.csproj` - 33/33 passed.
- `dotnet run --project tests\LightLaunchpad.App.Tests\LightLaunchpad.App.Tests.csproj` - 74/74 passed; historical regression baseline only.
- Post-commit build/diagnostics - `artifacts\native-ui-llp010-postcommit\LightLaunchpad.NativeUi.exe`, 1,650,176 bytes, SHA256 `C23EF857CE56875FEC0705AB0F7E51AC5B3357EDD927F2C6C3AE91450B9DFA69`, version `1.11.0-dev+689dc61619fd92f5880a0caf567d10d8b778630c.dirty`; executable path, commit, development singleton/window class, Per-Monitor-V2 identity, and protected startup path matched.

Visual, safety, and evidence limits:

- `product-design:audit` accepted eight current-run real-EXE frames under `artifacts\native-ui-llp010-final\visual-evidence`: Browse normal, hover, pressed, keyboard focus; Edit no-selection, selected, multiselected; and drag source plus green drop target. Edit/Done identity and all state boundaries remain readable without nested glass or magnified backdrop sampling.
- The production geometry module directly proves shared scaled metrics at 100%, 150%, and 200%; actual multi-monitor/OS-DPI screenshots, increased text size, high contrast, and reduced transparency remain explicitly assigned to LLP-012 and are not claimed here.
- Protected PID 22560 still runs `artifacts\native-ui-final-fix\LightLaunchpad.NativeUi.exe` with unchanged SHA256 `EBD9363316F45B8449CDA57218A8349C039EFDB6001FC030B69648436C47A0BD`; HKCU Run remains its exact quoted path, the stable install remains absent, and the `C:\TOOL` EXE remains SHA256 `2E44A11A27C96787EA85873B28CE782FCE9257A8918D10D4F619A27D50679C1A`.
- `LLP_reconstruction/` remains untracked/unstaged and ignored `LightLaunchpad.NativeUi.o` was not adopted. No stable-install, startup, `C:\TOOL`, push, tag, or release mutation occurred.
- Scoped implementation commit: `689dc61` (`feat(native-ui): separate launchpad browse and edit modes`).
- Task transition: LLP-010 marked `done`; LLP-011 marked as the sole `in_progress` task.

## LLP-009 Execution Evidence

Status at 2026-08-02 09:05 +08:00: complete.

Changed files in scope:

- `src/LightLaunchpad.NativeUi/InteractionState.{h,cpp}` - production Spotlight focus/state model for open, result publication, stable-ID preservation, safe missing-focus behavior, result navigation, focus traversal, printable input, progressive Escape, and Enter intent.
- `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp` - integrates the state model with cached SearchIndex results, keyboard/mouse handling, UIA focus, rendering, Shell launch requests, live Catalog publication, restrained empty state, and a real-EXE flow/refresh probe. Spotlight click launches directly and its selection, context, marquee, and drag paths are disabled; Launchpad behavior is otherwise retained for LLP-010.
- `src/LightLaunchpad.NativeUi/SearchAccessibility.{h,cpp}` - reports custom search ownership rather than equating HWND focus with search focus after Down/Tab navigation.
- `tools/build-native-ui.ps1` and clean-source fixtures in `tests/LightLaunchpad.NativeUi.Tests/{AppHost,ExecutableIdentity}.Tests.ps1` - compile and package `InteractionState` in normal and isolated builds.
- `tests/LightLaunchpad.NativeUi.Tests/InteractionState.Tests.{cpp,ps1}` - direct production state tests for open/Enter, arrows, printable/Tab/Shift+Tab, Escape, and stable refresh without launch substitution.
- `tests/LightLaunchpad.NativeUi.Tests/{LaunchProbeTarget.cpp,SpotlightFlowBehavior.Tests.ps1}` - isolated real NativeUi EXE, async Catalog/Shell STA, actual launch-target marker, UI Automation focus, live insertion/removal refresh, latency, and screenshot evidence.
- `tests/LightLaunchpad.NativeUi.Tests/SearchIndex.Tests.cpp` - explicitly verifies the empty Spotlight cap of 10 within the PRD's 8-12 range, alongside disabled visibility.
- `tests/LightLaunchpad.App.Tests/HostedAppModeSourceTests.cs` - keeps the historical feather-boundary assertion compatible with the explicit auxiliary-control exception; it is not used as visual or interaction authority.
- `docs/ai/{tasks,progress}.md` - records verified completion and activates LLP-010.

TDD and systematic-debugging evidence:

- RED: the production test target initially could not compile because `InteractionState.h/.cpp` did not exist. GREEN covers search-only open focus, search Enter, Down/arrows, printable return, forward/reverse Tab, progressive Escape, stable-ID reorder, missing-ID no-op Enter, and explicit Down recovery.
- The first real-EXE flow probe measured 234 ms P95 because `ShowSpotlightImmediately` synchronously forced `RedrawWindow`. Removing the synchronous redraw restored nonblocking invalidation and produced 47-63 ms P95 across fresh runs without UI-thread Sleep animation.
- A ranking fixture initially assumed alphabetical order; the real production SearchIndex correctly ranked shorter `Flow Beta` before `Flow Alpha`. The expectation was corrected without weakening production ranking, and Right then launched the exact second result.
- Product-design audit RED: the keyboard-focused auxiliary control was invisible because bottom content feathering blended it into the glass base. `ApplySpotlightPerPixelAlpha` now explicitly preserves the auxiliary control while retaining search/content feather boundaries; the recaptured focused state is visible and the latency budget remains green.
- The first live-refresh probe observed an automatic open refresh rather than the intended insertion refresh. The test now waits for the open publication, establishes result focus, then performs controlled insertion and removal generations. The real EXE retained `Flow Beta` through reorder, exposed no replacement after removal, and produced no third launch on Enter.
- The refresh fixture is a validated regular file inside a GUID-scoped temporary Launchpad; it is moved only to a validated path inside that same test root, and the root is checked for reparse points before cleanup.

Fresh post-commit verification against `7bb1d0a4f8bd`:

- `tests\LightLaunchpad.NativeUi.Tests\InteractionState.Tests.ps1` - 5/5 passed, including stable reorder and missing-ID no-substitution behavior.
- `tests\LightLaunchpad.NativeUi.Tests\SpotlightFlowBehavior.Tests.ps1 -EvidenceDirectory artifacts\native-ui-llp009-postcommit\visual-evidence` - real EXE passed both canonical Alt+D launch paths, search/result/aux focus traversal, printable return, progressive Escape, settings activation, exact Shell launch targets, live Catalog insertion/removal, and unintended-launch prevention; 7 opens, 2 successful launches, input-ready samples `31,47,47,47,47,47,62` ms, P95 62 ms.
- `tests\LightLaunchpad.NativeUi.Tests\SearchIndex.Tests.ps1` - 7/7 passed; empty visibility/cap passed, 300-item rebuild 412 microseconds and cached pinyin-query P95 97 microseconds.
- `tests\LightLaunchpad.NativeUi.Tests\TextInput.Tests.ps1` - 7/7 passed; 2,000 production edit+undo samples measured 200 ns P95.
- `tests\LightLaunchpad.NativeUi.Tests\TextInputBehavior.Tests.ps1` - real EXE passed UIA/selection/clipboard/IME behavior; final value `中a文b末`, caret 5, candidate point `(153,76)`.
- `tests\LightLaunchpad.NativeUi.Tests\CatalogShellBroker.Tests.ps1` - 3/3 passed; `inputResponseMs=0` during a 1,891 ms refresh, UI thread `20720`, STA broker thread `28136`, generation 2, import counts 1 then 0, skipped reparse points 1, and future search history unchanged.
- `tests\LightLaunchpad.NativeUi.Tests\Persistence.Tests.ps1` - 7/7 passed.
- `tests\LightLaunchpad.NativeUi.Tests\SettingsTransactions.Tests.ps1` - 3/3 passed.
- `tests\LightLaunchpad.NativeUi.Tests\AppHost.Tests.ps1` - 4/4 passed; `shellReadyMs=16`, `probeExitMs=390`, `settingsWindowMs=162`, `logBytes=1279`.
- `tests\LightLaunchpad.NativeUi.Tests\ExecutableIdentity.Tests.ps1` - 5/5 passed.
- `dotnet run --project tests\LightLaunchpad.Core.Tests\LightLaunchpad.Core.Tests.csproj --configuration Release` - 33/33 passed.
- `dotnet run --project tests\LightLaunchpad.App.Tests\LightLaunchpad.App.Tests.csproj --configuration Release` - 74/74 passed; historical regression baseline only.
- Post-commit build/diagnostics - `artifacts\native-ui-llp009-postcommit\LightLaunchpad.NativeUi.exe`, build 23,307 ms, 1,637,888 bytes, SHA256 `968DC465474F5B81C87A1F46940C4A6D87CE6D7AA06FDBDBE5C90B4D192C0974`, diagnostics exit 0 in 244 ms, version `1.11.0-dev+7bb1d0a4f8bd.dirty`, executable path, commit, channel, singleton, and startup identities matched. The development dirty label is expected because the protected untracked `LLP_reconstruction/` must not be adopted or deleted.

Visual, safety, and evidence limits:

- `product-design:audit` accepted four current-run screenshots: step 1 `00-empty-search.png` is a restrained disabled-recents prompt with search as sole focus; step 2 `01-search-focus.png` shows results without painted selection; step 3 `02-result-focus.png` shows one distinct keyboard focus frame; step 4 `03-auxiliary-focus.png` shows a visible but subordinate bottom-right auxiliary focus state. All are under `artifacts\native-ui-llp009-postcommit\visual-evidence` and were inspected at original resolution.
- Keyboard behavior and Shell launch correctness come from the real-EXE probe, not screenshots. Screenshots alone cannot establish UIA semantics, contrast ratios, DPI scaling, high contrast, or assistive-technology reading order; those release gates remain assigned to LLP-012 and LLP-016.
- Protected PID 22560 still runs `artifacts\native-ui-final-fix\LightLaunchpad.NativeUi.exe` with unchanged SHA256 `EBD9363316F45B8449CDA57218A8349C039EFDB6001FC030B69648436C47A0BD`; HKCU Run remains its exact quoted path, the stable install remains absent, and the `C:\TOOL` EXE remains SHA256 `2E44A11A27C96787EA85873B28CE782FCE9257A8918D10D4F619A27D50679C1A`.
- `LLP_reconstruction/` remains untracked/unstaged and ignored `LightLaunchpad.NativeUi.o` was not adopted. No stable-install, startup, `C:\TOOL`, push, tag, or release mutation occurred.
- Scoped implementation commit: `7bb1d0a` (`feat(native-ui): complete Spotlight keyboard state machine`).
- Task transition: LLP-009 marked `done`; LLP-010 marked as the sole `in_progress` task.

## LLP-008 Execution Evidence

Status at 2026-08-02 08:14 +08:00: complete.

Changed files in scope:

- `src/LightLaunchpad.NativeUi/TextInput.{h,cpp}` - production UTF-16 text buffer with normalized caret/anchor selection, surrogate-safe character and word movement/deletion, clipboard primitives, bounded undo/redo, and inline IME composition/result/duplicate-suppression state.
- `src/LightLaunchpad.NativeUi/SearchAccessibility.{h,cpp}` - native server-side UI Automation provider exposing the search edit role/name/ID, Value and Text patterns, focus state, document text, selection, and selectable text ranges.
- `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp` - integrates caret-aware rendering and horizontal scrolling, selection and composition visuals, mouse positioning/drag selection, standard keyboard edits, Win32 clipboard commands, IMM composition/candidate placement, composition-time navigation suspension, UIA events, and an isolated real-EXE probe. Only committed text enters formal search filtering; composition text is rendered inline without changing the query.
- `tools/build-native-ui.ps1` and clean-source fixtures in `tests/LightLaunchpad.NativeUi.Tests/{AppHost,ExecutableIdentity}.Tests.ps1` - compile the input/accessibility modules and link documented IMM, OLE Automation, and UI Automation libraries in normal and isolated builds.
- `tests/LightLaunchpad.NativeUi.Tests/TextInput.Tests.{cpp,ps1}` - direct production C++ behavior, Unicode-boundary, IME duplicate, undo/redo, and latency coverage.
- `tests/LightLaunchpad.NativeUi.Tests/TextInputBehavior.Tests.ps1` - isolated real-EXE UIA, Win32 message, input-queue modifier, clipboard-preservation, mouse selection, English/Chinese insertion, candidate-position, visible-render, and repeatable screenshot probe.
- `tests/LightLaunchpad.App.Tests/{HostedAppModeSourceTests.cs,Program.cs}` - removes retired Tab-completion assertions and retains historical module-boundary regression checks only; it is not used as input/IME behavior authority.
- `docs/ai/{tasks,progress}.md` - records verified completion and activates LLP-009.

TDD and systematic-debugging evidence:

- RED: the initial production target could not compile because `TextInput.h/.cpp` did not exist. GREEN exercises middle insertion, reverse selection replacement, UTF-16 surrogate boundaries, word/character navigation and deletion, clipboard primitives, select-all, undo/redo, composition replacement, cancellation, and modern-result/legacy-character duplicate suppression.
- The first real-EXE UIA probe exposed PowerShell's actual `System.Windows.Automation.Text.*` enum namespace. A later global `SendInput` attempt was correctly rejected by Windows foreground-input policy; the test now attaches only to the isolated probe input queue, sets and restores its keyboard state, and drives the production `GetKeyState` shortcut branches without sending keys to another window.
- RED: visibility was sampled from `WM_DESTROY`, where a correctly rendered window is necessarily no longer visible. GREEN records visibility immediately after the real window is shown and reports it at shutdown.
- Review caught that `SearchDisplayText()` made uncommitted IME composition affect formal filtering. GREEN follows PRD section 6.2: committed text alone drives `SearchIndex`, while composition text remains visibly inserted at the caret and updates immediately.
- Clipboard behavior tests snapshot and restore the user's full Windows Forms data object in `finally`; all process/configuration data uses isolated temporary roots, and the behavior probe skips tray, global hotkey, and startup writes.

Fresh post-commit verification against `d7f398f9da37`:

- `tests\LightLaunchpad.NativeUi.Tests\TextInput.Tests.ps1` - 7/7 passed; 2,000 production edit+undo samples measured 400 ns P95.
- `tests\LightLaunchpad.NativeUi.Tests\TextInputBehavior.Tests.ps1` - real EXE passed UIA Value/Text discovery and operation, UIA selection replacement, mouse caret positioning and drag selection, Home/Delete, Ctrl+C/X/V/A/Z/Y, English and Chinese insertion at start/middle/end, visible render state, and IMM candidate placement; final value `中a文b末`, caret 5, candidate point `(153,76)`.
- `tests\LightLaunchpad.NativeUi.Tests\SearchIndex.Tests.ps1` - 7/7 passed; 300-item rebuild 394 microseconds and cached pinyin-query P95 88 microseconds.
- `tests\LightLaunchpad.NativeUi.Tests\CatalogShellBroker.Tests.ps1` - 3/3 passed; `inputResponseMs=0` during a 1,875 ms refresh, UI thread `20608`, STA broker thread `5884`, generation 2, import counts 1 then 0, skipped reparse points 1, and future search history unchanged.
- `tests\LightLaunchpad.NativeUi.Tests\Persistence.Tests.ps1` - 7/7 passed.
- `tests\LightLaunchpad.NativeUi.Tests\SettingsTransactions.Tests.ps1` - 3/3 passed.
- `tests\LightLaunchpad.NativeUi.Tests\AppHost.Tests.ps1` - 4/4 passed; `shellReadyMs=31`, `probeExitMs=367`, `settingsWindowMs=199`, `logBytes=1279`.
- `tests\LightLaunchpad.NativeUi.Tests\ExecutableIdentity.Tests.ps1` - 5/5 passed.
- `dotnet run --project tests\LightLaunchpad.Core.Tests\LightLaunchpad.Core.Tests.csproj` - 33/33 passed.
- `dotnet run --project tests\LightLaunchpad.App.Tests\LightLaunchpad.App.Tests.csproj` - 74/74 passed; historical regression baseline only.
- Post-commit build/diagnostics - `artifacts\native-ui-llp008-postcommit\LightLaunchpad.NativeUi.exe`, build 23,198 ms, 1,624,064 bytes, SHA256 `E7D92E1F4BEBA532CBD6432F31D2D65E0CBDCB3C48DC5CF0C21D22B3359A09F1`, diagnostics exit 0 in 291 ms, version `1.11.0-dev+d7f398f9da371153892e5e6fadfb20042c2dfaa4.dirty`, executable path and commit identities matched.

Visual, safety, and evidence limits:

- `product-design:audit` inspection of `artifacts\native-ui-llp008-postcommit\visual-evidence\{llp008-caret-ime,llp008-selection}.png` confirmed vertically centered unclipped Chinese/Latin text, a distinct insertion caret, a visible blue selection state, stable search-bar bounds, and no duplicate glyph rendering in the matched real-EXE state. The behavior probe and candidate-coordinate assertion, not the screenshots alone, establish input correctness.
- This task does not claim the 12-background adaptive-material, three-DPI, high-contrast, or full animation release gates; those remain assigned to LLP-012 through LLP-016. It does establish the production input/IME/accessibility foundation those gates consume.
- Protected PID 22560 still runs `artifacts\native-ui-final-fix\LightLaunchpad.NativeUi.exe` with unchanged SHA256 `EBD9363316F45B8449CDA57218A8349C039EFDB6001FC030B69648436C47A0BD`; HKCU Run remains its exact quoted path, the stable install remains absent, and the `C:\TOOL` EXE remains SHA256 `2E44A11A27C96787EA85873B28CE782FCE9257A8918D10D4F619A27D50679C1A`.
- `LLP_reconstruction/` remains untracked/unstaged and ignored `LightLaunchpad.NativeUi.o` was not adopted. No stable-install, startup, `C:\TOOL`, push, tag, or release mutation occurred.
- Scoped implementation commit: `d7f398f` (`feat(native-ui): complete search text and IME controller`).
- Task transition: LLP-008 marked `done`; LLP-009 marked as the sole `in_progress` task.

## LLP-007 Execution Evidence

Status at 2026-08-02 07:24 +08:00: complete.

Changed files in scope:

- `src/LightLaunchpad.NativeUi/SearchIndex.{h,cpp}` - production normalized index, complete/initial pinyin candidates, deterministic match classes and stable-ID ordering, bounded Damerau-Levenshtein typo matching, usage/favorite history, privacy controls, reset, and strict JSON schema parsing.
- `src/third_party/pinyin-data/{PinyinData.generated.h,LICENSE.MIT}` and `tools/vendor-pinyin-data.ps1` - pinned MIT `mozillazg/pinyin-data` commit `923b108dc5d45dee061324c011b478fb649f8b73`, fail-closed upstream hashes, 8,105 code points, 2,144 reading sets, generated-header SHA256 `2D81EAEA6138350852AFD738E6DD9AAB5D381F54B3759B7352250A9FC81E28F5`, and adjacent license SHA256 `9C048697BE2502A16E8BCB282D5D465A07295B2DEF0FFB05A269C5D39DBE1586`.
- `src/LightLaunchpad.NativeUi/Catalog.{h,cpp}` and `ShellBroker.{h,cpp}` - separate alias/subtitle metadata, resolve shortcut/URL metadata on the Shell STA, load immutable history with catalog snapshots, publish persistence identity, and protect malformed/future history as read-only.
- `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp` - replaces the 129-character handwritten index with production search results, preserves focus by stable ID, keeps normal path text out of normal ranking, records only successful launches, queues atomic history persistence, preserves history across refresh races, and exposes privacy/history diagnostics.
- `tools/build-native-ui.ps1` and clean-source fixtures in `tests/LightLaunchpad.NativeUi.Tests/{AppHost,ExecutableIdentity}.Tests.ps1` - compile and package the production search module and pinned data in normal and isolated builds.
- `tests/LightLaunchpad.NativeUi.Tests/SearchIndex.Tests.{cpp,ps1}` - direct production C++ behavior and performance tests plus dependency-integrity checks.
- `tests/LightLaunchpad.NativeUi.Tests/CatalogShellBroker.Tests.ps1` - real-EXE future-history non-overwrite probe alongside the existing STA/input/import gates.
- `tests/LightLaunchpad.App.Tests/HostedAppModeSourceTests.cs` - updates the historical regression boundary to the new module; it is not treated as search behavior authority.
- `docs/ai/{tasks,progress}.md` - records verified completion and activates LLP-008.

TDD and systematic-debugging evidence:

- RED: the initial production test target could not compile because `SearchIndex` did not exist. GREEN added the production C++ module and exercised exact, prefix, word-boundary, contains, alias, full pinyin, initials, typo, duplicate-name determinism, stable focus, history ranking, privacy, reset, and JSON round trip.
- A first ranking RED used `My Alpha Tool` as a contains fixture even though it is a word-boundary match. The fixture was corrected to `MyAlphaTool`; the implementation retained the PRD order rather than weakening the boundary rule.
- The old 129-character map was measured against current launch-folder representatives: `个人中心`, `黑神话悟空`, `三角洲行动`, `觅长生`, `网易云音乐`, and `给梨加速器` produced 0/6 complete expected forms. The pinned generated table produces 6/6 complete forms and all six expected initials.
- RED review probe: a future-schema `search-history.json` was rejected on load but the next save was still queued, risking a downgrade overwrite. GREEN marks unsafe history read-only; the real EXE reports `queued=False` and preserves SHA256 `5D5FD4CC37218C4645A6E7FE72810EAA5C668D38441E15C62CCF35E15256AB6C` byte-for-byte.
- Review hardening prevents a late catalog refresh from replacing newer in-memory launch history, removes per-entry pinyin-vector copies from the query hot path, caps polyphonic candidate expansion, and records usage only after the Shell broker reports successful launch.

Fresh post-commit verification against `b092424f2844`:

- `tests\LightLaunchpad.NativeUi.Tests\SearchIndex.Tests.ps1` - 7/7 passed; dependency hashes passed, current-name pinyin coverage 6/6, 300-item rebuild 402 microseconds, cached pinyin query P95 90 microseconds over 100 iterations.
- `tests\LightLaunchpad.NativeUi.Tests\CatalogShellBroker.Tests.ps1` - 3/3 passed; `inputResponseMs=0` during a 1,890 ms refresh, UI thread `43808`, STA broker thread `43164`, generation 2, import counts 1 then 0, skipped reparse points 1, and future search history not queued or changed.
- `tests\LightLaunchpad.NativeUi.Tests\Persistence.Tests.ps1` - 7/7 passed.
- `tests\LightLaunchpad.NativeUi.Tests\SettingsTransactions.Tests.ps1` - 3/3 passed.
- `tests\LightLaunchpad.NativeUi.Tests\AppHost.Tests.ps1` - 4/4 passed; `shellReadyMs=15`, `probeExitMs=391`, `settingsWindowMs=176`, `logBytes=1168`.
- `tests\LightLaunchpad.NativeUi.Tests\ExecutableIdentity.Tests.ps1` - 5/5 passed.
- `dotnet run --project tests\LightLaunchpad.Core.Tests\LightLaunchpad.Core.Tests.csproj` - 33/33 passed.
- `dotnet run --project tests\LightLaunchpad.App.Tests\LightLaunchpad.App.Tests.csproj` - 74/74 passed; historical regression baseline only.
- Post-commit build/diagnostics - `artifacts\native-ui-llp007-postcommit\LightLaunchpad.NativeUi.exe`, build 21,354 ms, 1,599,488 bytes, SHA256 `85AC30FF2FF79D5F5EB25D746FA453F3628CEC38C13AA8C8151F698C3A1E2312`, diagnostics exit 0 in 262 ms, version `1.11.0-dev+b092424f2844.dirty`, executable/commit/startup identities matched.
- `git diff --cached --check` passed before the implementation commit; the implementation staged set contained exactly 17 explicit files and excluded both protected paths.

Safety, performance, and evidence limits:

- Protected PID 22560 still runs `artifacts\native-ui-final-fix\LightLaunchpad.NativeUi.exe` with unchanged SHA256 `EBD9363316F45B8449CDA57218A8349C039EFDB6001FC030B69648436C47A0BD`; HKCU Run remains its exact quoted path, the stable install remains absent, and the `C:\TOOL` EXE remains SHA256 `2E44A11A27C96787EA85873B28CE782FCE9257A8918D10D4F619A27D50679C1A`.
- `LLP_reconstruction/` remains untracked/unstaged and ignored `LightLaunchpad.NativeUi.o` was not adopted. No stable-install, startup, `C:\TOOL`, push, tag, or release mutation occurred.
- Search uses cached normalized candidates; the 300-item pinyin-query P95 is 0.09 ms at the module boundary, while cached input remained 0 ms during the intentionally slow broker refresh. Full hotkey-to-input and animation budgets remain owned by LLP-009, LLP-014, and LLP-016.
- Screenshot evidence is not applicable to this nonvisual index/ranking/data-safety task. Release-relevant evidence comes from production C++ and real NativeUi EXE probes; input/IME visuals begin in LLP-008 and material/DPI pixel evidence remains assigned to LLP-012 through LLP-016.
- Scoped implementation commit: `b092424` (`feat: add native search index and ranking`).
- Task transition: LLP-007 marked `done`; LLP-008 marked as the sole `in_progress` task.

## LLP-006 Execution Evidence

Status at 2026-08-02 06:42 +08:00: complete.

Changed files in scope:

- `src/LightLaunchpad.NativeUi/Catalog.{h,cpp}` - immutable snapshot types, normalized stable item IDs, scoped-layout loading/migration, missing-item retention, guarded folder creation and background catalog scanning.
- `src/LightLaunchpad.NativeUi/ShellBroker.{h,cpp}` - dedicated COM STA thread/message pump and queued catalog, icon, launch, folder/location, import, Recycle Bin, and persistence operations; normalized target/source deduplication, reparse/cancellation guards, graceful persistence drain on shutdown, and per-item recycle verification.
- `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp` - cached-snapshot-first presentation, stable-ID focus/icon publication, asynchronous refresh/icon/Shell call sites, diagnostics and real-EXE probes, immediate nonblocking Spotlight show/hide, and failure feedback.
- `tools/build-native-ui.ps1` - compiles the production Catalog and ShellBroker modules.
- `tests/LightLaunchpad.NativeUi.Tests/CatalogShellBroker.Tests.ps1` - real-EXE STA, broken/slow catalog, actual hotkey-path input, cache/stable-ID, shutdown persistence, import deduplication, shortcut-target, executable-copy ban, and reparse tests.
- `tests/LightLaunchpad.NativeUi.Tests/Persistence.Tests.ps1` - adds a rejected future-schema legacy-layout non-mutation/non-archive regression.
- `tests/LightLaunchpad.NativeUi.Tests/{AppHost,ExecutableIdentity}.Tests.ps1` - adds the new production modules to clean-source build fixtures.
- `tests/LightLaunchpad.App.Tests/HostedAppModeSourceTests.cs` - updates the historical source boundary checks for the broker modules and bans the old UI-thread Sleep animation.
- `docs/ai/{tasks,progress}.md` - records the verified LLP-006 completion and activates LLP-007.

TDD and systematic-debugging evidence:

- RED: the pre-change EXE did not recognize `--catalog-broker-probe-file`; the first broker compile also exposed MinGW common-controls include ordering. GREEN builds and runs the real probe through a dedicated STA.
- RED architecture review found synchronous Shell/icon/catalog helpers still resident in the UI module. GREEN moves those calls behind the broker; the compiled UI accepts input while the worker handles a deliberately invalid `.lnk` and a 1,200 ms artificial slow refresh.
- The initial 200 ms input check measured before the window became input-ready and therefore included the pre-existing blocking animation. The measurement boundary was corrected to the first focused/ready frame on the actual `WM_HOTKEY` path, and the blocking `Sleep(8/7)` animation itself was removed rather than accepted.
- RED: a rejected future-schema legacy layout was archived as though migration succeeded. GREEN requires an accepted document before migration and preserves the rejected file byte-for-byte without another archive.
- RED: an asynchronous layout edit queued immediately before `DestroyWindow` was lost. GREEN cancels non-persistence work, drains queued atomic persistence requests, then stops the broker; the real EXE reads back `Shutdown Flush Sentinel` after exit.
- Review hardening prevents stale asynchronous icon-size results from replacing current-size icons, recursively creates/validates launch folders, and removes layout entries only for files confirmed to have left their original paths after Recycle Bin work.

Fresh post-commit verification against `0bd788df848f`:

- `tests\LightLaunchpad.NativeUi.Tests\CatalogShellBroker.Tests.ps1` - 3/3 passed; `inputResponseMs=0`, `refreshMs=1891`, UI thread `46244`, STA broker thread `11024`, generation 2; import counts `1` then `0`, skipped reparse points `1`.
- `tests\LightLaunchpad.NativeUi.Tests\Persistence.Tests.ps1` - 7/7 passed, including the new rejected future legacy schema case.
- `tests\LightLaunchpad.NativeUi.Tests\SettingsTransactions.Tests.ps1` - 3/3 passed.
- `tests\LightLaunchpad.NativeUi.Tests\AppHost.Tests.ps1` - 4/4 passed; `shellReadyMs=16`, `probeExitMs=195`, `settingsWindowMs=149`, `logBytes=1173`.
- `tests\LightLaunchpad.NativeUi.Tests\ExecutableIdentity.Tests.ps1` - 5/5 passed.
- `dotnet run --project tests\LightLaunchpad.Core.Tests\LightLaunchpad.Core.Tests.csproj` - 33/33 passed.
- `dotnet run --project tests\LightLaunchpad.App.Tests\LightLaunchpad.App.Tests.csproj` - 74/74 passed; historical regression baseline only.
- Post-commit build/diagnostics - `artifacts\native-ui-llp006-postcommit\LightLaunchpad.NativeUi.exe`, build 17,110 ms, 1,450,496 bytes, SHA256 `392664016AE776286DAECAB0EEB37A6F2B18C979B952D0BA7ABA07E7EAA49EE6`, version `1.11.0-dev+0bd788df848f.dirty`, build ID `development:0bd788df848f`.
- `git diff --cached --check` passed before the implementation commit; post-commit tracked source was clean.

Safety, performance, and evidence limits:

- Protected PID 22560 still runs `artifacts\native-ui-final-fix\LightLaunchpad.NativeUi.exe` with unchanged SHA256 `EBD9363316F45B8449CDA57218A8349C039EFDB6001FC030B69648436C47A0BD`; HKCU Run still contains its exact quoted path, the stable install remains absent, and `C:\TOOL` remains untouched.
- All behavior probes used isolated temporary app-data/profile roots, validated cleanup boundaries, and removed a known junction before recursive cleanup. `LLP_reconstruction/` remained untracked/unstaged; ignored `LightLaunchpad.NativeUi.o` was not adopted.
- No stable install, startup, `C:\TOOL`, push, tag, or release mutation occurred.
- Cached input stayed responsive under the instrumented 1.891 s refresh, and AppHost stayed well inside its 500 ms shell-first gate. LLP-006 does not claim the PRD memory, startup-to-first-frame, or animation frame-time budgets; those remain assigned to later performance/animation tasks.
- Screenshot evidence is not applicable to this worker/thread/data-safety task. Release-relevant evidence comes from the production C++ modules and real NativeUi EXE; visual/DPI acceptance remains assigned to LLP-012, LLP-015, LLP-016, and LLP-017.
- Scoped implementation commit: `0bd788d` (`feat: move catalog work to Shell STA`).
- Task transition: LLP-006 marked `done`; LLP-007 marked as the sole `in_progress` task.

## LLP-005 Execution Evidence

Status at 2026-08-02 05:39 +08:00: complete.

Changed files in scope:

- `src/LightLaunchpad.NativeUi/Persistence.{h,cpp}` - strict UTF-8 conversion, normalized folder identity and hashed scoped layout paths, bounded object parsing, valid-primary/backup loading, corruption quarantine, flushed temporary writes, and atomic primary/backup replacement.
- `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp` - schema-v2 settings/layout documents, schema-v1 compatibility, typed validation diagnostics, legacy layout migration/archive, scoped folder-identity enforcement, normalized stable-path deduplication, missing-item metadata retention/reattachment, and a real-EXE persistence probe.
- `src/third_party/nlohmann/json.hpp` and `LICENSE.MIT` - vendored nlohmann/json 3.12.0 and license; header SHA256 pinned to `AAF127C04CB31C406E5B04A63F1AE89369FCCDE6D8FA7CDDA1ED4F32DFC5DE63` from the official [v3.12.0 release](https://github.com/nlohmann/json/releases/tag/v3.12.0).
- `tools/vendor-nlohmann-json.ps1` - reproducible download with fail-closed checksum verification and temporary-file cleanup.
- `tools/build-native-ui.ps1` - compiles the production persistence module and vendored include.
- `tests/LightLaunchpad.NativeUi.Tests/Persistence.Tests.ps1` - isolated real-EXE tests for parser fidelity, validation, migration, folder scopes, missing metadata, backup recovery, quarantine, atomic restoration, and first-run behavior.
- `tests/LightLaunchpad.NativeUi.Tests/{AppHost,ExecutableIdentity}.Tests.ps1` - include the persistence module and vendored dependency in clean-source build fixtures.

TDD and production behavior evidence:

- RED: the pre-change real EXE did not recognize `--persistence-probe-file` and timed out without producing a probe.
- RED: a present `SpotlightWidth` string was silently accepted and defaulted without diagnostics; schema-v2 layout without `FolderIdentity` was also accepted. GREEN reports the exact invalid field, retains safe defaults, rejects future schema versions without mutating their files, and requires scoped identity for schema v2.
- RED: duplicate source paths in valid JSON produced two visible tiles. GREEN normalizes stable paths, retains the first record, and reports the ignored duplicate.
- RED: an absent settings primary/backup was reported as corruption. GREEN treats this as normal first run while retaining `Alt+D` and the 980 px default width.
- Real-EXE round trip preserved quotes, braces, Chinese text, and a UTF-16 surrogate-pair rocket across legacy migration, schema-v2 save, and reload.
- Folder A metadata did not appear in folder B; returning to A restored its region and display name. Deleting the source moved its metadata to the missing set; recreating the same stable path reattached it without metadata loss.
- Truncated settings and scoped-layout primaries were quarantined, restored to parseable schema-v2 JSON from `.bak`, left backup SHA256 values unchanged, and left no `.tmp` or `.recovery.tmp` residue.

Fresh post-commit verification against `a3648830cada`:

- NativeUi persistence behavior: 7/7 named real-EXE checks passed.
- NativeUi settings transactions: 3/3 passed.
- NativeUi AppHost behavior: 4/4 passed; `shellReadyMs=16`, `probeExitMs=345`, `settingsWindowMs=171`, `logBytes=1081`.
- NativeUi identity/package behavior: 5/5 passed.
- Core tests: `33/33 tests passed`.
- Legacy App/source suite: `74/74 tests passed`; historical regression coverage only.
- Post-commit NativeUi build/diagnostics: `artifacts\native-ui-llp005-postcommit\LightLaunchpad.NativeUi.exe`, build 11,540 ms, 1,379,840 bytes, SHA256 `60E2D4178C3F866EFB6C4E7649B0EAEC49BE98CF843D72C34B5ACF0758BFE95E`, version `1.11.0-dev+a364883.dirty`, build ID `development:a364883`.
- `git diff --check` and staged-set checks passed; the vendored dependency checksum passed in every persistence run.

Safety, performance, and evidence limits:

- All persistence processes used isolated temporary `APPDATA`, `LOCALAPPDATA`, and `USERPROFILE`; cleanup rejected reparse-point roots before recursive removal.
- Protected PID 22560 remains on `artifacts\native-ui-final-fix\LightLaunchpad.NativeUi.exe` with unchanged SHA256 `EBD9363316F45B8449CDA57218A8349C039EFDB6001FC030B69648436C47A0BD`; the Run value remains the exact quoted path, and the stable install remains absent.
- No `C:\TOOL`, startup, stable-install, push, tag, or release mutation occurred. `LLP_reconstruction/` and ignored `LightLaunchpad.NativeUi.o` remained outside staging.
- The production EXE grew from 561,152 to 1,379,840 bytes due to the standards-compliant static parser and still remains a single executable. AppHost response metrics stayed far below the existing 500 ms shell-first gate; memory optimization remains assigned to LLP-014.
- Screenshot evidence is not applicable to persistence semantics. All release-relevant claims come from the compiled production C++ module and real EXE; the C# suites remain regression baselines only.
- Scoped implementation commit: `a364883` (`feat: make native persistence resilient`).
- Task transition: LLP-005 marked `done`; LLP-006 marked as the sole `in_progress` task.

## LLP-004 Execution Evidence

Status at 2026-08-02 05:04 +08:00: complete.

Changed files in scope:

- `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp` - candidate settings snapshots, alternate-ID hotkey reservation with `MOD_NOREPEAT`, commit/rollback ordering, exact startup-registration snapshot/restore, visible settings status, candidate-based serialization, and temporary-file cleanup after failed atomic replacement.
- `tests/LightLaunchpad.NativeUi.Tests/SettingsTransactions.Tests.ps1` - real-EXE Win32 tests with isolated app-data, a dedicated competing hotkey thread, UI control inspection, ownership probes, forced write failure, development-only registry isolation, startup rollback, and zero-residue checks.
- `tests/LightLaunchpad.App.Tests/HostedAppModeSourceTests.cs` - updates the historical icon-size source assertion to the candidate-settings boundary.
- `.learnings/ERRORS.md` - records and resolves the startup-isolation error discovered during the first RED test.

TDD and production behavior evidence:

- RED: the first real-EXE conflict test showed the settings control changed from `Ctrl+Alt+Shift+9` to the occupied `Ctrl+Alt+Shift+8`; the rejected value was also persisted and the old registration had been released by the legacy order.
- GREEN conflict: an independent Win32 thread owns the candidate combination; Apply reports the Windows conflict, restores the old control/file value, and another ownership probe confirms the product still holds the old binding.
- GREEN success: after releasing the competing owner, Apply persists and owns the candidate first, then releases the prior binding; external `RegisterHotKey` probes prove both sides of the handoff.
- GREEN write/startup rollback: the isolated `settings.json` target is temporarily replaced by a directory and the startup checkbox is changed. The settings write fails, the candidate hotkey is released, the prior hotkey/control/file values remain, the isolated startup value is removed back to its exact snapshot, and no `.tmp` file remains.
- Failure and success reasons are read from the real settings Diagnostics control (ID 3015), rather than inferred from source strings.

Fresh post-commit verification against `bc5ea3e5f8de`:

- NativeUi settings transactions: 3/3 passed.
- NativeUi AppHost behavior: 4/4 passed; isolated timing remained within LLP-003 gates (`shellReadyMs=32`, `probeExitMs=332`, `settingsWindowMs=172`, `logBytes=1086`).
- NativeUi identity behavior: 5/5 passed.
- Core tests: `33/33 tests passed`.
- Legacy App/source suite: `74/74 tests passed`; historical regression coverage only.
- Native build and diagnostics: `release\llp004-postcommit\LightLaunchpad.NativeUi.exe`, 561,152 bytes, SHA256 `6151535E799359BC6D3553619991ECD339A8FFA36CA6AECCF86562048A9DC53E`, `ProductVersion=1.11.0-dev+bc5ea3e5f8de.dirty`, diagnostics commit `bc5ea3e5f8de`.
- `git diff --check` and cached diff checks: exit 0; only Git line-ending notices appeared while staging.

Safety incident, correction, and final state:

- The first RED test isolated `APPDATA` but invoked the legacy Apply path before startup registration was isolated. That old path unconditionally called `SetStartWithWindows(false)` and removed the real `HKCU\...\Run\LightLaunchpad` value.
- Work stopped at discovery. The exact baseline Run string was restored only after the protected EXE path and SHA256 were revalidated. A development-only override now accepts only `HKCU\Software\LightLaunchpad\Tests\...`; the real test aborts if the formal Run baseline is missing, compares it after every transaction, and removes its exact test key in `finally`.
- Final safety verification: Run is exactly `"C:\Users\thoma\Desktop\启动台\artifacts\native-ui-final-fix\LightLaunchpad.NativeUi.exe"`; protected EXE SHA256 is `EBD9363316F45B8449CDA57218A8349C039EFDB6001FC030B69648436C47A0BD`; test registry key count is 0; only protected PID 22560 remains.
- No stable install, `C:\TOOL`, push, tag, or release action was performed. `LLP_reconstruction/` and ignored `LightLaunchpad.NativeUi.o` stayed outside staging.
- Screenshot evidence is not applicable to this transactional behavior task; real visible status and control rollback were verified through Win32 controls. Full settings visual reflow remains assigned to LLP-015.
- Scoped implementation commit: `bc5ea3e` (`feat: make native settings transactional`).
- Task transition: LLP-004 marked `done`; LLP-005 marked as the sole `in_progress` task.

## LLP-003 Execution Evidence

Status at 2026-08-02 04:48 +08:00: complete.

Changed files in scope:

- `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp` - shell-first startup order, channel-specific IPC window, bounded singleton activation, explicit mismatch/failure feedback, bounded tray retries, `TaskbarCreated` recovery, startup/runtime probes, and a 256 KiB-bounded local JSONL startup log. Initial catalog/layout/search work is deferred until the product is opened.
- `tools/build-native-ui.ps1` - injects stable or commit-specific development window-class identity alongside the singleton identity.
- `tests/LightLaunchpad.NativeUi.Tests/AppHost.Tests.ps1` - real-EXE shell-first, unavailable-folder, same-build IPC, stable mismatch, one-process, taskbar recovery, settings reachability, log-bound, and optional real Explorer-restart checks in isolated application-data roots.
- `tests/LightLaunchpad.NativeUi.Tests/ExecutableIdentity.Tests.ps1` - verifies and locates the generated IPC class in diagnostics and real NativeUi windows.
- `tests/LightLaunchpad.App.Tests/HostedAppModeSourceTests.cs` - removes the obsolete private tray-registry contract and keeps only the supported Shell notification/recovery architecture checks.

TDD and debugging evidence:

- RED: the new architecture test found `IsPromoted` and the private `NotifyIconSettings` mutation before that route was removed.
- RED: with an unavailable UNC launch folder, the pre-change real EXE did not complete the shell-first startup probe within five seconds because startup entered catalog work first.
- GREEN: `powershell -NoProfile -ExecutionPolicy Bypass -File tests\LightLaunchpad.NativeUi.Tests\AppHost.Tests.ps1` passes four named real-EXE checks: private-registry ban, shell-first/recovery, matching activation/one process, and mismatch activation/reporting.
- GREEN, real Explorer: the same test with `-RestartExplorer` stopped and restarted the current-session Explorer shell, observed `TaskbarCreated`, re-added the real tray icon (`recoveryCount=1`, `trayAttempts=2`), and reopened the real settings window.
- A post-commit parallel .NET run initially hit `CS2012` because Core and App simultaneously wrote `LightLaunchpad.Core.dll`; process inspection showed a test-orchestration file lock. Rerunning the two shared-output suites sequentially passed and no product change was made for the harness-only race.

Fresh post-commit verification against `33a139efd932`:

- NativeUi AppHost behavior: 4/4 passed; final isolated metrics were `shellReadyMs=31`, `probeExitMs=336`, `settingsWindowMs=164`, and `logBytes=1085` with an unavailable launch folder.
- Real Explorer recovery run: passed with `shellReadyMs=31`, `probeExitMs=248`, `settingsWindowMs=166`, `logBytes=1083`, recovery count 1, and tray-add attempts 2.
- NativeUi identity behavior: 5/5 passed, including real PE/manifest, diagnostics, channel separation, Win32 settings diagnostics, and package identity gates.
- Core tests: `33/33 tests passed`.
- Legacy App/source suite: `74/74 tests passed`; retained only as historical regression coverage.
- Native build and diagnostics: `release\llp003-postcommit\LightLaunchpad.NativeUi.exe`, 556,544 bytes, SHA256 `FE560F080969DFC355BCB226D339529892A9816EF1802EBB4D196EB2C030C93D`, `ProductVersion=1.11.0-dev+33a139efd932.dirty`, `FileVersion=1.11.0.0`, diagnostics commit `33a139efd932`, development channel, commit-specific IPC class, and commit-specific singleton.
- `git diff --check` and cached diff checks: exit 0; only Git line-ending notices appeared while staging.

Safety, UI, and evidence limits:

- The Explorer recovery test used only an isolated development EXE/configuration and ensured Explorer was running again in `finally`. It left no development process behind.
- After Explorer recovery, protected PID 22560 still ran `artifacts\native-ui-final-fix\LightLaunchpad.NativeUi.exe` with unchanged SHA256 `EBD9363316F45B8449CDA57218A8349C039EFDB6001FC030B69648436C47A0BD`; the Run value still pointed to that exact file.
- No stable install, `C:\TOOL`, Run entry, push, tag, or release action was performed. `LLP_reconstruction/` and ignored `LightLaunchpad.NativeUi.o` stayed outside staging.
- Screenshot evidence is not applicable to this lifecycle/IPC task. The real settings window was located and restored through Win32 process/window inspection; visual/DPI acceptance remains assigned to LLP-012 and LLP-015.
- Scoped implementation commit: `33a139e` (`feat: make native startup shell-first`).
- Task transition: LLP-003 marked `done`; LLP-004 marked as the sole `in_progress` task.

## LLP-002 Execution Evidence

Status at 2026-08-02 04:21 +08:00: complete.

Changed files in scope:

- `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp` - compiled build identity, channel-specific singleton name, JSON diagnostics command, safe diagnostics UI mode, runtime identity fields, and About/Diagnostics settings surface.
- `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.rc` - PE icon, VERSIONINFO, build metadata, and embedded manifest resources.
- `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.manifest` - Per-Monitor V2, long-path, compatibility, and as-invoker declarations.
- `src/LightLaunchpad.NativeUi/LightLaunchpad.Version.h` - version/build identity defaults for supported builds.
- `tools/build-native-ui.ps1` - semantic version, commit, dirty state, development/stable channel, generated identity header, and distinct singleton injection.
- `tools/package-release.ps1` - clean stable-package gate and explicit `-Development` package identity.
- `tests/LightLaunchpad.NativeUi.Tests/ExecutableIdentity.Tests.ps1` - builds and inspects real development/stable PE files, resources, diagnostics, Win32 settings control, singleton channels, dirty-package rejection, and ZIP contents.
- `docs/ai/evidence/LLP-002/01-about-diagnostics.png` - accepted current-run screenshot of the real NativeUi About/Diagnostics surface.

TDD evidence:

- RED: real PE `ProductName` was empty before VERSIONINFO was added.
- RED: real EXE produced no `--diagnostics-file` output before compiled diagnostics were added.
- RED: real EXE did not expose a settings diagnostics surface before `--diagnostics-ui` and control ID 3015 were added.
- RED: stable packaging succeeded on a dirty repository before the clean-package gate was implemented.
- RED: stable build channel was unavailable before stable/development identity separation was implemented and verified against two real EXEs.
- RED: diagnostics UI mislabeled an uninitialized renderer and disabled test hotkey state before explicit statuses were implemented.
- GREEN: `powershell -ExecutionPolicy Bypass -File tests\LightLaunchpad.NativeUi.Tests\ExecutableIdentity.Tests.ps1` exits 0 with 5/5 named real-EXE behavior checks.

Fresh verification before the implementation commit:

- NativeUi identity behavior: 5/5 passed, including real PE version/manifest, JSON diagnostics, stable-vs-development singleton separation, real Win32 settings control, dirty stable-package rejection, and explicit development ZIP content.
- Core tests: `33/33 tests passed`.
- Legacy App/source suite: `74/74 tests passed`; retained as a historical regression baseline only.
- Native build: `release\llp002-check\LightLaunchpad.NativeUi.exe`, 545,280 bytes, SHA256 `B8C1C9BD6D0C32A2525627EE7075D4E45367A48B35374050951FD01E4D6D2BF7`, `ProductVersion=1.11.0-dev+f74a4bc2a742.dirty`, `FileVersion=1.11.0.0`.
- Real EXE diagnostics smoke: exit 0; executable path, commit `f74a4bc2a742`, development channel, build ID, stable install contract, startup command, DPI declaration, monitor/renderer/hotkey fields all parsed from produced JSON.
- `git diff --check`: exit 0; only Git LF-to-CRLF working-copy notices were emitted.

Visual and accessibility audit:

- Accepted screenshot: `docs/ai/evidence/LLP-002/01-about-diagnostics.png` (372 x 486 captured logical pixels).
- The real settings window loaded completely with visible Save/Apply/Cancel actions and a readable About/Diagnostics surface.
- UI Automation exposed the diagnostics label as text ID 3028 and its read-only edit surface as ID 3015, alongside the existing keyboard-focusable settings controls.
- Known nonblocking risk: at the current 150% desktop scale the settings layout is physically dense and long paths require scrolling/wrapping. Per-task scope leaves full DIP scaling, reflow, and DPI screenshots to LLP-012 rather than hiding the risk.
- Visual audit did not claim contrast or full accessibility compliance from the screenshot alone.

Safety and performance:

- No stable install, `C:\TOOL`, Run entry, push, tag, or release action was performed.
- The visible audit used a development-channel EXE with tray/hotkey integration disabled and stopped only its validated temporary PID after capture.
- Performance budgets are not changed or claimed by LLP-002. The prior hidden Private Memory baseline remains 31,207,424 bytes until a later measured optimization task.
- `LLP_reconstruction/` and `LightLaunchpad.NativeUi.o` remain untracked/ignored and outside staging.
- Scoped implementation commit: `a9037bb` (`feat: add verifiable native executable identity`).

Post-commit verification against `a9037bbbed4f`:

- NativeUi identity behavior: 5/5 passed again from the committed source.
- Core tests: 33/33 passed again.
- Legacy App/source tests: 74/74 passed again; not treated as NativeUi release authority.
- Native build: `release\llp002-postcommit\LightLaunchpad.NativeUi.exe`, 545,792 bytes, SHA256 `65198E9CB78B7F94A4A978E48F7A3999D5966F892A6C844F3D63FFF2F560A768`.
- PE identity: `ProductName=LightLaunchpad`, `ProductVersion=1.11.0-dev+a9037bbbed4f.dirty`, `FileVersion=1.11.0.0`.
- Real EXE diagnostics: exit 0, commit `a9037bbbed4f`, development channel, singleton `Local\LightLaunchpad.NativeUi.SingleInstance.development.a9037bbbed4f`, stable install path contract, and unchanged user startup command.
- Safety revalidation: protected PID 22560 still runs the original artifact with SHA256 `EBD9363316F45B8449CDA57218A8349C039EFDB6001FC030B69648436C47A0BD`; HKCU Run still points there; stable install remains absent; TOOL SHA256 remains `2E44A11A27C96787EA85873B28CE782FCE9257A8918D10D4F619A27D50679C1A`; no dev audit process remains.
- `git diff --check` and `git diff --cached --check`: exit 0. Only protected `LLP_reconstruction/` content remains untracked.
- Task transition: LLP-002 marked `done`; LLP-003 marked as the sole `in_progress` task.

## Current Session Baseline Revalidation

Revalidated on 2026-08-02 before changing product code:

- Git branch was `feature/llp-reconstruction` at `b88e9421e4a975e0f403910c95c2b81451dd6e1d`; development continued on new branch `codex/apple-first-v1.1` from the same commit.
- The index and tracked working tree were clean. Untracked product documents, `LLP_reconstruction/`, and `LightLaunchpad.NativeUi.o` were present.
- `LLP_reconstruction/` and `LightLaunchpad.NativeUi.o` remain untracked and are excluded from LLP-001 staging. Generated `*.o` files are now ignored by repository policy.
- PID 22560 was running `C:\Users\thoma\Desktop\启动台\artifacts\native-ui-final-fix\LightLaunchpad.NativeUi.exe` with SHA256 `EBD9363316F45B8449CDA57218A8349C039EFDB6001FC030B69648436C47A0BD`.
- The process measured 11,808,768 bytes Working Set and 31,207,424 bytes Private Memory; Private Memory remains above the 15 MiB product target.
- `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\LightLaunchpad` points to the same artifact EXE. No matching Startup-folder item exists.
- `%LocalAppData%\Programs\LightLaunchpad\LightLaunchpad.NativeUi.exe` does not exist yet.
- `C:\TOOL\LightLaunchpad-nativeui-win-x64-V1.10\LightLaunchpad.NativeUi.exe` remains untouched with SHA256 `2E44A11A27C96787EA85873B28CE782FCE9257A8918D10D4F619A27D50679C1A`.
- Ordinary development will not stop or overwrite the running EXE, change the Run entry, create the stable install, modify `C:\TOOL`, push, tag, or release.

LLP-001 verification to run sequentially:

```powershell
dotnet run --project tests\LightLaunchpad.Core.Tests\LightLaunchpad.Core.Tests.csproj
dotnet run --project tests\LightLaunchpad.App.Tests\LightLaunchpad.App.Tests.csproj
powershell -ExecutionPolicy Bypass -File tools\build-native-ui.ps1 -OutputDirectory release\baseline-check
git diff --check
```

## LLP-001 Execution Evidence

Status at 2026-08-02 03:50 +08:00: complete.

Changed files in scope:

- `.gitignore` - ignore generated `*.o` objects without adopting `LLP_reconstruction/`.
- `PRD.md` - tracked product contract v1.1.
- `docs/ai/goal-prompt.md`
- `docs/ai/progress.md`
- `docs/ai/project-standards.md`
- `docs/ai/requirements-and-route.md`
- `docs/ai/tasks.md`

Fresh commands and results:

- `dotnet run --project tests\LightLaunchpad.Core.Tests\LightLaunchpad.Core.Tests.csproj` - exit 0, `33/33 tests passed`.
- `dotnet run --project tests\LightLaunchpad.App.Tests\LightLaunchpad.App.Tests.csproj` - exit 0, `74/74 tests passed`; legacy/source-contract baseline only.
- `powershell -ExecutionPolicy Bypass -File tools\build-native-ui.ps1 -OutputDirectory release\baseline-check` - exit 0; produced `release\baseline-check\LightLaunchpad.NativeUi.exe`, 533,504 bytes, SHA256 `AEA2BD00973E89CA92666CF05D19F036336DC535677F1AE4E8DA2BA0948094C3`.
- `git diff --check` - exit 0; Git reported only the repository's LF-to-CRLF working-copy notice for `.gitignore`.

Evidence and limits:

- Runnable NativeUi build artifact retained at `release\baseline-check\LightLaunchpad.NativeUi.exe`; the already running user build was not terminated or replaced.
- Runtime performance snapshot: hidden PID 22560 Private Memory 31,207,424 bytes and Working Set 11,808,768 bytes. LLP-001 establishes this baseline; it does not claim the 15 MiB target.
- Screenshot/visual evidence: not applicable to this documentation-and-branch baseline; no visual behavior changed.
- Protected content remains outside task scope: `LLP_reconstruction/` is untracked and unstaged; `LightLaunchpad.NativeUi.o` remains untracked and is ignored as generated output.
- Primary scoped commit: `d2da5c0` (`docs: establish Apple-first execution baseline`).
- Task transition: LLP-001 marked `done`; LLP-002 marked as the sole `in_progress` task immediately after the verified baseline commit.

## Intentional New Documentation

- `PRD.md` - version 1.1 Apple-first product baseline.
- `docs/ai/requirements-and-route.md`
- `docs/ai/tasks.md`
- `docs/ai/project-standards.md`
- `docs/ai/progress.md`
- `docs/ai/goal-prompt.md`

These files are untracked at handoff and should be reviewed and committed in `LLP-001` without staging unrelated untracked content.

## Existing Untracked Work To Protect

- `LLP_reconstruction/` - mechanical source split from earlier exploration; not the chosen route and not approved for deletion.
- `LightLaunchpad.NativeUi.o` - generated object file; do not stage as product source.

## Runtime Identity At Handoff

- Running process: PID 22560 at snapshot time.
- Running path: `C:\Users\thoma\Desktop\启动台\artifacts\native-ui-final-fix\LightLaunchpad.NativeUi.exe`.
- Running SHA256: `EBD9363316F45B8449CDA57218A8349C039EFDB6001FC030B69648436C47A0BD`.
- Running measurement: Working Set about 11 MiB; Private Bytes about 29.76 MiB.
- Startup registry points to the same artifact path, not a stable install path.
- TOOL path: `C:\TOOL\LightLaunchpad-nativeui-win-x64-V1.10\LightLaunchpad.NativeUi.exe`.
- TOOL SHA256: `2E44A11A27C96787EA85873B28CE782FCE9257A8918D10D4F619A27D50679C1A`.
- Do not terminate, replace, or repoint the user's current running/startup build merely to begin development.

## Baseline Verification Completed

Commands run on 2026-08-02:

```powershell
dotnet run --project tests\LightLaunchpad.Core.Tests\LightLaunchpad.Core.Tests.csproj
dotnet run --project tests\LightLaunchpad.App.Tests\LightLaunchpad.App.Tests.csproj
powershell -ExecutionPolicy Bypass -File tools\build-native-ui.ps1 -OutputDirectory "$env:TEMP\LightLaunchpad-pre-goal-baseline"
```

Results:

- Core tests: 33/33 passed.
- App tests: 74/74 passed.
- Native build: passed; output size 533,504 bytes.
- Baseline build SHA256: `9D1EADF0102F352C1B946E607D1413EFA8C009F63724A62825DED2A28C76E2A6`.
- Important: the green App suite includes source-string checks and superseded WPF/Agent, double-click, Tab-completion, tray-registry, and old visual contracts. It is a historical baseline, not release confidence.

## Audit Facts That Must Not Be Lost

1. NativeUi is about 7,530 lines with dozens of globals and duplicate D2D/GDI paths.
2. Startup performs settings/layout/catalog work before tray and hotkey initialization.
3. `RegisterHotKey`, `ShellExecute`, persistence, registry, tray, import, and delete failures are frequently ignored.
4. Search is a custom drawn field without complete text editing, UI Automation, or robust IME ownership.
5. Full-pinyin coverage is limited by a small hand-written map.
6. Current focus lens magnifies captured background at 1.16x and can create upward/offset ghost imagery.
7. UI-thread `Sleep` blocks opening and closing animation.
8. No Per-Monitor V2 manifest or complete DPI, high-contrast, reduced-motion, and reduced-transparency behavior exists.
9. `NotifyIconSettings\IsPromoted` mutation is private, fragile, and now explicitly rejected.
10. Manual import copies `.exe`/`.com` binaries and repeated imports have created numbered duplicates.

## Current Blockers

None. The next session can start `LLP-001` immediately.

## Next Session Start Procedure

1. Read `PRD.md` and every `docs/ai/*.md` file.
2. Inspect git and runtime identity; do not assume this snapshot is still current.
3. Create the `/goal` using `docs/ai/goal-prompt.md`.
4. Start `LLP-001`, update its status to `in_progress`, and record all verification evidence here.
