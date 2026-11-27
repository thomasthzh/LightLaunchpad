#pragma once

#include "InputStats.h"

#include <windows.h>

namespace llp::stats
{
// Registers the custom Direct2D stats dashboard child window class.
bool RegisterInputStatsPanelClass(HINSTANCE instance);

// Creates the Direct2D visual dashboard (4 KPI cards + 2D Keyboard Heatmap + 7-day trend).
HWND CreateInputStatsPanel(HWND parent, HINSTANCE instance, int controlId, int x, int y, int width, int height);

// Updates the dashboard's snapshot, theme, language, and triggers an immediate repaint.
void UpdateInputStatsPanel(HWND panelHwnd, const Snapshot& snapshot, bool darkMode, bool chinese, int dpi);
}
