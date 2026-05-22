# Native UI M3 Interaction Parity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Continue migrating WPF launchpad interactions into NativeUi with multi-select app dragging, region drag sorting, and basic native context menus.

**Architecture:** Keep one low-memory Win32 process. Add explicit selection state, drag mode state, and context menu commands around the existing `g_items`, `g_regions`, and `SaveLayout` data path so every accepted mutation persists to `%AppData%\LightLaunchpad\layout.json`.

**Tech Stack:** C++17 Win32, Shell API, GDI double buffering, source-level C# regression tests, PowerShell release packaging.

---

### Task 1: M3 Regression Tests

- [ ] Add source tests covering multi-select state, group drag ordering, region drag ordering, and context menu commands.
- [ ] Register the tests in `tests/LightLaunchpad.App.Tests/Program.cs`.
- [ ] Run App tests and confirm they fail against current NativeUi.

### Task 2: Multi-Select App Drag

- [ ] Track selected source paths.
- [ ] Support Ctrl-click toggling and normal click single selection.
- [ ] Drag all selected items when the drag starts from a selected item.
- [ ] Persist group reorder through the existing atomic `SaveLayout` path.

### Task 3: Region Drag Sorting And Context Menus

- [ ] Track region header hit rectangles.
- [ ] Start a region drag from headers after the drag threshold.
- [ ] Move region order and persist it through `SaveLayout`.
- [ ] Add native item and region context menus for launch/open-location/remove and region delete.

### Task 4: Verification And Release

- [ ] Run Core tests, App tests, solution build, NativeUi build, and package release.
- [ ] Start the packaged NativeUi with a clean `PATH` and record Working Set/Private Memory.
- [ ] Commit and push `main`.
