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

## Download

Use the latest GitHub Release and download `LightLaunchpad.exe`.

The release build is a self-contained single-file Windows executable. It does not require a separate .NET installation.

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

The package is written under `release\LightLaunchpad-win-x64-<version>` with a matching `.zip`. Start `LightLaunchpad.App.exe` from that folder.

## License

MIT
