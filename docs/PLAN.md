# freeloops — Development Plan

> A free, open-source sample browser for producers. Search, preview and drag-and-drop loops from free sources straight into your DAW, the way Splice and Loopcloud work, but without a subscription.

*Status: planning · Primary target: FL Studio on Windows · Last updated: 30 Oct 2026*

---

## 1. What freeloops is

freeloops is a sample/loop browser that sits next to your DAW. You type a search ("dark trap melody 140bpm"), filter by BPM, key, category and length, audition results instantly, and drag the one you like into the Channel Rack or Playlist.

It aggregates **free** libraries behind one interface:

| Source | Content | Access method |
|---|---|---|
| **Freesound** | CC-licensed sounds and one-shots | Official REST API (safe) |
| **Looperman** | Free user-made loops, acapellas | No official API (see §3 and §8) |
| **Sample Focus** | Free community samples | No official API (see §3 and §8) |
| **Local folders** | The user's own library | Filesystem |

### Goals
- Search → preview → drag into DAW in under 3 seconds, with no browser tab and no manual unzip.
- Filter by BPM, key, instrument type and length.
- Auto-sync previews to project tempo (stretch to match) so loops audition in context.
- Favorites, history and collections.
- Free and open source. Modular source "providers" so new libraries can be added by anyone.

### Non-goals
- Hosting, mirroring or re-distributing anyone's samples. freeloops is a **browser and downloader**, not a library.
- Selling or paywalling content.
- Bypassing logins, paywalls or rate limits of any source.
- Replacing a full sample manager (tagging, cloud sync) in v1.

---

## 2. Feasibility research

### 2.1 Can a plugin drag audio files *out* into FL Studio?

**Yes, this is the supported path.** JUCE provides `DragAndDropContainer::performExternalDragDropOfFiles()`, which starts an OS-level file drag from a plugin window to the host or the filesystem.

- A JUCE forum report lists VST3 as working in the latest FL Studio, Studio One and Reaper, with Logic working for AU and Ableton failing for VST3. Ableton workaround: drop to desktop first.
- A separate JUCE thread reports drag-to-MIDI from a plugin works in FL, Ableton, Logic, Reaper, Pro Tools and GarageBand.
- Drag-out has caveats in some DAWs: reported crashes in Ableton after file dialogs, and flaky drops in Studio One. FL Studio is the best-reported host, which suits our target.

### 2.2 Can a plugin accept drags *in* from FL Studio?

**Unreliable, and we don't need it.** JUCE forum threads report that FL Studio's browser drops can behave differently from other hosts, and that dropping from Edison triggers `DropLeave` instead of `Drop`. freeloops only needs drag-**out**, so this is a non-issue. Do not build features that depend on dropping DAW audio onto the plugin (e.g. "drop your loop to find similar") in v1.

### 2.3 Can the plugin UI be web-based?

**Yes, with a known trap.** A JUCE forum thread on WebView-based plugins reports that drag-out works if you pass the editor/WebView as the source component, but a JavaScript `dragstart` listener can interfere with the native file drag (the native drag never starts properly on macOS). Rule: **do not rely on HTML5 drag events for the file drag**. Detect the gesture in JS, send a message to the native side, and let native code start the drag.

### 2.4 Can freeloops fetch audio from Looperman and Sample Focus?

**Technically yes, but unofficially.**
- Neither site offers a public API that I could find. A 2019 Ruby gem exists for scraping Looperman, which shows it has been done, but it is old and tells us nothing about current page structure or terms.
- Scrapers are brittle. Any HTML change breaks them, so each source needs an isolated, replaceable adapter and tests.
- **Unverified and must be checked by hand before building:** whether previews are public MP3 URLs, whether downloads require a logged-in session, and whether there are bot protections (Cloudflare, captchas, rate limits).

### 2.5 Licensing facts found

