# SPlayer-Next — Nintendo Switch port (nx)

A native homebrew port of SPlayer-Next for the Nintendo Switch.

The desktop app is Electron + Vue + Rust (napi). None of that can run on the
Switch (no Node, no napi, no Chromium), so this port is **a ground-up native
rewrite of the core** that keeps the product's look and feel:

| Layer    | Desktop (upstream)          | nx port (this directory)          |
| -------- | --------------------------- | --------------------------------- |
| Shell    | Electron + Vue 3            | libnx applet + software UI        |
| Rendering| Chromium / UnoCSS           | in-tree immediate-mode canvas     |
| Text     | system fonts via Chromium   | stb_truetype + bundled CJK font   |
| Audio    | `audio-engine` (Rust/FFmpeg)| dr_libs (MP3/FLAC/WAV) + audout   |
| Lyrics   | `lyric-kit` (LRC/QRC/YRC…)  | LRC parser + centred lyric scroll |

## Features

- Library browser for `sdmc:/` music (MP3 / FLAC / WAV), recursive scan
- Now-playing panel with progress, volume and playback state
- LRC lyrics with smooth, centred, active-line highlighting
- Play / pause / next / prev, ±10 s seek, loop, shuffle, live volume
- Bundled WenQuanYi Micro Hei so CJK titles and lyrics render correctly

## Quick start (on device)

1. Copy `SPlayer-Next.nro` to `sdmc:/switch/SPlayer-Next/`.
2. Put music in one of:
   - `sdmc:/switch/SPlayer-Next/music/`
   - `sdmc:/SPlayer-Next/music/`
   - `sdmc:/music/`
3. (Optional) drop a sidecar `.lrc` next to each track, same basename.
4. Launch via the homebrew menu.

## Controls

| Input        | Action                     |
| ------------ | -------------------------- |
| D-Pad Up/Down| Move selection             |
| A            | Play / toggle pause        |
| B            | Pause                      |
| X / Y        | Next / previous track      |
| L / R        | Seek −10 s / +10 s         |
| Stick L/R    | Toggle shuffle             |
| Stick Y      | Volume                     |
| ZL           | Toggle loop                |
| ZR           | Rescan library             |
| +            | Exit                       |

## Building

Requires [devkitPro](https://devkitpro.org/) with `devkitA64` and `switch-dev`
(libnx). No portlibs are needed — everything is either in-tree or vendored.

```bash
cd nx
make -j"$(nproc)"
# -> build/SPlayer-Next.nro
```

`DEVKITPRO` defaults to `/opt/devkitpro`; override it if your install differs:

```bash
make DEVKITPRO=/path/to/devkitpro
```

## CI

`.github/workflows/nx.yml` builds the NRO inside the `devkitpro/devkita64`
container and uploads both `SPlayer-Next-nx` (the `.nro`) and `nx-build-log`.
It is the **only** active workflow — the desktop Electron workflows were moved
to `.github/workflows-desktop/` and are intentionally disabled.

## Third-party

- `external/dr_libs` — dr_mp3 / dr_flac / dr_wav (public domain / MIT-0)
- `external/stb/stb_truetype.h` — MIT
- `romfs/fonts/wqy-microhei.ttc` — WenQuanYi Micro Hei (Apache-2.0 / GPLv3 with
  font embedding exception)

## Layout

```
nx/
├── Makefile              # devkitA64 + libnx build
├── include/spn/*.hpp     # public headers
├── source/
│   ├── main.cpp          # app shell, layout, input
│   ├── ui/               # canvas (software rasteriser) + font (stb_truetype)
│   ├── audio/            # decoder + audout streaming engine
│   ├── library/          # recursive SD-card scanner
│   ├── lyrics/           # LRC parser
│   └── host_preview.cpp  # headless renderer used to eyeball the layout
├── external/             # vendored single-header libs
└── romfs/fonts/          # embedded CJK font
```

## Status / limitations

- Lyrics: LRC only (the desktop app also speaks QRC / YRC / TTML).
- Streaming servers (Subsonic / Jellyfin / Emby) are not implemented — this port
  is local-library first.
- No cover-art decoding yet; the cover slot is a styled placeholder.
- Audio resampling is a simple linear interpolator (adequate, not audiophile).
