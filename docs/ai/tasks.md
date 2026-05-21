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

## A6 - Native Win32 Agent

Status: done

Dependency: A2

Purpose: Add a true low-memory background process inspired by LightFrame's native Win32 architecture.

Likely files:

- `src/LightLaunchpad.NativeAgent/LightLaunchpad.NativeAgent.cpp`
- `tools/build-native-agent.ps1`
- `tests/LightLaunchpad.App.Tests/HostedAppModeSourceTests.cs`

Verification:

- `.\tools\build-native-agent.ps1 -OutputDirectory release\native-agent-probe`
- Start the native agent briefly and measure Working Set and Private Memory.

Completion notes:

- Added native Win32 hotkey/tray/message-loop host.
- Native probe measured approximately 8.9 MB Working Set and 1.4 MB Private Memory while idle without opening the WPF UI.

## A7 - Native Agent Hardening

Status: done

Dependency: A6

Purpose: Make the native low-memory host suitable for release packaging instead of a throwaway probe.

Likely files:

- `src/LightLaunchpad.NativeAgent/LightLaunchpad.NativeAgent.cpp`
- `tools/build-native-agent.ps1`
- `tests/LightLaunchpad.App.Tests/HostedAppModeSourceTests.cs`

Verification:

- App source tests cover native icon loading and job object cleanup.
- Build native agent with `tools/build-native-agent.ps1`.
- Start native agent briefly and measure Working Set and Private Memory.

Completion notes:

- NativeAgent now loads `Assets\Alice.ico` for the tray icon.
- NativeAgent wraps hosted UI process in a Job Object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`.
- Hardened native probe measured approximately 9.9 MB Working Set and 1.6 MB Private Memory while idle.

## A8 - Formal Native Release Package

Status: done

Dependency: A7

Purpose: Make the local release folder reproducible for every build.

Likely files:

- `tools/package-release.ps1`
- `README.md`
- release output only

Verification:

- `.\tools\package-release.ps1`
- zip contains `LightLaunchpad.App.exe`, `LightLaunchpad.Agent.exe`, `LightLaunchpad.NativeAgent.exe`, and `Assets\Alice.ico`
- Start packaged `LightLaunchpad.NativeAgent.exe` briefly and measure Working Set and Private Memory.

Completion notes:

- Added a single packaging command that publishes the WPF UI, includes the managed fallback agent, builds the native Win32 agent, and writes both folder and zip outputs under ignored local `release/`.
- Hardened the native build step for this Chinese-path workspace by compiling in an ASCII temp directory and copying the result back into `release/`.
- Startup registration now prefers `LightLaunchpad.NativeAgent.exe`, then `LightLaunchpad.Agent.exe`, then the current process path.
- Formal packaged NativeAgent probe measured approximately 9.89 MB Working Set and 1.57 MB Private Memory while idle.

## A9 - Revert Agent Default and Fix Search State

Status: done

Dependency: A8

Purpose: Restore responsive repeated opening and fix icon disappearance after no-result search.

Likely files:

- `src/LightLaunchpad.App/ViewModels/LaunchpadViewModel.cs`
- `src/LightLaunchpad.App/App.xaml.cs`
- `tools/package-release.ps1`
- `README.md`
- `docs/ai/requirements-and-route.md`

Verification:

- App tests cover no-result search followed by clearing the search text.
- App tests cover single-process startup/package defaults.
- Package script creates `LightLaunchpad-win-x64-<version>.zip`.

Completion notes:

- Search filtering now captures icon state before replacing `FilteredItems`, so clearing a no-result query restores already-loaded icons and queues missing icons again.
- Startup registration now resolves to the app process instead of preferring agent executables.
- Default package now contains the single-process WPF app only; agent/native-agent are retained as experiments, not as the recommended path.

## N1 - Native UI M1

Status: done

Purpose: Start the full LightFrame-style rewrite with a real native launcher surface instead of an agent that cold-starts WPF.

Likely files:

- `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp`
- `tools/build-native-ui.ps1`
- `tools/package-release.ps1`
- `tests/LightLaunchpad.App.Tests/HostedAppModeSourceTests.cs`
- `docs/superpowers/plans/2026-05-22-native-ui-m1.md`

Verification:

- App source tests cover Win32-native window/tray/hotkey/search/launch contracts.
- `.\tools\build-native-ui.ps1 -OutputDirectory release\native-ui-probe`
- `.\tools\package-release.ps1`

Completion notes:

- NativeUi M1 reads existing `%AppData%\LightLaunchpad\settings.json` and `layout.json`.
- NativeUi M1 owns hotkey, tray, search input, double-buffered drawing, icon loading, mouse selection/double-click launch, Enter launch, Esc hide, and wheel scrolling.
- NativeUi release builds statically link the larger MinGW C/C++ runtime pieces, bundle transitive `libwinpthread-1.dll`, and the package script now fails hard if publish/build steps fail.
- WPF remains fallback for settings and full editing until later native milestones.
