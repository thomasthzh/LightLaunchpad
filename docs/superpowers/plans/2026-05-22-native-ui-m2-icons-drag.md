# Native UI M2 Icons And Drag Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve NativeUi icon quality and add native app drag sorting with layout persistence.

**Architecture:** Keep the low-memory Win32 process. Use the shell system image list for high-resolution icons, draw with transparent alpha, and add a small native drag state machine that reorders the in-memory item list and writes `layout.json` atomically.

**Tech Stack:** C++17 Win32, Shell API, GDI double buffering, existing C# source tests, PowerShell release scripts.

---

### Task 1: Register M2 Source Tests

**Files:**
- Modify: `tests/LightLaunchpad.App.Tests/Program.cs`
- Test: `tests/LightLaunchpad.App.Tests/HostedAppModeSourceTests.cs`

- [ ] Register `NativeUiSource_LoadsHighQualityShellIcons`.
- [ ] Register `NativeUiSource_SupportsDragSortingAndLayoutPersistence`.
- [ ] Run app tests and confirm the new tests fail against current NativeUi.

### Task 2: High Quality Native Icons

**Files:**
- Modify: `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp`
- Test: `tests/LightLaunchpad.App.Tests/HostedAppModeSourceTests.cs`

- [ ] Replace `SHGFI_USEFILEATTRIBUTES` icon loading with `SHGFI_SYSICONINDEX`.
- [ ] Request `SHIL_JUMBO` first and `SHIL_EXTRALARGE` as fallback through `SHGetImageList` and `IID_IImageList`.
- [ ] Draw icons with alpha transparency using `ILD_TRANSPARENT`.
- [ ] Run app tests and native build.

### Task 3: Native Drag Sorting And Save

**Files:**
- Modify: `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp`
- Test: `tests/LightLaunchpad.App.Tests/HostedAppModeSourceTests.cs`

- [ ] Track tile and region header hit rectangles.
- [ ] Start drag after a small mouse movement threshold.
- [ ] During drag, compute a `DropTarget` from the tile grid or region header.
- [ ] On mouse up, reorder the dragged item in `g_items`, renumber orders per region, and save `layout.json` through an atomic `.tmp` replace.
- [ ] Keep drag disabled while search is active so filtered results do not corrupt full layout order.

### Task 4: Verify, Package, Commit, Push

**Files:**
- Modify docs progress notes if behavior or measured evidence changes.

- [ ] Run Core tests.
- [ ] Run App tests.
- [ ] Build solution.
- [ ] Build NativeUi.
- [ ] Package release.
- [ ] Clean-`PATH` launch packaged NativeUi and record memory.
- [ ] Commit and push `main`.
