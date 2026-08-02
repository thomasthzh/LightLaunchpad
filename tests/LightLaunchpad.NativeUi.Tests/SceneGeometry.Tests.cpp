#include "SceneGeometry.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using llp::geometry::BuildScene;
using llp::geometry::BuildSceneMetrics;
using llp::geometry::HitTestItem;
using llp::geometry::LayoutItem;
using llp::geometry::LayoutRequest;
using llp::geometry::MetricsRequest;
using llp::geometry::BuildSettingsMetrics;
using llp::geometry::SettingsMetricsRequest;
using llp::geometry::BuildTextInputMetrics;
using llp::geometry::TextInputMetricsRequest;

int passed = 0;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void Pass(const char* name)
{
    ++passed;
    std::cout << "PASS " << name << "\n";
}

LayoutRequest RequestForScale(int percent)
{
    LayoutRequest request;
    request.clientWidth = 1000 * percent / 100;
    request.tileSize = 128 * percent / 100;
    request.gap = 16 * percent / 100;
    request.left = 44 * percent / 100;
    request.initialContentTop = 114 * percent / 100;
    request.regionHeaderHeight = 38 * percent / 100;
    request.scrollOffset = 40 * percent / 100;
    request.groupByRegion = true;
    request.items = {
        { 0, L"one" },
        { 1, L"one" },
        { 2, L"two" },
        { 3, L"two" }
    };
    return request;
}

void TestDrawingHitTestingAndScrollingShareExactSlotsAtSupportedScales()
{
    for (const int percent : { 100, 150, 200 })
    {
        const auto scene = BuildScene(RequestForScale(percent));
        Require(scene.items.size() == 4, "Scene lost an item slot.");
        Require(scene.regions.size() == 2, "Scene lost a region header slot.");
        for (const auto& item : scene.items)
        {
            const int centerX = (item.contentBounds.left + item.contentBounds.right) / 2;
            const int centerY = (item.contentBounds.top + item.contentBounds.bottom) / 2;
            Require(HitTestItem(scene, centerX, centerY) == item.filteredIndex, "Hit test did not match the drawn content slot.");
            Require(item.viewportBounds.top == item.contentBounds.top - RequestForScale(percent).scrollOffset, "Scroll projection diverged from content geometry.");
        }
    }
    Pass("SceneGeometry_DrawHitScrollMatchAt100150200Percent");
}

void TestSearchLayoutSuppressesRegionHeadersAndUsesStableColumns()
{
    auto request = RequestForScale(100);
    request.groupByRegion = false;
    request.scrollOffset = 0;
    const auto scene = BuildScene(request);
    Require(scene.regions.empty(), "Search layout emitted region headers.");
    Require(scene.columns == 6, "Column calculation did not use the shared width metrics.");
    Require(scene.items[0].contentBounds.top == request.initialContentTop, "Search layout started below a phantom header.");
    Require(scene.items[1].contentBounds.left == scene.items[0].contentBounds.right + request.gap, "Adjacent item slots did not share the configured gap.");
    Pass("SceneGeometry_SearchLayoutUsesSharedColumnsWithoutHeaders");
}

void TestEmptyRegionsAndBoundsLookupUseTheSameSnapshot()
{
    auto request = RequestForScale(100);
    request.items.clear();
    request.emptyRegionIds = { L"custom-one", L"custom-two" };
    const auto scene = BuildScene(request);
    Require(scene.items.empty(), "Empty scene created item slots.");
    Require(scene.regions.size() == 2, "Empty scene did not retain explicit region headers.");
    Require(scene.contentHeight >= scene.regions.back().contentBounds.bottom, "Content height clipped the final region header.");
    Require(scene.FindItem(0) == nullptr, "Missing filtered index returned a geometry slot.");
    Pass("SceneGeometry_EmptyRegionsAndLookupShareSnapshot");
}

void TestDipChromeAndControlsShareScaledGeometry()
{
    for (const int dpi : { 96, 144, 192 })
    {
        MetricsRequest request;
        request.clientWidthPx = 1200 * dpi / 96;
        request.clientHeightPx = 800 * dpi / 96;
        request.dpi = dpi;
        request.textScalePercent = 100;
        request.spotlight = true;
        request.tileSizeDip = 128;
        request.gapDip = 16;
        const auto metrics = BuildSceneMetrics(request);
        Require(metrics.search.top == 34 * dpi / 96, "Search top was not derived from DIP.");
        Require(metrics.search.bottom - metrics.search.top == 50 * dpi / 96, "Search height was not derived from DIP.");
        Require(metrics.contentClip.top == metrics.search.bottom + 16 * dpi / 96,
            "Content clip diverged from shared search geometry.");
        Require(metrics.tileSizePx == 128 * dpi / 96, "Tile size was not derived from DIP.");
        Require(metrics.gapPx == 16 * dpi / 96, "Tile gap was not derived from DIP.");
        Require(metrics.searchText.left > metrics.search.left && metrics.searchText.right < metrics.search.right,
            "Search text bounds escaped the shared search control.");
    }
    Pass("SceneGeometry_DipChromeScalesAt100150200Percent");
}

