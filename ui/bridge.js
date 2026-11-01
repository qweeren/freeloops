// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// window.fl: the only way the UI talks to the native side.
//   fl.call(name, ...args) -> Promise   (JUCE native functions, see plugin/Source/PluginEditor.cpp)
//   fl.on(event, handler)               (events emitted by the plugin: "library", "status", "dragEnded")
// Inside the plugin this speaks JUCE's WebBrowserComponent protocol. In a normal browser it falls
// back to a mock library so the UI can be developed without building the plugin.
(() => {
  "use strict";

  const juce = window.__JUCE__;

  if (juce && juce.backend) {
    let nextId = 0;
    const pending = new Map();

    juce.backend.addEventListener("__juce__complete", ({ promiseId, result }) => {
      const resolve = pending.get(promiseId);
      if (resolve) {
        pending.delete(promiseId);
        resolve(result);
      }
    });

    window.fl = {
      native: true,
      call: (name, ...params) => new Promise((resolve) => {
        const id = nextId++;
        pending.set(id, resolve);
        juce.backend.emitEvent("__juce__invoke", { name, params, resultId: id });
      }),
      on: (event, handler) => juce.backend.addEventListener(event, handler),
    };
    return;
  }

  window.fl = createMock();

  function createMock() {
    const listeners = {};
    const emit = (event, data) => (listeners[event] || []).forEach((fn) => fn(data));

    const loopWords = ["Dark", "Trap", "Melody", "Pluck", "Keys", "Guitar", "Bell", "Pad", "Vox", "Chop", "Drill", "Lofi", "Piano", "Synth", "Flute", "Strings"];
    const shotWords = ["Kick", "Snare", "Clap", "Hat", "Open Hat", "Perc", "808", "Rim", "Crash", "FX", "Riser", "Tom"];
    const keys = ["Am", "C#m", "F", "Gm", "D#", "Bm", "E", "F#m", "Cm", "G"];
    const folders = ["C:\\Samples\\MZ Drum Kit 2019", "C:\\Samples\\Free Loops"];
    let favorites = new Set();
    let samples = [];

    function rand(seed) {
      let s = seed >>> 0;
      return () => ((s = (s * 1664525 + 1013904223) >>> 0) / 4294967296);
    }

    function build() {
      const r = rand(7);
      const pick = (arr) => arr[Math.floor(r() * arr.length)];
      samples = [];
      if (folders.includes("C:\\Samples\\MZ Drum Kit 2019")) {
        for (let i = 0; i < 180; i++) {
          const kind = pick(shotWords);
          const name = `${kind} ${pick(["Hard", "Dusty", "909", "Long", "Short", "Punchy", "Wide"])} ${i + 1}`;
          const path = `C:\\Samples\\MZ Drum Kit 2019\\${kind}\\${name}.wav`;
          samples.push({ id: path, name, folder: `MZ Drum Kit 2019\\${kind}`, size: 40000 + Math.floor(r() * 200000) });
        }
      }
      if (folders.includes("C:\\Samples\\Free Loops")) {
        for (let i = 0; i < 240; i++) {
          const bpm = pick([90, 120, 128, 140, 142, 150, 160, 174]);
          const name = `${pick(loopWords)} ${pick(loopWords)} Loop ${bpm}bpm ${pick(keys)}`;
          const path = `C:\\Samples\\Free Loops\\${name} ${i}.wav`;
          samples.push({ id: path, name: `${name} ${i}`, folder: "Free Loops", size: 300000 + Math.floor(r() * 2000000) });
        }
      }
      for (const extra of folders.slice(2)) {
        const short = extra.split("\\").pop();
        for (let i = 0; i < 12; i++) {
          const name = `${short} Sample ${i + 1}`;
          samples.push({ id: `${extra}\\${name}.wav`, name, folder: short, size: 100000 });
        }
      }
      // Like the plugin: sorted by name, naturally.
      samples.sort((a, b) => a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: "base" }));
    }

    function state() {
      return {
        folders: folders.slice(),
        scanning: false,
        samples: samples.map((s) => ({ ...s, fav: favorites.has(s.id) })),
      };
    }

    let preview = null;
    let progress = 0;
    let host = 140;
    setInterval(() => {
      if (preview) {
        progress = Math.min(1, progress + 0.025);
        if (progress >= 1) preview = null;
      }
      emit("status", { bpm: host, hostPlaying: false, previewId: preview, previewProgress: progress, previewPlaying: !!preview });
    }, 100);

    function peaksFor(id) {
      let h = 0;
      for (const c of id) h = (h * 31 + c.charCodeAt(0)) >>> 0;
      const r = rand(h);
      const shot = !/loop/i.test(id);
      const peaks = [];
      for (let i = 0; i < 96; i++) {
        const t = i / 96;
        const env = shot ? Math.exp(-t * 7) : 0.45 + 0.4 * Math.abs(Math.sin(t * Math.PI * 8));
        peaks.push(Math.round(Math.min(1, env * (0.6 + 0.4 * r())) * 100) / 100);
      }
      return { duration: shot ? 0.3 + r() * 1.2 : 6.86, peaks };
    }

    const api = {
      getState: () => state(),
      addFolder: () => {
        folders.push(`C:\\Samples\\New Folder ${folders.length - 1}`);
        build();
        setTimeout(() => emit("library", state()), 200);
        return true;
      },
      removeFolder: (path) => {
        const i = folders.indexOf(path);
        if (i >= 0) folders.splice(i, 1);
        build();
        setTimeout(() => emit("library", state()), 100);
        return true;
      },
      rescan: () => {
        setTimeout(() => emit("library", state()), 400);
        return true;
      },
      setFavorite: (id, on) => {
        on ? favorites.add(id) : favorites.delete(id);
        return true;
      },
      preview: (id) => {
        preview = id;
        progress = 0;
        return true;
      },
      stopPreview: () => {
        preview = null;
        return true;
      },
      getPeaks: (id) => peaksFor(id),
      startDrag: (id) => {
        console.log("[mock] startDrag", id);
        setTimeout(() => emit("dragEnded", {}), 600);
        return true;
      },
      reveal: (id) => {
        console.log("[mock] reveal", id);
        return true;
      },

      lmState: () => ({ ...lm, signingIn: false, folder: "C:\\Users\\you\\Music\\freeloops\\Looperman" }),
      lmSetEnabled: (on) => {
        lm.enabled = !!on;
        if (!on) lm.signedIn = false;
        return api.lmState();
      },
      lmSearch: (query, page, filters) => new Promise((resolve) => setTimeout(() => {
        const r = rand(page * 977 + query.length + JSON.stringify(filters || {}).length);
        const range = filters && filters.bpm && filters.bpm[0];
        const pick = (arr) => arr[Math.floor(r() * arr.length)];
        const loops = [];
        for (let i = 0; i < 25; i++) {
          const n = (page - 1) * 25 + i;
          const title = `${query ? query.replace(/\b\w/g, (c) => c.toUpperCase()) + " " : ""}${pick(loopWords)} ${pick(loopWords)} Loop`;
          loops.push({
            id: `lm:${430000 + n}`, name: title, author: pick(["NOFVCEXX", "Danke", "SoulBeats", "Lowkey808", "ProdByRay"]),
            genre: pick(["Trap", "Hip Hop", "Lofi", "Drill", "House"]), category: pick(["Piano", "Synth", "Guitar", "Vocal"]),
            key: (filters && filters.key) || pick(["D#m", "Am", "C", "F#m", "A#", ""]),
            bpm: range ? Math.round(range[0] ?? 100) + 1 : pick([90, 120, 130, 140, 145, 160]),
            duration: 8 + r() * 25, url: "", path: "",
          });
        }
        resolve({ loops, page, hasMore: page < 3 });
      }, 700)),
      lmPreview: (id) => {
        preview = id;
        progress = 0;
        return true;
      },
      lmDownload: (id) => new Promise((resolve) => setTimeout(() => {
        resolve(lm.signedIn ? { path: `C:\\Users\\you\\Music\\freeloops\\Looperman\\${id.slice(3)}.wav` } : { error: "signed-out" });
      }, 900)),
      lmSignIn: () => {
        setTimeout(() => {
          lm.signedIn = true;
          emit("lmState", api.lmState());
        }, 800);
        return true;
      },
      lmSignOut: () => {
        lm.signedIn = false;
        emit("lmState", api.lmState());
        return true;
      },
      lmOpen: (id) => {
        console.log("[mock] open on Looperman", id);
        return true;
      },

      fsState: () => ({
        ...fs, configured: !!fs.apiKey, canSignIn: !!(fs.apiKey && fs.clientId), builtIn: false,
        apiKey: undefined, signingIn: false, folder: "C:\\Users\\you\\Music\\freeloops\\Freesound",
      }),
      fsSetCredentials: (clientId, apiKey) => {
        Object.assign(fs, { clientId, apiKey, signedIn: false, username: "" });
        return api.fsState();
      },
      fsSearch: (query, page, filters) => new Promise((resolve) => setTimeout(() => {
        if (!fs.apiKey) return resolve({ error: "no-key" });
        const r = rand(page * 131 + query.length + JSON.stringify(filters || {}).length);
        const pick = (arr) => arr[Math.floor(r() * arr.length)];
        const sounds = [];
        for (let i = 0; i < 30; i++) {
          const n = (page - 1) * 30 + i;
          const loop = r() < 0.5;
          const range = filters && filters.bpm && filters.bpm[0];
          const bpm = range ? Math.round(range[0] ?? 100) + 1 : loop && r() < 0.7 ? pick([90, 100, 120, 128, 140]) : 0;
          sounds.push({
            id: `fs:${400000 + n}`, name: `${query ? query + " " : ""}${loop ? `${pick(loopWords)} loop` : pick(shotWords)} ${n}`,
            author: pick(["InspectorJ", "klankbeeld", "Erokia", "Setuniman", "unfa"]),
            license: pick(["CC0", "CC BY 4.0", "CC BY 3.0", "CC BY-NC 4.0"]),
            key: filters && filters.key ? filters.key : pick(["Am", "C", "F#m", "", ""]), bpm,
            duration: loop ? 4 + r() * 12 : 0.2 + r() * 2, url: "", path: "",
          });
        }
        resolve({ sounds, page, hasMore: page < 4 });
      }, 500)),
      fsPreview: (id) => {
        preview = id;
        progress = 0;
        return { ok: true };
      },
      fsDownload: (id) => new Promise((resolve) => setTimeout(() => {
        resolve(fs.signedIn ? { path: `C:\\Users\\you\\Music\\freeloops\\Freesound\\${id.slice(3)}.wav` } : { error: "signed-out" });
      }, 900)),
      fsSignIn: () => {
        setTimeout(() => {
          Object.assign(fs, { signedIn: true, username: "you" });
          emit("fsState", api.fsState());
        }, 800);
        return { ...api.fsState(), signingIn: true };
      },
      fsSignOut: () => {
        Object.assign(fs, { signedIn: false, username: "" });
        return api.fsState();
      },
      fsOpen: (id) => {
        console.log("[mock] open on Freesound", id);
        return true;
      },
      fsOpenApply: () => {
        console.log("[mock] open freesound.org/apiv2/apply");
        return true;
      },
      fsCopyCredit: (id) => {
        console.log("[mock] copy credit", id);
        return true;
      },

      sfState: () => ({
        ...sf, signingIn: false, checking: false, credits: sf.signedIn ? sf.credits : -1, folder: "C:\\Users\\you\\Music\\freeloops\\Sample Focus",
      }),
      sfCheck: () => {
        setTimeout(() => {
          sf.challenge = false;
          emit("sfState", api.sfState());
        }, 800);
        return true;
      },
      sfSetEnabled: (on) => {
        sf.enabled = !!on;
        if (!on) sf.signedIn = false;
        return api.sfState();
      },
      sfSearch: (query, page, filters) => new Promise((resolve) => setTimeout(() => {
        if (!query) return resolve({ samples: [], page, hasMore: false });
        const r = rand(page * 53 + query.length + JSON.stringify(filters || {}).length);
        const range = filters && filters.bpm && filters.bpm[0];
        const pick = (arr) => arr[Math.floor(r() * arr.length)];
        const samples = [];
        for (let i = 0; i < 20; i++) {
          const n = (page - 1) * 20 + i;
          const loop = r() < 0.6;
          samples.push({
            id: `sf:${580000 + n}`, name: `${query.replace(/\b\w/g, (c) => c.toUpperCase())} ${loop ? `${pick(loopWords)} Loop` : pick(shotWords)}`,
            author: pick(["MN Beat", "Carter Schaaf", "briann", "Paul Roys Watson II", "T2"]),
            tags: loop ? ["loop", pick(["dark", "chill", "bouncy"]), pick(["trap", "hip hop", "lo-fi"])] : ["one shot", pick(["punchy", "dry"])],
            loop, key: (filters && filters.key) || pick(["Am", "F#m", "C", "D", ""]),
            bpm: loop ? (range ? Math.round(range[0] ?? 100) + 1 : pick([90, 120, 140, 150])) : 0,
            duration: loop ? 6 + r() * 12 : 0.3 + r() * 2, url: "", path: "",
          });
        }
        resolve({ samples, page, hasMore: page < 3 });
      }, 600)),
      sfPreview: (id) => {
        preview = id;
        progress = 0;
        return { ok: true };
      },
      sfDownload: (id) => new Promise((resolve) => setTimeout(() => {
        if (!sf.signedIn) return resolve({ error: "signed-out" });
        if (sf.credits <= 0) return resolve({ error: "out-of-credits" });
        sf.credits--;
        emit("sfState", api.sfState());
        resolve({ path: `C:\\Users\\you\\Music\\freeloops\\Sample Focus\\${id.slice(3)}.wav` });
      }, 900)),
      sfSignIn: () => {
        setTimeout(() => {
          sf.signedIn = true;
          emit("sfState", api.sfState());
        }, 800);
        return true;
      },
      sfSignOut: () => {
        sf.signedIn = false;
        emit("sfState", api.sfState());
        return true;
      },
      sfOpen: (id) => {
        console.log("[mock] open on Sample Focus", id);
        return true;
      },
    };
    const lm = { enabled: false, signedIn: false };
    const fs = { clientId: "", apiKey: "", signedIn: false, username: "" };
    const sf = { enabled: false, signedIn: false, challenge: false, credits: 3 };

    build();

    return {
      native: false,
      call: (name, ...args) => new Promise((resolve) => setTimeout(() => resolve(api[name](...args)), 15)),
      on: (event, handler) => (listeners[event] ||= []).push(handler),
    };
  }
})();
