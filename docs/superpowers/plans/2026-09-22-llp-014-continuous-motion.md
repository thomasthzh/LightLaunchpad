# LLP-014 Continuous Focus And Nonblocking Motion

## Outcome

Spotlight and Launchpad motion is now time-based and nonblocking. Opening, closing, focus, and press feedback are sampled from a QPC clock; the latest frame replaces older requests, and input remains available while the first visual frame is being composed.

## Acceptance contract

- Open: 170 ms; close: 140 ms; focus: 100 ms; press: 72 ms.
- Reduced motion: 90/90/80/60 ms, with no offset or scale motion.
- Focus scale is capped at 1.015; spatial offset is capped at 10 DIP.
- High-fidelity motion is submitted through the renderer-owned DirectComposition root. Fallback motion updates the layered alpha on the 16 ms timer.
- Reopening during a close reverses from the current frame, resets focus to Search, and preserves the current query so rapid Alt+D cannot lose text.
- The opening call does not synchronously rebuild the full fallback bitmap; the first motion tick presents it after the HWND is visible and focusable.

## Changed files

- `src/LightLaunchpad.NativeUi/MotionTimeline.{h,cpp}` - pure motion timeline and easing channels.
- `src/LightLaunchpad.NativeUi/MaterialRenderer.{h,cpp}` - renderer-thread motion request and DirectComposition offset application.
- `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp` - QPC/timer lifecycle, reverse-open handling, focus/press feedback, and fallback startup deferral.
- `tests/LightLaunchpad.NativeUi.Tests/MotionTimeline.Tests.{cpp,ps1}` - RED/GREEN timing, reversal, reduced-motion, and bounds coverage.
- `tests/LightLaunchpad.NativeUi.Tests/SpotlightFlowBehavior.Tests.ps1` - close-animation waits and performance diagnostics for repeated open/close.
- `tools/build-native-ui.ps1` - includes the production timeline module.

## Verification evidence

- RED: the focused timeline test initially failed to compile because the production module did not exist.
- GREEN: `MotionTimeline.Tests.ps1` -> 4/4.
- `MaterialRenderer.Tests.ps1` -> 43/43.
- Normal real EXE `SpotlightFlowBehavior.Tests.ps1` -> PASS; DISPLAY1, high-fidelity, real Alt+D, search/result navigation, catalog refresh focus retention, auxiliary settings, and delayed close all passed.
- Forced fallback real EXE `SpotlightFlowBehavior.Tests.ps1 -MaterialFailure initialization` -> PASS; fallback ownership and canonical navigation passed.
- Real performance probe `SpotlightFlowBehavior.Tests.ps1 -PerformanceOnly -EvidenceDirectory $env:TEMP\llp014-final-performance` -> PASS: hotkey-to-input-ready P95 94 ms, frame P95 8.759 ms, focus P95 31.93 ms, hidden CPU 0%, hidden acquired/presented counters stationary, retained hidden Private Bytes growth 39,022,592 bytes after the first graphics teardown. The measured footprint is recorded without relabeling; the enforced smoothness/input gates remain green.
- `AdaptiveWindowBehavior.Tests.ps1` -> 4/4 real-window cases, including reduced motion.
- Native build: `tools/build-native-ui.ps1 -OutputDirectory artifacts\native-ui-llp014-dev -ProductVersion 1.11.0 -BuildCommit llp014dev -BuildChannel Development` -> success; EXE 1,871,360 bytes, SHA256 `770F2C21A63578E8E4C6950FC48A1701AA5B6EDAE8873EE7405415E5A6320EF2`.
- `ExecutableIdentity.Tests.ps1` -> 6/6 after the clean-source fixture was updated to include `MotionTimeline.{h,cpp}`. Core/App suites pass 33/33 and 74/74. Protected runtime path/hash, startup value, and `C:\TOOL` hash remain unchanged.
- `git diff --check` and PowerShell parse checks -> pass.

## Close status

LLP-014 is complete. The next implementation gate is LLP-015 (Settings Information Architecture); no install, startup, `C:\TOOL`, push, tag, or release mutation was performed.
