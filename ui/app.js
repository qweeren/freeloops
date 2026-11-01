// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

(() => {
  "use strict";

  const ROW_H = 46;
  const OVERSCAN = 8;
  const DRAG_THRESHOLD = 5;      // px of movement before a press becomes a file drag
  const PEAK_CONCURRENCY = 3;
  const HOST_BPM_TOLERANCE = 3;  // "Match project" accepts +-3 bpm, and half/double time

  const NOTE_INDEX = { C: 0, "C#": 1, Db: 1, D: 2, "D#": 3, Eb: 3, E: 4, F: 5, "F#": 6, Gb: 6, G: 7, "G#": 8, Ab: 8, A: 9, "A#": 10, Bb: 10, B: 11 };
  const SHARPS = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

  const $ = (id) => document.getElementById(id);
  const els = {
    list: $("list"), rows: $("rows"), empty: $("empty"), search: $("search"),
    tempo: $("tempo"), tempoValue: $("tempo-value"), bpmMode: $("bpm-mode"), bpmRange: $("bpm-range"),
    bpmMin: $("bpm-min"), bpmMax: $("bpm-max"), key: $("key"), sort: $("sort"),
    resultCount: $("result-count"), folders: $("folders"), status: $("status"), toast: $("toast"),
    rescan: $("rescan"), views: $("views"),
    lmButton: $("source-looperman"), lmDot: $("lm-dot"), lmBadge: $("lm-badge"), lmBar: $("lm-bar"),
    lmStatus: $("lm-status"), lmSignIn: $("lm-signin"), lmSignOut: $("lm-signout"), lmOff: $("lm-off"),
  };

  const state = {
    samples: [], byId: new Map(), folders: [], scanning: false,
    source: "local", // "local" (the user's folders) or "looperman"
    view: "all", query: "", bpmMode: "any", key: "", sort: "default", shuffle: new Map(),
    results: [], selectedId: null,
    previewId: null, previewProgress: 0, previewPlaying: false,
    hostBpm: 0, hostPlaying: false,
    lm: {
      enabled: false, signedIn: false, folder: "",
      results: [], byId: new Map(), page: 0, hasMore: false, loading: false, error: null, searched: null,
    },
  };

  // ---------------------------------------------------------------- filename parsing

  function parseKey(text) {
    const t = text.replace(/♯/g, "#").replace(/♭/g, "b");
    const re = /(?:^|[\s_\-([.,])([A-G])([#b]?)[\s_-]?(major|minor|Major|Minor|maj|min|Maj|Min|m)?(?=$|[\s_\-)\].,])/g;
    let match;
    let found = null;
    while ((match = re.exec(t))) {
      const [, note, accidental, quality] = match;
      if (!accidental && !quality) continue; // a bare letter ("Kick A") is too ambiguous
      const pc = NOTE_INDEX[note + accidental];
      if (pc === undefined) continue;
      const minor = !!quality && /^(m|min|minor|Min|Minor)$/.test(quality);
      found = { pc, minor, label: SHARPS[pc] + (minor ? "m" : "") };
    }
    return found;
  }

  function parseBpm(text, loopish) {
    let m = text.match(/(\d{2,3}(?:\.\d+)?)\s?_?(?:bpm|BPM|Bpm)/) || text.match(/(?:bpm|BPM|Bpm)\s?_?(\d{2,3})/);
    if (m) {
      const v = Math.round(parseFloat(m[1]));
      if (v >= 50 && v <= 250) return v;
    }
    if (!loopish) return null;
    for (const token of text.split(/[\s_\-()[\].,]+/)) {
      if (/^\d{2,3}$/.test(token)) {
        const v = +token;
        if (v >= 60 && v <= 200) return v;
      }
    }
    return null;
  }

  function enrich(raw) {
    const text = `${raw.folder} ${raw.name}`;
    const loopish = /loop/i.test(text);
    const bpm = parseBpm(raw.name, loopish) ?? parseBpm(raw.folder, false);
    const key = parseKey(raw.name);
    return {
      id: raw.id,
      name: raw.name,
      folder: raw.folder,
      fav: !!raw.fav,
      bpm,
      key,
      isLoop: loopish || bpm !== null,
      hay: text.toLowerCase(),
      peaks: null,
      duration: null,
    };
  }

  // Looperman gives keys like "D#m", "C" or "Bb": plain enough to read directly.
  function normalizeKey(label) {
    const m = /^([A-G])([#b]?)\s*(m|min|minor)?$/i.exec((label || "").trim());
    if (!m) return null;
    const pc = NOTE_INDEX[m[1].toUpperCase() + m[2]];
    if (pc === undefined) return null;
    const minor = !!m[3];
    return { pc, minor, label: SHARPS[pc] + (minor ? "m" : "") };
  }

  function enrichLooperman(raw) {
    return {
      id: raw.id,
      lm: true,
      name: raw.name,
      folder: `by ${raw.author || "unknown"}${raw.genre ? ` · ${raw.genre}` : ""}${raw.category ? ` · ${raw.category}` : ""}`,
      author: raw.author,
      path: raw.path || "",
      fav: false,
      bpm: raw.bpm || null,
      key: normalizeKey(raw.key),
      isLoop: true,
      hay: "",
      peaks: null,
      duration: typeof raw.duration === "number" && raw.duration > 0 ? raw.duration : null,
    };
  }

  // ---------------------------------------------------------------- library

  function loadLibrary(lib) {
    if (!lib) return;
    const old = state.byId;
    state.folders = lib.folders || [];
    state.scanning = !!lib.scanning;
    state.samples = (lib.samples || []).map((raw) => {
      const s = enrich(raw);
      const prev = old.get(s.id);
      if (prev) {
        s.peaks = prev.peaks;
        s.duration = prev.duration;
      }
      return s;
    });
    state.byId = new Map(state.samples.map((s) => [s.id, s]));
    renderFolders();
    renderCounts();
    applyFilters();
  }

  // ---------------------------------------------------------------- filtering

  function bpmMatches(s) {
    if (state.bpmMode === "any") return true;
    if (s.bpm === null) return false;
    if (state.bpmMode === "host") {
      const h = state.hostBpm;
      if (!h) return true;
      return [h, h * 2, h / 2].some((t) => Math.abs(s.bpm - t) <= HOST_BPM_TOLERANCE);
    }
    const lo = parseFloat(els.bpmMin.value);
    const hi = parseFloat(els.bpmMax.value);
    return (isNaN(lo) || s.bpm >= lo) && (isNaN(hi) || s.bpm <= hi);
  }

  function keyMatches(s) {
    if (!state.key) return true;
    return s.key !== null && s.key.label === state.key;
  }

  function viewMatches(s) {
    switch (state.view) {
      case "loops": return s.isLoop;
      case "oneshots": return !s.isLoop;
      case "favorites": return s.fav;
      default: return true;
    }
  }

  function compare(a, b) {
    const byName = a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: "base" });
    switch (state.sort) {
      case "bpm": return (a.bpm ?? 999) - (b.bpm ?? 999) || byName;
      case "key": return (a.key ? a.key.pc * 2 + a.key.minor : 99) - (b.key ? b.key.pc * 2 + b.key.minor : 99) || byName;
      case "folder": return a.folder.localeCompare(b.folder, undefined, { numeric: true }) || byName;
      case "random": return shuffleRank(a) - shuffleRank(b);
      case "name": return byName;
      default: return a.lm && b.lm ? a.order - b.order : byName; // Looperman's own order (newest first)
    }
  }

  function shuffleRank(s) {
    let r = state.shuffle.get(s.id);
    if (r === undefined) {
      r = Math.random();
      state.shuffle.set(s.id, r);
    }
    return r;
  }

  function applyFilters({ keepScroll = false } = {}) {
    if (state.source === "looperman") {
      // The search text already went to Looperman; BPM, key and sort still apply here.
      state.results = state.lm.results.filter((s) => bpmMatches(s) && keyMatches(s));
    } else {
      const tokens = state.query.toLowerCase().split(/\s+/).filter(Boolean);
      state.results = state.samples.filter((s) =>
        viewMatches(s) && bpmMatches(s) && keyMatches(s) && tokens.every((t) => s.hay.includes(t)));
    }
    state.results.sort(compare);
    if (!keepScroll) els.list.scrollTop = 0;
    els.resultCount.textContent = `${state.results.length.toLocaleString()} result${state.results.length === 1 ? "" : "s"}`;
    renderList();
    renderEmpty();
    renderStatus();
  }

  // ---------------------------------------------------------------- rendering

  const icons = {
    play: '<svg viewBox="0 0 24 24"><path d="M8 5v14l11-7z"/></svg>',
    stop: '<svg viewBox="0 0 24 24"><rect x="6" y="6" width="12" height="12" rx="1.5"/></svg>',
  };

  function esc(text) {
    return text.replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
  }

  function formatTime(seconds) {
    if (seconds === null) return "";
    if (seconds < 10) return `${seconds.toFixed(1)}s`;
    const m = Math.floor(seconds / 60);
    const s = Math.floor(seconds % 60);
    return `${m}:${String(s).padStart(2, "0")}`;
  }

  function visibleRange() {
    const top = els.list.scrollTop;
    const height = els.list.clientHeight;
    const first = Math.max(0, Math.floor(top / ROW_H) - OVERSCAN);
    const last = Math.min(state.results.length, Math.ceil((top + height) / ROW_H) + OVERSCAN);
    return [first, last];
  }

  function renderList() {
    els.rows.style.height = `${state.results.length * ROW_H}px`;
    const [first, last] = visibleRange();
    let html = "";
    for (let i = first; i < last; i++) {
      const s = state.results[i];
      const playing = s.id === state.previewId && state.previewPlaying;
      const cls = ["row", s.id === state.selectedId ? "selected" : "", playing ? "playing" : ""].join(" ");
      html += `<div class="${cls}" data-i="${i}" style="top:${i * ROW_H}px">`
        + `<button class="play" tabindex="-1" title="Preview">${playing ? icons.stop : icons.play}</button>`
        + `<canvas class="wave" width="300" height="56"></canvas>`
        + `<div class="title"><div class="name" title="${esc(s.name)}">${esc(s.name)}</div><div class="folder-path">${esc(s.folder)}</div></div>`
        + `<span class="badge key ${s.key ? "" : "empty-cell"}">${s.key ? s.key.label : "-"}</span>`
        + `<span class="num ${s.bpm ? "" : "empty-cell"}">${s.bpm ?? "-"}</span>`
        + `<span class="num">${formatTime(s.duration)}</span>`
        + (s.lm ? downloadButton(s) : `<button class="fav ${s.fav ? "on" : ""}" tabindex="-1" title="Favorite (F)">${s.fav ? "★" : "☆"}</button>`)
        + `</div>`;
    }
    if (state.source === "looperman" && (state.lm.hasMore || state.lm.loading) && last === state.results.length) {
      html += `<div class="load-more" style="top:${state.results.length * ROW_H}px">${state.lm.loading ? "Loading more from Looperman..." : "Scroll for more"}</div>`;
    }
    els.rows.innerHTML = html;
    for (const row of els.rows.querySelectorAll(".row")) {
      const s = state.results[+row.dataset.i];
      drawWave(row.querySelector("canvas"), s);
      if (!s.peaks) requestPeaks(s);
    }
  }

  function downloadButton(s) {
    if (s.path) return `<button class="dl done" tabindex="-1" title="Downloaded. Drag it into FL Studio">✓</button>`;
    if (s.downloading) return `<button class="dl busy" tabindex="-1" title="Downloading...">⤓</button>`;
    return `<button class="dl" tabindex="-1" title="Download with your Looperman account">⤓</button>`;
  }

  function rowFor(s) {
    const i = state.results.indexOf(s);
    return i < 0 ? null : els.rows.querySelector(`.row[data-i="${i}"]`);
  }

  function refreshRow(s) {
    const row = rowFor(s);
    if (!row) return;
    drawWave(row.querySelector("canvas"), s);
    row.querySelector(".num:last-of-type").textContent = formatTime(s.duration);
  }

  function drawWave(canvas, s) {
    const ctx = canvas.getContext("2d");
    const w = canvas.width;
    const h = canvas.height;
    ctx.clearRect(0, 0, w, h);
    const peaks = s.peaks;
    if (!peaks || peaks.length === 0) {
      ctx.fillStyle = "#2c3137";
      ctx.fillRect(0, h / 2 - 1, w, 2);
      return;
    }
    const playing = s.id === state.previewId && state.previewPlaying;
    const progress = playing ? state.previewProgress : 0;
    const bar = w / peaks.length;
    for (let i = 0; i < peaks.length; i++) {
      const v = Math.max(0.04, peaks[i]);
      const bh = v * (h - 4);
      ctx.fillStyle = playing && i / peaks.length <= progress ? "#ff8c1a" : (playing ? "#8a6a4c" : "#6b737d");
      ctx.fillRect(i * bar, (h - bh) / 2, Math.max(1, bar - 1), bh);
    }
  }

  function renderFolders() {
    els.folders.innerHTML = state.folders.map((path, i) => {
      const short = path.split(/[\\/]/).filter(Boolean).pop() || path;
      return `<li class="folder" title="${esc(path)}"><span class="folder-name">${esc(short)}</span>`
        + `<button class="remove" data-folder="${i}" title="Remove folder">✕</button></li>`;
    }).join("");
    els.rescan.classList.toggle("spinning", state.scanning);
  }

  function renderCounts() {
    const counts = { all: state.samples.length, loops: 0, oneshots: 0, favorites: 0 };
    for (const s of state.samples) {
      if (s.isLoop) counts.loops++; else counts.oneshots++;
      if (s.fav) counts.favorites++;
    }
    for (const el of document.querySelectorAll("[data-count]")) el.textContent = counts[el.dataset.count].toLocaleString();
  }

  function renderEmpty() {
    let html = "";
    if (state.source === "looperman") {
      const lm = state.lm;
      if (!lm.enabled) {
        html = `<h2>Looperman</h2>`
          + `<p>Search and preview Looperman's free loops right here, then drag them into FL Studio. Downloads use your own Looperman account.</p>`
          + `<p class="fine">freeloops loads Looperman's public pages for your searches and previews, one request at a time, and downloads a loop only when you drag or download it, through your own signed-in session. Looperman's terms restrict downloaders and automated access, so using this could put your Looperman account at risk. Loops stay under Looperman's licence: registered users may use them in their own music.</p>`
          + `<button class="cta" data-action="lm-enable">Turn on Looperman</button>`;
      } else if (lm.error && state.results.length === 0) {
        html = `<h2>Looperman isn't responding</h2><p>${esc(lm.error)}</p>`;
      } else if (lm.loading && state.results.length === 0) {
        html = `<h2>Searching Looperman...</h2>`;
      } else if (state.results.length === 0 && lm.searched !== null) {
        html = `<h2>No loops found</h2><p>Try other words, or clear the BPM and key filters.</p>`;
      }
    } else if (state.folders.length === 0) {
      html = `<h2>Add your sample folders</h2><p>freeloops indexes the folders you choose. Nothing is uploaded or copied. Then search, preview and drag samples straight into FL Studio.</p><button class="cta" data-action="add-folder">Add a folder</button>`;
    } else if (state.scanning && state.samples.length === 0) {
      html = `<h2>Scanning your folders...</h2>`;
    } else if (state.results.length === 0) {
      html = `<h2>No samples match</h2><p>Try a different search, or clear the BPM and key filters.</p>`;
    }
    els.empty.innerHTML = html;
    els.empty.hidden = html === "";
  }

  function renderLooperman() {
    const lm = state.lm;
    els.lmBadge.textContent = !lm.enabled ? "off" : lm.signedIn ? "on" : "sign in";
    els.lmDot.className = `dot ${lm.enabled ? (lm.signedIn ? "on" : "ready") : ""}`;
    els.lmBar.hidden = state.source !== "looperman" || !lm.enabled;
    els.lmStatus.textContent = lm.signedIn
      ? `Signed in. Downloads are saved to ${lm.folder}`
      : "Previews work without an account. Sign in to drag loops into FL Studio.";
    els.lmSignIn.hidden = lm.signedIn;
    els.lmSignIn.classList.toggle("primary", !lm.signedIn);
    els.lmSignOut.hidden = !lm.signedIn;
    els.lmButton.classList.toggle("active", state.source === "looperman");
  }

  function renderStatus() {
    if (state.source === "looperman") {
      const n = state.lm.results.length;
      els.status.textContent = state.lm.enabled
        ? `Looperman · ${n} loop${n === 1 ? "" : "s"} loaded${state.lm.searched ? ` for "${state.lm.searched}"` : ""}`
        : "Looperman is off";
      return;
    }
    const n = state.samples.length.toLocaleString();
    const f = state.folders.length;
    els.status.textContent = state.scanning
      ? `Scanning... ${n} samples so far`
      : `${n} samples in ${f} folder${f === 1 ? "" : "s"}${window.fl.native ? "" : " · mock data"}`;
  }

  function renderTempo() {
    els.tempoValue.textContent = state.hostBpm ? (Math.round(state.hostBpm * 10) / 10).toString() : "-";
    els.tempo.classList.toggle("playing", state.hostPlaying);
  }

  let toastTimer = 0;
  function toast(message) {
    els.toast.textContent = message;
    els.toast.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => (els.toast.hidden = true), 2600);
  }

  // ---------------------------------------------------------------- waveforms

  const peakQueue = [];
  let peaksInFlight = 0;

  function requestPeaks(s) {
    if (s.peaks || s.peaksPending) return;
    s.peaksPending = true;
    peakQueue.push(s);
    pumpPeaks();
  }

  function isOnScreen(s) {
    const i = state.results.indexOf(s);
    const [first, last] = visibleRange();
    return i >= first && i < last;
  }

  function pumpPeaks() {
    while (peaksInFlight < PEAK_CONCURRENCY && peakQueue.length) {
      const s = peakQueue.pop(); // newest first: what's on screen now
      if (!isOnScreen(s)) {
        s.peaksPending = false;
        continue;
      }
      peaksInFlight++;
      window.fl.call("getPeaks", s.id).then((r) => {
        peaksInFlight--;
        s.peaksPending = false;
        s.peaks = r && r.peaks ? r.peaks : [];
        if (r && typeof r.duration === "number") s.duration = r.duration;
        refreshRow(s);
        pumpPeaks();
      });
    }
  }

  // ---------------------------------------------------------------- actions

  function select(s, { scroll = true, audition = true } = {}) {
    if (!s) return;
    state.selectedId = s.id;
    if (scroll) {
      const i = state.results.indexOf(s);
      const top = i * ROW_H;
      if (top < els.list.scrollTop) els.list.scrollTop = top;
      else if (top + ROW_H > els.list.scrollTop + els.list.clientHeight) els.list.scrollTop = top + ROW_H - els.list.clientHeight;
    }
    if (audition) preview(s);
    else renderList();
  }

  function findSample(id) {
    return state.byId.get(id) || state.lm.byId.get(id);
  }

  function preview(s) {
    state.previewId = s.id;
    state.previewPlaying = true;
    state.previewProgress = 0;
    state.previewRequestedAt = performance.now();
    // A Looperman loop that hasn't been auditioned yet is downloaded first, which takes a moment.
    state.previewGraceMs = s.lm && !s.path ? 20000 : 500;
    if (s.lm) {
      window.fl.call("lmPreview", s.id).then((r) => {
        if (!r || r.error) {
          const error = r && r.error;
          toast(error === "signed-out"
            ? "Sign in to Looperman to preview loops. Looperman only plays its previews on its own site."
            : LM_ERRORS[error] || "Couldn't load the preview from Looperman.");
          if (state.previewId === s.id) state.previewPlaying = false;
          renderList();
        } else if (!s.peaks || s.peaks.length === 0) {
          s.peaks = null; // the audition is on disk now, so it has a waveform
          requestPeaks(s);
        }
      });
    } else {
      window.fl.call("preview", s.id);
    }
    renderList();
  }

  function togglePreview(s) {
    if (s.id === state.previewId && state.previewPlaying) {
      state.previewPlaying = false;
      window.fl.call("stopPreview");
      renderList();
    } else {
      state.selectedId = s.id;
      preview(s);
    }
  }

  function toggleFavorite(s) {
    if (s.lm) return;
    s.fav = !s.fav;
    window.fl.call("setFavorite", s.id, s.fav);
    renderCounts();
    if (state.view === "favorites") applyFilters({ keepScroll: true });
    else renderList();
  }

  function moveSelection(delta) {
    if (state.results.length === 0) return;
    const current = state.results.findIndex((s) => s.id === state.selectedId);
    const next = Math.min(state.results.length - 1, Math.max(0, current < 0 ? 0 : current + delta));
    select(state.results[next]);
  }

  function addFolder() {
    window.fl.call("addFolder");
  }

  // ---------------------------------------------------------------- Looperman

  const LM_ERRORS = {
    "signed-out": "Sign in to Looperman to download loops.",
    "no-download-button": "Couldn't find Looperman's download button for this loop. Right-click it to open it on Looperman.",
    "not-a-file": "Looperman sent a web page instead of the file. Right-click the loop to open it on Looperman.",
    "zip-not-supported": "This loop downloads as a zip, which freeloops can't unpack yet.",
    "timed out": "The download from Looperman timed out.",
  };

  function applyLoopermanState(st) {
    if (!st) return;
    const lm = state.lm;
    const wasEnabled = lm.enabled;
    lm.enabled = !!st.enabled;
    lm.signedIn = !!st.signedIn;
    lm.folder = st.folder || "";
    renderLooperman();
    if (state.source !== "looperman") return;
    if (lm.enabled && !wasEnabled) searchLooperman(state.query, 1);
    else applyFilters({ keepScroll: true });
  }

  function searchLooperman(query, page) {
    const lm = state.lm;
    if (!lm.enabled) return;
    const token = (lm.token = (lm.token || 0) + 1);
    lm.loading = true;
    lm.error = null;
    if (page === 1) {
      lm.results = [];
      lm.byId = new Map();
      lm.searched = query;
      lm.hasMore = false;
      applyFilters();
    }
    window.fl.call("lmSearch", query, page).then((r) => {
      if (token !== lm.token) return; // a newer search replaced this one
      lm.loading = false;
      if (!r || r.error) {
        lm.error = (r && r.error) || "No response";
      } else {
        for (const raw of r.loops || []) {
          if (lm.byId.has(raw.id)) continue;
          const s = enrichLooperman(raw);
          s.order = lm.results.length;
          lm.byId.set(s.id, s);
          lm.results.push(s);
        }
        lm.page = page;
        lm.hasMore = !!r.hasMore;
      }
      applyFilters({ keepScroll: page > 1 });
    });
  }

  function maybeLoadMore() {
    const lm = state.lm;
    if (state.source !== "looperman" || !lm.hasMore || lm.loading) return;
    if (els.list.scrollTop + els.list.clientHeight < els.list.scrollHeight - ROW_H * 6) return;
    searchLooperman(lm.searched ?? "", lm.page + 1);
  }

  // Looperman loops need your account to download; only then can they be dragged into FL.
  // onReady runs when the file is saved (used to start a drag the user is still holding).
  function downloadLoop(s, onReady) {
    if (s.path || s.downloading) return;
    if (!state.lm.signedIn) {
      toast("Sign in to Looperman to download loops.");
      return;
    }
    s.downloading = true;
    toast(onReady ? "Getting the loop from Looperman... keep holding to drag it." : "Downloading from Looperman...");
    renderList();
    window.fl.call("lmDownload", s.id).then((r) => {
      s.downloading = false;
      if (r && r.path) {
        s.path = r.path;
        if (!(onReady && onReady())) toast("Ready. Drag it into FL Studio.");
      } else {
        const error = r && r.error;
        toast(LM_ERRORS[error] || `Download failed: ${error || "unknown error"}`);
      }
      renderList();
    });
  }

  // ---------------------------------------------------------------- input: list

  // The file drag is started by native code (plan §2.3): HTML5 drag events would fight the OS drag.
  let press = null;

  els.list.addEventListener("dragstart", (e) => e.preventDefault());

  els.list.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;
    const row = e.target.closest(".row");
    press = row ? { i: +row.dataset.i, x: e.clientX, y: e.clientY, dragging: false } : null;
  });

  els.list.addEventListener("pointermove", (e) => {
    if (!press || press.dragging || !(e.buttons & 1)) return;
    if (Math.hypot(e.clientX - press.x, e.clientY - press.y) < DRAG_THRESHOLD) return;
    const s = state.results[press.i];
    if (!s) return;
    press.dragging = true;

    if (s.lm && !s.path) {
      // Not downloaded yet: fetch it, and start the drag if the button is still held when it's saved.
      const held = press;
      return downloadLoop(s, () => {
        if (press !== held) return false;
        startFileDrag(s);
        return true;
      });
    }
    startFileDrag(s);
  });

  function startFileDrag(s) {
    document.body.classList.add("dragging");
    const row = rowFor(s);
    if (row) row.classList.add("dragging");
    window.fl.call("startDrag", s.lm ? s.path : s.id).then((ok) => {
      if (!ok) toast("That file is missing. Try rescanning the folder.");
    });
  }

  window.addEventListener("pointerup", (e) => {
    const p = press;
    press = null;
    if (!p || p.dragging) return endDrag();
    const s = state.results[p.i];
    if (!s) return;
    // Released outside any element (e.g. over another window): look up what's under the pointer.
    const target = e.target instanceof Element ? e.target : document.elementFromPoint(e.clientX, e.clientY);
    if (!target) return;
    if (target.closest(".fav")) toggleFavorite(s);
    else if (target.closest(".dl")) downloadLoop(s);
    else if (target.closest(".play")) togglePreview(s);
    else if (target.closest(".row")) {
      if (s.id === state.selectedId) togglePreview(s);
      else select(s, { scroll: false });
    }
    els.list.focus({ preventScroll: true });
  });

  function endDrag() {
    document.body.classList.remove("dragging");
    for (const row of els.rows.querySelectorAll(".row.dragging")) row.classList.remove("dragging");
  }

  els.list.addEventListener("contextmenu", (e) => {
    const row = e.target.closest(".row");
    if (!row) return;
    const s = state.results[+row.dataset.i];
    if (s) window.fl.call(s.lm ? "lmOpen" : "reveal", s.id);
  });

  let scrollQueued = false;
  els.list.addEventListener("scroll", () => {
    if (scrollQueued) return;
    scrollQueued = true;
    requestAnimationFrame(() => {
      scrollQueued = false;
      renderList();
      maybeLoadMore();
    });
  });

  window.addEventListener("resize", () => renderList());

  // ---------------------------------------------------------------- input: keyboard

  document.addEventListener("keydown", (e) => {
    const typing = e.target === els.search || e.target.matches("input, select");
    if (e.target === els.search && e.key === "ArrowDown") {
      e.preventDefault();
      els.list.focus();
      moveSelection(state.selectedId ? 0 : 1);
      return;
    }
    if (typing) return;
    const s = findSample(state.selectedId);
    switch (e.key) {
      case "ArrowDown": e.preventDefault(); moveSelection(1); break;
      case "ArrowUp": e.preventDefault(); moveSelection(-1); break;
      case " ": e.preventDefault(); if (s) togglePreview(s); break;
      case "Escape": window.fl.call("stopPreview"); state.previewPlaying = false; renderList(); break;
      case "f": case "F": if (s) toggleFavorite(s); break;
      case "/": e.preventDefault(); els.search.focus(); break;
    }
  });

  // ---------------------------------------------------------------- input: controls

  // Local folders filter as you type; Looperman searches once you pause (or press Enter).
  let searchTimer = 0;
  function runSearch() {
    state.query = els.search.value.trim();
    if (state.source !== "looperman") applyFilters();
    else if (state.query !== state.lm.searched) searchLooperman(state.query, 1);
  }
  els.search.addEventListener("input", () => {
    clearTimeout(searchTimer);
    searchTimer = setTimeout(runSearch, state.source === "looperman" ? 700 : 120);
  });
  els.search.addEventListener("keydown", (e) => {
    if (e.key !== "Enter") return;
    clearTimeout(searchTimer);
    runSearch();
  });

  els.bpmMode.addEventListener("change", () => {
    state.bpmMode = els.bpmMode.value;
    els.bpmRange.hidden = state.bpmMode !== "range";
    applyFilters();
  });
  els.bpmMin.addEventListener("input", () => applyFilters());
  els.bpmMax.addEventListener("input", () => applyFilters());

  els.key.innerHTML = `<option value="">Any</option>`
    + SHARPS.map((n) => `<option value="${n}">${n} major</option><option value="${n}m">${n} minor</option>`).join("");
  els.key.addEventListener("change", () => {
    state.key = els.key.value;
    applyFilters();
  });

  els.sort.addEventListener("change", () => {
    state.sort = els.sort.value;
    if (state.sort === "random") state.shuffle.clear();
    applyFilters();
  });

  els.views.addEventListener("click", (e) => {
    const btn = e.target.closest(".view");
    if (!btn) return;
    for (const v of els.views.querySelectorAll(".view")) v.classList.toggle("active", v === btn);
    state.source = "local";
    state.view = btn.dataset.view;
    state.query = els.search.value.trim();
    renderLooperman();
    applyFilters();
  });

  els.lmButton.addEventListener("click", () => {
    for (const v of els.views.querySelectorAll(".view")) v.classList.remove("active");
    state.source = "looperman";
    state.query = els.search.value.trim();
    renderLooperman();
    if (state.lm.enabled && state.lm.searched !== state.query) searchLooperman(state.query, 1);
    else applyFilters();
  });

  els.lmSignIn.addEventListener("click", () => window.fl.call("lmSignIn"));
  els.lmSignOut.addEventListener("click", () => window.fl.call("lmSignOut"));
  els.lmOff.addEventListener("click", () => window.fl.call("lmSetEnabled", false).then(applyLoopermanState));

  document.getElementById("add-folder").addEventListener("click", addFolder);
  els.empty.addEventListener("click", (e) => {
    if (e.target.closest("[data-action=add-folder]")) addFolder();
    if (e.target.closest("[data-action=lm-enable]")) window.fl.call("lmSetEnabled", true).then(applyLoopermanState);
  });
  els.rescan.addEventListener("click", () => window.fl.call("rescan"));
  els.folders.addEventListener("click", (e) => {
    const btn = e.target.closest(".remove");
    if (btn) window.fl.call("removeFolder", state.folders[+btn.dataset.folder]);
  });

  document.addEventListener("contextmenu", (e) => {
    if (!e.target.matches("input")) e.preventDefault();
  });

  // ---------------------------------------------------------------- native events

  window.fl.on("library", loadLibrary);

  window.fl.on("dragEnded", () => {
    press = null;
    endDrag();
  });

  window.fl.on("status", (st) => {
    const tempoChanged = st.bpm !== state.hostBpm || st.hostPlaying !== state.hostPlaying;
    state.hostBpm = st.bpm || 0;
    state.hostPlaying = !!st.hostPlaying;
    if (tempoChanged) {
      renderTempo();
      if (state.bpmMode === "host") applyFilters({ keepScroll: true });
    }

    // The native side needs a moment to load a clip; don't flip back to "stopped" while it does.
    const loading = performance.now() - (state.previewRequestedAt || 0) < (state.previewGraceMs || 500);
    if (loading && !st.previewPlaying) return;

    const wasPlaying = state.previewPlaying;
    const prevId = state.previewId;
    if (st.previewId) state.previewId = st.previewId;
    state.previewPlaying = !!st.previewPlaying;
    state.previewProgress = st.previewProgress || 0;
    if (wasPlaying !== state.previewPlaying || prevId !== state.previewId) renderList();
    else if (state.previewPlaying) {
      const s = findSample(state.previewId);
      const row = s && rowFor(s);
      if (row) drawWave(row.querySelector("canvas"), s);
    }
  });

  window.fl.on("lmState", applyLoopermanState);

  window.fl.call("getState").then(loadLibrary);
  window.fl.call("lmState").then(applyLoopermanState);
  renderTempo();
  renderLooperman();
})();
