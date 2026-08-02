#include "SceneGeometry.h"

#include <algorithm>

namespace llp::geometry
{
namespace
{
Rect ViewportRect(const Rect& content, int scrollOffset)
{
    return { content.left, content.top - scrollOffset, content.right, content.bottom - scrollOffset };
}

bool Contains(const Rect& rect, int x, int y)
{
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}
}

const ItemSlot* Scene::FindItem(int filteredIndex) const
{
    const auto found = std::find_if(items.begin(), items.end(), [&](const ItemSlot& slot) {
        return slot.filteredIndex == filteredIndex;
    });
    return found == items.end() ? nullptr : &*found;
}

const RegionSlot* Scene::FindRegion(const std::wstring& regionId) const
{
    const auto found = std::find_if(regions.begin(), regions.end(), [&](const RegionSlot& slot) {
        return slot.regionId == regionId;
    });
    return found == regions.end() ? nullptr : &*found;
}

Scene BuildScene(const LayoutRequest& request)
{
    Scene scene;
    const int tileSize = std::max(1, request.tileSize);
    const int gap = std::max(0, request.gap);
    const int left = std::max(0, request.left);
    const int right = std::max(left, request.clientWidth - left);
    const int availableWidth = std::max(1, right - left);
    scene.columns = std::max(1, (availableWidth + gap) / (tileSize + gap));
    scene.items.reserve(request.items.size());
    scene.regions.reserve(request.items.empty() ? request.emptyRegionIds.size() : request.items.size());

    int column = 0;
    int x = left;
    int y = std::max(0, request.initialContentTop);
    int contentBottom = y;
    std::wstring activeRegion;

    for (const auto& item : request.items)
    {
        if (request.groupByRegion && activeRegion != item.regionId)
        {
            if (column != 0)
            {
                y += tileSize + gap;
                column = 0;
                x = left;
            }
            activeRegion = item.regionId;
            const Rect content = { left, y, right, y + std::max(1, request.regionHeaderHeight) };
            scene.regions.push_back({ activeRegion, content, ViewportRect(content, request.scrollOffset) });
            y = content.bottom;
            contentBottom = std::max(contentBottom, y);
        }

        const Rect content = { x, y, x + tileSize, y + tileSize };
        scene.items.push_back({ item.filteredIndex, item.regionId, content, ViewportRect(content, request.scrollOffset) });
        contentBottom = std::max(contentBottom, content.bottom);
        x += tileSize + gap;
        ++column;
        if (column >= scene.columns)
        {
            column = 0;
            x = left;
            y += tileSize + gap;
        }
    }

    if (request.groupByRegion && !request.emptyRegionIds.empty())
    {
        if (column != 0)
        {
            y += tileSize + gap;
            column = 0;
        }
        for (const auto& regionId : request.emptyRegionIds)
        {
            const Rect content = { left, y, right, y + std::max(1, request.regionHeaderHeight) };
            scene.regions.push_back({ regionId, content, ViewportRect(content, request.scrollOffset) });
            y = content.bottom;
            contentBottom = std::max(contentBottom, y);
        }
    }

    scene.contentHeight = contentBottom + 44;
    return scene;
}

int HitTestItem(const Scene& scene, int contentX, int contentY)
{
    const auto found = std::find_if(scene.items.begin(), scene.items.end(), [&](const ItemSlot& slot) {
        return Contains(slot.contentBounds, contentX, contentY);
    });
    return found == scene.items.end() ? -1 : found->filteredIndex;
}

bool Intersects(const Rect& left, const Rect& right)
{
    return left.left < right.right
        && left.right > right.left
        && left.top < right.bottom
        && left.bottom > right.top;
}
}
