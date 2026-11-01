const { app, BrowserWindow, ipcMain, nativeImage, dialog, shell } = require('electron');
const path = require('node:path');
const fs = require('node:fs');
const { pathToFileURL } = require('node:url');
const { writeTestLoops } = require('./wav');

// Only paths in this set may be dragged, so the renderer can't ask for arbitrary files.
const allowed = new Set();
let win;
let dragIcon;

// Solid 32x32 swatch; startDrag needs a non-empty icon.
function makeDragIcon() {
  const size = 32;
  const bgra = Buffer.alloc(size * size * 4);
  for (let i = 0; i < size * size; i++) bgra.set([0x10, 0x8c, 0xff, 0xff], i * 4);
  return nativeImage.createFromBitmap(bgra, { width: size, height: size });
}

function describe(file) {
  allowed.add(file);
  return {
    path: file,
    name: path.basename(file),
    size: fs.statSync(file).size,
    url: pathToFileURL(file).href,
  };
}

app.whenReady().then(() => {
  // Not "cache": on Windows that is the same folder as Chromium's own Cache dir.
  const cacheDir = path.join(app.getPath('userData'), 'samples');
  const files = writeTestLoops(cacheDir);
  dragIcon = makeDragIcon();

  ipcMain.handle('list-files', () => files.map(describe));

  ipcMain.handle('add-files', async () => {
    const { canceled, filePaths } = await dialog.showOpenDialog(win, {
      properties: ['openFile', 'multiSelections'],
      filters: [{ name: 'Audio', extensions: ['wav', 'mp3', 'ogg', 'flac', 'aif', 'aiff'] }],
    });
    return canceled ? [] : filePaths.map(describe);
  });

  ipcMain.on('start-drag', (event, file) => {
    if (!allowed.has(file) || !fs.existsSync(file)) return;
    event.sender.startDrag({ file, icon: dragIcon });
  });

  ipcMain.on('reveal', (_event, file) => {
    if (allowed.has(file)) shell.showItemInFolder(file);
  });

  ipcMain.on('set-always-on-top', (_event, on) => win.setAlwaysOnTop(Boolean(on)));

  win = new BrowserWindow({
    width: 520,
    height: 560,
    title: 'freeloops drag spike',
    backgroundColor: '#15161a',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      sandbox: true,
    },
  });
  win.setMenuBarVisibility(false);
  win.loadFile('index.html');
});

app.on('window-all-closed', () => app.quit());
