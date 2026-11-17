# SPlayer-Next — Nintendo Switch port (nx)

A native homebrew port of SPlayer-Next for the Nintendo Switch.

The desktop app is Electron + Vue + Rust (napi). None of that runs on the
Switch (no Node, no napi, no Chromium), so this port is **a ground-up native
rewrite** that reproduces the desktop app's page structure and look:

| Layer     | Desktop (upstream)           | nx port (this directory)             |
| --------- | ---------------------------- | ------------------------------------ |
| Shell     | Electron + Vue 3             | libnx applet + immediate-mode UI     |
| Rendering | Chromium / UnoCSS            | in-tree software canvas (RGBA8888)   |
| Text      | Chromium system fonts        | stb_truetype + bundled CJK font      |
| Icons     | icon font (Iconify)          | hand-rasterised vector icons         |
| Audio     | `audio-engine` (Rust/FFmpeg) | dr_libs (MP3/FLAC/WAV/OGG) + audout  |
| Metadata  | node `music-metadata`        | in-tree ID3v2 / FLAC / RIFF reader   |
| Covers    | Chromium image pipeline      | stb_image + bounded LRU cover cache  |
| Lyrics    | `lyric-kit` (LRC/QRC/YRC…)   | LRC parser + centred lyric scroll    |
| State     | Electron store (JSON)        | plain-text store on the SD card      |

## Pages

All eleven pages mirror the desktop sidebar / router:

| Page            | Contents                                                     |
| --------------- | ------------------------------------------------------------ |
| 首页 Home        | Cover-tinted hero, 4 stat tiles, recently played, album grid |
| 音乐库 Library   | Tabs: songs (with covers) / albums grid / artists list        |
| 我喜欢的音乐     | Heart header card + liked tracks, `Y` to unlike               |
| 播放历史 History | Reverse-chronological history with relative timestamps        |
| 下载管理         | Local-source summary, format breakdown, cached-track list     |
| 我的收藏         | Saved playlists with cover preview strips                     |
| 文件夹 Folders   | Folder list + the selected folder's tracks                    |
| 统计 Stats       | Duration / listening tiles, format bars, top artists          |
| 搜索 Search      | `+` opens the system keyboard, live filtering                 |
| 全局设置         | Theme, lyric size, shuffle, repeat, volume, rescan, clear     |
| 正在播放 Player  | Large cover, transport, tech badges, karaoke lyric scroll     |

## Features

- Recursive SD-card library scan (MP3 / FLAC / WAV / OGG), depth-limited
- **Real tag parsing**: ID3v2.2/2.3/2.4 + ID3v1, FLAC Vorbis comments,
  RIFF `LIST/INFO` — title, artist, album, album artist, genre, year,
  track/disc number
- **Embedded cover art**: APIC frames and FLAC `PICTURE` blocks are decoded
  lazily through a bounded LRU cache, with a dominant-colour accent used for
  the hero card and the full-player backdrop
- Album and artist aggregation derived from the tags
- Likes, play history, playlists and settings persisted to
  `sdmc:/switch/SPlayer-Next/state/`
- Three themes (暮色 / 午夜 / 极光) switched live from the settings page
- LRC lyrics with smooth centred highlighting
- Play / pause / next / prev, ±10 s seek, shuffle, three repeat modes,
  live volume from the stick, and a transient status toast

## Quick start (on device)

1. Copy `SPlayer-Next.nro` to `sdmc:/switch/SPlayer-Next/`.
2. Put music in one of:
   - `sdmc:/switch/SPlayer-Next/music/`
   - `sdmc:/SPlayer-Next/music/`
   - `sdmc:/music/`
3. (Optional) drop a sidecar `.lrc` next to each track, same basename.
4. Launch via the homebrew menu.

State files are created next to the NRO under `sdmc:/switch/SPlayer-Next/state/`.

## Controls

| Input            | Action                                         |
| ---------------- | ---------------------------------------------- |
| D-Pad Up/Down    | Move selection (one row)                       |
| Left stick ↑↓    | Move selection faster (five rows) / volume     |
| Left stick ←/→   | Switch page (sidebar order)                    |
| D-Pad Left/Right | Cycle library tabs · folder · adjust a slider  |
| A                | Play / pause / activate the highlighted item   |
| B                | Back (player → previous page) / pause          |
| X / Y            | Next track / like the highlighted track        |
| L / R            | Seek −10 s / +10 s                             |
| ZL               | Cycle repeat: off → all → one                  |
| ZR               | Rescan the library                             |
| +                | Exit (on the search page: open the keyboard)   |
| −                | Clear search text / clear history              |

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

