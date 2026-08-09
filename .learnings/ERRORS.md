# Error Log

## [ERR-20260809-011] text-input-behavior-ps51-source-compatibility

**Logged**: 2026-08-09T18:48:00+08:00
**Priority**: medium
**Status**: resolved
**Area**: tests

### Summary

The complete NativeUi baseline passed 15 suites, then Windows PowerShell 5.1 parsed the only non-ASCII, UTF-8-without-BOM test script as the active ANSI code page and corrupted its Chinese IME literals into invalid syntax.

### Error

```text
TextInputBehavior.Tests.ps1:274 Missing ')' in method call.
TextInputBehavior.Tests.ps1:277 The Try statement is missing its Catch or Finally block.
TextInputBehavior.Tests.ps1:306 The string is missing the terminator: '.
```

### Context

- `TextInputBehavior.Tests.ps1` starts with bytes `70 61 72 61 6D` (`param`) and has no UTF-8 BOM.
- It is the only NativeUi PowerShell suite containing non-ASCII source and the only such suite without a BOM.
- Reading the file explicitly as UTF-8 shows intact `中`, `文`, and `末` IME literals and expected strings; the corruption occurs only in Windows PowerShell 5.1 `-File` decoding.
- The same file also has three direct nonzero `Int32` to `UIntPtr` casts (`0x24`, `0x2E`, and `1`). Independent Windows PowerShell 5.1 probes confirm direct `Int32` conversion fails while `Char` conversion succeeds, so these would become the next pre-production interop failures after parsing is fixed.
- The protected runtime identity and hashes, HKCU Run value, stable-install absence, and `C:\TOOL` hash remained unchanged. No matching text-input-behavior temp root existed after the parser stop.

### Suggested Fix

Keep the repository's UTF-8-without-BOM convention by expressing the three Chinese UTF-16 code units with ASCII numeric code points and building expected strings from those characters. Use explicit `UIntPtr::new([uint64]...)` construction for every nonzero or variable WPARAM in the probe while retaining `UIntPtr::Zero`, then run the complete real-EXE TextInputBehavior suite twice under Windows PowerShell 5.1 and obtain independent spec and quality reviews before restarting all 16 suites.

### Metadata

- Reproducible: yes
- Related Files: `tests/LightLaunchpad.NativeUi.Tests/TextInputBehavior.Tests.ps1`
- See Also: `ERR-20260809-007`, `ERR-20260809-010`

### Resolution

- **Resolved**: 2026-08-09T19:15:00+08:00
- **Commit/PR**: `fe4b0f4c70775573799af08465ba4f02b4b3adb7`
- **Notes**: Replaced non-ASCII source literals with three runtime UTF-16 code-point variables and composed expected strings, while converting all 13 nonzero/variable WPARAM values to explicit `UIntPtr` construction. The implementer demonstrated parser RED, encoding-only UIntPtr RED, and two real-EXE GREEN runs; independent spec and quality runs also passed with final value `中a文b末`, caret 5, and candidate `(188,118)`.

---

## [ERR-20260809-010] spotlight-probe-uintptr-conversion

**Logged**: 2026-08-09T18:02:00+08:00
**Priority**: medium
**Status**: resolved
**Area**: tests

### Summary

The complete NativeUi baseline passed 13 suites, then the Spotlight real-EXE probe failed before its first activation because Windows PowerShell 5.1 cannot directly convert a nonzero `Int32` to `UIntPtr`.

### Error

```text
Cannot convert the "19541" value of type "System.Int32" to type "System.UIntPtr".
SpotlightFlowBehavior.Tests.ps1:209
```

### Context

- The first failing expression is `[UIntPtr]0x4C55` in `SpotlightFlowBehavior.Tests.ps1`.
- The file also directly casts variable key/character values and nonzero command identifiers to `UIntPtr`.
- Production NativeUi did not receive the first failing message; this is a test-driver interop conversion issue.
- The protected runtime path, PID, SHA256, HKCU Run value, stable-install absence, and `C:\TOOL` SHA256 remained unchanged after the stopped baseline.
- The only matching Spotlight temp root was created on 2026-08-02, contains no reparse points or running executable, and was left untouched.

### Suggested Fix

