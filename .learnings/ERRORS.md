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
