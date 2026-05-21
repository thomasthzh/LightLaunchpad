# LightLaunchpad Project Standards

## Debug Protocol

Reproduce the failure first, capture the exact command and output, isolate one cause, make the smallest fix, and rerun the failing check plus nearby regression checks.

## Test Protocol

Use test-first for behavior changes when practical. Core logic should be covered in `LightLaunchpad.Core.Tests`; WPF source contracts can be covered by source/XAML tests when full UI automation is too heavy.

## Version Control

Inspect `git status --short` before editing. Do not revert unrelated user changes. Keep commits scoped to one route step.

## Performance Claims

Do not claim memory, CPU, startup, or responsiveness improvements without fresh measurement. Report Working Set and Private Memory separately.

## Local Environment

Windows PowerShell workspace: `C:\Users\thoma\Desktop\启动台`. Use the repo-local `.dotnet\dotnet.exe` for build, test, and publish.

## Handoff Protocol

A restarting agent should read:

- `docs/ai/requirements-and-route.md`
- `docs/ai/tasks.md`
- `docs/ai/project-standards.md`
- `docs/ai/progress.md`

Then inspect current code and choose the first pending or in-progress task.