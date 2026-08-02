#include "SceneGeometry.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using llp::geometry::BuildScene;
using llp::geometry::HitTestItem;
using llp::geometry::LayoutItem;
using llp::geometry::LayoutRequest;

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
}

int main()
{
    try
    {
        TestDrawingHitTestingAndScrollingShareExactSlotsAtSupportedScales();
        TestSearchLayoutSuppressesRegionHeadersAndUsesStableColumns();
        TestEmptyRegionsAndBoundsLookupUseTheSameSnapshot();
        std::cout << passed << "/3 tests passed\n";
        return passed == 3 ? 0 : 1;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL " << exception.what() << "\n";
        return 1;
    }
}
