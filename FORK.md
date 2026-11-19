# Personal fork

Maintained at https://github.com/heide-oficial/obs-streamup-chapter-marker-manager.
Based on StreamUPTips/obs-streamup-chapter-marker-manager; original copyright and
GPL-2.0 license are retained. Changes dated 2026-11-19.

- The main dock has **New chapter** and a settings gear. The chapter button opens a modal picker of the
  default chapter type and custom presets. Select a type and click **Create chapter**
  (or double-click it). Cancel creates nothing.
- **Ctrl+Alt+N** opens the same picker from an OBS frontend hotkey, configurable in
  **Settings > Hotkeys > Open New chapter (Chapter Marker Manager)**. Press **1–9**
  or **0** for the first ten displayed types, using the number row or numpad.
  Each row shows its assigned number. Additional types remain selectable with
  arrows and Enter. Escape cancels; holding a number does not repeatedly create markers.
- Configure types at **Tools > Manage chapter types**. Existing presets and optional
  hotkeys still work. Configuration is at **Tools > Chapter Marker Manager settings**.
  The annotation dock is still available through OBS's Docks menu.
- Chapter sidecar files (`.txt`, `.xml`, `.fcpxml`, `.edl`) are written into a
  subfolder with the video's stem, next to the video. For `Videos/session.mp4`,
  all chapter exports go in `Videos/session/`; the video stays at its original path.
  Existing sidecars from older recordings are not moved automatically.
- **Last chapter created:** remains visible, including after recording stops. Errors
  and other temporary messages use a separate label. History is kept until the next
  recording starts.
- Enable **Use incremental custom chapter names** in settings for separate counters
  per custom type. They start at 1 on each recording. The existing default-name
  setting remains separate. Picker previews/cancels consume no numbers; history
  double-clicks reuse the original type, never the timestamp or numbered display row.
- Native OBS chapters are imported into history and enabled sidecar exports without
  adding them to the recording again. Their original generated names are preserved.
  Since OBS has no public chapter-added event, this uses the actual MP4/MOV output
  insertion lines from the current OBS log (OBS 30.2+ log format), filtered to the
  main recording output. Detection follows encoder/log flushing and may lag briefly.
  If future OBS versions change that log format, this adapter will need updating.
  File splitting remains an upstream limitation; this fork does not claim per-file
  sidecar synchronization for split recordings.

## Build and checks

The fork's Push workflow builds a Windows x64 plugin ZIP using the upstream build
scripts, and runs C++ regression checks for independent counters and native chapter
log parsing, export paths and the actual modal's keyboard events. Download the Windows artifact from the successful GitHub Actions run.
It replaces the original plugin module; do not load both copies in one OBS process.

Run the small Qt Core regression suite separately:

```sh
cmake -S tests -B build_tests
cmake --build build_tests
ctest --test-dir build_tests --output-on-failure
```
