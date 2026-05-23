# Native UI Optimization And Refactor Notes

## Current Performance Position

- NativeUi is still the correct route for the 10 MB preferred / 15 MB acceptable / 20 MB hard-limit target.
- The hot path is a single Win32 process with lazy icon loading, Direct2D/DirectWrite primary rendering, GDI fallback, and static MinGW runtime linking.
- Recent clean-path probes remain around 11 MB Working Set and under 2 MB Private Memory, so we can add small DWM and search features without changing architecture.

## Near-Term Optimizations

- Cache search scoring per query while typing. Current ranked search is lightweight enough for typical app counts, but `SearchScore` recomputes during `stable_sort`.
- Avoid duplicated layout traversal in Direct2D and GDI paint paths. Both renderers independently perform region/header/tile layout logic.
- Move visible tile layout into a shared `LayoutPass` data structure containing tile rects, headers, hit targets, and content height.
- Keep icon alpha-bound caching in memory only, but prune it on icon destroy and reload, which is already done.

## Small Refactor Route

- Extract pure layout/search helpers from `LightLaunchpad.NativeUi.cpp` into `NativeUiLayout.*` and `NativeUiSearch.*`.
- Keep Win32 message handling, rendering, settings, import, and file operations in the current file.
- Effect: easier tests for search ranking, tab completion, hit testing, and scroll estimates without increasing runtime memory.

## Medium Refactor Route

- Split the native UI into five modules: `NativeUiState`, `NativeUiLayout`, `NativeUiSearch`, `NativeUiRender`, and `NativeUiWin32`.
- Make Direct2D and GDI consume the same layout pass.
- Effect: less duplicate rendering logic, safer future UI changes, and likely lower CPU spikes during repaint because layout is computed once.

## Full Refactor Route

- Replace the monolithic immediate-mode renderer with a retained lightweight scene model: search surface, region headers, tiles, drag overlays, and selection overlay.
- Add an icon cache service with bounded LRU handles and optional disk thumbnail cache.
- Effect: cleaner architecture and better scalability for very large app sets, but more code and more risk to the current low-memory profile.

## Recommendation

Stay on the small refactor route next. The app is still inside the memory target, so the best next improvement is to reduce duplication and make search/layout testable before changing the renderer architecture.
