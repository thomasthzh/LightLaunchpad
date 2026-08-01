# LightLaunchpad Project Standards

## Source Of Truth

1. Read `PRD.md`, then every file in `docs/ai/`, before choosing work.
2. `PRD.md` owns product behavior. `docs/ai/tasks.md` owns implementation order. `docs/ai/progress.md` owns current state.
3. Update stale docs before continuing when code reality differs.
4. Never rely on a prior chat, artifact folder name, or EXE timestamp as version identity.

## Task Discipline

1. Keep at most one task `in_progress`.
2. Announce the task ID, behavior, likely files, and verification before editing.
3. Prefer a vertical change that leaves a runnable NativeUi executable.
4. Do not perform an architecture-only split with shared extern globals and call it modularization.
5. Add abstractions only at real ownership boundaries described in `requirements-and-route.md`.
6. At every pause, update progress with exact changed files, commands, results, risks, and next task.

## Debugging Protocol

1. Reproduce the user-visible failure against a known EXE path and record version/hash first.
2. Capture exact behavior, error code, screenshot, trace, timing, or log.
3. Form one hypothesis and identify the owning module.
4. Write or update a behavior test when practical.
5. Make the smallest root-cause fix.
6. Rerun the failing check, neighboring regressions, native build, and real EXE flow.
7. Never call a problem fixed based only on source inspection or a green source-string test.

## Testing Protocol

1. Production C++ behavior is the release authority. C# Core tests may be retained as design references until their logic is migrated, but they do not prove NativeUi behavior.
2. Source-string tests may enforce only a few architectural bans, such as no external Assist dependency. They must not assert visual constants or superseded interaction behavior.
3. Use test-first development for hotkeys, persistence, search, geometry, state transitions, history, and failure handling.
4. Real EXE smoke tests cover tray, settings, singleton activation, hotkey, IME, launch, Explorer restart, DPI, monitor placement, and packaging.
5. Visual verification requires matched-state screenshots, pixel checks, and comparison across at least 12 varied backgrounds. A screenshot alone is not a pass.
6. Test 100%, 150%, and 200% DPI; Chinese IME; reduced motion; reduced transparency; and high contrast.

## Performance Evidence

1. Measure Private Bytes, not only Working Set.
2. Report hotkey-to-input-ready P50/P95, frame-time P50/P95, focus-response P95, hidden CPU, visible peak Private Bytes, and hidden Private Bytes.
3. Do not use `SetProcessWorkingSetSize` as evidence of lower memory ownership.
4. Do not claim smoothness from animation duration alone. Verify input availability and frame pacing.
5. Directory I/O, Shell COM, icon extraction, and JSON persistence may not occur in paint callbacks.

## Visual And Interaction Standards

1. Material communicates hierarchy; it is not decoration applied to every surface.
2. Use one continuous main glass layer. No glass-on-glass captures and no magnified desktop screenshot inside a focused tile.
3. Search center remains spatially aligned. Never sample outside/upward to simulate refraction.
4. Keep focus, hover, pressed, selected, multiselected, drag source, drop target, and activation distinct.
5. Keyboard focus never moves solely because the mouse hovers.
6. Animation must be nonblocking and respect reduced-motion and transparency settings.
7. Text, icons, and caret remain crisp and above material effects.

## Persistence And Destructive Operations

1. Use structured JSON parsing, schema versions, validation, backup, temporary write, and atomic replace.
2. The launchpad folder owns item existence; folder-scoped layout owns metadata only.
3. Do not discard metadata solely because a file is temporarily unavailable.
4. Do not mutate layout after a failed filesystem operation.
5. Delete through Recycle Bin and provide undo. Never uninstall target applications.

## Git And Workspace Safety

1. Inspect `git status` before every task and before committing.
2. Treat unrelated dirty or untracked files as user work. Do not delete, reset, clean, or stage them.
3. In particular, do not adopt, remove, or stage `LLP_reconstruction/` or `LightLaunchpad.NativeUi.o` without explicit task scope.
4. Use a `codex/` branch for the Apple-first implementation unless the user explicitly chooses another branch.
5. Keep commits scoped to one task or coherent behavior. Record the commit in progress.
6. Never use `git reset --hard` or destructive checkout to resolve drift.

## Build And Verification Commands

Run sequentially to avoid shared-output races:

```powershell
dotnet run --project tests\LightLaunchpad.Core.Tests\LightLaunchpad.Core.Tests.csproj
dotnet run --project tests\LightLaunchpad.App.Tests\LightLaunchpad.App.Tests.csproj
powershell -ExecutionPolicy Bypass -File tools\build-native-ui.ps1 -OutputDirectory release\native-ui-check
git diff --check
```

As production C++ tests and smoke harnesses are introduced, add their exact commands here and make them mandatory before package creation.

## Runtime And Release Safety

1. Before launching a test build, record and display its absolute path, build ID, and channel.
2. Do not allow the installed production singleton to make a development launch silently exit.
3. Do not change `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` during ordinary development.
4. Do not overwrite `%LocalAppData%\Programs\LightLaunchpad`, `C:\TOOL`, push, tag, or release without an explicit user delivery instruction.
5. Release only from a clean commit after tests, native build, real EXE smoke, visual checks, performance gates, version checks, and package-content checks pass.

## Handoff Protocol

A restarting agent must:

1. Read `PRD.md` and all `docs/ai/*.md` files.
2. Inspect git status, branch, HEAD, running LightLaunchpad process path/hash, and startup registry path.
3. Reconcile `docs/ai/progress.md` with actual files and test output.
4. Resume the first `in_progress` task, or the first unblocked `pending` task when none is active.
5. Update tasks and progress before ending the session.
