# Error Log

## [ERR-20260824-009] visual-manifest-directory-name

**Logged**: 2026-08-24T18:47:00+08:00
**Priority**: low
**Status**: resolved
**Area**: docs

### Summary

The first Task 10 evidence summary looked for the visual manifest in the performance directory instead of the retained material-evidence directory.

### Error

```text
Cannot find path 'artifacts\native-ui-llp013-verification\manifest.json' because it does not exist.
```

### Context

- Performance evidence is stored under `native-ui-llp013-verification`.
- The 12+1 visual manifest is stored under `native-ui-llp013-material-evidence`.
- The failed command was read-only and changed no evidence.

### Suggested Fix

Enumerate the retained LLP-013 artifact directories before aggregating evidence and read each artifact from its recorded owner directory.

### Metadata

- Reproducible: yes
- Related Files: `artifacts/native-ui-llp013-material-evidence/manifest.json`, `artifacts/native-ui-llp013-verification/performance.json`

### Resolution

- **Resolved**: 2026-08-24T18:47:00+08:00
- **Commit/PR**: current LLP-013 Task 9 worktree
- **Notes**: The visual manifest and performance JSON were read from their separate retained directories and both schemas were verified.

---

## [ERR-20260824-008] asynchronous-gpu-teardown-retained-memory

**Logged**: 2026-08-24T18:43:00+08:00
**Priority**: high
**Status**: resolved
**Area**: frontend

### Summary

Two fresh 30-open performance runs retained 34-45 MB above the first hidden baseline even though capture/presentation counters and hidden CPU were stationary.

### Error

```text
Hidden retained Private Bytes growth exceeded 15 MB after the first complete graphics teardown.
```

### Context

- DirectComposition was detached and committed, all renderer-owned COM resources were released, the D3D context was cleared/flushed, `IDXGIDevice3::Trim` ran, and the renderer thread exited.
- `Flush` is asynchronous, so repeated device recreation could begin before the prior GPU command stream had completed and released driver allocations.
- The absolute hidden Private Bytes figure remains dominated by NVIDIA user-mode driver commitment and is recorded separately from renderer-retained growth.

### Suggested Fix

Before releasing the device, issue a `D3D11_QUERY_EVENT`, flush, and wait for completion with a strict renderer-thread-only deadline; then perform the existing release/trim sequence.

### Metadata

- Reproducible: yes
- Related Files: `src/LightLaunchpad.NativeUi/MaterialRenderer.cpp`, `tests/LightLaunchpad.NativeUi.Tests/SpotlightFlowBehavior.Tests.ps1`

### Resolution

- **Resolved**: 2026-08-24T18:43:00+08:00
- **Commit/PR**: current LLP-013 Task 9 worktree
- **Notes**: Two consecutive real-EXE reruns passed: retained growth 0 bytes and 14,012,416 bytes; hidden CPU remained 0% and frame counters did not advance.

---

## [ERR-20260824-007] expired-exec-session-after-compaction

**Logged**: 2026-08-24T18:30:00+08:00
**Priority**: low
**Status**: resolved
**Area**: tests

### Summary

Polling a previously yielded command after conversation compaction returned an unknown process id, so its final console tail was unavailable.

### Error

```text
write_stdin failed: Unknown process id 50986
```

### Context

- The process had already exited and the tool session had been retired.
- The retained `performance.json` timestamp and contents provided the authoritative failed measurement.

### Suggested Fix

Inspect the command's durable evidence and process table, then rerun only the focused gate when the terminal tail cannot be recovered.

### Metadata

- Reproducible: no
- Related Files: `artifacts/native-ui-llp013-verification/performance.json`

### Resolution

- **Resolved**: 2026-08-24T18:31:00+08:00
- **Commit/PR**: current LLP-013 Task 9 worktree
- **Notes**: The stale measurement was read directly and the focused performance gate was rerun with a captured exit code.

---

## [ERR-20260824-006] singleton-forwarded-diagnostics-invocation

**Logged**: 2026-08-24T18:25:00+08:00
**Priority**: low
**Status**: resolved
**Area**: tests

### Summary

Directly invoking the precommit EXE returned success through the existing singleton-forwarding path without creating the requested diagnostics file.

### Error

```text
The process exited 0 but the requested diagnostics JSON was absent.
```

### Context

- The protected user runtime already owned the product singleton.
- A bare invocation could therefore forward and exit before the isolated diagnostics path ran.

### Suggested Fix

