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
