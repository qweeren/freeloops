// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Generates small test loops so the spike needs no downloaded audio.
const fs = require('node:fs');
const path = require('node:path');

const SAMPLE_RATE = 44100;

// Mono float samples (-1..1) -> 16-bit PCM WAV buffer.
function encodeWav(samples, sampleRate = SAMPLE_RATE) {
  const dataSize = samples.length * 2;
  const buf = Buffer.alloc(44 + dataSize);
  buf.write('RIFF', 0);
  buf.writeUInt32LE(36 + dataSize, 4);
  buf.write('WAVE', 8);
  buf.write('fmt ', 12);
  buf.writeUInt32LE(16, 16); // fmt chunk size
  buf.writeUInt16LE(1, 20); // PCM
  buf.writeUInt16LE(1, 22); // mono
  buf.writeUInt32LE(sampleRate, 24);
  buf.writeUInt32LE(sampleRate * 2, 28); // byte rate
  buf.writeUInt16LE(2, 32); // block align
  buf.writeUInt16LE(16, 34); // bits per sample
  buf.write('data', 36);
  buf.writeUInt32LE(dataSize, 40);
  for (let i = 0; i < samples.length; i++) {
    const s = Math.max(-1, Math.min(1, samples[i]));
    buf.writeInt16LE(Math.round(s * 32767), 44 + i * 2);
  }
  return buf;
}

// Two bars of four-on-the-floor kick: a pitch-swept sine with exponential decay.
function kickLoop(bpm = 140, bars = 2) {
  const beat = 60 / bpm;
  const out = new Float32Array(Math.round(beat * 4 * bars * SAMPLE_RATE));
  for (let b = 0; b < 4 * bars; b++) {
    const start = Math.round(b * beat * SAMPLE_RATE);
    let phase = 0;
    for (let i = 0; i < beat * SAMPLE_RATE && start + i < out.length; i++) {
      const t = i / SAMPLE_RATE;
      phase += (2 * Math.PI * (50 + 100 * Math.exp(-t * 30))) / SAMPLE_RATE;
      out[start + i] = 0.9 * Math.sin(phase) * Math.exp(-t * 8);
    }
  }
  return out;
}

// Two bars of A-minor eighth-note arpeggio with plucky envelopes.
function arpLoop(bpm = 120, bars = 2) {
  const notes = [220, 261.63, 329.63, 440, 329.63, 261.63, 220, 164.81]; // A3 C4 E4 A4 E4 C4 A3 E3
  const step = 60 / bpm / 2;
  const out = new Float32Array(Math.round(step * 8 * bars * SAMPLE_RATE));
  for (let n = 0; n < 8 * bars; n++) {
    const freq = notes[n % notes.length];
    const start = Math.round(n * step * SAMPLE_RATE);
    for (let i = 0; i < step * SAMPLE_RATE && start + i < out.length; i++) {
      const t = i / SAMPLE_RATE;
      const tone = Math.sin(2 * Math.PI * freq * t) + 0.3 * Math.sin(4 * Math.PI * freq * t);
      out[start + i] = 0.45 * tone * Math.exp(-t * 6) * Math.min(1, t * 400);
    }
  }
  return out;
}

// Writes the loops (named the way §3.2 of the plan names cached files) and returns their paths.
function writeTestLoops(dir) {
  fs.mkdirSync(dir, { recursive: true });
  const loops = [
    ['freeloops - Kick Loop - 140bpm.wav', kickLoop(140)],
    ['freeloops - Arp - 120bpm - Am.wav', arpLoop(120)],
  ];
  return loops.map(([name, samples]) => {
    const file = path.join(dir, name);
    if (!fs.existsSync(file)) fs.writeFileSync(file, encodeWav(samples));
    return file;
  });
}

module.exports = { encodeWav, writeTestLoops };
