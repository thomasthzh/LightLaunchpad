# Launchpad V2 Editable Regions Design

## Goal

Extend LightLaunchpad from a read-only shortcut grid into an editable launchpad with two interchangeable region views, a top-right gear menu, better icon quality, faster open response, and broader imports.

## View Modes

The app uses one shared layout model and two render modes:

- Inline Regions: regions appear as full-width bands inside the grid. This is the default.
- Region Tabs: regions appear as tabs/pills near the top. The selected region is shown as a clean grid.

The top-right gear button opens a compact menu with:

- View mode: Inline Regions or Region Tabs.
- Icon size: small, medium, large.
- Icon quality: balanced or high.
- Import Start Menu apps.
- Import `.vui` files.
- Settings.

Switching view mode must not change item order, region membership, or shortcuts.

## Editable Regions

The layout model contains:

- Regions: id, name, order.
- Items: source path, display name, target path, kind, region id, order.

Built-in regions:

- `All`: virtual view, not stored as a normal editable region.
- `Uncategorized`: default stored region for imported or unassigned items.

Region actions:

- Right-click empty launchpad area: create region.
- Right-click region title or tab: rename region, delete region.
- Deleting a region moves its items to `Uncategorized`; it does not delete shortcuts.

App actions:

- Drag apps to reorder.
- Drag apps into another region.
- Right-click app: launch, rename display name, remove from launchpad, open file location.
- Removing an app deletes or moves the launchpad shortcut file only after confirmation; it never uninstalls the app.

## Persistence

Keep the launchpad folder as the source of launchable files, but add layout metadata:

```text
%APPDATA%\LightLaunchpad\layout.json
```

The metadata stores region membership, display names, and order. If a file exists in the launchpad folder but not in metadata, it is added to `Uncategorized` at the end. If metadata points to a missing file, hide it until the file returns or the user removes the stale item.

## Icon And Visual Quality

Use larger shell icons when possible:

- Prefer 256px shell image list icons or high-resolution associated icons.
- Fall back to current `SHGetFileInfo` icon extraction.
- Cache icons by normalized path and file timestamp.
- Load icons lazily after the overlay is shown.

The app icon should use `爱丽丝.gif` as the source asset. Since Windows executable icons cannot be animated GIFs, generate or embed a static frame for the executable/tray/window icon, while optionally using the GIF as an in-app decorative icon in the settings/about surface.

The launchpad should look closer to macOS Launchpad:

- Cleaner icon spacing.
- Larger, sharper icons.
- Smooth but understated open animation.
- No heavy blur by default if it hurts performance.

## Performance Direction

Current measured baseline on this machine:

- About 85 MB working set at 0.5 seconds.
- About 124 MB working set after 3 seconds.

Observed root causes:

- Startup eagerly refreshes all items.
- View model eagerly extracts icons for all items.
- Opening the launchpad refreshes and rebuilds all item view models.

V2 performance changes:

- Do not extract all icons at startup.
- Load shortcut metadata first, icons second.
- Reuse item view models between refreshes when possible.
- Do not refresh the full list on every hotkey open unless the file watcher marked it dirty.
- Simplify animation to opacity plus small content translate, avoiding full-window scale.

Target:

- Hotkey-to-visible should feel immediate.
- Startup should return to idle quickly.
- Working set should be lower than the V1 124 MB baseline after idle.

## Imports

Start Menu import:

- Import `.lnk` and `.url` from user and common Start Menu folders.
- Deduplicate by normalized target path where resolvable, otherwise by source path.
- Preserve folder/category hints when possible by mapping parent folders to regions.

NeeView import:

- Import `C:\Users\thoma\AppData\Local\Microsoft\WindowsApps\NeeView.exe`.
- Prefer an existing Start Menu shortcut if found later; otherwise create a launchpad shortcut to the WindowsApps alias.

Existing `.vui` import remains available from the gear menu.

## Testing

Automated tests should cover:

- Layout metadata load/save defaults.
- New folder items are added to `Uncategorized`.
- Region rename/delete semantics.
- View mode setting round trips.
- Start Menu importer deduplicates shortcuts.

Manual checks:

- Gear menu switches Inline Regions and Region Tabs.
- Drag reorder persists after restart.
- Drag into region persists after restart.
- Right-click app remove does not uninstall anything.
- Right-click region delete moves items to `Uncategorized`.
- Start Menu and NeeView imports add expected entries.
- Open animation feels faster than V1.