- **Sample Focus** license: allows personal, commercial and client use, including in music, games and apps. It forbids redistributing the material "alone — even for free," placing it on any website in downloadable/archived form, or including it in sample packs. → freeloops **must not cache-and-serve files to other people or host a mirror.** Per-user local download is the safe pattern.
- **Looperman** FAQ: uploaders grant other **Looperman users** a royalty-free licence under Looperman's terms and conditions, and Looperman itself does not issue licences. → The licence is granted to users of the site, so freeloops should download using the **user's own account/session** rather than a shared one. Read the full T&Cs before launch; I could not retrieve them in this research.
- **Freesound**: official API. Previews (MP3/OGG) need no OAuth. Full-quality download needs OAuth2. Default limits are 60 requests/min and 2,000/day per key (per a third-party client's docs; verify against the official docs). Each sound carries its own Creative Commons licence (CC0 / CC-BY / CC-BY-NC), which the UI must show.

### 2.6 Verdict

| Question | Answer |
|---|---|
| Is an in-DAW VST3 plugin technically feasible for FL Studio? | **Yes**, for drag-out |
| Is it *simple*? | **No.** C++/JUCE, host quirks, crash risk inside the DAW process, installer/signing work |
| Is the data-source side the harder problem? | **Yes.** Scraping fragility and ToS matter more than the plugin shell |

**Decision: build in two phases that share one core.**
1. **Phase A: standalone app (primary deliverable).** Faster to build, no risk of crashing the user's DAW, and drag-and-drop from a normal window into FL Studio is the best-supported interaction.
2. **Phase B: VST3 plugin shell (stretch goal).** Reuses the same UI and core. It only adds the "lives inside FL Studio" convenience and tempo sync from the host.

If Phase B proves too fragile, the standalone app is still a complete product.

---

## 3. Architecture

```
┌────────────────────────── freeloops ──────────────────────────┐
│                                                               │
│   UI (web: React/Svelte)   ←── shared by app and plugin ──→   │
│        │  search, filters, waveform, preview, favorites       │
│        ▼                                                      │
│   Core service (Rust, runs as library or local sidecar)       │
│     • Provider layer  (Freesound | Looperman | SampleFocus | Local)
│     • Preview player + cache (temp, per-user, size-capped)    │
│     • Download manager (resolve → fetch → verify → cache)     │
│     • Metadata: BPM/key parsing + optional analysis           │
│     • Settings, favorites, history (SQLite)                   │
│        │                                                      │
│        ▼                                                      │
│   Native shell                                                │
│     • Phase A: Tauri window  → native file drag-out           │
│     • Phase B: JUCE VST3 plugin → performExternalDragDropOfFiles
└───────────────────────────────────────────────────────────────┘
```

### 3.1 Provider interface (the key abstraction)

Every source implements the same contract so that a broken scraper never breaks the app:

```
Provider {
  id, displayName, requiresLogin, licenseInfo
  search(query, filters, page) -> Page<SampleMeta>
  getPreviewUrl(sample)        -> URL
  resolveDownload(sample, auth)-> DownloadSource
  healthCheck()                -> ok | degraded | broken
}
SampleMeta { id, title, author, bpm?, key?, tags[], duration, license, sourceUrl, previewUrl }
```

- Providers can be toggled off individually. A broken provider shows "temporarily unavailable" and nothing else.
- Each scraping provider has **recorded HTML fixtures and tests**, so a site change is caught by CI rather than users.
- Always show a link back to the original page (attribution and traffic for the sources).

### 3.2 The drag-out flow (the critical path)

Native drag APIs need a **real file path at the moment the drag starts**, so the file must already exist locally.

1. User clicks or hovers a result → preview streams, and a **background prefetch** of the full file starts.
2. User presses and drags → if the file is cached, native drag starts immediately; otherwise show a "downloading…" state and block the drag until ready.
3. File is stored with a clean name: `Artist - Title - 128bpm - Am.wav` in a per-user cache folder.
4. Native layer calls the OS drag (Tauri/Electron: native drag plugin; JUCE: `performExternalDragDropOfFiles`).
5. Drop lands in FL Studio's Channel Rack or Playlist as a normal audio file.

### 3.3 Tempo/key matching

- **v1:** parse BPM and key from titles/tags when present (e.g. Looperman URLs include BPM). Show a badge and let FL's built-in stretching handle the rest.
- **v2:** local analysis and time-stretch before drag-out so the dropped file already matches the project tempo. Library licences matter here: Rubber Band and Essentia are GPL/AGPL (copyleft, fine for an open-source GPL app, not for permissive licensing); Signalsmith Stretch is MIT and librosa is ISC. The project licence is now AGPL-3.0-or-later (§9), so all of these are usable.

---

## 4. Tech stack

| Layer | Choice | Why |
|---|---|---|
| Core | **Rust** | Fast, single binary, good HTTP/HTML parsing (`reqwest`, `scraper`), usable as a library or sidecar |
| UI | **React or Svelte + TypeScript** | You already work in JavaScript; shared by app and plugin |
| Phase A shell | **Tauri** (Electron as fallback) | Small installer; native drag-out via a drag plugin/crate |
| Phase B shell | **JUCE 8 + WebView** | Standard route to VST3; WebView lets the same UI be reused |
| Storage | **SQLite** | Favorites, history, cache index |
| Audio playback | Web Audio in the UI (A); JUCE audio or web audio in the WebView (B) | Preview only, no heavy DSP |
| CI | GitHub Actions | Build, run scraper fixture tests, package installers |

Notes:
- If native drag-out in Tauri proves unreliable, switch the shell to **Electron**, whose `webContents.startDrag` API is purpose-built for dragging files out of a window. Verify against current docs when you reach this step.
- Windows first. macOS and Linux only after Windows is solid.

---

## 5. Roadmap

Estimates assume part-time, student-pace work. They are rough.

### Phase 0: Verify assumptions (≈ 1 week)
- [ ] Read the **full Looperman Terms & Conditions** and Sample Focus terms, and note any clause on automated access.
- [ ] Manually inspect network traffic on both sites: Are previews plain MP3 URLs? Does downloading require login? Any bot protection?
- [ ] Apply for a **Freesound API key**.
- [ ] Email Looperman and Sample Focus asking about an API or permission for a free, open-source, non-commercial browser. Keep their replies.
- [ ] Check "freeloops" name availability (GitHub, domain, trademarks).
- [ ] **Spike:** minimal Tauri window that drags a local `.wav` into FL Studio. If this fails, test Electron.

**Exit criterion:** a dragged file lands in FL Studio's Playlist from your own window.

### Phase 1: Core + Freesound MVP (≈ 3–4 weeks)
- [ ] Rust core: provider trait, Freesound provider, HTTP client with rate limiting and backoff.
- [ ] Preview player with waveform and instant audition.
- [ ] Download manager and cache (size cap, cleanup).
- [ ] Search UI with BPM, key, duration and licence filters; show CC licence per result.
- [ ] Native drag-out of cached files.
- [ ] Favorites and history (SQLite).

**Exit criterion:** search "piano loop" → preview → drag into FL Studio, using only official-API content.

### Phase 2: Community sources (≈ 3–4 weeks, gated by Phase 0 findings)
Only proceed for a source if its terms allow it or the owner agrees.
- [ ] Looperman provider: search, metadata (BPM from title/URL), preview, download via the **user's own login**.
- [ ] Sample Focus provider: same approach.
- [ ] Fixture-based parser tests, provider health checks, and a "degraded" UI state.
- [ ] Per-user session handling: credentials stored in the OS keychain, never logged, never shared.
- [ ] Throttling: low concurrency, local caching of search results, no bulk crawling.
- [ ] Attribution and "open on original site" links.

**Exit criterion:** a logged-in user can find and drag a Looperman loop with no browser needed.

### Phase 3: Polish and release the standalone app (≈ 2–3 weeks)
- [ ] BPM/key analysis and optional time-stretch to a user-set tempo.
- [ ] Collections, keyboard shortcuts, always-on-top "mini mode" to sit beside FL Studio.
- [ ] Installer, auto-update, crash reporting (opt-in).
- [ ] README, screenshots, demo video, contribution guide, provider-writing guide.
- [ ] Public release on GitHub (and qweren.com).

### Phase 4: VST3 plugin shell (≈ 4–8 weeks, stretch goal)
- [ ] JUCE 8 project hosting the shared web UI in a WebView.
- [ ] Native drag-out via `performExternalDragDropOfFiles`, with drag started from the native side (avoid the HTML5 `dragstart` conflict).
- [ ] Core as a bundled sidecar process or linked library. A sidecar keeps network and scraping crashes out of the DAW process.
- [ ] Read host tempo (and play state) to drive auto-stretch previews.
- [ ] Test matrix: FL Studio (primary), then Reaper, Studio One. Treat Ableton drag-out as best-effort.
- [ ] Installer drops the VST3 into the standard VST3 folder; verify FL Studio's plugin scan picks it up.
- [ ] Soak test: long sessions, project save/reload, closing the plugin window mid-download (known crash scenarios on other hosts).

**Exit criterion:** open freeloops as a plugin in FL Studio and drag a loop to the Playlist with no crashes over a 1-hour session.

---

## 6. Feature backlog

**MVP:** search, filters, preview, drag-out, favorites, licence display, source toggles.
**Next:** tempo/key sync, collections, waveform scrubbing, similar-sound search, mini mode.
**Later:** more providers (e.g. other CC0/CC-BY libraries), plugin shell, macOS/Linux, MIDI/loop slicing, community provider repository.

---

## 7. Risks and mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Source sites block or forbid automated access | Medium | High | Contact owners first; Freesound as the guaranteed backbone; providers optional |
| Scrapers break on site redesign | High | Medium | Provider isolation, fixtures, health checks, fast patch releases |
| Sample Focus redistribution clause violated by caching/hosting | Low if designed right | High | Never host or share files; per-user local cache only |
| Plugin crashes the DAW | Medium | High | Sidecar process, soak tests, standalone app as the primary product |
| Native drag-out flaky in Tauri | Medium | Medium | Spike in Phase 0; fallback to Electron |
| WebView drag conflict inside plugin | Medium | Medium | Native-side drag start |
| Licence confusion (CC-BY needs attribution) | Medium | Medium | Show licence on every result; one-click "copy attribution" |
| Name collision | Unknown | Low | Check early |

---

## 8. Legal and ethical rules for the project

1. **No hosting or mirroring.** Files go to the user's own machine only.
2. **No shared accounts.** Use each user's own login where a login is needed.
3. **No credential bypass.** If a source requires login, require it.
4. **Be a good citizen.** Throttle requests, cache aggressively, identify the app in the User-Agent, respect `robots.txt` and any rate-limit responses.
5. **Opt-in providers.** Community-site providers can be off by default, with a short notice about their terms.
6. **Respect licences.** Display licence and author; make attribution easy.
7. **Take-down friendly.** A documented way for a site or uploader to request a provider be removed.
8. This plan is not legal advice. Get actual terms in writing from the sources before the public release.

---

## 9. Open questions

- [x] What do the Looperman terms say about automated access? They prohibit "downloaders, page scrapers … web scraping" and any "robot, spider … or other data mining technology", except standard browser use (checked 1 Nov 2026). The project owner decided to build the Looperman provider anyway, with safeguards: opt-in with a notice about the terms, the user's own login, user-initiated requests only, throttled, honest User-Agent. Contacting Looperman for permission is still recommended.
- [ ] What do the Sample Focus terms say about automated access? No terms of service page could be found (checked 1 Nov 2026: `/terms` is a 404; only the [licence](https://samplefocus.com/license) and privacy policy are linked). Its `robots.txt` allows all paths for general user agents (`search=yes, ai-train=no`). The licence forbids sharing or selling a sample by itself. The Sample Focus provider was built on the Looperman safeguards (opt-in, the user's own login and credits, user-initiated requests only, throttled), with every request made from a samplefocus.com page in the plugin, so a Cloudflare check is shown to the user rather than bypassed. Ask Sample Focus for their terms before the public release.
- [x] Are previews public URLs, or tied to sessions? Looperman's and Sample Focus's CDNs both refuse requests from outside their own pages (HTTP 403). Freesound's previews are public.
- [x] Project licence: **AGPL-3.0-or-later** (decided 1 Nov 2026). It matches JUCE's free AGPLv3 option and allows Rubber Band/Essentia.
- [ ] Tauri or Electron, after the Phase 0 drag spike?
- [ ] Is the plugin shell worth it, or is a small always-on-top standalone window enough for FL Studio users?
- [ ] Windows only for v1, or also macOS?

---

## 10. Repository layout (proposed)

```
freeloops/
├─ core/            # Rust: providers, cache, downloads, metadata, SQLite
│  └─ providers/    # freesound/, looperman/, samplefocus/, local/ (+ fixtures/)
├─ ui/              # Web UI (TypeScript)
├─ app/             # Phase A: Tauri (or Electron) shell
├─ plugin/          # Phase B: JUCE VST3 shell
├─ docs/            # Provider guide, legal notes, architecture
└─ .github/         # CI: tests, fixture checks, packaging
```

---

## 11. References gathered during research

- JUCE forum: external drag-and-drop from plugins working in FL Studio / Studio One / Reaper, failing in Ableton VST3 (`performExternalDragDropOfFiles not working in Ableton`).
- JUCE forum: WebView plugin drag-out works with the editor as source; JS `dragstart` interferes (`[WebView] Drag and drop audio files out of plugin`).
- JUCE forum: FL Studio Edison drops into plugins trigger `DropLeave` (`Drag and Drop to VST from Edison in FL Studio`).
- Sample Focus standard license: https://samplefocus.com/license
- Looperman loops FAQ: https://looperman.com/help/faq/loops
- Freesound API overview: https://freesound.org/docs/api/overview.html
