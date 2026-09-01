# AGENTS.md — DXX Viewer

hbsCAD `.dxx` file viewer: a cross-section profile browser with a FLTK GUI plus a
small SVG-emitting CLI. Both share the same parser/core; no Qt or raw-Win32 code.

## Build

Two toolchains are supported for the FLTK GUI (MinGW and MSVC); the CLI builds
with either.

### FLTK GUI — MinGW (Qt Creator / `build.bat`)

```bat
cd fltk
cmd /c build.bat
```

Produces `fltk\build\dxxviewer-fltk.exe` plus the Cairo runtime DLLs it copies
next to the exe. Run with an optional path: `dxxviewer-fltk.exe file.dxx`.

Dependencies (absolute paths on this machine):
- FLTK static libs: `C:\Users\jissi\fltk-install` (`lib\libfltk*.a`, `include\FL\*.H`).
- Cairo headers + import lib: `C:\msys64\mingw64` (`include\cairo\cairo.h`, `lib\libcairo.dll.a`).
  Installed via `pacman -S mingw-w64-x86_64-cairo`. `build.bat` copies the DLL
  closure (cairo + pixman + fontconfig/freetype/harfbuzz/glib/png/zlib/...) next
  to the exe.

A qmake project (`fltk/dxxviewer-fltk.pro`) mirrors `build.bat` — open it in Qt
Creator with a MinGW kit.

### FLTK GUI — MSVC (Visual Studio)

Open `fltk/dxxviewer-fltk.vcxproj` (x64, v143, C++20). Depends on:
- FLTK static libs built for MSVC: `C:\Users\jissi\fltk-install-msvc`
  (`fltk.lib`/`fltk_images.lib`/`fltk_png.lib`/`fltk_z.lib`), built from
  `C:\Users\jissi\fltk-src` with the VS generator.
- Cairo for MSVC via vcpkg: `C:\Users\jissi\vcpkg` → `installed\x64-windows`.
  A post-build step copies the Cairo DLLs next to the exe.

The MinGW and MSVC builds render identically (both use the Cairo path).

### CLI

```bash
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

`dxxviewer.exe <file.dxx>` parses and writes `preview.svg` to the current
directory. No stdout on success. (`main.cpp` has a hardcoded default input path
only used when no argument is passed.) A Visual Studio project
(`dxxviewer.vcxproj`, x64/v143) also builds the CLI.

## Architecture

- **C++20**, built with MinGW g++ (no MSVC/Qt dependency).
- **Shared core** (used by both targets):
  - `dxx_parser.h/.cpp` — DXX parser (`dxx::DxxNode`, `dxx::DxxDocument`,
    `parseFile`) plus the geometry helpers: `extractCurves` (flattened local 2D),
    `extractCurves3D` (keeps 3D placement), `extractProfile2D` (projects CURVE
    points onto the profile plane for a true cross-section), `extractNodeColor`
    (70R/70G/70B lookup for the color swatch), and `tessellateCurveWorld`.
  - `gzip_decompress.cpp` — self-contained inflate; no external compression lib.
  - `colors.h` — header-only curve/tree-depth color palettes as `uint32_t`.
- **FLTK GUI** (`fltk/`, namespace `dxxviewer`):
  - `fltk_main.cpp` — entry point (scheme/fonts/accent/window). Calls
    `Fl::lock()` before `Fl::run()` so `HubClient`'s background thread can
    hand received documents to the GUI via `Fl::awake()`.
  - `FltkMainWindow` — coordinator: toolbar + `Fl_Tile` layout, file open,
    document ownership, search routing, tree→panels selection wiring. Also
    owns the `HubClient`.
  - `FltkTreePanel` (`Fl_Tree`) — tree population + search + selection.
  - `FltkPropertiesPanel` (`Fl_Table_Row`) — property/value inspector.
  - `FltkGeometryWidget` (`Fl_Widget`) — profile preview rendered with **Cairo**
    (anti-aliased) into an image surface, blitted via `fl_draw_image`; pan/zoom
    + dimension annotations.
  - `HubClient` — minimal hand-rolled WebSocket client (Winsock2 directly, no
    external WS library — same self-contained-over-dependency approach as
    `gzip_decompress.cpp`) that connects to `hsbWebSocketHub` (sibling project,
    `D:\dev_jp\hsbWebSocketHub`, `ws://127.0.0.1:8181/ws`), subscribes to the
    `"map"` topic, and reconnects indefinitely on failure. A topic broadcast is
    expected to be a JSON string (the raw `.dxx` text) — decoded and parsed
    via `dxx::parseString`, then displayed exactly like an opened file except
    the window title reads `map@8181` and there is no `m_filePath` (Reload is
    a no-op for a live document). Also reports live connect/disconnect via a
    second callback; `FltkMainWindow::updateTitle()` composes the title from
    app version + source (file path / `map@8181`) + `[hub: connected|offline]`
    from that state. GUI-only: the CLI has no network code.
- **CLI** (`main.cpp`) — parses and writes `preview.svg`.

## FLTK implementation gotchas

- **`Fl_Tile` must be anchored at (0,0)** — its size_range-mode resize math
  assumes the origin. The toolbar is a sibling floating over the tile's empty
  top strip, re-pinned in `FltkMainWindow::resize()`.
- **`Fl_Table`'s constructor does not `end()`** — it leaves the current group on
  its internal `Fl_Scroll` (unlike `Fl_Tree`). `buildLayout()` calls
  `content->begin()` before creating the widget that follows the properties
  panel, or that widget nests inside the table.
- **FLTK draw functions use window coordinates**, not widget-relative ones —
  the geometry widget's `toScreen` lambdas add `x()/y()`; the cairo surface is
  widget-sized (0..w) and blitted at `x(),y()`.
- `build.bat` must stay CRLF + ASCII (no em-dashes; a `)` inside an
  `if (...)` block terminates the block early).

## DXX file format

Text-based DXF-like format with UTF-8 BOM, CRLF line endings, two-line
attribute pairs, and `START <name>` / `END <name>` blocks (some files also use
`OBJECT <type>` / `END OBJECT`). Curves carry `11PTX/11PTY/11PTZ` + `41BULGE`
and a plane normal `13NORMAL*`/`13VECN*`; profiles live under
`ExtrProfile[]` → `PLANEPROFILE` → `RING[]` → `RING` → `CURVE`.

Key group codes: `70` (int), `5` (handle/UUID), `10`/`11` (coordinate), `13`
(vector), `40` (real), `41` (bulge). ZIP JSON uses `\ZIP:H4sIAAAA...` values
decompressed by `gzip_decompress.cpp` and exposed on `DxxNode::zipData`.

## Coding style

- 4-space indent, braces on same line, `#pragma once` headers.
- `[[nodiscard]]` on accessors; `std::string_view` for read-only string params.
- Shared geometry/color logic goes in `dxx_parser.*` / `colors.h`, not in the
  FLTK widgets — keep GUI code rendering-only.
