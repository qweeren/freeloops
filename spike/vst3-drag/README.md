# Phase 4 spike: drag-out from a VST3 plugin inside FL Studio (JUCE)

Throwaway test for the riskiest part of Phase 4 in [docs/PLAN.md](../../docs/PLAN.md):
*can a plugin window inside FL Studio start a file drag that lands in the Playlist?*

A bare JUCE 8 instrument plugin (outputs silence). It has no web UI yet. The drag uses
`DragAndDropContainer::performExternalDragDropOfFiles`, started from native code.
It also shows the host tempo, which Phase 4 needs for auto-stretch previews.

## Build

Needs Visual Studio Build Tools 2022 (C++), which includes CMake. JUCE 8.0.15 is fetched on the first configure.

```bash
CMAKE="/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" -S . -B build -G "Visual Studio 17 2022" -A x64
"$CMAKE" --build build --config Release
```

Output:

- `build/FreeloopsSpike_artefacts/Release/VST3/freeloops spike.vst3`
- `build/FreeloopsSpike_artefacts/Release/Standalone/freeloops spike.exe` (quick check outside FL)

Each build also copies the bundle to `C:\Program Files\Common Files\VST3\freeloops spike.vst3`
(`COPY_PLUGIN_AFTER_BUILD`). On this machine that folder is writable without admin; on a stock
Windows install it isn't, and the copy step fails.

Close the plugin in FL Studio before rebuilding. FL keeps the DLL locked while it's loaded, so the copy fails.

## Load it in FL Studio

1. **Options → Manage plugins → Find installed plugins**. *freeloops spike* (by freeloops) appears as a generator.
2. Channel Rack **+** → *freeloops spike*.

Don't point FL at the build folder with a custom search path. FL Studio's plugin manager labels custom
paths as **VST** (VST2) and scans them for `.dll` files, so it never finds a `.vst3` bundle there.
Use the standard `Common Files\VST3` folder.

## What to test

The plugin lists:

- `freeloops - Kick Loop (stereo) - 140bpm.wav`, written by JUCE's own WAV writer into `%APPDATA%\freeloops\samples`.
- Anything you add with **Add file...**.

If you launch anything from Claude's tools (the desktop app is an MSIX package), new files it writes under
`%APPDATA%` land in `%LOCALAPPDATA%\Packages\Claude_*\LocalCache\Roaming\` instead. FL Studio can't see them,
and it silently ignores a drop with that path. That's why the Electron spike's generated loops never dropped.
Start apps yourself, or write test files outside `AppData`.

Drag rows into the **Playlist** and the **Channel Rack**. The log at the bottom shows drag start and finish.
Also press play in FL and check that the header shows the project tempo.

Things to watch for, since they are known trouble spots for plugins on other hosts (§5, Phase 4):

- closing the plugin window during or right after a drag
- saving and reloading the project with the plugin in it
