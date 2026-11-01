// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

const api = window.freeloops;
const list = document.getElementById('list');
const logEl = document.getElementById('log');
const player = document.getElementById('player');

const DRAG_THRESHOLD = 4; // px of movement before a press counts as a drag

function log(msg) {
  logEl.textContent += `${new Date().toLocaleTimeString()}  ${msg}\n`;
  logEl.scrollTop = logEl.scrollHeight;
}

function mode() {
  return document.querySelector('input[name="mode"]:checked').value;
}

function addRow(file) {
  const li = document.createElement('li');
  li.innerHTML = '<button class="play" title="Preview">▶</button><span class="name"></span><span class="size"></span><button class="reveal" title="Show in Explorer">📁</button>';
  li.querySelector('.name').textContent = file.name;
  li.querySelector('.size').textContent = `${(file.size / 1024).toFixed(0)} KB`;

  li.querySelector('.play').addEventListener('click', () => {
    player.src = file.url;
    player.play().then(() => log(`preview: ${file.name}`), (err) => log(`preview failed: ${err.message}`));
  });
  li.querySelector('.reveal').addEventListener('click', () => api.reveal(file.path));

  // Mode "native": the plan's rule (§2.3) — detect the gesture in JS, let the main process start the OS drag.
  let press = null;
  li.addEventListener('pointerdown', (e) => {
    if (e.button !== 0 || e.target.closest('button')) return;
    press = { x: e.clientX, y: e.clientY, fired: false };
  });
  li.addEventListener('pointermove', (e) => {
    if (mode() !== 'native' || !press || press.fired || !(e.buttons & 1)) return;
    if (Math.hypot(e.clientX - press.x, e.clientY - press.y) < DRAG_THRESHOLD) return;
    press.fired = true;
    api.startDrag(file.path);
    log(`drag started (native gesture): ${file.name}`);
  });
  li.addEventListener('pointerup', () => { press = null; });

  // Mode "html5": Electron's documented pattern — cancel the HTML drag and hand off to webContents.startDrag.
  li.addEventListener('dragstart', (e) => {
    e.preventDefault();
    if (mode() !== 'html5') return;
    api.startDrag(file.path);
    log(`drag started (HTML5 dragstart): ${file.name}`);
  });

  list.appendChild(li);
}

function syncDraggable() {
  const html5 = mode() === 'html5';
  for (const li of list.children) li.draggable = html5;
}

document.querySelectorAll('input[name="mode"]').forEach((r) => r.addEventListener('change', () => {
  syncDraggable();
  log(`mode: ${mode()}`);
}));
document.getElementById('ontop').addEventListener('change', (e) => api.setAlwaysOnTop(e.target.checked));
document.getElementById('add').addEventListener('click', async () => {
  const added = await api.addFiles();
  added.forEach(addRow);
  syncDraggable();
});

api.listFiles().then((files) => {
  files.forEach(addRow);
  syncDraggable();
  log(`cache: ${files.length ? files[0].path.replace(/[\\/][^\\/]+$/, '') : '(empty)'}`);
});