Launch the exact absolute EXE with `Start-Process -Wait`, an absolute diagnostics path, and isolated test profile arguments; verify the file rather than trusting exit code alone.

### Metadata

- Reproducible: yes while the protected singleton is running
- Related Files: `tools/build-native-ui.ps1`, `tests/LightLaunchpad.NativeUi.Tests/ExecutableIdentity.Tests.ps1`

### Resolution

- **Resolved**: 2026-08-24T18:25:00+08:00
- **Commit/PR**: current LLP-013 Task 9 worktree
- **Notes**: The precommit probe was rerun through an explicit absolute process launch and its diagnostics file was verified.

---

## [ERR-20260824-005] clipboard-restore-contention

**Logged**: 2026-08-24T18:22:00+08:00
**Priority**: medium
**Status**: resolved
**Area**: tests

### Summary

The real text-input suite passed its product assertions, then a transient clipboard lock made its single-attempt user-clipboard restore fail in `finally`.

### Error

```text
Requested Clipboard operation did not succeed.
```

### Context

- The product probe had already exited successfully with the expected UIA/IME state.
- The failure was in safety cleanup and therefore could not be ignored or downgraded.

### Suggested Fix

Restore the captured clipboard through a short bounded retry loop and still fail explicitly if all retries are exhausted.

### Metadata

- Reproducible: intermittent
- Related Files: `tests/LightLaunchpad.NativeUi.Tests/TextInputBehavior.Tests.ps1`

### Resolution

- **Resolved**: 2026-08-24T18:22:00+08:00
- **Commit/PR**: current LLP-013 Task 9 worktree
- **Notes**: Clipboard clear/set now retries up to twenty times at 50 ms intervals; the affected suite passed with the original clipboard restored.

---

## [ERR-20260824-004] performance-probe-observed-stale-state

**Logged**: 2026-08-24T18:10:00+08:00
**Priority**: medium
**Status**: resolved
**Area**: tests

### Summary

The first performance harness revisions treated a stale ready JSON snapshot and a globally occupied Alt+9 registration as renderer failures.

### Error

```text
Performance setup did not reach the hidden warm-open baseline.
Visible interaction frame 1 was not presented.
```

### Context

- The protected user runtime already owned Alt+9, so physical key injection targeted that process rather than the isolated probe.
- Hidden and asynchronous presentation state required an explicit status publication before reading the ready JSON.
- Search-caret animation also meant a test must lock the generation created by an input instead of chasing the continuously newest generation.

### Suggested Fix

Drive the isolated probe's production `WM_HOTKEY` handler directly, explicitly refresh diagnostic state, and use two-phase fixed-generation presentation waits.

### Metadata

- Reproducible: yes
- Related Files: `tests/LightLaunchpad.NativeUi.Tests/SpotlightFlowBehavior.Tests.ps1`

### Resolution

- **Resolved**: 2026-08-24T18:10:00+08:00
- **Commit/PR**: current LLP-013 Task 9 worktree
- **Notes**: The final 30-open/600-frame/20-focus real-EXE performance gate passes without touching the protected runtime.

---

## [ERR-20260824-003] performance-evidence-parameter-name

**Logged**: 2026-08-24T17:25:00+08:00
**Priority**: low
**Status**: resolved
**Area**: tests

### Summary

The first manual performance command used `-EvidenceOutputDirectory`, but the new suite contract exposes `-EvidenceDirectory`.

### Error

```text
Performance mode requires -EvidenceDirectory.
```

### Context

- Parameter validation stopped before the performance probe or product executable started.
- No runtime, repository, or evidence state was changed by the failed invocation.

### Suggested Fix

Read the script parameter block before issuing a new mode command and use the exact declared parameter name.

### Metadata

- Reproducible: yes
- Related Files: `tests/LightLaunchpad.NativeUi.Tests/MaterialRendererBehavior.Tests.ps1`

### Resolution

- **Resolved**: 2026-08-24T17:26:00+08:00
- **Commit/PR**: current LLP-013 Task 9 worktree
- **Notes**: All subsequent focused and final performance runs used `-EvidenceDirectory`.

---

## [ERR-20260824-002] windows-powershell-get-filehash-unavailable

**Logged**: 2026-08-24T17:05:00+08:00
**Priority**: low
**Status**: resolved
**Area**: tests

### Summary

The Windows PowerShell host used by the nested Spotlight evidence flow did not expose `Get-FileHash`, so EXE provenance capture stopped before product launch.

### Error

