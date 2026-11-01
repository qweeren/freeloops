# freeloops

A free, open-source sample browser for producers. Search, preview and drag samples straight into your DAW,
the way Splice and Loopcloud work, but with no subscription.

freeloops runs **inside FL Studio** as a VST3 plugin (Windows). It indexes your own sample folders, reads BPM
and key from file names, plays previews through FL's mixer, and lets you drag any sample into the Playlist or
Channel Rack. It also searches Freesound (with your own free API key) and, if you turn it on, Looperman.
See the [development plan](docs/PLAN.md).

> Status: early development. Works with local sample folders in FL Studio 2026 on Windows.

## Repository

| Folder | What's in it |
|---|---|
| [`plugin/`](plugin) | The VST3 plugin (C++, JUCE 8 + WebView2). Build and install steps are in its README. |
| [`ui/`](ui) | The browser UI (HTML/CSS/JS). It runs inside the plugin, or in a normal browser with mock data. |
| [`docs/`](docs) | The development plan and research notes. |
| [`spike/`](spike) | Throwaway experiments that proved drag-and-drop into FL Studio works. |

## License

freeloops is free software: you can redistribute it and/or modify it under the terms of the
**GNU Affero General Public License, version 3 or (at your option) any later version**, as published by the
Free Software Foundation. See [`LICENSE`](LICENSE).

freeloops is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
more details.

### Third-party code

The plugin is built with these, fetched at build time and not stored in this repository:

- [JUCE](https://github.com/juce-framework/JUCE) 8, used under its AGPLv3 option.
- The [Microsoft WebView2 SDK](https://www.nuget.org/packages/Microsoft.Web.WebView2), under its own licence
  (included in the NuGet package).
- The Steinberg VST3 SDK, bundled with JUCE, under its own licence.

### Samples

freeloops does not host, mirror or redistribute anyone's samples. It works with files on your own machine, and
each sample stays under the licence its creator gave it.
