# LightLaunchpad Agent Progress

## Current State

- Branch: `feature/launchpad-v2-polish`
- Workspace: `C:\Users\thoma\Desktop\启动台`
- Active task: complete through A5 - Performance Probe

## Completed

- Route chosen: split-process agent plus hosted WPF UI.
- A1 - Activation Contract.
- A2 - Hosted UI Mode.
- A3 - Agent Project.
- A4 - Packaging.
- A5 - Performance Probe.

## Commands Run

- `.\.dotnet\dotnet.exe run --project tests\LightLaunchpad.Core.Tests\LightLaunchpad.Core.Tests.csproj` - 32/32 passed.
- `.\.dotnet\dotnet.exe run --project tests\LightLaunchpad.App.Tests\LightLaunchpad.App.Tests.csproj` - 28/28 passed.
- `.\.dotnet\dotnet.exe build LightLaunchpad.sln` - passed with 0 warnings and 0 errors.
- Published `LightLaunchpad.App` and `LightLaunchpad.Agent` into one local release folder.
- Started the packaged agent without opening UI and measured approximately 28 MB Working Set / 6.8 MB Private Memory.

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

## Next Step

Update the GitHub PR branch and continue with the next route step: NativeAOT/C++ agent hardening, job-object cleanup, and optional Everything query prototype.

## Risks

- A strict sub-10MB always-on memory target may require NativeAOT or C++ agent work after the first managed skeleton.
- Hosted UI must avoid creating duplicate tray/hotkey services.