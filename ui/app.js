// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

(() => {
  "use strict";

  const ROW_H = 46;
  const OVERSCAN = 8;
  const DRAG_THRESHOLD = 5;      // px of movement before a press becomes a file drag
  const PEAK_CONCURRENCY = 4;    // the plugin reads waveforms on up to 4 threads
  const HOST_BPM_TOLERANCE = 3;  // "Match project" accepts +-3 bpm, and half/double time

  const MAX_PREVIEW_SECONDS = 60; // the plugin plays at most this much of a file

  const NOTE_INDEX = { C: 0, "C#": 1, Db: 1, D: 2, "D#": 3, Eb: 3, E: 4, F: 5, "F#": 6, Gb: 6, G: 7, "G#": 8, Ab: 8, A: 9, "A#": 10, Bb: 10, B: 11 };
  const SHARPS = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

  // One collator for every comparison: localeCompare with options builds a new one each call, which is slow.
  const collator = new Intl.Collator(undefined, { numeric: true, sensitivity: "base" });

  const $ = (id) => document.getElementById(id);
  const els = {
    list: $("list"), rows: $("rows"), empty: $("empty"), search: $("search"),
    tempo: $("tempo"), tempoValue: $("tempo-value"), bpmMode: $("bpm-mode"), bpmRange: $("bpm-range"),
    bpmMin: $("bpm-min"), bpmMax: $("bpm-max"), key: $("key"), sort: $("sort"),
    resultCount: $("result-count"), folders: $("folders"), status: $("status"), toast: $("toast"),
    rescan: $("rescan"), views: $("views"),
    lmButton: $("source-looperman"), lmDot: $("lm-dot"), lmBadge: $("lm-badge"), lmBar: $("lm-bar"),
    lmStatus: $("lm-status"), lmSignIn: $("lm-signin"), lmSignOut: $("lm-signout"), lmOff: $("lm-off"),
    fsButton: $("source-freesound"), fsDot: $("fs-dot"), fsBadge: $("fs-badge"), fsBar: $("fs-bar"),
    fsStatus: $("fs-status"), fsSignIn: $("fs-signin"), fsSignOut: $("fs-signout"), fsKey: $("fs-key"),
    sfButton: $("source-samplefocus"), sfDot: $("sf-dot"), sfBadge: $("sf-badge"), sfBar: $("sf-bar"),
    sfStatus: $("sf-status"), sfSignIn: $("sf-signin"), sfSignOut: $("sf-signout"), sfOff: $("sf-off"), sfCheck: $("sf-check"),
  };

  const state = {
    samples: [], byId: new Map(), folders: [], scanning: false,
    source: "local", // "local" (the user's folders), or an online source: "looperman", "freesound", "samplefocus"
    view: "all", query: "", bpmMode: "any", key: "", sort: "default", shuffle: new Map(),
    results: [], indexById: new Map(), selectedId: null,
    previewId: null, previewProgress: 0, previewProgressAt: 0, previewPlaying: false,
    hostBpm: 0, hostPlaying: false,
    lm: {
      enabled: false, signedIn: false, folder: "",
      results: [], byId: new Map(), page: 0, hasMore: false, loading: false, error: null, searched: null, sig: null,
    },
    fs: {
      configured: false, canSignIn: false, builtIn: false, clientId: "", signedIn: false, signingIn: false,
      username: "", folder: "", editing: false,
      results: [], byId: new Map(), page: 0, hasMore: false, loading: false, error: null, searched: null, sig: null,
    },
    sf: {
      enabled: false, signedIn: false, signingIn: false, challenge: false, checking: false, credits: -1, folder: "",
      results: [], byId: new Map(), page: 0, hasMore: false, loading: false, error: null, searched: null, sig: null,
    },
  };

  // Online sources. Search text and the BPM and key filters go to the source, so a filter change is a new
  // search; the filters also apply to what has loaded (the source may match more loosely), and sort does too.
  const SOURCES = {
    looperman: {
      label: "Looperman", prefix: "lm", unit: "loop", list: "loops",
      st: () => state.lm,
      ready: () => state.lm.enabled,
      search: (query, page) => window.fl.call("lmSearch", query, page, onlineFilters()),
      signature: (query) => `${query} ${JSON.stringify(onlineFilters())}`,
    },
    freesound: {
      label: "Freesound", prefix: "fs", unit: "sound", list: "sounds",
      st: () => state.fs,
      ready: () => state.fs.configured && !state.fs.editing,
      search: (query, page) => window.fl.call("fsSearch", query, page, onlineFilters()),
      signature: (query) => `${query} ${JSON.stringify(onlineFilters())}`,
    },
    samplefocus: {
      label: "Sample Focus", prefix: "sf", unit: "sample", list: "samples",
      st: () => state.sf,
      ready: () => state.sf.enabled,
      search: (query, page) => window.fl.call("sfSearch", query, page, onlineFilters()),
      signature: (query) => `${query} ${JSON.stringify(onlineFilters())}`,
    },
  };
  const SOURCE_BY_PREFIX = { lm: "looperman", fs: "freesound", sf: "samplefocus" };

  function online() {
    return SOURCES[state.source] || null;
  }

  function sourceOf(s) {
    return SOURCES[SOURCE_BY_PREFIX[s.src]];
  }

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

  function enrichOnline(raw, src) {
    const details = src === "fs" ? [raw.license] : src === "sf" ? (raw.tags || []).slice(0, 3) : [raw.genre, raw.category];
    const bpm = raw.bpm || (src === "fs" ? parseBpm(raw.name, /loop/i.test(raw.name)) : null);
    return {
      id: raw.id,
      src, // "lm" or "fs"
      name: raw.name,
      folder: [`by ${raw.author || "unknown"}`, ...details.filter(Boolean)].join(" · "),
      author: raw.author,
      license: raw.license || "",
      path: raw.path || "",
      fav: false,
      bpm: bpm || null,
      key: normalizeKey(raw.key),
      isLoop: src === "lm" || !!raw.loop || !!bpm,
      hay: "",
      peaks: null,
      duration: typeof raw.duration === "number" && raw.duration > 0 ? raw.duration : null,
    };
  }

  // ---------------------------------------------------------------- library

  function loadLibrary(lib) {
    if (!lib) return;
    state.folders = lib.folders || [];
    state.scanning = !!lib.scanning;
    renderFolders();
    if (!lib.samples) { // the plugin leaves the samples out when they haven't changed
      renderEmpty();
      renderStatus();
      return;
    }
    // A sample that was here before keeps its parsed name, waveform and duration.
    const old = state.byId;
    state.samples = lib.samples.map((raw, i) => {
      const prev = old.get(raw.id);
      const s = prev && prev.name === raw.name && prev.folder === raw.folder ? prev : enrich(raw);
      s.fav = !!raw.fav;
      s.order = i; // the plugin sorts by name (naturally), so this is the name order too
      return s;
    });
    state.byId = new Map(state.samples.map((s) => [s.id, s]));
    renderCounts();
    applyFilters({ keepScroll: old.size > 0 });
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

  // Local samples arrive sorted by name, so their order doubles as the name order.
  function byName(a, b) {
    return a.src || b.src ? collator.compare(a.name, b.name) : a.order - b.order;
  }

  let folderRank = new Map();
  function compare(a, b) {
    switch (state.sort) {
      case "bpm": return (a.bpm ?? 999) - (b.bpm ?? 999) || byName(a, b);
      case "key": return (a.key ? a.key.pc * 2 + a.key.minor : 99) - (b.key ? b.key.pc * 2 + b.key.minor : 99) || byName(a, b);
      case "folder": return folderRank.get(a.folder) - folderRank.get(b.folder) || byName(a, b);
      case "random": return shuffleRank(a) - shuffleRank(b);
      case "name": return byName(a, b);
      default: return a.src && b.src ? a.order - b.order : byName(a, b); // the source's own order
    }
  }

  function sortResults() {
    const local = !online();
    if (local && (state.sort === "default" || state.sort === "name")) return; // filtering kept the name order
    if (state.sort === "folder") {
      const folders = [...new Set(state.results.map((s) => s.folder))].sort(collator.compare);
      folderRank = new Map(folders.map((f, i) => [f, i]));
    }
    state.results.sort(compare);
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
    const src = online();
    if (src) {
      // The search text and filters already went to the source; they still apply here, as does sort.
      state.results = src.ready() ? src.st().results.filter((s) => bpmMatches(s) && keyMatches(s)) : [];
    } else {
      const tokens = state.query.toLowerCase().split(/\s+/).filter(Boolean);
      state.results = state.samples.filter((s) =>
        viewMatches(s) && bpmMatches(s) && keyMatches(s) && tokens.every((t) => s.hay.includes(t)));
    }
    sortResults();
    state.indexById = new Map(state.results.map((s, i) => [s.id, i]));
    if (!keepScroll) els.list.scrollTop = 0;
    els.resultCount.textContent = `${state.results.length.toLocaleString()} result${state.results.length === 1 ? "" : "s"}`;
    renderList();
    renderEmpty();
    renderStatus();
  }

  // ---------------------------------------------------------------- rendering

  const icons = {
    play: '<svg class="solid" viewBox="0 0 24 24"><path d="M8 5.5v13a1 1 0 0 0 1.5.86l10.6-6.5a1 1 0 0 0 0-1.72L9.5 4.64A1 1 0 0 0 8 5.5z"/></svg>',
    stop: '<svg class="solid" viewBox="0 0 24 24"><rect x="6.5" y="6.5" width="11" height="11" rx="2"/></svg>',
    star: '<svg viewBox="0 0 24 24"><path d="m12 3.5 2.6 5.3 5.9.9-4.3 4.1 1 5.8-5.2-2.7-5.2 2.7 1-5.8-4.3-4.1 5.9-.9z"/></svg>',
    starOn: '<svg class="solid" viewBox="0 0 24 24"><path d="m12 3.5 2.6 5.3 5.9.9-4.3 4.1 1 5.8-5.2-2.7-5.2 2.7 1-5.8-4.3-4.1 5.9-.9z"/></svg>',
    download: '<svg viewBox="0 0 24 24"><path d="M12 4v11m-4.5-4.5L12 15l4.5-4.5M5 19.5h14"/></svg>',
    check: '<svg viewBox="0 0 24 24"><path d="m5 12.5 4.5 4.5L19 7.5"/></svg>',
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

  // Rows are recycled: scrolling only fills the rows that come into view, and state changes (selection,
  // playback, downloads) touch just what changed. Each row keeps its sample and what it last drew.
  const rowEls = new Map(); // result index -> row element
  const spareRows = [];
  const loadMore = document.createElement("div");
  loadMore.className = "load-more";

  function makeRow() {
    const row = document.createElement("div");
    row.className = "row";
    row.innerHTML = `<button class="play" tabindex="-1" title="Preview"></button>`
      + `<canvas class="wave"></canvas>`
      + `<div class="title"><div class="name"></div><div class="folder-path"></div></div>`
      + `<span class="badge key"></span><span class="num bpm"></span><span class="num time"></span><span class="action"></span>`;
    const [play, wave, title, key, bpm, time, action] = row.children;
    Object.assign(row, { _play: play, _wave: wave, _name: title.firstChild, _folder: title.lastChild, _key: key, _bpm: bpm, _time: time, _action: action });
    sizeCanvas(wave);
    return row;
  }

  function setText(el, text) {
    if (el.textContent !== text) el.textContent = text;
  }

  function setHtml(el, html) {
    if (el._html === html) return;
    el._html = html;
    el.innerHTML = html;
  }

  function updateRow(row, s, i) {
    if (row._i !== i) {
      row._i = i;
      row.dataset.i = i;
      row.style.transform = `translateY(${i * ROW_H}px)`;
    }
    if (row._s !== s) {
      row._s = s;
      row._waveKey = null;
      setText(row._name, s.name);
      row._name.title = s.name;
      setText(row._folder, s.folder);
      setText(row._key, s.key ? s.key.label : "-");
      row._key.className = `badge key${s.key ? (s.key.minor ? " minor" : "") : " empty-cell"}`;
      setText(row._bpm, s.bpm ? String(s.bpm) : "-");
      row._bpm.classList.toggle("empty-cell", !s.bpm);
    }
    const playing = s.id === state.previewId && state.previewPlaying;
    row.classList.toggle("selected", s.id === state.selectedId);
    row.classList.toggle("playing", playing);
    setHtml(row._play, playing ? icons.stop : icons.play);
    setHtml(row._action, s.src ? downloadButton(s) : favoriteButton(s));
    setText(row._time, formatTime(s.duration));
    drawWave(row, s);
    if (!s.peaks) requestPeaks(s);
  }

  function renderList() {
    els.rows.style.height = `${state.results.length * ROW_H}px`;
    const [first, last] = visibleRange();

    for (const [i, row] of rowEls) {
      if (i >= first && i < last) continue;
      rowEls.delete(i);
      spareRows.push(row);
    }
    for (let i = first; i < last; i++) {
      let row = rowEls.get(i);
      if (!row) {
        row = spareRows.pop() || makeRow();
        rowEls.set(i, row);
        if (!row.isConnected) els.rows.appendChild(row);
      }
      updateRow(row, state.results[i], i);
    }
    for (const row of spareRows) {
      if (!row.isConnected) continue;
      row.remove();
      row._s = null;
    }

    const src = online();
    if (src && (src.st().hasMore || src.st().loading) && last === state.results.length) {
      loadMore.style.transform = `translateY(${state.results.length * ROW_H}px)`;
      setText(loadMore, src.st().loading ? `Loading more from ${src.label}...` : "Scroll for more");
      if (!loadMore.isConnected) els.rows.appendChild(loadMore);
    } else if (loadMore.isConnected) {
      loadMore.remove();
    }
  }

  function favoriteButton(s) {
    return `<button class="fav ${s.fav ? "on" : ""}" tabindex="-1" title="Favorite (F)">${s.fav ? icons.starOn : icons.star}</button>`;
  }

  function downloadButton(s) {
    if (s.path) return `<button class="dl done" tabindex="-1" title="Downloaded. Drag it into FL Studio">${icons.check}</button>`;
    if (s.downloading) return `<button class="dl busy" tabindex="-1" title="Downloading...">${icons.download}</button>`;
    const how = s.src === "fs" ? "the original file with your Freesound account"
      : s.src === "sf" ? "with your Sample Focus account (1 credit, unless you've downloaded it before)" : "with your Looperman account";
    return `<button class="dl" tabindex="-1" title="Download ${how}">${icons.download}</button>`;
  }

  function rowFor(s) {
    const i = state.indexById.get(s.id);
    const row = i === undefined ? null : rowEls.get(i);
    return row && row._s === s ? row : null;
  }

  function refreshRow(s) {
    const row = rowFor(s);
    if (row) updateRow(row, s, row._i);
  }

  // ---------------------------------------------------------------- waveforms: drawing

  const WAVE_W = 150;
  const WAVE_H = 28;
  const waveColors = {
    idle: "#4b525b", selected: "#8b939d", played: "#ff8c1a", unplayed: "rgba(255, 140, 26, 0.3)", empty: "#2a2e34",
  };

  function sizeCanvas(canvas) {
    const dpr = Math.max(1, Math.round(window.devicePixelRatio || 1));
    canvas.width = WAVE_W * dpr;
    canvas.height = WAVE_H * dpr;
    canvas._dpr = dpr;
  }

  // Redraws only when what it shows changed: the peaks, the selection, or (while playing) the playhead.
  function drawWave(row, s, progress = state.previewProgress) {
    const playing = s.id === state.previewId && state.previewPlaying;
    const selected = s.id === state.selectedId;
    const key = `${s.peaks ? s.peaks.length : -1}|${selected}|${playing ? Math.round(progress * WAVE_W * 2) : -1}`;
    if (row._waveKey === key) return;
    row._waveKey = key;

    const canvas = row._wave;
    const ctx = canvas.getContext("2d");
    ctx.setTransform(canvas._dpr, 0, 0, canvas._dpr, 0, 0);
    ctx.clearRect(0, 0, WAVE_W, WAVE_H);
    const peaks = s.peaks;
    if (!peaks || peaks.length === 0) {
      ctx.fillStyle = waveColors.empty;
      ctx.fillRect(0, WAVE_H / 2 - 0.5, WAVE_W, 1);
      return;
    }
    const step = WAVE_W / peaks.length;
    const barW = Math.max(1 / canvas._dpr, step * 0.62);
    const playX = playing ? progress * WAVE_W : -1;
    for (let i = 0; i < peaks.length; i++) {
      const x = i * step;
      const bh = Math.max(1.5, peaks[i] * (WAVE_H - 2));
      ctx.fillStyle = playing ? (x < playX ? waveColors.played : waveColors.unplayed) : (selected ? waveColors.selected : waveColors.idle);
      ctx.fillRect(x, (WAVE_H - bh) / 2, barW, bh);
    }
  }

  // The plugin reports progress 20 times a second; in between, the playhead moves on its own so it glides.
  let playheadFrame = 0;
  function animatePlayhead() {
    if (playheadFrame) return;
    const tick = () => {
      playheadFrame = 0;
      if (!state.previewPlaying) return;
      const s = findSample(state.previewId);
      const row = s && rowFor(s);
      if (row) {
        let progress = state.previewProgress;
        const length = s.duration ? Math.min(s.duration, MAX_PREVIEW_SECONDS) : 0;
        if (length > 0) { // run ahead of the last report by at most a few reports' worth
          const ahead = Math.max(0, performance.now() - state.previewProgressAt) / 1000 / length;
          progress = Math.min(1, progress + Math.min(ahead, 0.15 / length));
        }
        drawWave(row, s, progress);
      }
      playheadFrame = requestAnimationFrame(tick);
    };
    playheadFrame = requestAnimationFrame(tick);
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
    } else if (state.source === "freesound") {
      const fs = state.fs;
      if (!fs.configured || fs.editing) {
        html = freesoundSetup();
      } else if (fs.error && state.results.length === 0) {
        html = `<h2>Freesound isn't responding</h2><p>${esc(fs.error)}</p>`
          + (/API key/.test(fs.error) ? `<button class="cta" data-action="fs-edit">Change the API key</button>` : "");
      } else if (fs.loading && state.results.length === 0) {
        html = `<h2>Searching Freesound...</h2>`;
      } else if (state.results.length === 0 && fs.searched !== null) {
        html = `<h2>No sounds found</h2><p>Try other words, or clear the BPM and key filters. Freesound only knows the tempo and key of sounds it has analysed.</p>`;
      }
    } else if (state.source === "samplefocus") {
      const sf = state.sf;
      if (!sf.enabled) {
        html = `<h2>Sample Focus</h2>`
          + `<p>Search and preview Sample Focus's free community samples right here, then drag them into FL Studio. Downloads use your own Sample Focus account and its download credits.</p>`
          + `<p class="fine">freeloops loads Sample Focus's public search pages for your searches, one request at a time, and plays previews through samplefocus.com the way its own player does (no credits). A sample is downloaded only when you drag or download it, through your own signed-in session, and each new download costs one of your Sample Focus credits. Samples stay under the Sample Focus licence: use them in your own music, but don't share or sell them on their own.</p>`
          + `<button class="cta" data-action="sf-enable">Turn on Sample Focus</button>`;
      } else if (sf.challenge || (sf.error === "challenge" && state.results.length === 0)) {
        html = `<h2>Sample Focus wants a browser check</h2>`
          + `<p>Sample Focus's security service (Cloudflare) wants to check that a person is using this browser. Open Sample Focus and answer the check yourself; freeloops picks up where it left off once it's done.</p>`
          + `<button class="cta" data-action="sf-check">${sf.checking ? "Waiting for the check..." : "Open Sample Focus"}</button>`;
      } else if (sf.error && state.results.length === 0) {
        html = `<h2>Sample Focus isn't responding</h2><p>${esc(sf.error)}</p>`;
      } else if (sf.loading && state.results.length === 0) {
        html = `<h2>Searching Sample Focus...</h2>`;
      } else if (state.results.length === 0 && sf.searched === "") {
        html = `<h2>Search Sample Focus</h2><p>Type what you're looking for, e.g. "trap piano".</p>`;
      } else if (state.results.length === 0 && sf.searched !== null) {
        html = `<h2>No samples found</h2><p>Try other words, or clear the BPM and key filters.</p>`;
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

  function freesoundSetup() {
    const fs = state.fs;
    return `<h2>Freesound</h2>`
      + `<p>Search and preview Freesound's Creative Commons sounds right here, then drag them into FL Studio. Freesound needs a free API key, made with your own Freesound account:</p>`
      + `<ol class="steps">`
      + `<li>Sign in at freesound.org (or make a free account) and <button class="link-btn inline" data-action="fs-apply">apply for API credentials</button>.</li>`
      + `<li>Any name and description will do. Keep the callback URL Freesound suggests.</li>`
      + `<li>Paste the <b>Client ID</b> and the <b>Client secret / API key</b> here.</li>`
      + `</ol>`
      + `<form class="fs-form">`
      + `<input id="fs-client-id" placeholder="Client ID" autocomplete="off" spellcheck="false" value="${esc(fs.clientId || "")}">`
      + `<input id="fs-api-key" placeholder="Client secret / API key" autocomplete="off" spellcheck="false">`
      + `<button class="cta" type="submit">Save</button>`
      + (fs.editing ? `<button class="link-btn" type="button" data-action="fs-cancel">Cancel</button>` : "")
      + `</form>`
      + `<p class="fine">The key stays on this computer, in freeloops' settings. Searches and previews use only the key; downloading the original files also needs you to sign in to Freesound. Every sound keeps its own licence (CC0, CC BY or CC BY-NC), shown on each result. CC BY sounds need credit when you release your track: freeloops adds it to CREDITS.txt next to the files you download.</p>`;
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
    renderFreesound();
  }

  function renderFreesound() {
    const fs = state.fs;
    els.fsBadge.textContent = !fs.configured ? "set up" : fs.signedIn ? "on" : "ready";
    els.fsDot.className = `dot ${fs.configured ? (fs.signedIn ? "on" : "ready") : ""}`;
    els.fsBar.hidden = state.source !== "freesound" || !fs.configured || fs.editing;
    els.fsStatus.textContent = fs.signedIn
      ? `Signed in${fs.username ? ` as ${fs.username}` : ""}. Downloads are saved to ${fs.folder} · C copies a sound's credit`
      : fs.signingIn
        ? "Finish signing in in the Freesound window..."
        : "Previews work with just the key. Sign in to drag the original files into FL Studio.";
    els.fsSignIn.hidden = fs.signedIn || !fs.canSignIn;
    els.fsSignIn.textContent = fs.signingIn ? "Signing in..." : "Sign in to download";
    els.fsSignIn.classList.toggle("primary", !fs.signedIn);
    els.fsSignOut.hidden = !fs.signedIn;
    els.fsKey.hidden = fs.builtIn;
    els.fsButton.classList.toggle("active", state.source === "freesound");
    renderSampleFocus();
  }

  function renderSampleFocus() {
    const sf = state.sf;
    const credits = sf.credits >= 0 ? ` · ${sf.credits} download credit${sf.credits === 1 ? "" : "s"} left` : "";
    els.sfBadge.textContent = !sf.enabled ? "off" : sf.signedIn ? "on" : "sign in";
    els.sfDot.className = `dot ${sf.enabled ? (sf.signedIn ? "on" : "ready") : ""}`;
    els.sfBar.hidden = state.source !== "samplefocus" || !sf.enabled;
    els.sfStatus.textContent = sf.checking
      ? "Answer the check in the Sample Focus window..."
      : sf.challenge
        ? "Sample Focus wants a browser check before it answers."
        : sf.signedIn
          ? `Signed in${credits}. Downloads are saved to ${sf.folder}`
          : sf.signingIn
            ? "Finish signing in in the Sample Focus window..."
            : "Previews work without an account. Sign in to drag samples into FL Studio.";
    els.sfCheck.hidden = !sf.challenge;
    els.sfCheck.classList.toggle("primary", sf.challenge);
    els.sfSignIn.hidden = sf.signedIn || sf.challenge;
    els.sfSignIn.textContent = sf.signingIn ? "Signing in..." : "Sign in to download";
    els.sfSignIn.classList.toggle("primary", !sf.signedIn);
    els.sfSignOut.hidden = !sf.signedIn;
    els.sfButton.classList.toggle("active", state.source === "samplefocus");
  }

  function renderStatus() {
    const src = online();
    if (src) {
      const st = src.st();
      const n = st.results.length;
      els.status.textContent = src.ready()
        ? `${src.label} · ${n} ${src.unit}${n === 1 ? "" : "s"} loaded${st.searched ? ` for "${st.searched}"` : ""}`
        : state.source === "freesound" ? "Freesound needs an API key" : `${src.label} is off`;
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
    els.toast.classList.add("show");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => els.toast.classList.remove("show"), 2600);
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
    const i = state.indexById.get(s.id);
    if (i === undefined) return false;
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
      const i = state.indexById.get(s.id) ?? 0;
      const top = i * ROW_H;
      if (top < els.list.scrollTop) els.list.scrollTop = top;
      else if (top + ROW_H > els.list.scrollTop + els.list.clientHeight) els.list.scrollTop = top + ROW_H - els.list.clientHeight;
    }
    if (audition) preview(s);
    else renderList();
  }

  function findSample(id) {
    return state.byId.get(id) || state.lm.byId.get(id) || state.fs.byId.get(id) || state.sf.byId.get(id);
  }

  function preview(s) {
    state.previewId = s.id;
    state.previewPlaying = true;
    state.previewProgress = 0;
    state.previewProgressAt = Infinity; // hold the playhead at the start until the plugin reports
    state.previewRequestedAt = performance.now();
    // An online sound is fetched before it plays (a Looperman loop is downloaded), which takes a moment.
    state.previewGraceMs = s.src && !s.path ? 20000 : 500;
    if (s.src) {
      window.fl.call(`${s.src}Preview`, s.id).then((r) => {
        if (!r || r.error) {
          const error = r && r.error;
          if (s.src === "fs") toast(error ? `Couldn't load the preview from Freesound: ${error}` : "Couldn't load the preview from Freesound.");
          else if (s.src === "sf") toast(error === "challenge" ? SF_ERRORS.challenge
            : error ? `Couldn't load the preview from Sample Focus: ${error}` : "Couldn't load the preview from Sample Focus.");
          else toast(error === "signed-out"
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
    if (s.src) return;
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

  // ---------------------------------------------------------------- online sources

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
    if (lm.enabled && !wasEnabled) searchOnline("looperman", state.query, 1);
    else applyFilters({ keepScroll: true });
  }

  const FS_ERRORS = {
    "signed-out": "Sign in to Freesound to download the original file.",
    "not-a-file": "Freesound sent a web page instead of the file. Right-click the sound to open it on Freesound.",
  };

  function applyFreesoundState(st) {
    if (!st) return;
    const fs = state.fs;
    const wasReady = SOURCES.freesound.ready();
    for (const k of ["configured", "canSignIn", "builtIn", "signedIn", "signingIn"]) fs[k] = !!st[k];
    fs.clientId = st.clientId || "";
    fs.username = st.username || "";
    fs.folder = st.folder || "";
    if (st.error) toast(st.error);
    renderLooperman();
    if (state.source !== "freesound") return;
    if (SOURCES.freesound.ready() && !wasReady) searchOnline("freesound", state.query, 1);
    else applyFilters({ keepScroll: true });
  }

  const SF_ERRORS = {
    "signed-out": "Sign in to Sample Focus to download samples.",
    "out-of-credits": "You're out of Sample Focus download credits. Upload samples on samplefocus.com to earn more.",
    "not-a-file": "Sample Focus sent a web page instead of the file. Right-click the sample to open it on Sample Focus.",
    "zip-not-supported": "This sample downloads as a zip, which freeloops can't unpack yet.",
    "timed out": "The download from Sample Focus timed out.",
    challenge: "Sample Focus wants a browser check first. Click \"Open Sample Focus\" and answer it.",
  };

  function applySampleFocusState(st) {
    if (!st) return;
    const sf = state.sf;
    const wasEnabled = sf.enabled;
    const wasChallenged = sf.challenge;
    for (const k of ["enabled", "signedIn", "signingIn", "challenge", "checking"]) sf[k] = !!st[k];
    sf.credits = typeof st.credits === "number" ? st.credits : -1;
    sf.folder = st.folder || "";
    if (sf.challenge) sf.sig = null; // search again once the check is answered
    renderLooperman();
    if (state.source !== "samplefocus") return;
    if (sf.enabled && (!wasEnabled || (wasChallenged && !sf.challenge))) searchOnline("samplefocus", state.query, 1);
    else applyFilters({ keepScroll: true });
  }

  function searchOnline(name, query, page) {
    const src = SOURCES[name];
    const st = src.st();
    if (!src.ready()) return;
    const token = (st.token = (st.token || 0) + 1);
    st.loading = true;
    st.error = null;
    if (page === 1) {
      st.results = [];
      st.byId = new Map();
      st.searched = query;
      st.sig = src.signature(query);
      st.hasMore = false;
      if (state.source === name) applyFilters();
    }
    src.search(query, page).then((r) => {
      if (token !== st.token || (r && r.superseded)) return; // a newer search replaced this one
      st.loading = false;
      if (r && r.error === "no-key") {
        state.fs.configured = false;
        st.sig = null;
        renderLooperman();
      } else if (!r || r.error) {
        st.error = (r && r.error) || "No response";
      } else {
        for (const raw of r[src.list] || []) {
          if (st.byId.has(raw.id)) continue;
          const s = enrichOnline(raw, src.prefix);
          s.order = st.results.length;
          st.byId.set(s.id, s);
          st.results.push(s);
        }
        st.page = page;
        st.hasMore = !!r.hasMore;
      }
      if (state.source === name) applyFilters({ keepScroll: page > 1 });
    });
  }

  // A new search on the current source if the words or the filters changed.
  function refreshOnline() {
    const src = online();
    if (src && src.ready() && src.signature(state.query) !== src.st().sig) searchOnline(state.source, state.query, 1);
    else applyFilters();
  }

  function maybeLoadMore() {
    const src = online();
    const st = src && src.st();
    if (!st || !st.hasMore || st.loading) return;
    if (els.list.scrollTop + els.list.clientHeight < els.list.scrollHeight - ROW_H * 6) return;
    searchOnline(state.source, st.searched ?? "", st.page + 1);
  }

  // Online sounds need your account to download; only then can they be dragged into FL.
  // onReady runs when the file is saved (used to start a drag the user is still holding).
  function downloadOnline(s, onReady) {
    if (s.path || s.downloading) return;
    const src = sourceOf(s);
    const errors = { fs: FS_ERRORS, sf: SF_ERRORS }[s.src] || LM_ERRORS;
    if (!src.st().signedIn) {
      toast(errors["signed-out"]);
      return;
    }
    s.downloading = true;
    toast(onReady ? `Getting the ${src.unit} from ${src.label}... keep holding to drag it.` : `Downloading from ${src.label}...`);
    renderList();
    window.fl.call(`${s.src}Download`, s.id).then((r) => {
      s.downloading = false;
      if (r && r.path) {
        s.path = r.path;
        if (!(onReady && onReady())) {
          const credits = s.src === "sf" && state.sf.credits >= 0 ? ` ${state.sf.credits} Sample Focus credit${state.sf.credits === 1 ? "" : "s"} left.` : "";
          toast(s.license.startsWith("CC BY") ? `Ready. ${s.license}: credit ${s.author} when you release it.` : `Ready. Drag it into FL Studio.${credits}`);
        }
      } else {
        const error = r && r.error;
        toast(errors[error] || `Download failed: ${error || "unknown error"}`);
      }
      renderList();
    });
  }

  function copyCredit(s) {
    if (s.src !== "fs") return;
    window.fl.call("fsCopyCredit", s.id).then((ok) => ok && toast(`Credit for "${s.name}" copied.`));
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

    if (s.src && !s.path) {
      // Not downloaded yet: fetch it, and start the drag if the button is still held when it's saved.
      const held = press;
      return downloadOnline(s, () => {
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
    window.fl.call("startDrag", s.src ? s.path : s.id).then((ok) => {
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
    else if (target.closest(".dl")) downloadOnline(s);
    else if (target.closest(".play")) togglePreview(s);
    else if (target.closest(".row")) {
      if (s.id === state.selectedId) togglePreview(s);
      else select(s, { scroll: false });
    }
    els.list.focus({ preventScroll: true });
  });

  function endDrag() {
    document.body.classList.remove("dragging");
    for (const row of rowEls.values()) row.classList.remove("dragging");
  }

  els.list.addEventListener("contextmenu", (e) => {
    const row = e.target.closest(".row");
    if (!row) return;
    const s = state.results[+row.dataset.i];
    if (s) window.fl.call(s.src ? `${s.src}Open` : "reveal", s.id);
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

  window.addEventListener("resize", () => {
    for (const row of [...rowEls.values(), ...spareRows]) { // the display's scale may have changed
      sizeCanvas(row._wave);
      row._waveKey = null;
    }
    renderList();
  });

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
      case "c": case "C": if (s && !e.ctrlKey) copyCredit(s); break;
      case "/": e.preventDefault(); els.search.focus(); break;
    }
  });

  // ---------------------------------------------------------------- input: controls

  // Local folders filter as you type; online sources search once you pause (or press Enter).
  let searchTimer = 0;
  function runSearch() {
    state.query = els.search.value.trim();
    refreshOnline();
  }
  els.search.addEventListener("input", () => {
    clearTimeout(searchTimer);
    searchTimer = setTimeout(runSearch, online() ? 700 : 120);
  });

  // Online sources filter by BPM and key themselves, so a filter change there is a new search.
  let filterTimer = 0;
  function filtersChanged() {
    if (!online()) return applyFilters();
    clearTimeout(filterTimer);
    filterTimer = setTimeout(refreshOnline, 400);
  }

  function onlineFilters() {
    const filters = {};
    if (state.bpmMode === "host" && state.hostBpm) {
      const h = Math.round(state.hostBpm);
      filters.bpm = [h, h * 2, h / 2].map((t) => [t - HOST_BPM_TOLERANCE, t + HOST_BPM_TOLERANCE]);
    } else if (state.bpmMode === "range") {
      const lo = parseFloat(els.bpmMin.value);
      const hi = parseFloat(els.bpmMax.value);
      if (!isNaN(lo) || !isNaN(hi)) filters.bpm = [[isNaN(lo) ? null : lo, isNaN(hi) ? null : hi]];
    }
    if (state.key) filters.key = state.key;
    return filters;
  }
  els.search.addEventListener("keydown", (e) => {
    if (e.key !== "Enter") return;
    clearTimeout(searchTimer);
    runSearch();
  });

  els.bpmMode.addEventListener("change", () => {
    state.bpmMode = els.bpmMode.value;
    els.bpmRange.hidden = state.bpmMode !== "range";
    filtersChanged();
  });
  els.bpmMin.addEventListener("input", filtersChanged);
  els.bpmMax.addEventListener("input", filtersChanged);

  els.key.innerHTML = `<option value="">Any</option>`
    + SHARPS.map((n) => `<option value="${n}">${n} major</option><option value="${n}m">${n} minor</option>`).join("");
  els.key.addEventListener("change", () => {
    state.key = els.key.value;
    filtersChanged();
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

  for (const btn of [els.lmButton, els.fsButton, els.sfButton]) {
    btn.addEventListener("click", () => {
      for (const v of els.views.querySelectorAll(".view")) v.classList.remove("active");
      state.source = btn.dataset.source;
      state.query = els.search.value.trim();
      renderLooperman();
      refreshOnline();
    });
  }

  els.lmSignIn.addEventListener("click", () => window.fl.call("lmSignIn"));
  els.lmSignOut.addEventListener("click", () => window.fl.call("lmSignOut"));
  els.lmOff.addEventListener("click", () => window.fl.call("lmSetEnabled", false).then(applyLoopermanState));
  els.fsSignIn.addEventListener("click", () => window.fl.call("fsSignIn").then(applyFreesoundState));
  els.fsSignOut.addEventListener("click", () => window.fl.call("fsSignOut").then(applyFreesoundState));
  els.fsKey.addEventListener("click", () => editFreesoundKey(true));
  els.sfSignIn.addEventListener("click", () => window.fl.call("sfSignIn"));
  els.sfSignOut.addEventListener("click", () => window.fl.call("sfSignOut"));
  els.sfOff.addEventListener("click", () => window.fl.call("sfSetEnabled", false).then(applySampleFocusState));
  els.sfCheck.addEventListener("click", () => window.fl.call("sfCheck"));

  function editFreesoundKey(on) {
    state.fs.editing = on;
    renderLooperman();
    if (on) applyFilters();
    else refreshOnline();
  }

  document.getElementById("add-folder").addEventListener("click", addFolder);
  els.empty.addEventListener("click", (e) => {
    if (e.target.closest("[data-action=add-folder]")) addFolder();
    if (e.target.closest("[data-action=lm-enable]")) window.fl.call("lmSetEnabled", true).then(applyLoopermanState);
    if (e.target.closest("[data-action=sf-enable]")) window.fl.call("sfSetEnabled", true).then(applySampleFocusState);
    if (e.target.closest("[data-action=sf-check]")) window.fl.call("sfCheck");
    if (e.target.closest("[data-action=fs-apply]")) window.fl.call("fsOpenApply");
    if (e.target.closest("[data-action=fs-edit]")) editFreesoundKey(true);
    if (e.target.closest("[data-action=fs-cancel]")) editFreesoundKey(false);
  });
  els.empty.addEventListener("submit", (e) => {
    e.preventDefault();
    const clientId = $("fs-client-id").value.trim();
    const apiKey = $("fs-api-key").value.trim();
    if (!clientId || !apiKey) return toast("Paste both the Client ID and the API key.");
    state.fs.editing = false;
    state.fs.sig = null; // search again with the new key
    window.fl.call("fsSetCredentials", clientId, apiKey).then((st) => {
      applyFreesoundState(st);
      refreshOnline();
    });
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
      if (state.bpmMode === "host") {
        if (online()) filtersChanged();
        else applyFilters({ keepScroll: true });
      }
    }

    // The native side needs a moment to load a clip; don't flip back to "stopped" while it does.
    const loading = performance.now() - (state.previewRequestedAt || 0) < (state.previewGraceMs || 500);
    if (loading && !st.previewPlaying) return;

    const wasPlaying = state.previewPlaying;
    const prevId = state.previewId;
    if (st.previewId) state.previewId = st.previewId;
    state.previewPlaying = !!st.previewPlaying;
    state.previewProgress = st.previewProgress || 0;
    state.previewProgressAt = performance.now();
    if (wasPlaying !== state.previewPlaying || prevId !== state.previewId) renderList();
    if (state.previewPlaying) animatePlayhead();
  });

  window.fl.on("lmState", applyLoopermanState);
  window.fl.on("fsState", applyFreesoundState);
  window.fl.on("sfState", applySampleFocusState);

  window.fl.call("getState").then(loadLibrary);
  window.fl.call("lmState").then(applyLoopermanState);
  window.fl.call("fsState").then(applyFreesoundState);
  window.fl.call("sfState").then(applySampleFocusState);
  renderTempo();
  renderLooperman();
})();