As in the reviewed Launchpad probe fix, use `UIntPtr::new([uint64]value)` for every nonzero or variable WPARAM and retain `UIntPtr::Zero` for zero; run the complete real-EXE Spotlight flow twice under Windows PowerShell 5.1, obtain independent spec and quality reviews, then restart the complete 16-suite baseline from suite 1.

### Metadata

- Reproducible: yes
- Related Files: `tests/LightLaunchpad.NativeUi.Tests/SpotlightFlowBehavior.Tests.ps1`
- See Also: `ERR-20260809-007`

### Resolution

- **Resolved**: 2026-08-09T18:25:00+08:00
- **Commit/PR**: `325c5d6b1b107acd96964f16e286fa5b1e3ea5b4`
- **Notes**: Replaced all 11 nonzero or variable WPARAM casts with explicit `UIntPtr` construction and retained both zero constants. Two implementer runs plus independent spec and quality review runs passed the complete real-EXE Spotlight flow under Windows PowerShell 5.1 with P95 16 ms, two launches, seven opens, and an unchanged historical temp-root set.

---

## [ERR-20260809-009] search-index-windows-powershell-string-overload

**Logged**: 2026-08-09T17:05:00+08:00
**Priority**: medium
**Status**: resolved
**Area**: tests

### Summary

The NativeUi baseline stopped in `SearchIndex.Tests.ps1` because Windows PowerShell 5.1 cannot bind the two-argument `String.Contains(string, StringComparison)` overload.

### Error

```text
Cannot find an overload for "Contains" and the argument count: "2".
```

### Context

- The full baseline runner uses Windows PowerShell 5.1.
- `SearchIndex.Tests.ps1` line 28 calls `$llpHeader.Contains($llpEvidence, [System.StringComparison]::Ordinal)`.
- The same unchanged test works under PowerShell 7, so the failure is test-runner compatibility rather than production search behavior.
- Earlier NativeUi suites, including the repaired Launchpad flow, had passed before this stop.

### Suggested Fix

Use the Windows PowerShell 5.1-compatible `IndexOf(string, StringComparison) -ge 0` form while preserving ordinal provenance matching, then run the focused suite twice under Windows PowerShell 5.1 before repeating the complete baseline.

### Metadata

- Reproducible: yes
- Related Files: `tests/LightLaunchpad.NativeUi.Tests/SearchIndex.Tests.ps1`
- See Also: `ERR-20260809-005`

### Resolution

- **Resolved**: 2026-08-09T17:20:00+08:00
- **Commit/PR**: `e63484d7cc40da3bc56f9b5c5d5213657fc5d043`
- **Notes**: Replaced the unsupported overload with ordinal `IndexOf(...) -lt 0`. Two implementer runs plus independent spec and quality review runs passed under Windows PowerShell 5.1; each native run was 7/7 and left the pre-existing temp-root set unchanged.

---

## [ERR-20260809-008] search-index-test-path-assumption

**Logged**: 2026-08-09T17:04:00+08:00
**Priority**: low
**Status**: resolved
**Area**: tests

### Summary

A read-only inspection assumed the SearchIndex test lived under `tests/NativeUi`, but the repository stores it under `tests/LightLaunchpad.NativeUi.Tests`.

### Error

```text
Cannot find path 'tests/NativeUi/SearchIndex.Tests.ps1' because it does not exist.
```

### Context

- The command combined Git status checks with a direct `Get-Content` on the assumed path.
- Git state queries completed; only the file read failed.
- No repository, product process, installation, startup item, or temporary data changed.

### Suggested Fix

Resolve unfamiliar repository paths with `rg --files` before opening them. The verified path is `tests/LightLaunchpad.NativeUi.Tests/SearchIndex.Tests.ps1`.

### Metadata

- Reproducible: yes
- Related Files: `tests/LightLaunchpad.NativeUi.Tests/SearchIndex.Tests.ps1`

### Resolution

- **Resolved**: 2026-08-09T17:04:00+08:00
- **Commit/PR**: next scoped learning-log commit
- **Notes**: Located the file with `rg --files | rg 'SearchIndex\\.Tests\\.ps1$'` and resumed inspection at the verified path.

---

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
