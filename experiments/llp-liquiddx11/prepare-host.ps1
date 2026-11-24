#Requires -Version 5.1
<#
.SYNOPSIS
Creates a new isolated LiquidDX11 host translation unit from the existing NativeUi host.
.DESCRIPTION
Does not build, launch, copy profiles, edit production sources, or change the environment/registry.
Every source transformation requires one exact anchor. Existing output can be replaced only
when its manifest proves it is this generator's unmodified source for the same profile.
The caller supplies independent build identity macros and copies the existing JSON profile separately.
.PARAMETER Destination
Absolute path of NativeHost.cpp. Its parent directory is created if necessary.
.PARAMETER ProfileDirectory
Absolute prototype-only data directory, used directly for settings, scoped layouts, history and log.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$Destination,
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$ProfileDirectory,
    [switch]$ProductionRelease
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$llpRepositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$llpSourcePath = Join-Path $llpRepositoryRoot 'src\LightLaunchpad.NativeUi\LightLaunchpad.NativeUi.cpp'

function Get-AbsolutePath([string]$Value) {
    if (-not [IO.Path]::IsPathRooted($Value)) { throw "An absolute path is required: $Value" }
    return [IO.Path]::GetFullPath($Value).TrimEnd('\', '/')
}

function Test-Within([string]$Candidate, [string]$Directory) {
    return $Candidate.Equals($Directory, [StringComparison]::OrdinalIgnoreCase) -or
        $Candidate.StartsWith($Directory.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)
}

function Assert-NoReparseAncestor([string]$Path) {
    $llpWalk = $Path
    while (-not [string]::IsNullOrEmpty($llpWalk)) {
        if (Test-Path -LiteralPath $llpWalk) {
            $llpItem = Get-Item -LiteralPath $llpWalk -Force
            if (($llpItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Reparse-point paths are not accepted for isolated outputs: $llpWalk"
            }
        }
        $llpParent = [IO.Path]::GetDirectoryName($llpWalk)
        if ($llpParent -eq $llpWalk) { break }
        $llpWalk = $llpParent
    }
}

$llpOutputPath = Get-AbsolutePath $Destination
$llpProfilePath = Get-AbsolutePath $ProfileDirectory
$llpManifestPath = $llpOutputPath + '.preparation.json'
$llpProductionSourceRoot = Join-Path $llpRepositoryRoot 'src'
$llpProductionDataRoot = [IO.Path]::GetFullPath((Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'LightLaunchpad'))
if ([IO.Path]::GetExtension($llpOutputPath) -ine '.cpp') { throw 'Destination must name a new .cpp file.' }
if (Test-Within $llpOutputPath $llpProductionSourceRoot) { throw 'Destination must be outside the production src tree.' }
if ((Test-Within $llpProfilePath $llpProductionDataRoot) -or (Test-Within $llpProductionDataRoot $llpProfilePath)) {
    throw 'The prototype profile must not contain or lie within the real LightLaunchpad data directory.'
}
if (Test-Path -LiteralPath $llpProfilePath -PathType Leaf) { throw 'ProfileDirectory names a file.' }
foreach ($llpNewPath in @($llpOutputPath, $llpManifestPath)) {
    Assert-NoReparseAncestor $llpNewPath
}
$llpOverwriteGenerated = (Test-Path -LiteralPath $llpOutputPath) -or (Test-Path -LiteralPath $llpManifestPath)
if ($llpOverwriteGenerated) {
    if (-not (Test-Path -LiteralPath $llpOutputPath -PathType Leaf) -or -not (Test-Path -LiteralPath $llpManifestPath -PathType Leaf)) {
        throw 'Existing generated source requires its matching preparation manifest.'
    }
    $llpPrevious = Get-Content -LiteralPath $llpManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($llpPrevious.schema -ne 'llp.liquiddx11.host-preparation.v1' -or
        $llpPrevious.destination -ne $llpOutputPath -or $llpPrevious.sourcePath -ne $llpSourcePath -or
        $llpPrevious.profileDirectory -ne $llpProfilePath -or
        (Get-FileHash -LiteralPath $llpOutputPath -Algorithm SHA256).Hash -ne $llpPrevious.destinationSha256) {
        throw 'Refusing to overwrite a file that is not this unmodified generated host.'
    }
}
Assert-NoReparseAncestor $llpProfilePath
$llpSourceHash = (Get-FileHash -LiteralPath $llpSourcePath -Algorithm SHA256).Hash
$llpOriginalText = [IO.File]::ReadAllText($llpSourcePath)
$script:llpHostText = $llpOriginalText.Replace("`r`n", "`n")
$llpChanges = [Collections.Generic.List[object]]::new()

function Replace-Unique([string]$Name, [string]$Old, [string]$New) {
    $Old = $Old.Replace("`r`n", "`n")
    $New = $New.Replace("`r`n", "`n")
    $llpAt = $script:llpHostText.IndexOf($Old, [StringComparison]::Ordinal)
    if ($llpAt -lt 0 -or $script:llpHostText.IndexOf($Old, $llpAt + $Old.Length, [StringComparison]::Ordinal) -ge 0) {
        throw "Expected exactly one source anchor for $Name"
    }
    $script:llpHostText = $script:llpHostText.Remove($llpAt, $Old.Length).Insert($llpAt, $New)
    $llpChanges.Add([ordered]@{ name=$Name; matches=1 })
}

function Replace-Function([string]$Name, [string]$Signature, [string]$NextSignature, [string]$Replacement) {
    $llpBegin = $script:llpHostText.IndexOf($Signature + "`n{", [StringComparison]::Ordinal)
    if ($llpBegin -lt 0) { throw "Missing function anchor: $Signature" }
    $llpEnd = $script:llpHostText.IndexOf("`n" + $NextSignature, $llpBegin, [StringComparison]::Ordinal)
    if ($llpEnd -lt 0) { throw "Missing next-function anchor: $NextSignature" }
    $llpOldBody = $script:llpHostText.Substring($llpBegin, $llpEnd - $llpBegin)
    if (-not $llpOldBody.TrimEnd().EndsWith('}', [StringComparison]::Ordinal)) { throw "Unexpected function boundary: $Signature" }
    Replace-Unique $Name $llpOldBody ($Replacement.TrimEnd() + "`n")
}

$llpCppProfile = $llpProfilePath.Replace('\', '\\').Replace('"', '\"')
Replace-Unique 'liquid-adapter-include' '#include "MaterialRenderer.h"' "#include `"MaterialRenderer.h`"`n#include `"LiquidMaterialRenderer.h`""
Replace-Unique 'liquid-mode-before-request' "llp::material::ShowRequest CurrentMaterialShowRequest(std::uint64_t requiredSceneGeneration)`n{" @'
llp::material::ShowRequest CurrentMaterialShowRequest(std::uint64_t requiredSceneGeneration)
{
    llp::material::SetLiquidSpotlightMode(IsSpotlightMode());
    llp::material::SetLiquidGlassSettings({
        g_settings.glassBlurPercent / 100.0f,
        g_settings.glassTintPercent / 100.0f,
        static_cast<float>(g_settings.glassRefractionPx),
        g_settings.glassRimPercent / 100.0f });
    llp::material::SetLiquidSpotlightCaptions({
        Text(L"Search applications", L"搜索应用"),
        Text(L"Type to search", L"输入应用名称开始搜索"),
        Text(L"No matching applications", L"没有找到匹配的应用") });
'@
if (-not $ProductionRelease) {
Replace-Function 'isolated-data-path' 'std::wstring GetLightLaunchpadDataPath()' 'std::wstring GetLayoutPath()' @"
std::wstring GetLightLaunchpadDataPath()
{
    return L"$llpCppProfile";
}
"@
Replace-Function 'isolated-default-log' 'std::wstring DefaultStartupLogPath()' 'void AppendStartupLog(const wchar_t* stage, bool succeeded, const std::wstring& detail = {})' @'
std::wstring DefaultStartupLogPath()
{
    return JoinPath(JoinPath(GetLightLaunchpadDataPath(), L"logs"), L"apphost.log");
}
'@
Replace-Function 'no-startup-registry-writes' 'bool WriteStartupRegistrationValue(bool exists, DWORD type, const std::wstring& value)' 'bool SetStartWithWindows(bool enabled)' @'
bool WriteStartupRegistrationValue(bool, DWORD, const std::wstring&)
{
    return false; // No Run writes or deletion, even from rollback or other callers.
}
'@
Replace-Unique 'settings-apply-no-startup-side-effect' '    const bool startupChanged = candidate.startWithWindows != previous.startWithWindows;' '    const bool startupChanged = false; // Prototype preferences are local only; never modify Run.'
}
Replace-Unique 'refresh-localized-spotlight-captions' '            && (geometryChanged || candidate.reduceMotion != previous.reduceMotion))' '            && (geometryChanged || candidate.reduceMotion != previous.reduceMotion || candidate.language != previous.language))'
Replace-Unique 'refresh-policy-after-mode-change' '    if (previous.reduceMotion != candidate.reduceMotion) RefreshAdaptiveEnvironment();' '    if (previous.reduceMotion != candidate.reduceMotion || previousSpotlight != candidateSpotlight) RefreshAdaptiveEnvironment();'
Replace-Unique 'initial-spotlight-only' "    LoadSettings();`n    RefreshAdaptiveEnvironment();" "    LoadSettings();`n    g_settings.displayMode = L`"Spotlight`"; // Initial view only; later settings may switch modes.`n    llp::material::RequestLiquidFrameExport(CommandLineValue(L`"--export-render`"));`n    llp::material::RequestLiquidPresentExport(CommandLineValue(L`"--export-presents`"));`n    RefreshAdaptiveEnvironment();"
Replace-Unique 'preview-only-transparency-policy' "    next.animationsEnabled = next.animationsEnabled && !g_settings.reduceMotion;`n    const auto nextPolicy = llp::adaptive::ResolvePolicy(next);" @'
    next.animationsEnabled = next.animationsEnabled && !g_settings.reduceMotion;
    // Isolated LiquidDX11 demonstration only: no registry or settings mutation.
    // High contrast and all existing reduced-motion decisions remain authoritative.
    if (IsSpotlightMode() && !next.highContrast
        && !CommandLineHasArgument(L"--respect-system-effects"))
    {
        next.transparencyEnabled = true;
    }
    const auto nextPolicy = llp::adaptive::ResolvePolicy(next);
'@
if (-not $ProductionRelease) {
Replace-Unique 'independent-main-window-title' '        L"LightLaunchpad Native UI",' '        L"LightLaunchpad LiquidDX11 Preview",'
Replace-Unique 'independent-tray-tooltip' '    CopyText(nid.szTip, ARRAYSIZE(nid.szTip), L"LightLaunchpad Native UI");' '    CopyText(nid.szTip, ARRAYSIZE(nid.szTip), L"LightLaunchpad LiquidDX11 Preview");'
}
Replace-Function 'complete-ime-composition-events' 'void UpdateSearchComposition(HWND hwnd, LPARAM compositionFlags)' 'bool IsSpotlightMode()' @'
void UpdateSearchComposition(HWND hwnd, LPARAM compositionFlags)
{
    ActivateSearchInput();
    bool textChanged = false;
    if (compositionFlags & GCS_RESULTSTR)
    {
        const std::wstring result = ReadImeCompositionString(hwnd, GCS_RESULTSTR);
        if (!g_searchInput.IsComposing()) g_searchInput.BeginComposition();
        if (g_searchInput.CommitComposition(result)) g_scrollOffset = 0;
        textChanged = true;
    }
    if (compositionFlags & GCS_COMPSTR)
    {
        if ((compositionFlags & GCS_RESULTSTR) && !g_searchInput.IsComposing())
            g_searchInput.BeginComposition(true);
        g_searchInput.UpdateComposition(ReadImeCompositionString(hwnd, GCS_COMPSTR),
            static_cast<size_t>(ReadImeCompositionCursor(hwnd)));
        textChanged = true;
    }
    else if ((compositionFlags & GCS_CURSORPOS) && g_searchInput.IsComposing())
        g_searchInput.UpdateComposition(g_searchInput.CompositionText(), static_cast<size_t>(ReadImeCompositionCursor(hwnd)));
    else if (!textChanged) return;
    if (textChanged) RebuildFiltered();
    UpdateImeWindowPosition(hwnd);
    RequestSearchRepaint(hwnd, (compositionFlags & GCS_RESULTSTR) != 0);
    if (textChanged) g_searchAccessibility.NotifyTextChanged();
    g_searchAccessibility.NotifySelectionChanged();
}
'@
Replace-Unique 'ime-offset-before-scene' "    snapshot->search.displayText = g_searchInput.DisplayText();" "    UpdateImeWindowPosition(g_hwnd);`n    snapshot->search.displayText = g_searchInput.DisplayText();"
Replace-Unique 'scaled-gdi-input-fallback' '    return CreateFontW(26, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");' '    return CreateFontW(-ScaleTextDip(26), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");'
Replace-Function 'shared-search-prefix-layout' 'int MeasureSearchPrefixGdi(HWND hwnd, const std::wstring& text, size_t position)' 'size_t SearchTextPositionFromClientX(HWND hwnd, int clientX)' @'
int MeasureSearchPrefixGdi(HWND hwnd, const std::wstring& text, size_t position)
{
    if (text.empty()) return 0;
    if (!g_dwriteFactory) InitializeDirectRenderer();
    RECT client = {}; GetClientRect(hwnd, &client);
    const auto metrics = CurrentSceneMetrics(client);
    IDWriteTextLayout* layout = nullptr;
    if (SUCCEEDED(llp::material::CreateSpotlightSearchLayout(g_dwriteFactory, text,
        static_cast<float>(metrics.searchFontPx), static_cast<float>(metrics.searchText.right-metrics.searchText.left),
        static_cast<float>(metrics.searchText.bottom-metrics.searchText.top), &layout)))
    {
        FLOAT x = 0, y = 0; DWRITE_HIT_TEST_METRICS hit = {};
        const HRESULT result = layout->HitTestTextPosition(static_cast<UINT32>(std::min(position, text.size())), FALSE, &x, &y, &hit);
        layout->Release();
        if (SUCCEEDED(result)) return static_cast<int>(std::ceil(x));
    }
    HDC dc = GetDC(hwnd); if (!dc) return 0;
    HFONT font = CreateSearchInputFont();
    HGDIOBJ previous = SelectObject(dc, font);
    const int width = MeasureTextWidthGdi(dc, text.substr(0, std::min(position, text.size())));
    SelectObject(dc, previous); DeleteObject(font); ReleaseDC(hwnd, dc);
    return width;
}
'@
Replace-Function 'shared-search-mouse-layout' 'size_t SearchTextPositionFromClientX(HWND hwnd, int clientX)' 'POINT SearchCaretClientPoint(HWND hwnd)' @'
size_t SearchTextPositionFromClientX(HWND hwnd, int clientX)
{
    RECT client = {}; GetClientRect(hwnd, &client);
    const auto metrics = CurrentSceneMetrics(client);
    const auto& bounds = metrics.searchText;
    const std::wstring text = SearchCommittedText();
    if (text.empty()) return 0;
    if (!g_dwriteFactory) InitializeDirectRenderer();
    IDWriteTextLayout* layout = nullptr;
    if (SUCCEEDED(llp::material::CreateSpotlightSearchLayout(g_dwriteFactory, text,
        static_cast<float>(metrics.searchFontPx), static_cast<float>(bounds.right-bounds.left),
        static_cast<float>(bounds.bottom-bounds.top), &layout)))
    {
        BOOL trailing = FALSE, inside = FALSE; DWRITE_HIT_TEST_METRICS hit = {};
        const HRESULT result = layout->HitTestPoint(static_cast<float>(clientX-bounds.left+g_searchTextOffset),
            static_cast<float>(bounds.bottom-bounds.top)*.5f, &trailing, &inside, &hit);
        layout->Release();
        if (SUCCEEDED(result)) return std::min(text.size(), static_cast<size_t>(hit.textPosition)+(trailing?hit.length:0));
    }
    const int contentX = clientX-bounds.left+g_searchTextOffset;
    int previous = 0;
    for (size_t position=1; position<=text.size(); ++position)
    {
        const unsigned int character = static_cast<unsigned short>(text[position-1]);
        if (character>=0xD800 && character<=0xDBFF && position<text.size()) continue;
        const int width = MeasureSearchPrefixGdi(hwnd, text, position);
        if (contentX < previous+(width-previous)/2) return position-1;
        previous=width;
    }
    return text.size();
}
'@
Replace-Function 'shared-search-caret-layout' 'POINT SearchCaretClientPoint(HWND hwnd)' 'void UpdateImeWindowPosition(HWND hwnd)' @'
POINT SearchCaretClientPoint(HWND hwnd)
{
    RECT client = {}; GetClientRect(hwnd, &client);
    const auto metrics = CurrentSceneMetrics(client);
    const auto& bounds = metrics.searchText;
    const std::wstring text = SearchDisplayText();
    int caretWidth = 0, totalWidth = 0;
    if (!text.empty())
    {
        if (!g_dwriteFactory) InitializeDirectRenderer();
        IDWriteTextLayout* layout = nullptr;
        bool measured = false;
        if (SUCCEEDED(llp::material::CreateSpotlightSearchLayout(g_dwriteFactory, text,
            static_cast<float>(metrics.searchFontPx), static_cast<float>(bounds.right-bounds.left),
            static_cast<float>(bounds.bottom-bounds.top), &layout)))
        {
            FLOAT x = 0, y = 0; DWRITE_HIT_TEST_METRICS hit = {}; DWRITE_TEXT_METRICS full = {};
            measured = SUCCEEDED(layout->HitTestTextPosition(static_cast<UINT32>(std::min(g_searchInput.DisplayCaret(), text.size())), FALSE, &x, &y, &hit))
                && SUCCEEDED(layout->GetMetrics(&full));
            if (measured) { caretWidth = static_cast<int>(std::ceil(x)); totalWidth = static_cast<int>(std::ceil(full.widthIncludingTrailingWhitespace)); }
            layout->Release();
        }
        if (!measured)
        {
            caretWidth = MeasureSearchPrefixGdi(hwnd, text, g_searchInput.DisplayCaret());
            totalWidth = MeasureSearchPrefixGdi(hwnd, text, text.size());
        }
    }
    UpdateSearchTextOffset(caretWidth, totalWidth, std::max(1, bounds.right-bounds.left-4));
    return { std::clamp(bounds.left+caretWidth-g_searchTextOffset, bounds.left+1, bounds.right-2), bounds.bottom-8 };
}
'@
Replace-Unique 'solid-spotlight-d2d-fallback' "void DrawGlassBackground(ID2D1DCRenderTarget* target, const RECT& client)`n{`n    if (g_adaptivePolicy.solidSurface)" "void DrawGlassBackground(ID2D1DCRenderTarget* target, const RECT& client)`n{`n    if (g_adaptivePolicy.solidSurface || IsSpotlightMode())"
Replace-Unique 'solid-spotlight-gdi-fallback' "void DrawGlassBackground(HDC dc, const RECT& client)`n{`n    const bool spotlight = IsSpotlightMode();`n    if (g_adaptivePolicy.solidSurface)" "void DrawGlassBackground(HDC dc, const RECT& client)`n{`n    const bool spotlight = IsSpotlightMode();`n    if (g_adaptivePolicy.solidSurface || spotlight)"
Replace-Function 'gpu-only-desktop-source' 'bool CaptureSpotlightBackdrop(const RECT& windowRect)' 'UINT DpiForMonitor(HMONITOR monitor)' @'
bool CaptureSpotlightBackdrop(const RECT&)
{
    // Desktop sampling belongs to liquidDX11. Retire any old CPU capture.
    ReleaseSpotlightBackdrop();
    return false;
}
'@
Replace-Function 'single-host-input-bitmap' 'bool EnsureSpotlightLayerBuffer(int width, int height)' 'void ApplySpotlightPerPixelAlpha(DWORD* basePixels, DWORD* fullPixels, int width, int height)' @'
bool EnsureSpotlightLayerBuffer(int width, int height)
{
    if (width <= 0 || height <= 0) return false;
    if (g_spotlightLayerBuffer.fullDc && g_spotlightLayerBuffer.fullBitmap
        && g_spotlightLayerBuffer.fullBits && g_spotlightLayerBuffer.width == width
        && g_spotlightLayerBuffer.height == height) return true;
    ReleaseSpotlightLayerBuffer();
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    g_spotlightLayerBuffer.fullDc = CreateCompatibleDC(screen);
    g_spotlightLayerBuffer.fullBitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS,
        &g_spotlightLayerBuffer.fullBits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!g_spotlightLayerBuffer.fullDc || !g_spotlightLayerBuffer.fullBitmap || !g_spotlightLayerBuffer.fullBits)
    {
        ReleaseSpotlightLayerBuffer();
        return false;
    }
    g_spotlightLayerBuffer.oldFullBitmap = SelectObject(g_spotlightLayerBuffer.fullDc, g_spotlightLayerBuffer.fullBitmap);
    g_spotlightLayerBuffer.width = width; g_spotlightLayerBuffer.height = height;
    g_spotlightLayerDirty = true;
    return true;
}
'@
Replace-Function 'gpu-owned-spotlight-and-solid-fallback' 'bool PaintSpotlightLayeredWindow(bool repaint)' 'bool PresentTransparentSpotlightDirectLayer()' @'
bool PaintSpotlightLayeredWindow(bool repaint)
{
    if (!g_hwnd || !IsSpotlightMode()) return false;
    const auto state = g_materialRenderer.SnapshotDiagnostics();
    if (!g_adaptivePolicy.solidSurface && state.failure == llp::material::FailureReason::None
        && state.state != llp::material::LifecycleState::Fallback)
        return PresentTransparentSpotlightDirectLayer();

    RECT client = {}; GetClientRect(g_hwnd, &client);
    const int width = std::max(1L, client.right), height = std::max(1L, client.bottom);
    if (!EnsureSpotlightLayerBuffer(width, height)) return false;
    if (repaint || g_spotlightLayerDirty || g_spotlightLayerBuffer.directPixelsTransparent)
    {
        std::memset(g_spotlightLayerBuffer.fullBits, 0, static_cast<size_t>(width) * height * sizeof(DWORD));
        PaintIntoDib(g_spotlightLayerBuffer.fullDc, client, false);
        llp::spotlight::AlphaParameters alpha;
        alpha.cornerRadiusPx = ScaleDip(SpotlightCornerRadius);
        alpha.glassAlpha = 255;
        auto* pixels = reinterpret_cast<std::uint32_t*>(g_spotlightLayerBuffer.fullBits);
        llp::spotlight::ApplyAlpha(pixels, pixels, width, height, alpha);
        g_spotlightLayerDirty = false;
    }
    RECT window = {}; GetWindowRect(g_hwnd, &window);
    POINT destination = { window.left, window.top }, source = { 0, 0 };
    SIZE size = { width, height };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, g_spotlightPaintOpacity, AC_SRC_ALPHA };
    HDC screen = GetDC(nullptr); if (!screen) return false;
    const BOOL updated = UpdateLayeredWindow(g_hwnd, screen, &destination, &size,
        g_spotlightLayerBuffer.fullDc, &source, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
    if (updated) g_spotlightLayerBuffer.directPixelsTransparent = false;
    return updated != FALSE;
}
'@
Replace-Function 'retained-host-hit-mask' 'bool PresentTransparentSpotlightDirectLayer()' 'void RequestSearchRepaint(HWND hwnd, bool erase)' @'
bool PresentTransparentSpotlightDirectLayer()
{
    if (!g_hwnd || !IsSpotlightMode()) return false;
    RECT client = {}; GetClientRect(g_hwnd, &client);
    const int width = std::max(1L, client.right), height = std::max(1L, client.bottom);
    if (!EnsureSpotlightLayerBuffer(width, height)) return false;
    if (g_spotlightLayerBuffer.directPixelsTransparent) return true;
    llp::spotlight::FillCompositionHitMask(reinterpret_cast<std::uint32_t*>(g_spotlightLayerBuffer.fullBits),
        width, height, ScaleDip(SpotlightCornerRadius));
    RECT window = {}; GetWindowRect(g_hwnd, &window);
    POINT destination = { window.left, window.top }, source = { 0, 0 };
    SIZE size = { width, height };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    HDC screen = GetDC(nullptr); if (!screen) return false;
    const BOOL updated = UpdateLayeredWindow(g_hwnd, screen, &destination, &size,
        g_spotlightLayerBuffer.fullDc, &source, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
    if (updated) g_spotlightLayerBuffer.directPixelsTransparent = true;
    return updated != FALSE;
}
'@
Replace-Unique 'initial-direct-show' "    MSG msg = {};`n    while (GetMessageW(&msg, nullptr, 0, 0) > 0)" "    ShowNativeUi(); // Direct startup; no global hotkey is required.`n    MSG msg = {};`n    while (GetMessageW(&msg, nullptr, 0, 0) > 0)"
if (-not $ProductionRelease -and [regex]::IsMatch($script:llpHostText, 'Reg(?:SetValueEx|DeleteValue)W\s*\(')) { throw 'An unexpected registry mutation remains in the generated host.' }
if ((Get-FileHash -LiteralPath $llpSourcePath -Algorithm SHA256).Hash -ne $llpSourceHash) { throw 'Production host changed during preparation.' }

$llpOutputParent = [IO.Path]::GetDirectoryName($llpOutputPath)
$null = [IO.Directory]::CreateDirectory($llpOutputParent)
function Write-NewUtf8([string]$Path, [string]$Text) {
    $llpBytes = [Text.UTF8Encoding]::new($false).GetBytes($Text)
    $llpMode = if ($llpOverwriteGenerated) { [IO.FileMode]::Create } else { [IO.FileMode]::CreateNew }
    $llpStream = [IO.File]::Open($Path, $llpMode, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try { $llpStream.Write($llpBytes, 0, $llpBytes.Length); $llpStream.Flush($true) }
    finally { $llpStream.Dispose() }
}
Write-NewUtf8 $llpOutputPath $script:llpHostText
$llpManifest = [ordered]@{
    schema='llp.liquiddx11.host-preparation.v1'
    sourcePath=$llpSourcePath; sourceSha256=$llpSourceHash
    destination=$llpOutputPath; destinationSha256=(Get-FileHash -LiteralPath $llpOutputPath -Algorithm SHA256).Hash
    profileDirectory=$llpProfilePath; transformations=$llpChanges.ToArray()
    initialMode='Spotlight'; globalHotkeyRegistration=$false; startupRegistryMutation=$false
    profileCopied=$false; targetLaunched=$false; productionSourceModified=$false
    previewSystemTransparencyOverride=[ordered]@{
        scope='Generated isolated Spotlight demonstration only; not a production or system-policy change.'
        defaultEnabled=$true; optOutArgument='--respect-system-effects'
        condition='IsSpotlightMode() && !next.highContrast && no opt-out argument'
        changedLocalField='next.transparencyEnabled=true before ResolvePolicy(next)'
        highContrastHonored=$true; reducedMotionUnchanged=$true
        systemRegistryChanged=$false; originalConfigurationChanged=$false
    }
    callerMustProvide='Independent executable/build identity, LLP_SINGLETON_NAME_WIDE and LLP_WINDOW_CLASS_NAME_WIDE; copied JSON profile; include paths for production headers and LiquidMaterialRenderer.h.'
    preferencesScope='Hotkey and StartWithWindows may be stored in the copied settings but have no OS effect. LaunchpadFolder remains whatever the copied configuration contains.'
}
Write-NewUtf8 $llpManifestPath (($llpManifest | ConvertTo-Json -Depth 8) + "`n")
$llpManifest | ConvertTo-Json -Depth 8
