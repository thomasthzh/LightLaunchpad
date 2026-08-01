# LightLaunchpad Development Progress

## Handoff Snapshot

- Updated: 2026-08-02 06:42:51 +08:00, Asia/Hong_Kong
- Workspace: `C:\Users\thoma\Desktop\启动台`
- Branch: `codex/apple-first-v1.1`
- HEAD: `0bd788d`
- Remote main: `origin/main@33e91c2`
- Active task: `LLP-007 - Native Search Index And Ranking` (`in_progress`)
- Completed tasks: `LLP-001`, `LLP-002`, `LLP-003`, `LLP-004`, `LLP-005`, `LLP-006`
- Next task after verification: `LLP-008 - Complete Search Text And IME Controller`
- Goal status: active in Codex; LLP-007 is the only task in progress.

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
