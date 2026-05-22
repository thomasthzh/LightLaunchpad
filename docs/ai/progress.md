# LightLaunchpad Performance Progress

## Current State

- Branch: `main`
- Workspace: `C:\Users\thoma\Desktop\启动台`
- Active task: N1 - Native UI M1

## Completed

- Route chosen: split-process agent plus hosted WPF UI.
- A1 - Activation Contract.
- A2 - Hosted UI Mode.
- A3 - Agent Project.
- A4 - Packaging.
- A5 - Performance Probe.
- A6 - Native Win32 Agent.
- A7 - Native Agent Hardening.
- A8 - Formal Native Release Package.
- A9 - Revert Agent Default and Fix Search State.
- N1 - Native UI M1 source/build/package skeleton.

## Commands Run

- `.\.dotnet\dotnet.exe run --project tests\LightLaunchpad.Core.Tests\LightLaunchpad.Core.Tests.csproj` - 32/32 passed.
- `.\.dotnet\dotnet.exe run --project tests\LightLaunchpad.App.Tests\LightLaunchpad.App.Tests.csproj` - 28/28 passed.
- `.\.dotnet\dotnet.exe build LightLaunchpad.sln` - passed with 0 warnings and 0 errors.
- Published `LightLaunchpad.App` and `LightLaunchpad.Agent` into one local release folder.
- Started the packaged agent without opening UI and measured approximately 28 MB Working Set / 6.8 MB Private Memory.
- Researched LightFrame GitHub and local process. Repository architecture is native C/C++ Windows API plus DirectUI; local full LightFrame process measured about 25.2 MB Working Set / 95.3 MB Private Memory.
- Built native LightLaunchpad agent with MinGW g++ and measured approximately 8.9 MB Working Set / 1.4 MB Private Memory while idle.
- Hardened NativeAgent with tray icon loading and job-object cleanup. Rebuilt and measured approximately 9.9 MB Working Set / 1.6 MB Private Memory while idle.
- Added `tools/package-release.ps1` so each release can publish the WPF UI, managed fallback agent, native low-memory agent, and zip under local `release/`.
- Fixed native packaging for the Chinese workspace path by compiling to an ASCII temp path before copying the executable into `release/`.
- Updated startup registration to prefer the native low-memory agent when the packaged files are present.
- Formal release package probe measured approximately 9.89 MB Working Set / 1.57 MB Private Memory while idle.
- User rejected the agent route because cold-starting WPF is slow and stuttery in practice.
- Default release packaging now returns to the single-process WPF app for responsive repeated opening.
- Startup registration now stays on the app process instead of preferring agent executables.
- Search clear now preserves cached icon state after a no-result query.
- Added `LightLaunchpad.NativeUi`, a C++ Win32 native launcher surface that owns hotkey, tray, search, drawing, and app launch without cold-starting WPF.
- Added `tools/build-native-ui.ps1` and switched release packaging to native-first output with WPF fallback included.
- Fixed NativeUi package startup by statically linking the MinGW C/C++ runtime; clean-`PATH` launch from the package no longer needs MinGW DLLs next to the executable.
- Hardened `tools/package-release.ps1` so failed `dotnet publish` or native build steps abort instead of producing a partial package.
- Added NativeUi M2 icon and drag work: shell image-list icon loading now prefers `SHIL_JUMBO` / `SHIL_EXTRALARGE`, and native app drag sorting now writes `layout.json` atomically.
- Switched NativeUi to full static `-Os` / section-gc release builds and kept the tray icon on a light system icon; M2 probe measured approximately 8.87 MB Working Set / 1.55 MB Private Memory while idle.

## Changed Files

- `docs/ai/requirements-and-route.md`
- `docs/ai/tasks.md`
- `docs/ai/project-standards.md`
- `docs/ai/progress.md`
- `LightLaunchpad.sln`
- `src/LightLaunchpad.Core/Activation/LaunchpadActivationContext.cs`
- `src/LightLaunchpad.App/App.xaml.cs`
- `src/LightLaunchpad.App/Services/HostedActivationService.cs`
- `src/LightLaunchpad.Agent/LightLaunchpad.Agent.csproj`
- `src/LightLaunchpad.Agent/Program.cs`
- `tests/LightLaunchpad.Core.Tests/LaunchpadActivationContextTests.cs`
- `tests/LightLaunchpad.Core.Tests/Program.cs`
- `tests/LightLaunchpad.App.Tests/HostedAppModeSourceTests.cs`
- `tests/LightLaunchpad.App.Tests/Program.cs`
- `src/LightLaunchpad.NativeAgent/LightLaunchpad.NativeAgent.cpp`
- `tools/build-native-agent.ps1`
- `tools/package-release.ps1`
- `src/LightLaunchpad.App/ViewModels/LaunchpadViewModel.cs`
- `tests/LightLaunchpad.App.Tests/LaunchpadViewModelIconStabilityTests.cs`
- `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp`
- `tools/build-native-ui.ps1`
- `docs/superpowers/plans/2026-05-22-native-ui-m2-icons-drag.md`

## Next Step

Continue with native region editing, multi-select drag parity, and Direct2D/DirectWrite renderer migration.

## Risks

- WPF cannot honestly provide both very low resident memory and instant repeated opening; the true route is a native UI rewrite.
- The agent code remains in the tree for comparison but is no longer the default package/startup route.
