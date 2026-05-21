# LightLaunchpad Agent Progress

## Current State

- Branch: `feature/launchpad-v2-polish`
- Workspace: `C:\Users\thoma\Desktop\启动台`
- Active task: complete through A8 - Formal Native Release Package

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

## Next Step

Update the GitHub PR branch, then continue with optional Everything query prototype.

## Risks

- NativeAgent currently uses MinGW for local builds. A future CI/release path should standardize the C++ toolchain.
- Hosted UI must avoid creating duplicate tray/hotkey services.
