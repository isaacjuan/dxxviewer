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
- FLTK static libs: `C:\Users\jissi\fltk-install` (`lib\libfltk*.a`, `include\FL\*.H`),
  including `libfltk_gl.a`/`FL/Fl_Gl_Window.H` for the 3D mesh view — already
  built into this FLTK install, no separate install step.
- Cairo headers + import lib: `C:\msys64\mingw64` (`include\cairo\cairo.h`, `lib\libcairo.dll.a`).
  Installed via `pacman -S mingw-w64-x86_64-cairo`. `build.bat` copies the DLL
  closure (cairo + pixman + fontconfig/freetype/harfbuzz/glib/png/zlib/...) next
  to the exe.
- `opengl32`/`glu32` — Windows system libraries (MinGW and MSVC both ship import
  libs for these), linked for the 3D mesh view. No install, no vcpkg/pacman
  package; not the same thing as the `VulkanSDK` sitting at the workspace root,
  which this project does not use.

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
    (70R/70G/70B lookup for the color swatch), `tessellateCurveWorld`, and
    `extractMeshBody` (finds the nearest `vertexList`+`faceList` pair under a
    node - in practice a `SimpleBody`, a solid mesh unrelated to CURVE-based
    profiles - and reads it into a flat `MeshBody{vertices, faces}`).
  - `gzip_decompress.cpp` — self-contained inflate; no external compression lib.
  - `colors.h` — header-only curve/tree-depth color palettes as `uint32_t`.
- **FLTK GUI** (`fltk/`, namespace `dxxviewer`):
  - `fltk_main.cpp` — entry point (scheme/fonts/accent/window). Calls
    `Fl::lock()` before `Fl::run()` so `HubClient`'s background thread can
    hand received documents to the GUI via `Fl::awake()`.
  - `FltkMainWindow` — coordinator: toolbar + `Fl_Tile` layout, file open,
    document ownership, search routing, tree→panels selection wiring. Also
    owns the `HubClient`. `onNodeSelected` tries `dxx::extractMeshBody` on the
    selected node first; if it finds a mesh, `FltkMeshWidget` is shown and
    `FltkGeometryWidget` hidden, else vice versa with the 2D profile curves -
    both widgets sit at the same rect inside a plain `Fl_Group` (`m_geomHost`,
    a single child of the content `Fl_Tile`) so Fl_Tile's drag-resize
    hit-testing only ever sees one child there. `openFile`/`onMapReceived`
    call `onNodeSelected(nullptr)` before swapping `m_doc`, clearing both
    widgets' node/mesh pointers so neither can dereference the document being
    replaced (a live "map" arriving repeatedly makes this much more likely to
    matter than the original one-document-per-session file-open flow).
  - `FltkTreePanel` (`Fl_Tree`) — tree population + search + selection.
  - `FltkPropertiesPanel` (`Fl_Table_Row`) — property/value inspector.
  - `FltkGeometryWidget` (`Fl_Widget`) — 2D profile preview rendered with
    **Cairo** (anti-aliased) into an image surface, blitted via `fl_draw_image`;
    pan/zoom + dimension annotations.
  - `FltkMeshWidget` (`Fl_Gl_Window`) — 3D wireframe preview of a `MeshBody`
    (fixed-function/legacy OpenGL - `glBegin(GL_LINE_LOOP)` per face, no
    shading/lighting/hidden-line removal); orbit via left-drag, zoom via wheel,
    double-click to reset, mirroring the 2D widget's interaction vocabulary.
    Being an `Fl_Gl_Window` makes it a real native child window, not a plain
    widget drawn into the parent surface like `FltkGeometryWidget` - see the
    gotcha below.
  - `HubClient` — minimal hand-rolled WebSocket client (Winsock2 directly, no
    external WS library — same self-contained-over-dependency approach as
    `gzip_decompress.cpp`) that connects to `hsbWebSocketHub` (sibling project,
    `D:\dev_jp\hsbWebSocketHub`, `ws://127.0.0.1:8181/ws`), subscribes to the
    `"map"` topic, and reconnects indefinitely on failure. Accepts two shapes
    for a topic broadcast, detected by its first non-whitespace character:
    a `{filename,size,content_base64}` JSON object — the `dotnet/` toolkit's
    `cb64 | sendws` shape (see `dotnet/TOOLKIT.md`), which is how the real
    `chsbunzip -q *.hmlx | cxargs cb64 | sendws --topic map` pipeline sends a
    document (`content_base64` is base64-decoded to get the `.dxx` text,
    `filename` flows through for the title) — or a bare JSON string (the raw
    `.dxx` text directly, e.g. a hand-typed `sendws --topic map` test line).
    Either way the text is parsed via `dxx::parseString` and displayed exactly
    like an opened file, except the window title reads the map's filename (or
    `map@8181` if none was sent) and there is no `m_filePath` (Reload is a
    no-op for a live document). Also reports live connect/disconnect via a
    second callback; `FltkMainWindow::updateTitle()` composes the title from
    app version + source + `[hub: connected|offline]` from that state.
    GUI-only: the CLI has no network code.
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
- **`FltkMeshWidget` (`Fl_Gl_Window`) is a real native child window**, unlike
  every other widget in this app - hiding/showing it (to toggle with
  `FltkGeometryWidget`) doesn't get picked up by FLTK's normal shared-surface
  redraw the way a plain `Fl_Widget` sibling would. `onNodeSelected` calls
  `m_geomHost->redraw()` after every visibility toggle to force the newly-
  revealed widget to repaint; without it the previous widget's last frame can
  stay visible on top after switching selection.

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
