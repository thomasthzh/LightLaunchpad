#pragma once
#include "MaterialRenderer.h"
#include <dwrite.h>

namespace llp::material
{
// Set by the isolated host before its next Show/Rebuild. Production headers and
// the existing scene/config/input contract remain unchanged.
void SetLiquidSpotlightMode(bool spotlight);
struct SpotlightCaptions
{
    std::wstring placeholder;
    std::wstring emptyQuery;
    std::wstring noResults;
};
// Host supplies its existing localized strings; the worker snapshots them at Show.
void SetLiquidSpotlightCaptions(SpotlightCaptions captions);
struct GlassVisualSettings
{
    float blurMix = 0.06f;
    float tintOpacity = 0.015f;
    float refractionPx = 30.0f;
    float rimHighlight = 0.95f;
};
// Live optical material parameters controlled from Settings.
void SetLiquidGlassSettings(GlassVisualSettings settings);
// Optional one-shot export of a settled GPU frame, separate from performance capture.
void RequestLiquidFrameExport(std::wstring path);
// One-shot dump of the presenter's existing public feedback, without UI control.
void RequestLiquidPresentExport(std::wstring path);
// The UI hit tests and the GPU foreground use the same shaping and font scale.
HRESULT CreateSpotlightSearchLayout(IDWriteFactory* factory, const std::wstring& text,
    float fontSizePx, float widthPx, float heightPx, IDWriteTextLayout** layout) noexcept;
}
