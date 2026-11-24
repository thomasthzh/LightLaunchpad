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

int ScaleDip(int dip, int dpi)
{
    const int safeDpi = std::clamp(dpi, 48, 768);
    const long long scaled = static_cast<long long>(dip) * safeDpi;
    return static_cast<int>((scaled + (scaled >= 0 ? 48 : -48)) / 96);
}

int ScaleTextDip(int dip, int dpi, int textScalePercent)
{
    const int safeTextScale = std::clamp(textScalePercent, 100, 225);
    const long long scaled = static_cast<long long>(dip) * std::clamp(dpi, 48, 768) * safeTextScale;
    return static_cast<int>((scaled + 4800) / 9600);
}

SceneMetrics BuildSceneMetrics(const MetricsRequest& request)
{
    SceneMetrics metrics;
    metrics.dpi = std::clamp(request.dpi, 48, 768);
    metrics.textScalePercent = std::clamp(request.textScalePercent, 100, 225);
    const int clientWidth = std::max(1, request.clientWidthPx);
    const int clientHeight = std::max(1, request.clientHeightPx);
    const auto dip = [&](int value) { return ScaleDip(value, metrics.dpi); };
    const auto textDip = [&](int value) { return ScaleTextDip(value, metrics.dpi, metrics.textScalePercent); };

    const int baseTileDip = std::max(1, request.tileSizeDip);
    const int inferredIconDip = std::max(1, baseTileDip - 72);
    const int twoLineLabelDip = (38 * metrics.textScalePercent + 99) / 100;
    const int accessibleTileDip = inferredIconDip + 24 + twoLineLabelDip + 8;
    metrics.tileSizePx = std::max(1, dip(std::max(baseTileDip, accessibleTileDip)));
    metrics.gapPx = std::max(request.spotlight ? dip(10) : 0, dip(std::max(0, request.gapDip)));
    metrics.contentLeftPx = dip(request.spotlight ? 46 : 44);
    metrics.regionHeaderHeightPx = std::max(dip(38), textDip(30));
    metrics.cornerRadiusPx = dip(request.spotlight ? 26 : 34);
    metrics.searchFontPx = textDip(request.spotlight ? 21 : 26);
    metrics.tileFontPx = textDip(request.spotlight ? 13 : 15);
    metrics.headerFontPx = textDip(18);
    metrics.iconFontPx = textDip(17);

    if (request.spotlight)
    {
        const int outerPad = dip(28);
        const int searchTop = outerPad;
        const int searchHeight = std::max(dip(58), textDip(28) + dip(26));
        metrics.search = {
            outerPad,
            searchTop,
            std::max(outerPad, clientWidth - outerPad),
            std::min(clientHeight, searchTop + searchHeight)
        };
        metrics.auxiliary = { 0, 0, 0, 0 };
        const int searchLeftText = std::min(metrics.search.right, metrics.search.left + dip(52));
        const int searchRightText = std::max(searchLeftText, metrics.search.right - dip(20));
        metrics.searchText = {
            searchLeftText,
            metrics.search.top,
            searchRightText,
            metrics.search.bottom
        };
        const int contentTop = std::min(clientHeight, metrics.search.bottom + dip(2));
        metrics.contentClip = {
            outerPad + dip(10),
            contentTop,
            std::max(outerPad + dip(10), clientWidth - outerPad - dip(10)),
            std::max(contentTop, clientHeight - outerPad - dip(8))
        };
        metrics.initialContentTopPx = contentTop + dip(14);
    }
    else
    {
        const int searchTop = dip(34);
        const int searchHeight = std::max(dip(50), textDip(32) + dip(18));
        const int searchSideInset = dip(48);
        const int searchRightInset = dip(148);
        metrics.search = {
            searchSideInset,
            searchTop,
            std::max(searchSideInset, clientWidth - searchRightInset),
            std::min(clientHeight, searchTop + searchHeight)
        };
        const int searchTextInset = dip(22);
        metrics.searchText = {
            std::min(metrics.search.right, metrics.search.left + searchTextInset),
            metrics.search.top,
            std::max(metrics.search.left, metrics.search.right - searchTextInset),
            metrics.search.bottom
        };

        const int contentTop = std::min(clientHeight, metrics.search.bottom + dip(16));
        metrics.contentClip = {
            0,
            contentTop,
            clientWidth,
            clientHeight
        };
        metrics.initialContentTopPx = contentTop + dip(14);

        const int auxiliaryWidth = std::max(dip(48), textDip(32));
        const int auxiliaryHeight = std::max(dip(40), textDip(32));
        metrics.auxiliary = {
            std::max(0, clientWidth - dip(28) - auxiliaryWidth),
            std::max(0, clientHeight - dip(20) - auxiliaryHeight),
            std::max(0, clientWidth - dip(28)),
            std::max(0, clientHeight - dip(20))
        };
    }

    const int editWidth = std::max(dip(84), textDip(68));
    metrics.editControl = {
        std::max(0, clientWidth - dip(48) - editWidth),
        metrics.search.top,
        std::max(0, clientWidth - dip(48)),
        std::min(clientHeight, metrics.search.top + std::max(metrics.search.bottom - metrics.search.top, dip(44)))
    };

    const int feedbackWidth = std::min(dip(560), std::max(dip(280), clientWidth - dip(80)));
    const int feedbackHeight = std::max(dip(54), textDip(34) + dip(20));
    const int feedbackLeft = std::max(0, (clientWidth - feedbackWidth) / 2);
    const int feedbackBottom = std::max(feedbackHeight, clientHeight - dip(24));
    metrics.feedback = {
        feedbackLeft,
        std::max(0, feedbackBottom - feedbackHeight),
        std::min(clientWidth, feedbackLeft + feedbackWidth),
        std::min(clientHeight, feedbackBottom)
    };
    const int undoWidth = std::min(feedbackWidth, std::max(dip(92), textDip(72)));
    metrics.undo = metrics.feedback;
    metrics.undo.left = metrics.undo.right - undoWidth;
    return metrics;
}

