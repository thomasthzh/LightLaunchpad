# LightLaunchpad Agent Route

## Requirements

1. Must reduce background residency by moving hotkey and tray ownership out of the WPF launchpad process.
2. Must keep the current launchpad and spotlight UI behavior available through an on-demand UI process.
3. Must follow the PowerToys Quick Access pattern where a small host controls a separate UI executable through process launch and lightweight activation signals.
4. Must keep the current standalone app path working during the transition unless a release explicitly switches the startup target.
5. Must preserve current layout, settings, icon cache, region editing, and launch behavior.
6. Must make later Everything integration possible without putting indexing or search work into the always-on process.

## Chosen Route

Use a split-process architecture:

- `LightLaunchpad.Agent`: always-on host for hotkey, tray, settings load, and UI process launch.
- `LightLaunchpad.NativeAgent`: native Win32 always-on host for the strict sub-10MB background route.
- `LightLaunchpad.App`: WPF UI process. In normal mode it remains compatible with the current app. In hosted mode it is shown by the agent and exits when hidden.
- `LightLaunchpad.Core`: shared contracts for activation arguments, settings, hotkeys, layout, and pure logic.

The agent launches the WPF UI with hosted arguments:

```text
LightLaunchpad.App.exe --hosted-ui --show-event=<event> --exit-event=<event> --show-immediately --exit-on-hide
```

The UI listens to named events for future keep-warm activation. In the first phase, it also exits after hide so the WPF memory footprint does not remain in the background.

## Reference Notes

- adopt: PowerToys Quick Access host starts the UI process from Runner and signals show/exit with named events.
- adopt: PowerToys uses a job object to clean up the UI if the host dies. LightLaunchpad should add this after the initial C# agent skeleton.
- adapt: PowerToys named pipe IPC is useful later for settings sync and query messages, but the first phase only needs activation arguments and named events.
- adapt: UI memory trim after hide can help perceived memory, but the durable memory reduction comes from exiting the UI process.
- reject: PowerToys module management, GPO, telemetry, and settings platform are not part of this app route.
- adopt: LightFrame's native C/C++ Win32 style for the true-low-memory host. Its published repository identifies the architecture as C/C++ native Windows API and DirectUI; the full source is private, so only the architectural pattern is borrowed.
- adapt: Local LightFrame process evidence shows native does not automatically mean low memory when the full UI/runtime is resident. LightLaunchpad keeps the native host minimal and pushes WPF/UI/icon work into the on-demand process.

## Architecture Sketch

```mermaid
flowchart LR
    Agent["LightLaunchpad.NativeAgent\nhotkey + tray + launch"] -->|"CreateProcess + args"| UI["LightLaunchpad.App\nhosted WPF UI"]
    Agent -->|"SetEvent show"| UI
    Agent -->|"SetEvent exit"| UI
    UI --> Core["LightLaunchpad.Core\nsettings/layout/hotkeys/contracts"]
    Agent --> Core
```

## Success Criteria

- Solution builds with a new `LightLaunchpad.Agent` project.
- Core tests cover activation argument parsing.
- App tests cover hosted UI source wiring.
- Agent can launch the WPF UI with hosted arguments.
- WPF UI can exit on hide in hosted mode.
- Release packaging includes both executables.
- NativeAgent package probe stays under 10 MB idle Working Set on the local machine.
- Startup registration prefers the native agent when it is present in the package.

## Open Questions

- Whether Everything integration should use `es.exe` first for validation or go straight to the SDK IPC.
- Whether a future release should remove the managed agent fallback after the native route has enough user testing.
