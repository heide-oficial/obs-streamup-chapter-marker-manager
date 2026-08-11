# StreamUP Record Chapter Manager - Changelog

---

## v1.3.1 (09 May '26)
**Patch Focus:** macOS chapter embed and real Final Cut export
- Fixed hybrid MP4 chapter markers being silently dropped on macOS. They now embed into the recording properly.
- Final Cut Pro export is now real FCPXML 1.13, so modern Final Cut imports it straight away. The old format died with Final Cut Pro 7 and no current version could read it.
- Fixed marker timing drifting on fractional frame rates like 29.97. Timings now stay accurate all the way through the recording.
- Refreshed the dialog UI so it shares the StreamUP look.
- Settings now follow the active OBS theme.
- Dialogs scale with your operating system text-size setting for easier reading.
- Dialogs resize now. Grab any edge or corner and drag to the size you want.
- Dragging a dialog onto another monitor no longer leaves it blank until you let go of the mouse.
- Pressing Enter in a confirmation box now confirms it. Enter used to land on Cancel.
- Added an in-app Feedback button so you can send bugs and ideas straight from the plugin.

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
- Fixed "End" marker only being written to text export, now written to all enabled export formats
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
