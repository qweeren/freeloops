// freeloops - free, open-source sample browser
// Copyright (C) 2026 Eren and the freeloops contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('freeloops', {
  listFiles: () => ipcRenderer.invoke('list-files'),
  addFiles: () => ipcRenderer.invoke('add-files'),
  startDrag: (file) => ipcRenderer.send('start-drag', file),
  reveal: (file) => ipcRenderer.send('reveal', file),
  setAlwaysOnTop: (on) => ipcRenderer.send('set-always-on-top', on),
});