```text
Get-FileHash is not recognized as the name of a cmdlet.
```

### Context

- The parent PowerShell host supports `Get-FileHash`, but the nested compatibility host running the real-EXE suite does not.
- The failure occurred before NativeUi started and removed the forced case evidence, so no stale case could pass.

### Suggested Fix

Use `System.Security.Cryptography.SHA256` over an explicitly disposed read stream in cross-version PowerShell test harnesses.

### Metadata

- Reproducible: yes
- Related Files: `tests/LightLaunchpad.NativeUi.Tests/SpotlightFlowBehavior.Tests.ps1`

### Resolution

- **Resolved**: 2026-08-24T17:05:00+08:00
- **Commit/PR**: current LLP-013 Task 8 worktree
- **Notes**: The evidence flow now computes lowercase SHA256 through the .NET cryptography API.

---

## [ERR-20260824-001] system-drawing-pathinfo-constructor

**Logged**: 2026-08-24T16:45:00+08:00
**Priority**: low
**Status**: resolved
**Area**: tests

### Summary

A one-off pixel probe passed PowerShell's `PathInfo` object directly to `System.Drawing.Bitmap`, so overload resolution tried the image constructor and rejected the value.

### Error

```text
Cannot convert argument "original" ... from type System.Management.Automation.PathInfo to System.Drawing.Image.
```

### Context

- The probe inspected a retained Task 8 material-evidence PNG.
- The failure was isolated to the read-only diagnostic command; the evidence image and product runtime were unchanged.

### Suggested Fix

Resolve the file first and pass its string path explicitly: `$path = (Resolve-Path -LiteralPath $file).Path; [System.Drawing.Bitmap]::new($path)`.

### Metadata

- Reproducible: yes
- Related Files: `tools/capture-material-evidence.ps1`

### Resolution

- **Resolved**: 2026-08-24T16:45:00+08:00
- **Commit/PR**: current LLP-013 Task 8 worktree
- **Notes**: Subsequent standalone bitmap probes use the resolved `.Path` string.

---

## [ERR-20260812-019] task5-manual-build-identity-mismatch

**Logged**: 2026-08-12T12:06:15+08:00
**Priority**: medium
**Status**: resolved
**Area**: tests

### Summary

A retained Task 5 artifact was built with a manually copied short commit string that did not equal the repository HEAD, so its otherwise successful real probe could not be identity evidence.

### Error

```text
Requested build commit 0c593a2d9fa2 did not equal HEAD 0c593a291d10444a4e0a697251f334963a4a3eac.
```

### Context

- The artifact ran `high-fidelity/None`, but its compiled identity was wrong.
- It was retained under ignored `artifacts/` and explicitly excluded from all completion evidence.
- A new isolated build used the exact full `git rev-parse HEAD` value and passed identity plus real-probe checks.

### Suggested Fix

Resolve the full HEAD programmatically in the same foreground PowerShell command that validates and passes the build parameter; never retype a commit identifier for identity-bearing artifacts.

### Metadata

- Reproducible: yes
- Related Files: `tools/build-native-ui.ps1`

### Resolution

- **Resolved**: 2026-08-12T12:06:15+08:00
- **Commit/PR**: Task 5 completion-evidence documentation commit
- **Notes**: Final evidence uses exact HEAD `7a435550b05e52b1c9bc3a5d808f5b477f780ae0` and independent real builds only.

---

## [ERR-20260812-018] material-probe-unsupported-field-assertion

**Logged**: 2026-08-12T12:06:15+08:00
**Priority**: low
**Status**: resolved
**Area**: tests

### Summary

A reviewer wrapper asserted a `buildId` property that the material-probe JSON contract does not publish, causing a false outer failure after the exact-HEAD suite had passed and cleaned up.

### Error

```text
The wrapper attempted to read/assert a buildId field absent from material-probe JSON.
```

### Context

- The exact full commit was passed into the build and independently visible in executable diagnostics.
- The real material suite had already passed with `high-fidelity/None`, positive acquired/presented counts, stable rejection, and complete cleanup.
- The failure belonged only to the extra wrapper assertion.

### Suggested Fix

Assert identity from the documented diagnostics JSON and assert renderer behavior only from the material-probe JSON fields it actually defines.

### Metadata

- Reproducible: yes
- Related Files: `tests/LightLaunchpad.NativeUi.Tests/MaterialRendererBehavior.Tests.ps1`

### Resolution