void TestIncreasedTextExpandsControlsWithoutClippingOrHitDrift()
{
    MetricsRequest base;
    base.clientWidthPx = 1200;
    base.clientHeightPx = 800;
    base.dpi = 96;
    base.textScalePercent = 100;
    base.spotlight = false;
    base.tileSizeDip = 128;
    base.gapDip = 16;
    const auto normal = BuildSceneMetrics(base);

    base.textScalePercent = 200;
    const auto increased = BuildSceneMetrics(base);
    Require(increased.tileSizePx > normal.tileSizePx,
        "Increased text did not expand the shared tile draw/hit/navigation slot.");
    Require(increased.search.bottom - increased.search.top > normal.search.bottom - normal.search.top,
        "Increased text did not expand the search control.");
    Require(increased.feedback.bottom - increased.feedback.top > normal.feedback.bottom - normal.feedback.top,
        "Increased text did not expand transient feedback.");
    Require(increased.undo.left >= increased.feedback.left && increased.undo.right <= increased.feedback.right,
        "Undo hit target escaped the drawn feedback surface.");
    Require(increased.editControl.bottom - increased.editControl.top >= 44,
        "Edit control dropped below a 44 DIP-equivalent target.");
    Require(increased.contentClip.top > increased.search.bottom,
        "Expanded search control clipped content.");
    Pass("SceneGeometry_IncreasedTextExpandsSharedControlGeometry");
}

void TestSettingsColumnsAndRowsReflowForIncreasedText()
{
    SettingsMetricsRequest request;
    request.clientWidthPx = 1120;
    request.dpi = 192;
    request.textScalePercent = 200;
    const auto metrics = BuildSettingsMetrics(request);
    Require(metrics.labelWidthPx >= 520, "200% text left settings labels clipped their full names.");
    Require(metrics.inputWidthPx >= 400, "Expanded settings labels starved the input column.");
    Require(metrics.inputLeftPx + metrics.inputWidthPx <= request.clientWidthPx - 48,
        "Settings input column escaped the client bounds.");
    Require(metrics.rowHeightPx >= 120, "Settings rows did not expand for 200% text.");
    Require(metrics.controlHeightPx >= 80, "Settings controls did not expand for 200% text.");
    Require(metrics.fullWidthControlPx == request.clientWidthPx - 96,
        "Full-width settings controls did not retain the shared side insets.");
    Pass("SceneGeometry_SettingsColumnsAndRowsReflowAtIncreasedText");
}

void TestTextInputDialogScalesEveryControlAndRetainsMinimumTargets()
{
    TextInputMetricsRequest request;
    request.dpi = 192;
    request.textScalePercent = 200;
    const auto metrics = BuildTextInputMetrics(request);
    Require(metrics.clientWidthPx >= 752 && metrics.clientHeightPx > 276,
        "Text input dialog did not scale beyond its legacy client shell.");
    Require(metrics.label.left >= 36 && metrics.label.right < metrics.clientWidthPx,
        "Text input label did not retain scaled insets.");
    Require(metrics.edit.left == metrics.label.left && metrics.edit.right == metrics.label.right,
        "Text input edit and label columns diverged.");
    Require(metrics.edit.bottom - metrics.edit.top >= 88,
        "Text input edit dropped below its increased-text target height.");
    Require(metrics.ok.bottom - metrics.ok.top >= 96 && metrics.cancel.bottom - metrics.cancel.top >= 96,
        "Text input buttons dropped below increased-text target height.");
    Require(metrics.ok.right <= metrics.cancel.left,
        "Text input buttons overlapped after scaling.");
    Require(metrics.cancel.right <= metrics.clientWidthPx - metrics.sideInsetPx,
        "Text input controls escaped the dialog bounds.");
    Pass("SceneGeometry_TextInputDialogScalesAtDpiAndIncreasedText");
}
}

int main()
{
    try
    {
        TestDrawingHitTestingAndScrollingShareExactSlotsAtSupportedScales();
        TestSearchLayoutSuppressesRegionHeadersAndUsesStableColumns();
        TestEmptyRegionsAndBoundsLookupUseTheSameSnapshot();
        TestDipChromeAndControlsShareScaledGeometry();
        TestIncreasedTextExpandsControlsWithoutClippingOrHitDrift();
        TestSettingsColumnsAndRowsReflowForIncreasedText();
        TestTextInputDialogScalesEveryControlAndRetainsMinimumTargets();
        std::cout << passed << "/7 tests passed\n";
        return passed == 7 ? 0 : 1;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL " << exception.what() << "\n";
        return 1;
    }
}
