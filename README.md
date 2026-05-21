# LightLaunchpad

LightLaunchpad is a lightweight Windows launchpad for shortcuts, executables, and URLs. It is built with WPF on .NET 8 and focuses on fast startup, stable icon rendering, and simple drag-and-drop organization.

## Features

- Fullscreen launchpad overlay with hotkey support.
- Import `.lnk`, `.url`, and `.exe` entries.
- Import Windows Start Menu apps.
- Organize apps into regions with inline or tabbed views.
- Search and launch apps quickly.
- Single-click selection, rubber-band multi-selection, and double-click launch.
- Drag one or more selected icons with a blue insertion indicator.
- Icon cache stored under `Documents\LightLaunchpad\icons`.
- User settings and layout stored under `%AppData%\LightLaunchpad`.
- Single-process hotkey and tray mode for responsive repeated opening.
- Experimental native UI executable for the low-memory, high-fluidity rewrite path.

## Download

Use the latest GitHub Release and download the Windows zip package. Start `LightLaunchpad.NativeUi.exe` for the native launcher, or `LightLaunchpad.App.exe` for the current WPF fallback/settings surface.

The fallback app is a self-contained single-file Windows executable. The native launcher bundles the small MinGW C/C++ runtime DLLs it imports so it can start from the package without requiring the developer toolchain on `PATH`.

## Build

Requirements:

- Windows
- .NET SDK 8

```powershell
dotnet restore LightLaunchpad.sln
dotnet build LightLaunchpad.sln -c Release
dotnet run --project tests\LightLaunchpad.Core.Tests\LightLaunchpad.Core.Tests.csproj
dotnet run --project tests\LightLaunchpad.App.Tests\LightLaunchpad.App.Tests.csproj
```

Create the WPF UI executable:

```powershell
dotnet publish src\LightLaunchpad.App\LightLaunchpad.App.csproj `
  -c Release `
  -r win-x64 `
  --self-contained true `
  -p:PublishSingleFile=true `
  -p:EnableCompressionInSingleFile=true `
  -p:IncludeNativeLibrariesForSelfExtract=true `
  -o artifacts\publish-win-x64
```

Create the local release package:

```powershell
.\tools\package-release.ps1
```

The package is written under `release\LightLaunchpad-nativeui-win-x64-<version>` with a matching `.zip`. Start `LightLaunchpad.NativeUi.exe` for the native M1 launcher, or `LightLaunchpad.App.exe` for the current WPF fallback/settings surface.

Build only the native UI:

```powershell
.\tools\build-native-ui.ps1 -OutputDirectory release\native-ui-probe
```

Native UI M1 is a read-only launcher surface: hotkey, tray, search, native drawing, and app launch are implemented; WPF remains the fallback for settings and full region editing while the native rewrite continues.

## License

MIT