- **Resolved**: 2026-08-12T12:06:15+08:00
- **Commit/PR**: Task 5 completion-evidence documentation commit
- **Notes**: Final spec and quality evidence used supported fields plus separate exact-HEAD diagnostics.

---

## [ERR-20260812-017] powershell-single-staged-path-scalar

**Logged**: 2026-08-12T12:06:15+08:00
**Priority**: low
**Status**: resolved
**Area**: tests

### Summary

A staged-scope guard treated one line of `git diff --cached --name-only` as a scalar string, so `.Count` measured string behavior and stopped a valid one-file documentation commit before Git committed it.

### Error

```text
Unexpected staged scope.
```

### Context

- Only the intended plan file was staged; the guard failed safe before commit.
- No protected or unrelated path was staged.

### Suggested Fix

Always wrap Git line output as an array: `$names = @(git diff --cached --name-only)` before count and element checks.

### Metadata

- Reproducible: yes
- Related Files: `docs/superpowers/plans/2026-08-09-llp-013-live-material-renderer.md`
- See Also: `ERR-20260809-003`, `ERR-20260809-012`

### Resolution

- **Resolved**: 2026-08-12T12:06:15+08:00
- **Commit/PR**: `d0cbe69`
- **Notes**: Reissuing the same guarded commit with `@(...)` staged and committed exactly one plan file.

---

## [ERR-20260809-016] parallel-real-exe-review-singleton-collision

**Logged**: 2026-08-09T20:56:02+08:00
**Priority**: medium
**Status**: resolved
**Area**: tests

### Summary

Two independent Task 3 reviewers started `ExecutableIdentity.Tests.ps1` concurrently even though the harness shares a fixed development singleton and observes global temp-prefix sets, invalidating both runs as final evidence.

### Error

```text
Two review harnesses overlapped on the fixed development singleton and produced cross-run temp/process observations.
```

### Context

- The real-EXE outputs were otherwise isolated, and the protected PID 29376 was never involved.
- Neither reviewer terminated a process or deleted the other run's files; both harnesses completed their own `finally` cleanup naturally.
- Every overlapping result was discarded. Specification and quality reviewers then reran `ExecutableIdentity` separately with process/temp baselines at zero.

### Suggested Fix

Treat real-EXE harnesses with fixed mutex/singleton names or global temp-prefix assertions as mutually exclusive. Parallelize static review and isolated unit suites only; require an explicit all-processes-zero handoff before the next reviewer starts such a harness.

### Metadata

- Reproducible: yes
- Related Files: `tests/LightLaunchpad.NativeUi.Tests/ExecutableIdentity.Tests.ps1`

### Resolution

- **Resolved**: 2026-08-09T20:56:02+08:00
- **Commit/PR**: Task 3 completion-evidence documentation commit
- **Notes**: Final serial runs both passed 6/6 with identity temp 0 -> 0 and nonprotected NativeUi process 0 -> 0.

---

## [ERR-20260809-015] nested-exec-session-id-discarded

**Logged**: 2026-08-09T20:07:42+08:00
**Priority**: medium
**Status**: resolved
**Area**: tests

### Summary

A long test launched through `functions.exec` kept running after the JavaScript cell completed, but the wrapper emitted only `r.output` and discarded the nested command's `session_id`, temporarily hiding the remaining output and exit code.

### Error

```text
Only the first two PASS lines were returned while the child PowerShell process continued its clean-source build.
```

### Context

- Read-only process inspection confirmed the original Windows PowerShell test process was still active and not waiting for input.
- No duplicate test was started until the original process exited.
- The issue was the orchestration wrapper, not `ExecutableIdentity.Tests.ps1` or the new compiler selection.

### Suggested Fix

For long nested `exec_command` calls, emit the full returned object (including `session_id`) rather than only `output`, then poll that unified session with `write_stdin` until an explicit `exit_code` is returned.

### Metadata

- Reproducible: yes
- Related Files: `tests/LightLaunchpad.NativeUi.Tests/ExecutableIdentity.Tests.ps1`

### Resolution

- **Resolved**: 2026-08-09T20:07:42+08:00
- **Commit/PR**: Task 3 completion-evidence documentation commit
- **Notes**: The fresh rerun preserved session `95313` and collected all five PASS lines plus exit 0.

---

## [ERR-20260809-014] pwsh-gui-call-operator-exit-code

**Logged**: 2026-08-09T20:01:01+08:00
**Priority**: low
**Status**: resolved
**Area**: tests

### Summary

