# LightLaunchpad Agent Tasks

## A1 - Activation Contract

Status: done

Purpose: Add shared hosted UI argument parsing so the agent and WPF UI agree on the same command-line contract.

Likely files:

- `src/LightLaunchpad.Core/Activation/*`
- `tests/LightLaunchpad.Core.Tests/*`

Verification:

- `.\.dotnet\dotnet.exe run --project tests\LightLaunchpad.Core.Tests\LightLaunchpad.Core.Tests.csproj`

Completion notes:

- Added `LaunchpadActivationContext` with hosted UI parsing and process-launch argument generation.

## A2 - Hosted UI Mode

Status: done

Dependency: A1

Purpose: Let `LightLaunchpad.App` run without creating its own tray/hotkey services when launched by the agent.

Likely files:

- `src/LightLaunchpad.App/App.xaml.cs`
- `src/LightLaunchpad.App/Services/HostedActivationService.cs`
- `tests/LightLaunchpad.App.Tests/*`

Verification:

- `.\.dotnet\dotnet.exe run --project tests\LightLaunchpad.App.Tests\LightLaunchpad.App.Tests.csproj`

Completion notes:

- Added hosted UI mode in `App.xaml.cs` and `HostedActivationService` for named show/exit events.
- Hosted UI skips standalone tray/hotkey creation and exits after hide when launched with `--exit-on-hide`.

## A3 - Agent Project

Status: done

Dependency: A1

Purpose: Add `LightLaunchpad.Agent` as the always-on process that owns the global hotkey, tray entry, and WPF UI launch.

Likely files:

- `src/LightLaunchpad.Agent/*`
- `LightLaunchpad.sln`

Verification:

- `.\.dotnet\dotnet.exe build LightLaunchpad.sln`

Completion notes:

- Added `LightLaunchpad.Agent`, a non-WPF/non-WinForms host owning native hotkey registration, native tray icon, and WPF UI process launch.

## A4 - Packaging

Status: done

Dependency: A2, A3

Purpose: Package the app and agent together under `release/`.

Likely files:

- release output only
- README or docs if command changes are needed

Verification:

- publish command succeeds
- zip contains `LightLaunchpad.Agent.exe` and `LightLaunchpad.App.exe`

Completion notes:

- Release packaging now publishes `LightLaunchpad.App` and `LightLaunchpad.Agent` into the same ignored local release folder.

## A5 - Performance Probe

Status: done

Dependency: A3, A4

Purpose: Measure agent background memory and record the observed value without overclaiming.

Verification:

- Start packaged agent briefly, read `WorkingSet64` and `PrivateMemorySize64`, stop only the process started by the probe.

Completion notes:

- Managed agent probe after release publish: approximately 28 MB Working Set and 6.8 MB Private Memory while idle without opening the WPF UI.