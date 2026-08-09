# Error Log

## [ERR-20260802-001] native-settings-red-test-startup-isolation

**Logged**: 2026-08-02T04:59:27+08:00
**Priority**: critical
**Status**: resolved
**Area**: tests

### Summary

The first LLP-004 RED test invoked the legacy NativeUi settings Apply path before isolating its startup-registration side effect, which deleted the user's real `HKCU\...\Run\LightLaunchpad` value.

### Error

```text
Get-ItemPropertyValue: Property LightLaunchpad does not exist at path
HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run.
```

### Context

- The test correctly isolated `APPDATA` and `LOCALAPPDATA`, but the old production `ApplySettingsFromWindow` unconditionally called `SetStartWithWindows(false)` against the real HKCU Run key.
- The exact baseline Run string and protected EXE SHA256 had been recorded before testing, so the missing value was restored only after the EXE path and hash were revalidated.
- A development-only registry-subkey override is being constrained to `HKCU\Software\LightLaunchpad\Tests\...` so future settings RED tests cannot touch the product Run value.

### Suggested Fix

Before any test clicks a legacy or unknown settings Apply/Save path, inventory every external side effect and isolate settings files, startup registry state, shell integration, and hotkeys. Assert the real Run value before and after the test, and abort before product launch if that baseline cannot be read.

### Metadata

- Reproducible: yes
- Related Files: `tests/LightLaunchpad.NativeUi.Tests/SettingsTransactions.Tests.ps1`, `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp`

### Resolution

- **Resolved**: 2026-08-02T05:01:00+08:00
- **Commit/PR**: pending LLP-004 implementation commit
- **Notes**: Restored the exact recorded Run string only after revalidating the protected EXE SHA256. Added a development-only, prefix-constrained test registry subkey; assertions now require the real Run baseline before launch, compare it after rollback, and verify zero test-key residue. The real-EXE transaction suite passes all three cases.

---

## [ERR-20260809-007] launchpad-probe-uintptr-conversion

**Logged**: 2026-08-09T16:50:00+08:00
**Priority**: medium
**Status**: resolved
**Area**: tests

### Summary

The Launchpad real-EXE probe used direct PowerShell casts from `Int32` to `UIntPtr`, which failed before the first activation message reached production.

### Error

```text
Cannot convert the "19541" value of type "System.Int32" to type "System.UIntPtr".
```

### Context

- The first failure is `[UIntPtr]0x4C55` in `LaunchpadFlowBehavior.Tests.ps1`.
- The same file contains direct casts for key flags, key codes, commands, and later activation messages.
- Other passing real-EXE suites use `UIntPtr::new([uint64]value)` explicitly.
- Production NativeUi did not receive the failing message; this is a test-driver interop conversion issue.

### Suggested Fix

Use `UIntPtr::new([uint64]value)` consistently for every nonzero/variable WPARAM in the Launchpad probe, retain `UIntPtr::Zero` for zero, then run the full real-EXE Launchpad flow twice.

### Metadata

- Reproducible: yes
- Related Files: `tests/LightLaunchpad.NativeUi.Tests/LaunchpadFlowBehavior.Tests.ps1`

### Resolution

- **Resolved**: 2026-08-09T17:10:00+08:00
- **Commit/PR**: `b1ecaf281f8f7eefeccf5973010b47c971b4077d`
- **Notes**: All eight nonzero/variable WPARAM conversions now use the explicit UIntPtr constructor; three zero values remain `UIntPtr::Zero`. Two independent real-EXE flows passed with identical launch/edit/favorite metrics and added no temp roots. PowerShell 5.1 and 7.6 constructor checks passed.

---

## [ERR-20260809-006] powershell-junction-remove-null-reference

**Logged**: 2026-08-09T16:25:00+08:00
**Priority**: high
**Status**: resolved
**Area**: tests

### Summary

The Catalog/Shell real-EXE suite passed all product assertions, then Windows PowerShell `Remove-Item -LiteralPath` threw an internal null-reference while removing its test-created junction.

### Error

```text
Remove-Item : Object reference not set to an instance of an object.
```

### Context

- Exact root: `C:\Users\thoma\AppData\Local\Temp\lightlaunchpad-catalog-broker-tests-8db2a79ccba740e19228c131dc4bcd9c`.
- Exact junction: `StartMenuFixture\LinkedApps`; attributes are `Directory, ReparsePoint`, link type is `Junction`.
- The junction target is `ReparseTarget` inside the same GUID test root.
- The root is inside `%TEMP%`, is an ordinary directory, and was created by this test run.
- Product checks passed: cached input remained responsive, future history was preserved, import was idempotent, and the reparse point was skipped.
- No retry occurred before user authorization. After approval, the exact failed-test root and junction were revalidated and removed without traversing the target.

### Suggested Fix

After explicit approval, replace the junction cleanup with a non-recursive `.NET Directory.Delete(path, false)` only after exact `%TEMP%` boundary, reparse-point, junction-type, and in-root target validation. Then remove only the validated test root with the existing guarded recursive cleanup and rerun the suite.

### Metadata

- Reproducible: yes
- Related Files: `tests/LightLaunchpad.NativeUi.Tests/CatalogShellBroker.Tests.ps1`

### Resolution

