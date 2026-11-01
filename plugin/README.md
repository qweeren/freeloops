# freeloops VST3 plugin

The freeloops sample browser as a VST3 instrument for FL Studio (Windows). The UI is the web app in
[`../ui`](../ui), compiled into the plugin and shown in a WebView2 browser. Native code (JUCE 8) handles
everything the page can't do itself:

| Native function | What it does |
|---|---|
| `getState` | Folders, favorites and every indexed sample |
| `addFolder` / `removeFolder` / `rescan` | Manage the library folders (stored in `%APPDATA%\freeloops\freeloops.settings`) |
| `preview` / `stopPreview` | Decode a sample and play it **through the plugin's output** |
| `getPeaks` | Waveform peaks and duration, computed in the background |
| `startDrag` | Start an OS file drag, so the sample drops into the Playlist or Channel Rack |
| `setFavorite`, `reveal` | Favorites; show the file in Explorer |

Events back to the page: `library` (after scans and folder changes), `status` (15×/s: host tempo, preview
progress), `dragEnded`.

Sources for now: your own sample folders. Online sources (Freesound, then Looperman and Sample Focus if their
terms allow it) come next. See [docs/PLAN.md](../docs/PLAN.md).

## Build

Needs Visual Studio Build Tools 2022 (C++). JUCE 8.0.15 and the WebView2 SDK are fetched on the first configure.
Uninstall the plugin from FL first if it's loaded: the post-build copy fails while FL has the DLL open.

```bash
CMAKE="/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" -S . -B build -G "Visual Studio 17 2022" -A x64
"$CMAKE" --build build --config Release
```

The build copies `freeloops.vst3` to `C:\Program Files\Common Files\VST3`. In FL Studio: **Options → Manage
plugins → Find installed plugins**, then Channel Rack **+** → *freeloops*.

## Working on the UI

The page runs in a normal browser too. Without the plugin, `ui/bridge.js` swaps in a mock library:

```bash
npx http-server ../ui -p 5178
```

Changes to `ui/` need a plugin rebuild to show up inside FL.

## FL Studio notes

- **No sound when previewing?** FL's *Smart disable* can switch off an idle generator. Turn it off in the plugin
  window's menu (▸ → Smart disable).
- Plugins can't add docked panels to FL. Use the plugin window as a floating window, or **Detached** (same menu)
  to put it on another monitor. Save it in a project template to have it in every project.