PowerShell 7's call operator launched the GUI-subsystem NativeUi diagnostics EXE successfully but left `$LASTEXITCODE` empty, causing the verification wrapper to report a false failure.

### Error

```text
Diagnostics EXE exited .
```

### Context

- The exact built EXE exited and wrote a valid diagnostics JSON file.
- Read-only inspection found zero matching artifact processes afterward.
- The empty `$LASTEXITCODE` was wrapper behavior, not a NativeUi process failure.

### Suggested Fix

Use `Start-Process -PassThru -Wait -WindowStyle Hidden` and read `Process.ExitCode` when verifying a Windows GUI-subsystem EXE.

### Metadata

- Reproducible: yes
- Related Files: `artifacts/native-ui-llp013-bootstrap/LightLaunchpad.NativeUi.exe`

### Resolution

- **Resolved**: 2026-08-09T20:01:01+08:00
- **Commit/PR**: Task 3 completion-evidence documentation commit
- **Notes**: The next verification uses `Start-Process` and validates the generated diagnostics independently.

---

## [ERR-20260809-013] dynamic-temp-cleanup-command-rejected

**Logged**: 2026-08-09T20:00:24+08:00
**Priority**: low
**Status**: resolved
**Area**: tests

### Summary

A combined real-EXE diagnostics command was rejected before launch because it embedded dynamic recursive temporary-directory cleanup in the command string.

### Error

```text
CreateProcess ... rejected: blocked by policy
```

### Context

- The command intended to create a GUID runtime profile, launch the isolated NativeUi diagnostics process, validate identity, and recursively remove only that profile.
- The tool rejected the command before execution; the EXE was not started and no directory was created or removed.
- The Task 3 production build itself had already succeeded and was unaffected.

### Suggested Fix

For a one-shot diagnostic run, use a fixed profile directory inside the ignored build artifact and retain it. Keep cleanup out of the validation command unless a separate, fully resolved target has already been inspected.

### Metadata

- Reproducible: unknown
- Related Files: `artifacts/native-ui-llp013-bootstrap/LightLaunchpad.NativeUi.exe`

### Resolution

- **Resolved**: 2026-08-09T20:00:24+08:00
- **Commit/PR**: Task 3 completion-evidence documentation commit
- **Notes**: Switched to a retained artifact-local isolated profile; no destructive cleanup is required.

---

## [ERR-20260809-012] powershell-empty-temp-set-compare-object

**Logged**: 2026-08-09T19:04:00+08:00
**Priority**: low
**Status**: resolved
**Area**: tests

### Summary

The Task 1 spec review's read-only temp-root wrapper passed an empty collection to `Compare-Object`, which Windows PowerShell rejected after the MaterialRenderer suite and its cleanup had already succeeded.

### Error

```text
Compare-Object does not accept the empty temp-root collection argument.
```

### Context

- The focused `MaterialRenderer` production test had already passed `5/5` and removed its GUID temp root.
- Both the before and after matching root sets were empty.
- The failure was in the review wrapper's evidence comparison, not the test harness or production code.
- No temp root, product process, installation, startup value, or repository file changed because of the failed read-only comparison.

### Suggested Fix

Compare empty-safe arrays through explicit added/missing set differences and their counts, as used by the complete baseline runner, rather than passing a potentially empty collection directly to `Compare-Object`.

### Metadata

- Reproducible: yes
- Related Files: `tests/LightLaunchpad.NativeUi.Tests/MaterialRenderer.Tests.ps1`
- See Also: `ERR-20260809-003`

### Resolution

- **Resolved**: 2026-08-09T19:04:00+08:00
- **Commit/PR**: next scoped learning-log commit
- **Notes**: The reviewer used an empty-safe follow-up comparison for AdaptiveEnvironment and confirmed both relevant temp-prefix sets remained empty. Future review prompts will require explicit added/missing counts.

---

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

- **Resolved**: 2026-08-09T19:00:00+08:00
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
**Status**: resolved
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

### Resolution

- **Resolved**: 2026-08-09T20:56:02+08:00
- **Commit/PR**: `4f5b044c6c1e64af59859cebe4c5596b5b6a2e6d`
- **Notes**: The production resolver now rejects an ordinal requested-name echo and every non-rooted library response before checking the absolute leaf. Under a Strawberry-first PATH it rejects the incomplete Strawberry toolchain, selects complete WinLibs 16.1 deterministically, and the clean-source identity spoof gate proves an in-CWD fake import library cannot bypass capability detection.

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
