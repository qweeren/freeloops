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

| `lmState` / `lmSetEnabled` | Looperman on/off (off by default) and sign-in state |
| `lmSearch` | One page of Looperman search results |
| `lmPreview` | Audition a loop: download it with the user's session (to a cache) and play it through the plugin |
| `lmSignIn` / `lmSignOut` | Show Looperman's own login page inside the plugin / sign out |
| `lmDownload` | Download a loop with the user's signed-in session into `Music\freeloops\Looperman` |
| `lmOpen` | Open the loop's page on looperman.com |
| `fsState` / `fsSetCredentials` | Freesound API key and sign-in state / save the user's client ID and API key |
| `fsSearch` | One page of Freesound search results; BPM and key filters are applied by Freesound |
| `fsPreview` | Fetch the sound's HQ Ogg preview (to a cache) and play it through the plugin |
| `fsSignIn` / `fsSignOut` | Freesound's OAuth2 page in a popup / forget the sign-in |
| `fsDownload` | Download the original file with the user's account into `Music\freeloops\Freesound` |
| `fsOpen`, `fsOpenApply`, `fsCopyCredit` | Open the sound's page / the API key form; copy a sound's attribution |

Events back to the page: `library` (after scans and folder changes), `status` (15×/s: host tempo, preview
progress), `dragEnded`, `lmState`, `fsState`.

Sources: your own sample folders, Freesound, and Looperman (opt-in). See [docs/PLAN.md](../docs/PLAN.md).

## Freesound

Freesound has an official API ([`Freesound.cpp`](Source/Freesound.cpp)). Each user brings their own API
credentials, because a key is limited to 2,000 requests a day: the Freesound screen walks them through
[freesound.org/apiv2/apply](https://freesound.org/apiv2/apply) and stores the client ID and API key in
`freeloops.settings`. Personal builds can compile credentials in instead:
`-DFREELOOPS_FREESOUND_CLIENT_ID=... -DFREELOOPS_FREESOUND_API_KEY=...`.

- **Search** uses the API key only. Requests run one at a time, at most one every 1.1 s (Freesound allows 60 a
  minute), are cached for the session, and name freeloops in the User-Agent. BPM and key filters go to
  Freesound (`bpm:[137 TO 143]`, `tonality:"A minor"`), since most sounds have no tempo.
- **Previews** are Freesound's public HQ Ogg files, fetched into `%APPDATA%\freeloops\cache\freesound-previews`
  (capped at 300 MB) and played through the plugin. No account needed.
- **Downloads** of the original file need OAuth2. [`FreesoundSignIn`](Source/FreesoundSignIn.cpp) opens Freesound's
  own authorize page in a popup; freeloops never sees the password. The redirect back is caught before it loads
  (whatever callback URL the credentials name), the code is exchanged for tokens, and the refresh token is saved
  in `freeloops.settings` so the sign-in lasts. Files go to `Music\freeloops\Freesound` as
  `Name - 120bpm - Am - author [fs415987].wav`.
- **Licences:** every result shows its licence (CC0, CC BY, CC BY-NC). Each download adds the sound's
  attribution to `CREDITS.txt` in that folder, and **C** copies the selected sound's attribution.
- `FreesoundParseTest` checks the reply parser and the search filters against a hand-written sample reply, or a
  real one with `--live`:

```bash
build/FreesoundParseTest_artefacts/Release/FreesoundParseTest.exe tests/freesound-search.json
```

## Looperman

Looperman has no API, and its terms restrict downloaders and automated access, so this source is **off by
default** and the opt-in screen says so. How it behaves:

- **Search** loads Looperman's public search pages ([`Looperman.cpp`](Source/Looperman.cpp)). Requests run one
  at a time, at most one every 1.5 s, are cached for the session, and name freeloops in the User-Agent.
  Results load a page at a time as you scroll.
- **Downloads** need the user's own Looperman account. [`LoopermanView`](Source/LoopermanView.cpp) is a
  WebView2 browser on looperman.com with its own cookie store, kept 1×1 inside the plugin. Signing in opens a
  popup window on Looperman's real login page that shares that cookie store; freeloops never sees the password.
  To download, the signed-in page opens the loop's page, follows its download button, and hands the file back.
- **Previews:** Looperman refuses direct requests for its MP3 previews (HTTP 403, hotlink protection), and
  freeloops doesn't work around that. Instead, auditioning a loop downloads it with the user's session into a
  cache (`%APPDATA%\freeloops\cache\looperman-auditions`, capped at 500 MB) and plays the real file through
  the plugin. Each audition counts as a download on Looperman. Previewing needs sign-in.
- **Keeping a loop** (dragging it or clicking ⤓) moves it to `Music\freeloops\Looperman` as
  `Title - 140bpm - Am - Author [lm123456].wav`; FL projects refer to that path. Dragging a loop that isn't
  downloaded yet starts the drag as soon as it's saved, if the mouse button is still held.
- The page markup can change at any time. `LoopermanParseTest` checks the parser against a page you save
  yourself (Looperman's HTML isn't stored here):

```bash
curl -sL "https://www.looperman.com/loops?keys=trap" -o loops.html
build/LoopermanParseTest_artefacts/Release/LoopermanParseTest.exe loops.html
```

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