### Host-side layout preview (no Switch needed)

`source/host_preview.cpp` renders every page to a PPM with a synthetic library,
which is how the layout was validated before flashing. It is filtered out of the
NRO build by the Makefile.

```bash
g++ -std=gnu++20 -O2 -Iinclude -Iexternal -o /tmp/preview \
    source/host_preview.cpp source/ui/*.cpp source/library/*.cpp \
    source/lyrics/*.cpp source/audio/*.cpp      # plus a libnx stub on non-nx hosts
/tmp/preview romfs/fonts/wqy-microhei.ttc /tmp/out
```

## CI

`.github/workflows/nx.yml` builds the NRO inside the `devkitpro/devkita64`
container and uploads both `SPlayer-Next-nx` (the `.nro`) and `nx-build-log`.
It is the **only** active workflow — the desktop Electron workflows were moved
to `.github/workflows-desktop/` and are intentionally disabled.

## Third-party

- `external/dr_libs` — dr_mp3 / dr_flac / dr_wav (public domain / MIT-0)
- `external/stb/stb_truetype.h`, `stb_image.h` — MIT
- `external/stb/stb_vorbis.c` — MIT
- `romfs/fonts/wqy-microhei.ttc` — WenQuanYi Micro Hei (Apache-2.0 / GPLv3 with
  font embedding exception)

## Layout

```
nx/
├── Makefile                 # devkitA64 + libnx build
├── include/spn/
│   ├── types.hpp            # Color / Rect / Track / Album / Playlist
│   ├── canvas.hpp           # software rasteriser interface
│   ├── font.hpp             # stb_truetype glyph cache
│   ├── image.hpp            # decoded RGBA image
│   ├── cover.hpp            # bounded LRU cover cache
│   ├── tag.hpp              # ID3v2 / FLAC / RIFF tag reader
│   ├── library.hpp          # recursive scanner + aggregates
│   ├── store.hpp            # likes / history / playlists / settings
│   ├── audio.hpp            # dr_libs decoder + audout streaming
│   ├── lyrics.hpp           # LRC parser
│   ├── icons.hpp            # hand-rasterised vector icons
│   ├── widgets.hpp          # shared widgets (rows, tiles, pills)
│   └── ui.hpp               # pages, theme, App state
├── source/
│   ├── main.cpp             # entry point, input, frame loop
│   ├── ui/
│   │   ├── canvas.cpp       # rasteriser (clip stack, SDF shapes, images)
│   │   ├── font.cpp         # stb_truetype wrapper
│   │   ├── icons.cpp        # polygon / arc icon drawing
│   │   ├── image.cpp        # stb_image decode + dominant colour
│   │   ├── cover.cpp        # cover cache
│   │   ├── widgets.cpp      # shared widgets
│   │   ├── ui_shell.cpp     # theme table, sidebar, top bar, player bar
│   │   └── page_*.cpp       # one file per page group
│   ├── library/             # library.cpp, tag.cpp, store.cpp
│   ├── audio/               # audio.cpp, drlibs_impl.cpp
│   ├── lyrics/              # lyrics.cpp
│   └── host_preview.cpp     # headless renderer (excluded from the NRO)
├── external/                # vendored single-header libs
└── romfs/fonts/             # embedded CJK font
```

## Status / limitations

- Lyrics: LRC only (the desktop app also speaks QRC / YRC / TTML / LYS / KRC).
  The library will find `.ttml` / `.ass` sidecars, but only LRC is parsed.
- Streaming servers (Subsonic / Navidrome / Jellyfin / Emby) are not
  implemented — this port is local-library first.
- Playlists can be read and played, but new ones must be created elsewhere.
- Tag coverage is ID3v2/v1, FLAC Vorbis comments and RIFF INFO. OGG Vorbis files
  are listed and playable but their comment headers are not read yet, so they
  fall back to filename-derived metadata.
- Audio resampling is a linear interpolator (adequate, not audiophile).
- The library scan probes durations for the first 2000 tracks; beyond that rows
  show `--:--` until played once.