SettingsMetrics BuildSettingsMetrics(const SettingsMetricsRequest& request)
{
    SettingsMetrics metrics;
    const int dpi = std::clamp(request.dpi, 48, 768);
    const int textScale = std::clamp(request.textScalePercent, 100, 225);
    const auto dip = [&](int value) { return ScaleDip(value, dpi); };
    const int clientWidth = std::max(1, request.clientWidthPx);
    const int labelWidthDip = std::max(150, (132 * textScale + 99) / 100);
    const int controlHeightDip = std::max(26, (16 * textScale + 99) / 100 + 8);
    const int checkboxHeightDip = std::max(24, (16 * textScale + 99) / 100 + 8);
    const int buttonHeightDip = std::max(30, (16 * textScale + 99) / 100 + 10);

    metrics.sideInsetPx = dip(24);
    metrics.labelWidthPx = dip(labelWidthDip);
    metrics.inputLeftPx = metrics.sideInsetPx + metrics.labelWidthPx + dip(10);
    const int inputRight = std::max(metrics.inputLeftPx, clientWidth - metrics.sideInsetPx);
    metrics.inputWidthPx = inputRight - metrics.inputLeftPx;
    metrics.fullWidthControlPx = std::max(0, clientWidth - metrics.sideInsetPx * 2);
    metrics.rowHeightPx = dip(std::max(38, (26 * textScale + 99) / 100 + 12));
    metrics.controlHeightPx = dip(controlHeightDip);
    metrics.checkboxHeightPx = dip(checkboxHeightDip);
    metrics.buttonHeightPx = dip(buttonHeightDip);
    return metrics;
}

TextInputMetrics BuildTextInputMetrics(const TextInputMetricsRequest& request)
{
    TextInputMetrics metrics;
    const int dpi = std::clamp(request.dpi, 48, 768);
    const int textScale = std::clamp(request.textScalePercent, 100, 225);
    const auto dip = [&](int value) { return ScaleDip(value, dpi); };
    const auto textDip = [&](int value) { return ScaleTextDip(value, dpi, textScale); };

    metrics.clientWidthPx = dip(376);
    metrics.sideInsetPx = dip(18);
    metrics.fontHeightPx = std::max(1, textDip(16));
    const int contentRight = metrics.clientWidthPx - metrics.sideInsetPx;
    const int labelHeight = std::max(dip(24), textDip(22));
    const int editHeight = std::max(dip(26), textDip(16) + dip(12));
    const int buttonHeight = std::max(dip(30), textDip(16) + dip(16));
    const int buttonWidth = std::max(dip(88), textDip(68) + dip(20));
    const int buttonGap = dip(16);

    metrics.label = {
        metrics.sideInsetPx,
        metrics.sideInsetPx,
        contentRight,
        metrics.sideInsetPx + labelHeight
    };
    metrics.edit = {
        metrics.sideInsetPx,
        metrics.label.bottom + dip(6),
        contentRight,
        metrics.label.bottom + dip(6) + editHeight
    };
    metrics.cancel = {
        contentRight - buttonWidth,
        metrics.edit.bottom + dip(16),
        contentRight,
        metrics.edit.bottom + dip(16) + buttonHeight
    };
    metrics.ok = {
        metrics.cancel.left - buttonGap - buttonWidth,
        metrics.cancel.top,
        metrics.cancel.left - buttonGap,
        metrics.cancel.bottom
    };
    metrics.clientHeightPx = metrics.cancel.bottom + metrics.sideInsetPx;
    return metrics;
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
