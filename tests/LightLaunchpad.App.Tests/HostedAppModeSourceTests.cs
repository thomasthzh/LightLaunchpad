using System.IO;

namespace LightLaunchpad.App.Tests;

public sealed class HostedAppModeSourceTests
{
    public static void AppSource_SupportsHostedUiModeWithoutStandaloneTrayOrHotkey()
    {
        var code = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.App", "App.xaml.cs"));

        TestAssert.Contains("LaunchpadActivationContext.Parse", code);
        TestAssert.Contains("BuildHostedServices", code);
        TestAssert.Contains("StartHostedActivation", code);
        TestAssert.Contains("ExitOnHide", code);
        TestAssert.Contains("BuildStandaloneServices", code);
    }

    public static void AgentProject_UsesLightweightNativeShellContracts()
    {
        var project = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.Agent", "LightLaunchpad.Agent.csproj"));
        var program = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.Agent", "Program.cs"));

        TestAssert.DoesNotContain("<UseWPF>true</UseWPF>", project);
        TestAssert.DoesNotContain("<UseWindowsForms>true</UseWindowsForms>", project);
        TestAssert.Contains("RegisterHotKey", program);
        TestAssert.Contains("Shell_NotifyIcon", program);
        TestAssert.Contains("LaunchpadActivationContext.CreateHostedUiArguments", program);
    }

    public static void NativeAgentSource_UsesWin32OnlyBackgroundContracts()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeAgent", "LightLaunchpad.NativeAgent.cpp"));

