# freeloops VST3 plugin

The freeloops sample browser as a VST3 instrument for FL Studio (Windows). The UI is the web app in
[`../ui`](../ui), compiled into the plugin and shown in a WebView2 browser. Native code (JUCE 8) handles
everything the page can't do itself:

| Native function | What it does |
|---|---|
| `getState` | Folders, favorites and every indexed sample |
| `addFolder` / `removeFolder` / `rescan` | Manage the library folders (stored in `%APPDATA%\freeloops\freeloops.settings`) |
| `preview` / `stopPreview` | Decode a sample and play it **through the plugin's output**: it starts once the first moments are decoded, on its own thread, and the last few auditioned stay decoded (up to 96 MB) |
| `getPeaks` | Waveform peaks and duration, computed in the background and kept in `%APPDATA%\freeloops\cache\peaks.bin` |
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
| `sfState` / `sfSetEnabled` | Sample Focus on/off (off by default), sign-in, credits and browser-check state |
| `sfSearch` | One page of Sample Focus search results, loaded through the session page |
| `sfPreview` | Fetch the sample's MP3 preview through the session page (to a cache) and play it through the plugin |
| `sfSignIn` / `sfSignOut` / `sfCheck` | Sample Focus's login page in a popup / sign out / samplefocus.com in a popup for Cloudflare's check |
| `sfDownload` | Download a sample with the user's account (one credit) into `Music\freeloops\Sample Focus` |
| `sfOpen` | Open the sample's page on samplefocus.com |

Events back to the page: `library` (after scans and folder changes; the sample list only when it changed), `status` (up to 20×/s, when something changed: host tempo, preview
progress), `dragEnded`, `lmState`, `fsState`, `sfState`.

Sources: your own sample folders, Freesound, and Looperman and Sample Focus (both opt-in). See
[docs/PLAN.md](../docs/PLAN.md).

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

## Sample Focus

Sample Focus has no public API, its downloads cost the user's own download credits, and its licence forbids
sharing samples on their own, so this source is **off by default** and the opt-in screen says so. Everything
goes through [`SampleFocusView`](Source/SampleFocusView.cpp): a WebView2 browser on samplefocus.com with its own
cookie store, kept 1×1 inside the plugin. The site sits behind Cloudflare, and its CDN only serves previews to
its own pages, so every request is made from that page, the way the site's own scripts make it.

- **Search** loads Sample Focus's public search pages (`/samples?search=…&min_tempo=…&key=A%23&mode=minor`),
  which carry their results as JSON for the page's player; [`SampleFocus.cpp`](Source/SampleFocus.cpp) parses
  that. One request at a time, searches at most one every 1.5 s, cached for the session. Sample Focus needs
  search words: an empty search shows a prompt instead of a request.
- **Previews** are the site's MP3 previews, fetched by the page into `%APPDATA%\freeloops\cache\samplefocus-previews`
  (capped at 300 MB) and played through the plugin. No account and no credits needed.
- **Downloads** need the user's own account. Signing in opens a popup on Sample Focus's real login page that
  shares the cookie store; freeloops never sees the password. A download is the site's own request
  (`POST /samples/<slug>/download` with the page's CSRF token), which spends one credit unless the user already
  has the sample, then the file it links to. The remaining credits show in the source bar. Files go to
  `Music\freeloops\Sample Focus` as `Name - 140bpm - Am - Author [sf585539].wav`; dragging a sample that isn't
  downloaded yet starts the drag as soon as it's saved, if the mouse button is still held.
- **Browser check:** if Cloudflare answers with its "Just a moment..." check, freeloops stops sending requests
  and asks the user to open Sample Focus in a popup and answer the check themselves. Nothing is retried until
  they do.
- `SampleFocusParseTest` checks the parser and the search URLs against a page you save yourself (Sample Focus's
  pages aren't stored here):

```bash
curl -sL "https://samplefocus.com/samples?search=trap" -o samples.html
build/SampleFocusParseTest_artefacts/Release/SampleFocusParseTest.exe samples.html
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
