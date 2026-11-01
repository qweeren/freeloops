# Phase 0 spike: drag-out into FL Studio (Electron)

Throwaway test for the Phase 0 exit criterion in [docs/PLAN.md](../../docs/PLAN.md):
*a dragged file lands in FL Studio's Playlist from your own window.*

The plan prefers Tauri first, but Rust isn't installed on this machine yet, so this spike tests the
Electron fallback (`webContents.startDrag`). A Tauri spike can follow for comparison.

## Run

```bash
npm install
npm start
```

On launch it writes two generated test loops (no downloads) to
`%APPDATA%\freeloops-drag-spike\samples\`:

- `freeloops - Kick Loop - 140bpm.wav` (2 bars)
- `freeloops - Arp - 120bpm - Am.wav` (2 bars)

## What to test

Tick **Always on top** so the window stays visible over FL Studio, then for **each mode**:

| Mode | How the drag starts |
|---|---|
| Native gesture → main process | JS sees press + 4 px move, sends IPC, main process calls `startDrag`. This is the plan's §2.3 rule. |
| HTML5 dragstart | Electron's documented pattern: `dragstart` → `preventDefault` → IPC → `startDrag`. |

Drop targets to try:

1. FL Studio **Playlist** (the exit criterion)
2. FL Studio **Channel Rack**
3. Windows Explorer / desktop (checks the OS drag itself)
4. **Add file…** with a real sample from your library, to rule out anything odd about the generated WAVs

Record which mode × target combinations work. That answers "Tauri or Electron?" (§9) for the Electron side.
