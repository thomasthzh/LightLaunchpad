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

        TestAssert.Contains("Assets\\\\LightLaunchpad.ico", source);
        TestAssert.Contains("LoadImageW", source);
        TestAssert.Contains("CreateJobObjectW", source);
        TestAssert.Contains("JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE", source);
        TestAssert.Contains("AssignProcessToJobObject", source);
        TestAssert.Contains("param(", script);
        TestAssert.Contains("LightLaunchpad.App\\Assets\\LightLaunchpad.ico", script);
        TestAssert.Contains("GetTempPath", script);
        TestAssert.Contains("lightlaunchpad-native-", script);
    }

    public static void PackageReleaseScript_ExcludesAgentColdStartPackage()
    {
        var script = File.ReadAllText(FindRepoFile("tools", "package-release.ps1"));

        TestAssert.Contains("LightLaunchpad-nativeui-$Runtime-$Version", script);
        TestAssert.Contains("Compress-Archive", script);
        TestAssert.Contains("Package output must stay under the release directory.", script);
        TestAssert.Contains("-join [Environment]::NewLine", script);
        TestAssert.Contains("Native UI build failed", script);
        TestAssert.DoesNotContain("LightLaunchpad.App.csproj", script);
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
        TestAssert.Contains("g_shellBroker.RequestLaunch", source);
        TestAssert.Contains("g_shellBroker.RequestOpenFolder", source);
        TestAssert.DoesNotContain("LightLaunchpad.App.exe", source);
        TestAssert.DoesNotContain("CreateProcessW", source);
    }

    public static void NativeUiSource_ReadsExistingSettingsLayoutAndSearches()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));
        var catalog = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "Catalog.cpp"));

        TestAssert.Contains("settings.json", source);
        TestAssert.Contains("layout.json", catalog);
        TestAssert.Contains("LaunchpadFolder", source);
        TestAssert.Contains("DisplayName", catalog);
        TestAssert.Contains("SourcePath", catalog);
        TestAssert.Contains("RegionId", catalog);
        TestAssert.Contains("Search", source);
    }

    public static void NativeUiSource_LoadsHighQualityShellIcons()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));
        var broker = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "ShellBroker.cpp"));

        TestAssert.Contains("QueueItemIcon", source);
        TestAssert.Contains("SHGetImageList", broker);
        TestAssert.Contains("SHIL_JUMBO", broker);
        TestAssert.Contains("SHIL_EXTRALARGE", broker);
        TestAssert.Contains("IID_IImageList", broker);
        TestAssert.Contains("ILD_TRANSPARENT", broker);
        TestAssert.Contains("SHGFI_SYSICONINDEX", broker);
        TestAssert.DoesNotContain("SHGFI_USEFILEATTRIBUTES", broker);
        TestAssert.DoesNotContain("Assets\\\\Alice.ico", source);
        TestAssert.DoesNotContain("Assets\\\\LightLaunchpad.ico", source);
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
        var broker = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "ShellBroker.cpp"));

        TestAssert.Contains("ShowItemContextMenu", source);
        TestAssert.Contains("ShowRegionContextMenu", source);
        TestAssert.Contains("ItemLaunchCommand", source);
        TestAssert.Contains("ItemOpenLocationCommand", source);
        TestAssert.Contains("ItemRemoveCommand", source);
        TestAssert.Contains("RegionDeleteCommand", source);
        TestAssert.Contains("RemoveItemsFromLayout", source);
        TestAssert.Contains("DeleteRegionAndMoveItems", source);
        TestAssert.Contains("RequestOpenLocation", source);
        TestAssert.Contains("CLSID_FileOperation", broker);
        TestAssert.Contains("FOF_ALLOWUNDO", broker);
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
        var broker = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "ShellBroker.cpp"));

        TestAssert.Contains("WorkspaceImportFilesCommand", source);
        TestAssert.Contains("WorkspaceImportStartMenuCommand", source);
        TestAssert.Contains("ImportLaunchableFiles", source);
        TestAssert.Contains("ImportStartMenuApps", source);
        TestAssert.Contains("GetOpenFileNameW", source);
        TestAssert.Contains("RequestImportPaths", source);
        TestAssert.Contains("RequestImportTree", source);
        TestAssert.Contains("CreateShortcutToExecutable", broker);
        TestAssert.Contains("FILE_ATTRIBUTE_REPARSE_POINT", broker);
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
        TestAssert.Contains("SpotlightTransparentKey", source);
        TestAssert.Contains("UpdateLayeredWindow", source);
        TestAssert.Contains("ULW_ALPHA", source);
        TestAssert.Contains("ApplySpotlightPerPixelAlpha", source);
        TestAssert.Contains("ContentFeatherCoverage", source);
        TestAssert.Contains("BlendPixel", source);
        TestAssert.Contains("const DWORD cornerPreference = IsSpotlightMode() ? DWMWCP_DONOTROUND : DWMWCP_ROUND;", source);
        TestAssert.Contains("SetWindowRgn(g_hwnd, nullptr, TRUE)", source);
        TestAssert.DoesNotContain("SetWindowRgn(g_hwnd, region, TRUE)", source);
        TestAssert.Contains("ContentClipTop", source);
        TestAssert.Contains("PushAxisAlignedClip", source);
        TestAssert.Contains("SelectClipRgn", source);
        TestAssert.Contains("DrawSearchSurface", source);
    }

    public static void NativeUiSource_UsesExactSizedIconsAndGridKeyboardNavigation()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));
        var broker = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "ShellBroker.cpp"));

        TestAssert.Contains("GetIconLocation", broker);
        TestAssert.Contains("LoadItemIcon", broker);
        TestAssert.Contains("PrivateExtractIconsW", broker);
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
        TestAssert.Contains("ReadSettingsWindowCandidate", source);
        TestAssert.Contains("candidate.iconSize", source);
        TestAssert.Contains("SetIconSizeSelection", source);
        TestAssert.Contains("APP 大小", source);
        TestAssert.Contains("IconSizeName", source);
    }

    public static void NativeUiSource_DrawsAppTileFrameOnlyWhenSelected()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("DrawAppTileSurface", source);
        TestAssert.Contains("if (!selected) return;", source);
        TestAssert.Contains("DrawAppTileSurface(target, visualTileRect, selected)", source);
        TestAssert.Contains("DrawAppTileSurface(dc, visualTileRect, selected)", source);
        TestAssert.DoesNotContain("DrawRoundedRectDirect(target, tileRect, selected ?", source);
        TestAssert.DoesNotContain("DrawRoundedRect(dc, tileRect, selected ?", source);
    }

    public static void NativeUiSource_UsesStableInterTileDropTargets()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("FindDropTargetWithinRegion", source);
        TestAssert.Contains("InsertionOrderFromRegionHits", source);
        TestAssert.Contains("if (PtInRect(&regionHit.bounds, point))", source);
        TestAssert.Contains("return FindDropTargetWithinRegion(regionHit.regionId, point)", source);
        TestAssert.DoesNotContain("return { regionHit.regionId, CountItemsInRegion(regionHit.regionId, g_dragSourcePaths), true };", source);
    }

    public static void NativeUiSource_SupportsRubberBandSelection()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("g_selectionBoxActive", source);
        TestAssert.Contains("BeginSelectionBox", source);
        TestAssert.Contains("UpdateSelectionBox", source);
        TestAssert.Contains("CompleteSelectionBox", source);
        TestAssert.Contains("CancelSelectionBox", source);
        TestAssert.Contains("DrawSelectionBox", source);
        TestAssert.Contains("RectsIntersect", source);
        TestAssert.Contains("g_selectionBoxAdditive = (wParam & MK_CONTROL) != 0", source);
        TestAssert.Contains("SetCapture(hwnd)", source);
        TestAssert.Contains("ReleaseCapture()", source);
    }

    public static void NativeUiSource_FitsTransparentPaddedIcons()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));
        var script = File.ReadAllText(FindRepoFile("tools", "build-native-ui.ps1"));

        TestAssert.Contains("GetIconOpaqueBounds", source);
        TestAssert.Contains("DrawFittedIcon", source);
        TestAssert.Contains("GetDIBits", source);
        TestAssert.Contains("AlphaBlend", source);
        TestAssert.Contains("AC_SRC_ALPHA", source);
        TestAssert.Contains("DrawFittedIcon(dc, icon.x", source);
        TestAssert.Contains("DrawFittedIcon(dc, iconX", source);
        TestAssert.Contains("return 255;", source);
        TestAssert.Contains("DrawIconBackplateDirect", source);
        TestAssert.Contains("FillEllipse", source);
        TestAssert.Contains("-lmsimg32", script);
    }

    public static void NativeUiSource_DeletesLaunchpadItemsInsteadOfOnlyRemovingLayout()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));
        var broker = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "ShellBroker.cpp"));

        TestAssert.Contains("RequestRecycle", source);
        TestAssert.Contains("RemoveItemsFromLayout(result->affectedPaths)", source);
        TestAssert.Contains("CLSID_FileOperation", broker);
        TestAssert.Contains("IsDirectChild", broker);
        TestAssert.Contains("FOF_ALLOWUNDO", broker);
        TestAssert.DoesNotContain("DeleteFileW(sourcePath.c_str())", source);
        TestAssert.Contains("Text(L\"Delete icon\", L\"删除图标\")", source);
        TestAssert.Contains("Text(L\"Delete selected icons\", L\"删除已选图标\")", source);
        TestAssert.Contains("ForgetIconBounds", source);
        TestAssert.DoesNotContain("Remove from layout", source);
        TestAssert.DoesNotContain("从布局移除", source);
    }

    public static void NativeUiSource_UsesGlassBackdropAndSoftEdges()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));
        var script = File.ReadAllText(FindRepoFile("tools", "build-native-ui.ps1"));

        TestAssert.Contains("ApplyGlassBackdrop", source);
        TestAssert.Contains("SetWindowCompositionAttribute", source);
        TestAssert.Contains("ACCENT_ENABLE_ACRYLICBLURBEHIND", source);
        TestAssert.Contains("policy.accentState = spotlight ? ACCENT_DISABLED : ACCENT_ENABLE_ACRYLICBLURBEHIND;", source);
        TestAssert.Contains("DwmSetWindowAttribute", source);
        TestAssert.Contains("DwmEnableBlurBehindWindow", source);
        TestAssert.Contains("blur.fEnable = spotlight ? FALSE : TRUE;", source);
        TestAssert.Contains("const DWORD backdrop = spotlight ? DWMSBT_NONE : DWMSBT_TRANSIENTWINDOW;", source);
        TestAssert.Contains("DWMWA_WINDOW_CORNER_PREFERENCE", source);
        TestAssert.Contains("DWMWA_BORDER_COLOR", source);
        TestAssert.Contains("DWMWA_COLOR_NONE", source);
        TestAssert.Contains("const DWORD cornerPreference = IsSpotlightMode() ? DWMWCP_DONOTROUND : DWMWCP_ROUND;", source);
        TestAssert.Contains("UpdateLayeredWindow(g_hwnd", source);
        TestAssert.Contains("ULW_ALPHA", source);
        TestAssert.Contains("ApplySpotlightPerPixelAlpha", source);
        TestAssert.Contains("RoundedRectCoverage", source);
        TestAssert.Contains("targetAlpha * coverage", source);
        TestAssert.Contains("SpotlightContentFeatherSize", source);
        TestAssert.Contains("SpotlightGlassAlpha + (255 - SpotlightGlassAlpha) * contentCoverage", source);
        TestAssert.Contains("g_spotlightPaintOpacity", source);
        TestAssert.Contains("SpotlightLayerBuffer", source);
        TestAssert.Contains("PaintSpotlightLayeredWindow(false)", source);
        TestAssert.DoesNotContain("SetLayeredWindowAttributes(g_hwnd, SpotlightTransparentKey, alpha, LWA_ALPHA | LWA_COLORKEY)", source);
        TestAssert.Contains("CreateRoundRectRgn", source);
        TestAssert.Contains("SetWindowRgn(g_hwnd, nullptr, TRUE)", source);
        TestAssert.DoesNotContain("SetWindowRgn(g_hwnd, region, TRUE)", source);
        TestAssert.Contains("SpotlightRoundedRect", source);
        TestAssert.Contains("SpotlightPanelInset", source);
        TestAssert.Contains("SpotlightEdgeLayers", source);
        TestAssert.DoesNotContain("if (IsSpotlightMode()) return;", source);
        TestAssert.Contains("SetSpotlightLayeredOpacity", source);
        TestAssert.Contains("SpotlightGlassAlpha", source);
        TestAssert.Contains("ShowSpotlightImmediately", source);
        TestAssert.Contains("HideSpotlightImmediately", source);
        TestAssert.DoesNotContain("Sleep(8);", source);
        TestAssert.DoesNotContain("Sleep(7);", source);
        TestAssert.Contains("RenderSpotlightGlassDirect2D", source);
        TestAssert.Contains("CreateRadialGradientBrush", source);
        TestAssert.Contains("RenderSpotlightGlassGdi", source);
        TestAssert.Contains("CreateGradientStopCollection", source);
        TestAssert.Contains("glowBrush", source);
        TestAssert.Contains("GradientFill", source);
        TestAssert.Contains("DrawGlassBackground", source);
        TestAssert.Contains("DrawSoftWindowEdge", source);
        TestAssert.DoesNotContain("SpotlightBackgroundProbeStage", source);
        TestAssert.DoesNotContain("FillRectInSpotlightClip", source);
        TestAssert.Contains("-ldwmapi", script);
    }

    public static void NativeUiSource_UsesLiquidGlassOpticalLayers()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("SpotlightGlassRimLayers", source);
        TestAssert.Contains("DrawLiquidGlassSpecularSheen", source);
        TestAssert.Contains("DrawLiquidGlassInnerShadow", source);
        TestAssert.Contains("DrawLiquidGlassRimLight", source);
        TestAssert.Contains("DrawSearchLiquidSpecular", source);
        TestAssert.Contains("RenderSpotlightGlassGdiSpecular", source);
        TestAssert.Contains("DrawLiquidGlassSpecularSheen(target, panel, client);", source);
        TestAssert.Contains("DrawSearchLiquidSpecular(target, rounded, rect, active);", source);
        TestAssert.DoesNotContain("ID2D1Effect", source);
    }

    public static void NativeUiSource_UsesLiquidGlassFocusLensing()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("SpotlightBackdropCapture", source);
        TestAssert.Contains("CaptureSpotlightBackdrop", source);
        TestAssert.Contains("ReleaseSpotlightBackdrop", source);
        TestAssert.Contains("DrawLiquidGlassFocusLens", source);
        TestAssert.Contains("DrawSearchFocusLens", source);
        TestAssert.Contains("DrawFocusedTileLens", source);
        TestAssert.Contains("CaptureSpotlightBackdrop(windowRect);", source);
        TestAssert.Contains("DrawSearchFocusLens(target, rounded, rect, active);", source);
        TestAssert.Contains("DrawFocusedTileLens(target, rounded, rect);", source);
        TestAssert.Contains("StretchBlt", source);
        TestAssert.Contains("CAPTUREBLT", source);
        TestAssert.DoesNotContain("ID2D1Effect", source);
    }

    public static void NativeUiSource_UsesBackdropBlurForLiquidGlass()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("ApplyBoxBlurToBackdrop", source);
        TestAssert.Contains("BoxBlurHorizontalPass", source);
        TestAssert.Contains("BoxBlurVerticalPass", source);
        TestAssert.Contains("GetSpotlightBackdropD2dBitmap(target)", source);
        TestAssert.Contains("target->DrawBitmap(backdrop", source);
        TestAssert.Contains("D2D1_BITMAP_INTERPOLATION_MODE_LINEAR", source);
    }

    public static void NativeUiSource_UsesAlignedSearchFocusRefraction()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("DrawAlignedSearchBackdrop", source);
        TestAssert.Contains("DrawSearchLiquidMaterialWash", source);
        TestAssert.Contains("DrawSearchEdgeRefraction", source);
        TestAssert.Contains("DrawSearchHorizontalGlassRim", source);
        TestAssert.Contains("DrawSearchFocusGlow", source);
        TestAssert.Contains("DrawAlignedSearchBackdrop(target, rounded, rect, active);", source);
        TestAssert.Contains("DrawSearchLiquidMaterialWash(target, rounded, rect, active);", source);
        TestAssert.Contains("DrawSearchEdgeRefraction(target, rounded, rect, active);", source);
        TestAssert.Contains("DrawSearchHorizontalGlassRim(target, rounded, rect, active);", source);
        TestAssert.Contains("DrawAlignedSearchBackdrop(dc, rect, active);", source);
        TestAssert.Contains("DrawSearchLiquidMaterialWash(dc, rect, active);", source);
        TestAssert.Contains("DrawSearchEdgeRefraction(dc, rect, active);", source);
        TestAssert.Contains("DrawSearchHorizontalGlassRim(dc, rect, active);", source);
        TestAssert.Contains("active ? 0.035f : 0.020f", source);
        TestAssert.Contains("D2DColorAlpha(RGB(246, 252, 255), active ? 0.46f : 0.34f)", source);
        TestAssert.Contains("D2DColorAlpha(RGB(148, 214, 255), active ? 0.24f : 0.16f)", source);
        TestAssert.Contains("displayText.empty() ? RGB(110, 132, 150) : RGB(34, 54, 72)", source);
        TestAssert.DoesNotContain("DrawLiquidGlassFocusLens(target, rounded, rect, active ? 1.12f : 1.06f", source);
        TestAssert.DoesNotContain("DrawLiquidGlassFocusLens(dc, rect, SpotlightSearchCornerRadius, active ? 1.12f : 1.06f", source);
        TestAssert.DoesNotContain("drawBand(topBand", source);
        TestAssert.DoesNotContain("drawBand(bottomBand", source);
        TestAssert.DoesNotContain("drawBand(topBand, 0.0f, -shift", source);
        TestAssert.DoesNotContain("drawBand(bottomBand, 0.0f, shift", source);
        TestAssert.DoesNotContain("{ 0, -shift }", source);
        TestAssert.DoesNotContain("{ 0, shift }", source);
    }

    public static void NativeUiSource_TrimsResidentIconsForMemory()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("MaxResidentIconCount", source);
        TestAssert.Contains("iconLastUsed", source);
        TestAssert.Contains("TrimResidentIcons", source);
        TestAssert.Contains("TrimHiddenFootprint", source);
        TestAssert.Contains("ReleaseItemIcon", source);
        TestAssert.Contains("ReleaseAllResidentIcons", source);
        TestAssert.Contains("ReleaseSpotlightLayerBuffer", source);
        TestAssert.Contains("DestroyDirectRenderer", source);
        TestAssert.Contains("CoFreeUnusedLibrariesEx", source);
        TestAssert.Contains("SetProcessWorkingSetSize", source);
        TestAssert.Contains("g_filterScores.assign(g_items.size(), 0)", source);
        TestAssert.Contains("g_filtered.reserve(g_items.size())", source);
        TestAssert.Contains("ShowWindow(g_hwnd, SW_HIDE)", source);
        TestAssert.DoesNotContain("std::vector<int> scores(g_items.size(), 0)", source);
    }

    public static void NativeUiSource_UsesProductionSearchAndTextControllers()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));
        var searchSource = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "SearchIndex.cpp"));
        var textSource = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "TextInput.cpp"));
        var accessibilitySource = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "SearchAccessibility.cpp"));

        TestAssert.Contains("ActivateSearchInput", source);
        TestAssert.Contains("#include \"SearchIndex.h\"", source);
        TestAssert.Contains("RebuildSearchIndex", source);
        TestAssert.Contains("g_searchIndex.Rebuild(items, g_searchHistory)", source);
        TestAssert.Contains("g_searchIndex.Query(query, options)", source);
        TestAssert.Contains("options.showRecentAndFavorites", source);
        TestAssert.Contains("Index::BuildPinyinForms", searchSource);
        TestAssert.Contains("ScoreName", searchSource);
        TestAssert.Contains("DamerauLevenshtein", searchSource);
        TestAssert.Contains("History::RecordLaunch", searchSource);
        TestAssert.Contains("#include \"TextInput.h\"", source);
        TestAssert.Contains("#include \"SearchAccessibility.h\"", source);
        TestAssert.Contains("TextBuffer::MoveCaret", textSource);
        TestAssert.Contains("TextBuffer::BeginComposition", textSource);
        TestAssert.Contains("SearchAccessibility::HandleGetObject", accessibilitySource);
        TestAssert.Contains("IValueProvider", accessibilitySource);
        TestAssert.Contains("ITextProvider", accessibilitySource);
        TestAssert.Contains("DrawSearchGlassSurface", source);
        TestAssert.Contains("SpotlightSearchSideInset", source);
        TestAssert.Contains("SpotlightSearchCornerRadius", source);
        TestAssert.Contains("searchRect.top, searchRect.right", source);
        TestAssert.Contains("RGB(58, 140, 255)", source);
        TestAssert.Contains("VK_TAB", source);
        TestAssert.Contains("metrics.groupByRegion = SearchCommittedText().empty()", source);
        TestAssert.DoesNotContain("CompleteSearchFromSelection", source);
        TestAssert.DoesNotContain("g_completionCandidateText", source);
        TestAssert.DoesNotContain("KnownPinyinForChar", source);
        TestAssert.DoesNotContain("BuildPinyinSearchText", source);
        TestAssert.DoesNotContain("searchCandidateLower", source);
        TestAssert.DoesNotContain("ToLower(BuildSearchCandidateText(item))", source);
    }

    public static void NativeUiSource_ShowsSearchTypingStateAndImeComposition()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));
        var textSource = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "TextInput.cpp"));
        var accessibilitySource = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "SearchAccessibility.cpp"));
        var script = File.ReadAllText(FindRepoFile("tools", "build-native-ui.ps1"));

        TestAssert.Contains("g_searchInput", source);
        TestAssert.Contains("SearchCommittedText", source);
        TestAssert.Contains("SearchDisplayText", source);
        TestAssert.Contains("DrawSearchCaret", source);
        TestAssert.Contains("UpdateImeWindowPosition", source);
        TestAssert.Contains("WM_IME_COMPOSITION", source);
        TestAssert.Contains("ImmGetCompositionStringW", source);
        TestAssert.Contains("GCS_COMPSTR", source);
        TestAssert.Contains("TextBuffer::CommitComposition", textSource);
        TestAssert.Contains("UIA_TextPatternId", accessibilitySource);
        TestAssert.Contains("WM_GETOBJECT", source);
        TestAssert.Contains("-limm32", script);
        TestAssert.Contains("-luiautomationcore", script);
    }

    public static void NativeUiSource_CapturesHotkeyInputInSettings()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("HotkeyEditSubclassProc", source);
        TestAssert.Contains("SetWindowSubclass", source);
        TestAssert.Contains("RemoveWindowSubclass", source);
        TestAssert.Contains("FormatHotkeyFromKeyPress", source);
        TestAssert.Contains("WM_GETDLGCODE", source);
        TestAssert.Contains("DLGC_WANTALLKEYS", source);
        TestAssert.Contains("NormalizeHotkeyText", source);
        TestAssert.Contains("RegisterCurrentHotkey", source);
    }

    public static void NativeUiSource_RepaintsSearchInputImmediately()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("RequestSearchRepaint", source);
        TestAssert.Contains("PaintSpotlightLayeredWindow(true)", source);
        TestAssert.Contains("RequestSearchRepaint(hwnd, true)", source);
        TestAssert.Contains("RequestSearchRepaint(hwnd, false)", source);
    }

    public static void NativeUiSource_DoesNotFadeSearchChromePixels()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("SpotlightContentCoverageForPixel", source);
        TestAssert.Contains("if (y < contentClip.top)", source);
        TestAssert.Contains("const bool persistentControlPixel = PtInRect(&auxiliaryRect, point) != FALSE;", source);
        TestAssert.Contains("? (persistentControlPixel ? 1.0 : SpotlightContentCoverageForPixel(y, contentClip))", source);
    }

    public static void NativeUiSource_EmbedsTrayIconAndPackagesSingleExe()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));
        var buildScript = File.ReadAllText(FindRepoFile("tools", "build-native-ui.ps1"));

        TestAssert.Contains("AppIconResourceName", source);
        TestAssert.Contains("LoadEmbeddedAppIcon", source);
        TestAssert.Contains("LoadImageW(", source);
        TestAssert.Contains("g_instance", source);
        TestAssert.DoesNotContain("LR_LOADFROMFILE", source);
        TestAssert.DoesNotContain("Assets\\\\LightLaunchpad.ico", source);
        TestAssert.DoesNotContain("Copy-Item (Join-Path $repoRoot \"src\\LightLaunchpad.App\\Assets\\LightLaunchpad.ico\")", buildScript);
        TestAssert.DoesNotContain("New-Item -ItemType Directory -Force -Path (Join-Path $outputDirectoryFull \"Assets\")", buildScript);
    }

    public static void NativeUiSource_RestoresTrayIconWhenShellIsReady()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("TrayIconRetryTimerId", source);
        TestAssert.Contains("g_taskbarCreatedMessage", source);
        TestAssert.Contains("RegisterWindowMessageW(L\"TaskbarCreated\")", source);
        TestAssert.Contains("Shell_NotifyIconW(NIM_ADD", source);
        TestAssert.Contains("Shell_NotifyIconW(NIM_SETVERSION", source);
        TestAssert.Contains("NOTIFYICON_VERSION_4", source);
        TestAssert.Contains("SetTimer(g_hwnd, TrayIconRetryTimerId", source);
        TestAssert.Contains("KillTimer(g_hwnd, TrayIconRetryTimerId)", source);
        TestAssert.DoesNotContain("PromoteTrayIconVisibility", source);
        TestAssert.DoesNotContain("Control Panel\\\\NotifyIconSettings", source);
        TestAssert.DoesNotContain("IsPromoted", source);
        TestAssert.DoesNotContain("TrayIconPromotionRetryTimerId", source);
    }

    public static void NativeUiSource_HandlesTrayCallbacksFromNotifyIconVersion4()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("HandleTrayNotification", source);
        TestAssert.Contains("const UINT event = LOWORD(lParam);", source);
        TestAssert.Contains("event == NIN_SELECT", source);
        TestAssert.Contains("event == WM_CONTEXTMENU", source);
        TestAssert.Contains("HandleTrayNotification(lParam)", source);
        TestAssert.DoesNotContain("if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)", source);
    }

    public static void NativeUiSource_CanResetSearchOnOpenFromSettings()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("ResetSearchOnOpen", source);
        TestAssert.Contains("resetSearchOnOpen", source);
        TestAssert.Contains("ResetSearchForOpen", source);
        TestAssert.Contains("g_settings.resetSearchOnOpen", source);
        TestAssert.Contains("\"ResetSearchOnOpen\"", source);
        TestAssert.Contains("SettingsControlId::ResetSearchOnOpen", source);
        TestAssert.Contains("BM_GETCHECK", source);
        TestAssert.Contains("BM_SETCHECK", source);
    }

    public static void NativeUiSource_SplitsRenderingPipeline()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("struct RenderLayoutMetrics", source);
        TestAssert.Contains("CreateRenderLayoutMetrics", source);
        TestAssert.Contains("ResetRenderHitState", source);
        TestAssert.Contains("RenderFrameDirect2D", source);
        TestAssert.Contains("RenderFrameGdi", source);
        TestAssert.Contains("RenderDeferredIconOverlay", source);
        TestAssert.Contains("const RenderLayoutMetrics metrics = CreateRenderLayoutMetrics(client)", source);
        TestAssert.Contains("RenderFrameDirect2D(target, client)", source);
        TestAssert.Contains("RenderFrameGdi(dc, client)", source);
        TestAssert.Contains("ContentClipRect", source);
        TestAssert.Contains("SpotlightContentInset", source);
        TestAssert.Contains("SpotlightContentBottomInset", source);
    }

    public static void NativeUiSource_UsesQuietSelectionBoxAndScrollsWhileSelecting()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("DrawSubtleSelectionOutline", source);
        TestAssert.Contains("SelectionContentPointFromClient", source);
        TestAssert.Contains("BuildContentHitTiles", source);
        TestAssert.Contains("ScreenToClient(hwnd, &point);", source);
        TestAssert.Contains("UpdateSelectionBox(point);", source);
        TestAssert.Contains("CreatePen(PS_SOLID, 1, RGB(150, 166, 182))", source);
        TestAssert.Contains("CreatePen(PS_SOLID, 1, RGB(74, 88, 104))", source);
        TestAssert.DoesNotContain("if (g_selectionBoxActive) return 0;", source);
        TestAssert.DoesNotContain("0x30489BFF", source);
        TestAssert.DoesNotContain("RGB(98, 186, 255)", source);
    }

    public static void NativeUiSource_PreviewsDragAvoidanceReflow()
    {
        var source = File.ReadAllText(FindRepoFile("src", "LightLaunchpad.NativeUi", "LightLaunchpad.NativeUi.cpp"));

        TestAssert.Contains("ShouldHideTileForDragPreview", source);
        TestAssert.Contains("CountNonDraggedItemsBefore", source);
        TestAssert.Contains("DragPreviewSlotForItem", source);
        TestAssert.Contains("ApplyDragAvoidanceOffset", source);
        TestAssert.Contains("DragReflowSmoothing", source);
        TestAssert.Contains("UpdateStableDropTarget", source);
        TestAssert.Contains("DragIconAlphaForItem", source);
        TestAssert.Contains("g_selectedSourcePaths = movedPaths;", source);
        TestAssert.Contains("visualTileRect", source);
        TestAssert.Contains("if (ShouldHideTileForDragPreview(item))", source);
        TestAssert.Contains("DragPreviewSlotForItem(item)", source);
        TestAssert.DoesNotContain("RGB(80, 190, 255)", source);
        TestAssert.DoesNotContain("RGB(160, 225, 255)", source);
    }

    public static void NativeUiBuildScript_ProducesNativeUiExecutable()
    {
        var script = File.ReadAllText(FindRepoFile("tools", "build-native-ui.ps1"));

        TestAssert.Contains("LightLaunchpad.NativeUi.cpp", script);
        TestAssert.Contains("LightLaunchpad.NativeUi.rc", script);
        TestAssert.Contains("LightLaunchpad.NativeUi.exe", script);
        TestAssert.Contains("windres", script);
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
        TestAssert.DoesNotContain("LightLaunchpad.App.exe", script);
    }

    public static void ProjectStructureDocument_ExplainsRepositoryFiles()
    {
        var document = File.ReadAllText(FindRepoFile("PROJECT_STRUCTURE.md"));

        TestAssert.Contains("LightLaunchpad.NativeUi.exe", document);
        TestAssert.Contains("src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp", document);
        TestAssert.Contains("tools/package-release.ps1", document);
        TestAssert.Contains("tests/LightLaunchpad.App.Tests/", document);
        TestAssert.Contains("旧 WPF 与实验源码", document);
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
