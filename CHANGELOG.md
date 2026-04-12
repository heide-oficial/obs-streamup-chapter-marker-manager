# StreamUP Record Chapter Manager - Changelog

---

## v1.3.0 (12 Apr '26)
**Patch Focus:** StreamUP UI design system & settings redesign
- Migrated all settings dialogs to the StreamUP frameless design system with Catppuccin Mocha theming
- Replaced all QCheckBox controls with SwitchWidget toggles for visual consistency across StreamUP plugins
- Replaced QDialogButtonBox with styled buttons in all dialogs (settings, preset chapters, ignored scenes)
- Added palette() styling to dock list widgets for OBS theme compatibility
- Settings dialog now uses a two-column layout for better use of space
- Switched all dialogs from blocking exec() to non-blocking show/raise/activateWindow
- Added spacing between annotation button and settings button in the dock toolbar

## v1.2.1 (21 Mar '26)
**Patch Focus:** Export fixes & stability improvements
- Fixed FCP XML export producing invalid XML structure that could not be parsed by Final Cut Pro or third-party tools
- Fixed FCP XML missing required file URL format and name elements
- Fixed EDL timecodes being offset by 1 hour in DaVinci Resolve
- Fixed EDL header always reporting non-drop frame even when recording at 29.97/59.94 fps
- Fixed "Start" chapter marker not being embedded into the MP4 video file
- Fixed "End" marker only being written to text export — now written to all enabled export formats
- Fixed special characters in chapter names breaking XML exports
- Fixed potential crash when accessing chapter name from WebSocket while a chapter is being set
- Fixed potential crash from dangling string pointers when saving settings
- Fixed potential division by zero when retrieving frame rate
- Fixed default chapter name resetting to blank on first launch
- Improved thread safety for hotkey and WebSocket callbacks
- Added null safety checks throughout WebSocket and hotkey handlers
- Reduced code duplication across export format writers

## v1.2.0 (27 Oct '25)
**Patch Focus:** Export formats & workflow polish
- Added Final Cut Pro XML and Premiere Pro XML export options with NTSC handling and marker metadata
- Added Edit Decision List (EDL) export for DaVinci Resolve workflows
- Added a chapter history sidebar
- Fixed recording timestamp drift and ensured export files close cleanly when recordings stop
- Refined the dock UI, including a dedicated settings icon and spacing tweaks
- Added en-GB locale entries and translated the new export controls

## v1.1.0 (20 Jul '24)
**Patch Focus:** Scene collection reliability & incremental naming
- Added optional incremental chapter naming and re-enabled StreamUP Vertical support
- Added dedicated Aitum Vertical chapter marker integration
- Fixed duplicated hotkeys when changing scene collections

## v1.0.2 (04 Jul '24)
**Patch Focus:** Live websocket events
- Added obs-websocket emitter support for chapter markers

## v1.0.1 (24 Jun '24)
**Patch Focus:** Quality-of-life fixes
- Added a support link, improved label layout, and corrected OBS module text
- Prevented export files from being created when their toggles are disabled
- Fixed build configuration issues across platforms

## v1.0.0 (19 Jun '24)
**Initial Release**
Core functionality for recording chapter markers during OBS recordings, exporting chapter lists, and managing annotations within a dedicated dock.