        TestAssert.Contains("wWinMain", source);
        TestAssert.Contains("RegisterHotKey", source);
        TestAssert.Contains("Shell_NotifyIconW", source);
        TestAssert.Contains("CreateEventW", source);
        TestAssert.Contains("CreateProcessW", source);
        TestAssert.Contains("GetMessageW", source);
        TestAssert.DoesNotContain("System.Text.Json", source);
    }

    public static void NativeAgentSource_LoadsTrayIconAndCleansHostedUiWithJobObject()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeAgent", "LightLaunchpad.NativeAgent.cpp"));
        var script = File.ReadAllText(FindRepoFile("tools", "build-native-agent.ps1"));

        TestAssert.Contains("Assets\\\\Alice.ico", source);
        TestAssert.Contains("LoadImageW", source);
        TestAssert.Contains("CreateJobObjectW", source);
        TestAssert.Contains("JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE", source);
        TestAssert.Contains("AssignProcessToJobObject", source);
        TestAssert.Contains("param(", script);
        TestAssert.Contains("LightLaunchpad.App\\Assets\\Alice.ico", script);
        TestAssert.Contains("GetTempPath", script);
        TestAssert.Contains("lightlaunchpad-native-", script);
    }

    public static void PackageReleaseScript_ExcludesAgentColdStartPackage()
    {
        var script = File.ReadAllText(FindRepoFile("tools", "package-release.ps1"));

        TestAssert.Contains("LightLaunchpad-nativeui-$Runtime-$Version", script);
        TestAssert.Contains("src\\LightLaunchpad.App\\LightLaunchpad.App.csproj", script);
        TestAssert.Contains("Compress-Archive", script);
        TestAssert.Contains("Package output must stay under the release directory.", script);
        TestAssert.Contains("-join [Environment]::NewLine", script);
        TestAssert.Contains("dotnet publish failed", script);
        TestAssert.Contains("Native UI build failed", script);
        TestAssert.DoesNotContain("LightLaunchpad-native-agent-$Runtime-$Version", script);
        TestAssert.DoesNotContain("build-native-agent.ps1", script);
        TestAssert.DoesNotContain("LightLaunchpad.Agent.csproj", script);
    }

    public static void StartupRegistration_StaysOnSingleProcessAppForResponsiveRoute()
    {
        var code = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.App", "App.xaml.cs"));

        TestAssert.Contains("ResolveStartupExecutablePath", code);
        TestAssert.Contains("AppContext.BaseDirectory", code);
        TestAssert.Contains("Environment.ProcessPath", code);
        TestAssert.DoesNotContain("Assembly.GetEntryAssembly()?.Location", code);
        TestAssert.DoesNotContain("LightLaunchpad.NativeAgent.exe", code);
        TestAssert.DoesNotContain("LightLaunchpad.Agent.exe", code);
    }

    public static void LaunchpadWindow_DoesNotClearSearchTextWhenShown()
    {
        var code = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.App", "LaunchpadWindow.xaml.cs"));

        TestAssert.Contains("FocusSearchBox", code);
        TestAssert.Contains("SearchBox.SelectAll", code);
        TestAssert.DoesNotContain("SearchBox.Clear", code);
        TestAssert.DoesNotContain("SearchText = string.Empty", code);
    }

    public static void NativeUiSource_UsesWin32WindowNotWpfColdStart()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("wWinMain", source);
        TestAssert.Contains("RegisterHotKey", source);
        TestAssert.Contains("Shell_NotifyIconW", source);
        TestAssert.Contains("WM_PAINT", source);
        TestAssert.Contains("CreateCompatibleDC", source);
        TestAssert.Contains("ShellExecuteW", source);
        TestAssert.DoesNotContain("LightLaunchpad.App.exe", source);
        TestAssert.DoesNotContain("CreateProcessW", source);
    }

    public static void NativeUiSource_ReadsExistingSettingsLayoutAndSearches()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("settings.json", source);
        TestAssert.Contains("layout.json", source);
        TestAssert.Contains("LaunchpadFolder", source);
        TestAssert.Contains("DisplayName", source);
        TestAssert.Contains("SourcePath", source);
        TestAssert.Contains("RegionId", source);
        TestAssert.Contains("Search", source);
    }

    public static void NativeUiSource_LoadsHighQualityShellIcons()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("SHGetImageList", source);
        TestAssert.Contains("SHIL_JUMBO", source);
        TestAssert.Contains("SHIL_EXTRALARGE", source);
        TestAssert.Contains("IID_IImageList", source);
        TestAssert.Contains("ILD_TRANSPARENT", source);
        TestAssert.Contains("SHGFI_SYSICONINDEX", source);
        TestAssert.DoesNotContain("SHGFI_USEFILEATTRIBUTES", source);
        TestAssert.DoesNotContain("Assets\\\\Alice.ico", source);
    }

    public static void NativeUiSource_SupportsDragSortingAndLayoutPersistence()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("WM_MOUSEMOVE", source);
        TestAssert.Contains("WM_LBUTTONUP", source);
        TestAssert.Contains("SetCapture", source);
        TestAssert.Contains("ReleaseCapture", source);
        TestAssert.Contains("g_regionHits", source);
        TestAssert.Contains("DropTarget", source);
        TestAssert.Contains("BeginDrag", source);
        TestAssert.Contains("CompleteDrag", source);
        TestAssert.Contains("SaveLayout", source);
        TestAssert.Contains("WriteFileUtf8", source);
    }

    public static void NativeUiSource_SupportsMultiSelectDragAndRegionReorder()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("g_selectedSourcePaths", source);
        TestAssert.Contains("ToggleSelection", source);
        TestAssert.Contains("SetSingleSelection", source);
        TestAssert.Contains("ResolveDragSourcePaths", source);
        TestAssert.Contains("MoveItemsToTarget", source);
        TestAssert.Contains("DragMode::Items", source);
        TestAssert.Contains("DragMode::Region", source);
        TestAssert.Contains("BeginRegionDrag", source);
        TestAssert.Contains("CompleteRegionDrag", source);
        TestAssert.Contains("MoveRegionToTarget", source);
    }

    public static void NativeUiSource_OffersBasicContextMenus()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("ShowItemContextMenu", source);
        TestAssert.Contains("ShowRegionContextMenu", source);
        TestAssert.Contains("ItemLaunchCommand", source);
        TestAssert.Contains("ItemOpenLocationCommand", source);
        TestAssert.Contains("ItemRemoveCommand", source);
        TestAssert.Contains("RegionDeleteCommand", source);
        TestAssert.Contains("RemoveItemsFromLayout", source);
        TestAssert.Contains("DeleteRegionAndMoveItems", source);
        TestAssert.Contains("ShellExecuteW(g_hwnd, L\"open\", L\"explorer.exe\"", source);
    }

    public static void NativeUiSource_ProvidesNativeSettingsWindowAndSavesSettings()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("TraySettingsCommand", source);
        TestAssert.Contains("ShowSettingsWindow", source);
        TestAssert.Contains("SettingsWndProc", source);
        TestAssert.Contains("SaveSettings", source);
        TestAssert.Contains("SettingsControlId::DisplayMode", source);
        TestAssert.Contains("SettingsControlId::Language", source);
        TestAssert.Contains("SettingsControlId::SpotlightWidth", source);
        TestAssert.Contains("SettingsControlId::SpotlightHeight", source);
        TestAssert.Contains("SettingsControlId::AppSpacing", source);
        TestAssert.Contains("SettingsControlId::WheelSensitivity", source);
        TestAssert.Contains("SettingsControlId::StartWithWindows", source);
        TestAssert.Contains("RegisterCurrentHotkey", source);
        TestAssert.Contains("LightLaunchpad Settings", source);
        TestAssert.Contains("LightLaunchpad 设置", source);
    }

    public static void NativeUiSource_SupportsNativeRegionAndItemEditing()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("WorkspaceCreateRegionCommand", source);
        TestAssert.Contains("ItemRenameCommand", source);
        TestAssert.Contains("RegionRenameCommand", source);
        TestAssert.Contains("RegionRenameSelectedCommand", source);
        TestAssert.Contains("RegionDeleteSelectedCommand", source);
        TestAssert.Contains("g_selectedRegionIds", source);
        TestAssert.Contains("ShowWorkspaceContextMenu", source);
        TestAssert.Contains("CreateRegion", source);
        TestAssert.Contains("RenameItem", source);
        TestAssert.Contains("RenameRegion", source);
        TestAssert.Contains("RenameSelectedRegions", source);
        TestAssert.Contains("DeleteSelectedRegions", source);
        TestAssert.Contains("ShowTextInputDialog", source);
        TestAssert.Contains("ToggleRegionSelection", source);
    }

    public static void NativeUiSource_AppliesSpotlightAndLayoutTuning()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("IsSpotlightMode", source);
        TestAssert.Contains("WM_ACTIVATE", source);
        TestAssert.Contains("HideNativeUi", source);
        TestAssert.Contains("AppSpacing", source);
        TestAssert.Contains("g_settings.appSpacing", source);
        TestAssert.Contains("NormalizeAppSpacing", source);
        TestAssert.Contains("NormalizeWheelSensitivity", source);
        TestAssert.Contains("ApplyLanguageToSettingsWindow", source);
        TestAssert.DoesNotContain("MouseSensitivity", source);
    }

    public static void NativeUiSource_SupportsNativeImportWithoutVuiImport()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("WorkspaceImportFilesCommand", source);
        TestAssert.Contains("WorkspaceImportStartMenuCommand", source);
        TestAssert.Contains("ImportLaunchableFiles", source);
        TestAssert.Contains("ImportStartMenuApps", source);
        TestAssert.Contains("GetOpenFileNameW", source);
        TestAssert.Contains("CopyLaunchableIntoLaunchpad", source);
        TestAssert.Contains("PROGRAMDATA", source);
        TestAssert.Contains("Microsoft\\\\Windows\\\\Start Menu\\\\Programs", source);
        TestAssert.DoesNotContain(".vui", source);
    }

    public static void NativeUiSource_HasDirect2DDirectWriteRendererWithGdiFallback()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));
        var script = File.ReadAllText(FindRepoFile("tools", "build-native-ui.ps1"));

        TestAssert.Contains("ID2D1DCRenderTarget", source);
        TestAssert.Contains("IDWriteFactory", source);
        TestAssert.Contains("PaintContentDirect2D", source);
        TestAssert.Contains("InitializeDirectRenderer", source);
        TestAssert.Contains("DestroyDirectRenderer", source);
        TestAssert.Contains("PaintContent(memoryDc, client)", source);
        TestAssert.Contains("-ld2d1", script);
        TestAssert.Contains("-ldwrite", script);
    }

    public static void NativeUiSource_RoundsSpotlightAndClipsScrollableContentBelowSearch()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("ApplySpotlightWindowRegion", source);
        TestAssert.Contains("CreateRoundRectRgn", source);
        TestAssert.Contains("SetWindowRgn", source);
        TestAssert.Contains("ContentClipTop", source);
        TestAssert.Contains("PushAxisAlignedClip", source);
        TestAssert.Contains("SelectClipRgn", source);
        TestAssert.Contains("DrawSearchSurface", source);
    }

    public static void NativeUiSource_UsesExactSizedIconsAndGridKeyboardNavigation()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("ResolveShortcutIconPath", source);
        TestAssert.Contains("LoadExactSizedIcon", source);
        TestAssert.Contains("PrivateExtractIconsW", source);
        TestAssert.Contains("MoveSelectionHorizontal", source);
        TestAssert.Contains("MoveSelectionVertical", source);
        TestAssert.Contains("ScrollSelectedIntoView", source);
        TestAssert.Contains("VK_LEFT", source);
        TestAssert.Contains("VK_RIGHT", source);
    }

    public static void NativeUiSource_MapsSpotlightTuningAndImprovesSettingsRendering()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));
        var script = File.ReadAllText(FindRepoFile("tools", "build-native-ui.ps1"));

        TestAssert.Contains("TileGap", source);
        TestAssert.Contains("WheelScrollStep", source);
        TestAssert.Contains("ApplyControlFont", source);
        TestAssert.Contains("SetWindowTheme", source);
        TestAssert.Contains("g_uiFont", source);
        TestAssert.Contains("-lole32", script);
        TestAssert.Contains("-luxtheme", script);
    }

    public static void NativeUiSource_RemovesSearchPlaceholderAndSupportsApplySettings()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("SearchDisplayText", source);
        TestAssert.Contains("SettingsControlId::Apply", source);
        TestAssert.Contains("ApplySettingsFromWindow", source);
        TestAssert.Contains("SetDlgItemTextW(hwnd, ControlId(SettingsControlId::Apply)", source);
        TestAssert.Contains("L\"应用\" : L\"Apply\"", source);
        TestAssert.DoesNotContain("? L\"Search\" : g_searchText", source);
    }

    public static void NativeUiSource_ExposesAppSizeSetting()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("SettingsControlId::IconSize", source);
        TestAssert.Contains("SettingsControlId::IconSizeLabel", source);
        TestAssert.Contains("ResetIconSizeCombo", source);
        TestAssert.Contains("ReadIconSizeFromSettingsWindow", source);
        TestAssert.Contains("SetIconSizeSelection", source);
        TestAssert.Contains("APP 大小", source);
        TestAssert.Contains("IconSizeName", source);
    }

    public static void NativeUiBuildScript_ProducesNativeUiExecutable()
    {
        var script = File.ReadAllText(FindRepoFile("tools", "build-native-ui.ps1"));

        TestAssert.Contains("LightLaunchpad.NativeUi.cpp", script);
        TestAssert.Contains("LightLaunchpad.NativeUi.exe", script);
        TestAssert.Contains("GetTempPath", script);
        TestAssert.Contains("-mwindows", script);
        TestAssert.Contains("-static `", script);
        TestAssert.Contains("-static-libgcc", script);
        TestAssert.Contains("-static-libstdc++", script);
        TestAssert.Contains("-Os", script);
        TestAssert.Contains("-ffunction-sections", script);
        TestAssert.Contains("-fdata-sections", script);
        TestAssert.Contains("-Wl,--gc-sections", script);
        TestAssert.DoesNotContain("libwinpthread-1.dll", script);
    }

    public static void PackageReleaseScript_IncludesNativeUiPrimaryExecutable()
    {
        var script = File.ReadAllText(FindRepoFile("tools", "package-release.ps1"));

        TestAssert.Contains("LightLaunchpad-nativeui-$Runtime-$Version", script);
        TestAssert.Contains("build-native-ui.ps1", script);
        TestAssert.Contains("LightLaunchpad.NativeUi.exe", script);
        TestAssert.Contains("LightLaunchpad.App.exe", script);
    }

    private static string FindRepoFile(params string[] parts)
    {
        var current = new DirectoryInfo(Environment.CurrentDirectory);
        while (current is not null)
        {
            var candidateParts = new[] { current.FullName }.Concat(parts).ToArray();
            var candidate = Path.Combine(candidateParts);
            if (File.Exists(candidate))
            {
                return candidate;
            }

            current = current.Parent;
        }

        throw new FileNotFoundException($"Could not find {Path.Combine(parts)} from the test working directory.");
    }
}