- **Resolved**: 2026-08-09T16:35:00+08:00
- **Commit/PR**: `072763a7e1d4ede49340f4bfffa87d7da5755c37`
- **Notes**: The test now validates exact import/temp boundaries, directory reparse metadata, Junction type, and the one expected in-root target before nonrecursive `Directory.Delete`. Two focused real-EXE runs passed 3/3 and left matching temp roots at 0.

---

## [ERR-20260809-005] native-build-selects-incomplete-mingw

**Logged**: 2026-08-09T16:05:00+08:00
**Priority**: high
**Status**: in_progress
**Area**: config

### Summary

The NativeUi build selected the first `g++.exe` on PATH even though that MinGW installation lacked the UIAutomationCore import library required by production.

### Error

```text
ld.exe: cannot find -luiautomationcore: No such file or directory
collect2.exe: error: ld returned 1 exit status
```

### Context

- PATH selected `C:\Strawberry\c\bin\g++.exe` first.
- Strawberry contains `libd3d11.a`, `libdxgi.a`, and `libdcomp.a`, but no `libuiautomationcore.a`.
- The installed WinLibs compiler under the WinGet package contains all four required import libraries.
- The failure occurred in the pre-implementation baseline at `AdaptiveWindowBehavior.Tests.ps1`; no LLP-013 production code existed yet.

### Suggested Fix

Make the production build probe candidate compilers for every required header/import library and select one complete toolchain deterministically. Fail with a capability-specific message if no candidate is complete. Until Task 3 implements that check, prepend the verified WinLibs `bin` directory for baseline commands only.

### Metadata

- Reproducible: yes
- Related Files: `tools/build-native-ui.ps1`, `tests/LightLaunchpad.NativeUi.Tests/AdaptiveWindowBehavior.Tests.ps1`

---

## [ERR-20260809-004] windowsapps-pwsh-pty-access-denied

**Logged**: 2026-08-09T16:00:00+08:00
**Priority**: low
**Status**: resolved
**Area**: tests

### Summary

The unified executor could not create a PTY process through the WindowsApps `pwsh.exe` alias, so the NativeUi baseline command never started.

### Error

```text
Failed to create unified exec process ... WindowsApps\pwsh.exe ... 拒绝访问。 (os error 5)
```

### Context

- The command requested `tty: true` for a long PowerShell test loop.
- Process creation failed before the first suite ran.
- The repository, product processes, and test outputs were not changed by the failed launch.

### Suggested Fix

Run PowerShell test loops without PTY in this Windows desktop environment. Use the returned session ID for long-running non-PTY commands when needed.

### Metadata

- Reproducible: unknown
- Related Files: `tests/LightLaunchpad.NativeUi.Tests/*.Tests.ps1`

### Resolution

- **Resolved**: 2026-08-09T16:00:00+08:00
- **Commit/PR**: next scoped documentation commit
- **Notes**: The baseline will be reissued without PTY; only actual suite output will count as verification.

---

## [ERR-20260809-003] powershell-empty-git-output-trim

**Logged**: 2026-08-09T15:55:00+08:00
**Priority**: low
**Status**: resolved
**Area**: tests

### Summary

The worktree-isolation probe called `.Trim()` on an empty Git result, which PowerShell represented as `$null` for a repository without a superproject.

### Error

```text
You cannot call a method on a null-valued expression.
```

### Context

- `git rev-parse --show-superproject-working-tree` legitimately emitted no text.
- The probe attempted `(git rev-parse --show-superproject-working-tree).Trim()`.
- The failed command was read-only and made no repository or system change.

### Suggested Fix

Collect command output as an array, join it to an empty-safe string, then trim: `(@(git rev-parse --show-superproject-working-tree) -join '').Trim()`.

### Metadata

- Reproducible: yes
- Related Files: `.learnings/ERRORS.md`

### Resolution

- **Resolved**: 2026-08-09T15:55:00+08:00
- **Commit/PR**: next scoped documentation commit
- **Notes**: A direct `[string]` cast still produced a null-valued expression in this host. The verified correction uses an array plus `-join ''`, which yields an empty string when Git emits no lines.

---

## [ERR-20260809-002] protected-runtime-pid-assumption

**Logged**: 2026-08-09T15:35:00+08:00
**Priority**: medium
**Status**: resolved
**Area**: tests

### Summary

A safety baseline incorrectly treated the previously observed NativeUi PID as a stable identity even though the protected executable had legitimately restarted under a new PID.

### Error

```text
Protected PID 22560 is not running.
```

### Context

- The check queried only PID 22560 before validating the executable path.
- Read-only process enumeration found PID 29376 running the exact protected path; its creation time was 2026-08-04 23:42:39.
- The protected EXE SHA256, exact HKCU Run command, and `C:\TOOL` EXE SHA256 remained unchanged.
- No process was started, stopped, or replaced during diagnosis.

### Suggested Fix

Resolve protected runtime identity by normalized absolute executable path, require the expected process count, then validate the on-disk hash and startup command. Record PID and creation time as observations, not durable identity fields.

### Metadata

- Reproducible: yes
- Related Files: `docs/superpowers/plans/2026-08-09-llp-013-live-material-renderer.md`
- See Also: `ERR-20260802-001`

### Resolution

- **Resolved**: 2026-08-09T15:35:00+08:00
- **Commit/PR**: LLP-013 implementation-plan commit
- **Notes**: Replaced both plan safety checks with exact-path enumeration that requires one process while treating PID as observational.

---
