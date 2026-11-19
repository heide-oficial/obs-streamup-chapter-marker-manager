# StreamUP Chapter Marker Manager for OBS Studio

This is [heide-oficial's personal fork](https://github.com/heide-oficial/obs-streamup-chapter-marker-manager).
See [FORK.md](FORK.md) for the modal chapter picker, persistent last chapter,
per-type incremental names, native OBS chapter history, and build instructions.

Chapter markers and annotations for your OBS recordings. If you're making content for multiple platforms, this keeps track of the moments you want to come back to later. Drop a marker, keep recording, sort it out in the edit.

## Creating Chapter Markers

There are a few ways to do it depending on how you work:

- **Dock UI** - Type a name and hit save or just press Enter. Leave it blank and it'll use the default name.
- **Hotkeys** - Set up a default chapter hotkey, or create preset hotkeys with specific names for things you mark often.
- **WebSocket** - Full WebSocket support so tools like Streamer.Bot can create markers on your behalf.
- **Automatic** - Drops a chapter marker every time you switch scenes. You can set scenes to ignore so it doesn't go mental.

Incremental naming is built in too. Chapter 1, Chapter 2, and so on. No thinking required.

## Export Formats

Your markers can go wherever your editing software needs them:

- **Text file** - One line per chapter with timestamps. Simple.
- **FCP XML** - Frame-accurate markers for Final Cut Pro with NTSC drop-frame support.
- **Premiere XML** - Same deal but for Adobe Premiere Pro.
- **EDL** - Industry standard Edit Decision List. Colour-coded markers in DaVinci Resolve based on how the chapter was created (manual, hotkey, scene change, WebSocket).
- **Embedded in video** - Insert chapters directly into the recording file. Needs a compatible container like Hybrid MP4.

Files get saved alongside your recording automatically.

## Annotations

There's a separate Annotation dock for longer timestamped notes. Handy for jotting down thoughts during a recording that you want to reference in the edit. Annotations get written to the same export files as your chapters.

## Docks

Two docks get added to OBS:

- **Chapter Marker Dock** - The main one. Chapter input, current chapter display, settings, and optional chapter history.
- **Annotation Dock** - A text area for saving longer timestamped notes during recording.

## WebSocket API

Registered under the `streamup-chapter-manager` vendor with three requests:

| Request | What it does |
|---------|-------------|
| `setChapterMarker` | Creates a new chapter at the current timestamp |
| `getCurrentChapterMarker` | Returns the name of the active chapter |
| `setAnnotation` | Adds a timestamped annotation to the export files |

Also emits `ChapterMarkerSet` and `AnnotationSet` events so external tools can react to new markers in real time.

## Settings

All configurable from the gear icon in the dock:

- Default chapter name and incremental naming
- Show or hide chapter history
- Include the source (Manual, Hotkey, WebSocket, etc.) in chapter names
- Preset hotkeys for names you use often
- Which export formats to enable
- Auto chapter on scene change with ignored scene filtering

## Build

**In-tree build:**
1. Build OBS Studio: https://obsproject.com/wiki/Install-Instructions
2. Check out this repository to `frontend/plugins/obs-streamup-chapter-marker-manager`
3. Add `add_subdirectory(obs-streamup-chapter-marker-manager)` to `frontend/plugins/CMakeLists.txt`
4. Rebuild OBS Studio

**Stand-alone build (Linux only):**
1. Make sure you have the OBS development packages installed
2. Check out this repository and run `cmake -S . -B build -DBUILD_OUT_OF_TREE=On && cmake --build build`

## Guide

For the full walkthrough, check out the [guide on Notion](https://streamup.notion.site/OBS-Chapter-Marker-Manager-1567a7ed1438449cbbe8be87143568df?pvs=74).

Got feedback or requests? Drop them in the [StreamUP Discord](https://discord.com/invite/RnDKRaVCEu).

## Support

Built and maintained by Andi. If this has been useful, consider supporting to keep it going.

- [**Memberships**](https://andilippi.co.uk/pages/memberships) - Access all products and exclusive perks
- [**PayPal**](https://www.paypal.me/andilippi) - Buy me a beer
- [**Twitch**](https://www.twitch.tv/andilippi) - Come hang out and ask questions
- [**YouTube**](https://www.youtube.com/andilippi) - Tutorials on OBS and streaming
