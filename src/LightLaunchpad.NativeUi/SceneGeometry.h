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

struct MetricsRequest
{
    int clientWidthPx = 0;
    int clientHeightPx = 0;
    int dpi = 96;
    int textScalePercent = 100;
    bool spotlight = false;
    int tileSizeDip = 128;
    int gapDip = 16;
};

struct SceneMetrics
{
    int dpi = 96;
    int textScalePercent = 100;
    int tileSizePx = 0;
    int gapPx = 0;
    int contentLeftPx = 0;
    int initialContentTopPx = 0;
    int regionHeaderHeightPx = 0;
    int cornerRadiusPx = 0;
    int searchFontPx = 0;
    int tileFontPx = 0;
    int headerFontPx = 0;
    int iconFontPx = 0;
    Rect search;
    Rect searchText;
    Rect contentClip;
    Rect auxiliary;
    Rect editControl;
    Rect feedback;
    Rect undo;
};

struct SettingsMetricsRequest
{
    int clientWidthPx = 0;
    int dpi = 96;
    int textScalePercent = 100;
};

struct SettingsMetrics
{
    int sideInsetPx = 0;
    int labelWidthPx = 0;
    int inputLeftPx = 0;
    int inputWidthPx = 0;
    int fullWidthControlPx = 0;
    int rowHeightPx = 0;
    int controlHeightPx = 0;
    int checkboxHeightPx = 0;
    int buttonHeightPx = 0;
};

struct TextInputMetricsRequest
{
    int dpi = 96;
    int textScalePercent = 100;
};

struct TextInputMetrics
{
    int clientWidthPx = 0;
    int clientHeightPx = 0;
    int sideInsetPx = 0;
    int fontHeightPx = 0;
    Rect label;
    Rect edit;
    Rect ok;
    Rect cancel;
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
int ScaleDip(int dip, int dpi);
int ScaleTextDip(int dip, int dpi, int textScalePercent);
SceneMetrics BuildSceneMetrics(const MetricsRequest& request);
SettingsMetrics BuildSettingsMetrics(const SettingsMetricsRequest& request);
TextInputMetrics BuildTextInputMetrics(const TextInputMetricsRequest& request);
int HitTestItem(const Scene& scene, int contentX, int contentY);
bool Intersects(const Rect& left, const Rect& right);
}
