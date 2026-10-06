# Meitou Tracker

A minimal RE_Kenshi runtime plugin for Kenshi that remembers discovered Meitou weapons by their runtime hand and reports their current or last-known location.

## v0.1

- F9 — scan and show a report. The complete report is written to the Kenshi log.
- F10 — toggle automatic tracking. Automatic scans run every 1.5 seconds while enabled.
- F11 — clear the temporary location markers created by the tracker.
- Tracks Meitou weapons found on the ground and in nearby character/building inventories.
- Player-party inventories are scanned regardless of distance.
- Newly discovered or relocated Meitou weapons create a native Kenshi map debug marker at their last observed position.
- Each tracked weapon keeps its runtime handle, item name, string ID, holder/status, and last-known coordinates.

### Important limitation

This is a loaded-world tracker, not a save-file oracle. Kenshi only exposes currently loaded world objects to the runtime queries used here. When a Meitou leaves the loaded area, the plugin keeps the last observation rather than pretending to know its new location.

### Meitou detection

The plugin uses KenshiLib's Weapon::getLevel() and treats level 100 as Meitou quality.

## Installation

1. Install a compatible RE_Kenshi release.
2. Create Kenshi/mods/Meitou Tracker/.
3. Copy:
   - Meitou Tracker.mod
   - RE_Kenshi.json
   - MeitouTracker.dll
4. Enable Meitou Tracker in Kenshi's mod list.

## Build

The project targets Release|x64 and links against KenshiLib.

Set these environment variables:

- KENSHILIB_DIR — KenshiLib SDK root.
- BOOST_INCLUDE_PATH — Boost 1.60 root.

The included GitHub Actions workflow uses the same KenshiLib/Boost dependency bundle as the other RE_Kenshi plugin projects and produces an installable ZIP artifact.

## Logs

F9 writes entries containing the Meitou name, string ID, runtime hand, status, holder, and x/y/z position.

When the item is no longer visible to the current scan, the report says last known.

## License

MIT.
