#pragma once

#include <string>
#include <vector>

namespace llp::geometry
{
struct Rect
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct LayoutItem
{
    int filteredIndex = -1;
    std::wstring regionId;
};

struct LayoutRequest
{
    int clientWidth = 0;
    int tileSize = 0;
    int gap = 0;
    int left = 0;
    int initialContentTop = 0;
    int regionHeaderHeight = 38;
    int scrollOffset = 0;
    bool groupByRegion = true;
    std::vector<LayoutItem> items;
    std::vector<std::wstring> emptyRegionIds;
};

struct ItemSlot
{
    int filteredIndex = -1;
    std::wstring regionId;
    Rect contentBounds;
    Rect viewportBounds;
};

struct RegionSlot
{
    std::wstring regionId;
    Rect contentBounds;
    Rect viewportBounds;
};

struct Scene
{
    int columns = 1;
    int contentHeight = 0;
    std::vector<ItemSlot> items;
    std::vector<RegionSlot> regions;

    const ItemSlot* FindItem(int filteredIndex) const;
    const RegionSlot* FindRegion(const std::wstring& regionId) const;
};

Scene BuildScene(const LayoutRequest& request);
int HitTestItem(const Scene& scene, int contentX, int contentY);
bool Intersects(const Rect& left, const Rect& right);
}
