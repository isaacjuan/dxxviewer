# DXX Viewer

Cross-section profile viewer for hsbCAD `.dxx` files (structural framing style
definitions with parametric geometry, node graphs, and extrusion profiles).

## Features (FLTK GUI)

- **Tree browser** — hierarchical DXX document structure, color-coded by depth.
- **Property inspector** — key/value pairs for the selected node.
- **Geometry preview** — anti-aliased (Cairo) rendering of extrusion profiles,
  with pan/zoom and width/height dimension annotations.
- **Color swatches** — for nodes carrying `70R/70G/70B` properties.
- **Search** — across node names, property keys and values (wrap-around).
- **ZIP decompression** — transparent gzip of `\ZIP:` base64 blocks.

## Build

Requires MinGW-w64 g++, FLTK 1.4.5 (static), and Cairo (dynamic, via MSYS2).

```bat
cd fltk
cmd /c build.bat
```

Output: `fltk\build\dxxviewer-fltk.exe` (+ Cairo runtime DLLs). Run with an
optional file path.

CLI (parses a `.dxx` and writes `preview.svg`):

```bash
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

## Project structure

```
dxxviewer/
├── CMakeLists.txt            # CLI target only
├── dxx_parser.h / .cpp       # DXX parser + curve/profile/color extraction — shared
├── gzip_decompress.cpp       # built-in inflate — shared
├── colors.h                  # curve/tree color palettes — shared
├── main.cpp                  # CLI → preview.svg
└── fltk/                     # FLTK GUI
    ├── build.bat             # MinGW + FLTK + Cairo build
    ├── fltk_main.cpp         # entry point
    ├── FltkMainWindow.*      # toolbar + tile layout, file open, routing
    ├── FltkTreePanel.*       # tree browser + search
    ├── FltkPropertiesPanel.* # property inspector
    └── FltkGeometryWidget.*  # Cairo profile preview
```

## Controls

| Control | Action |
|---------|--------|
| `Open` / `Reload` | Open / re-parse a `.dxx` file |
| `+ Expand` / `- Collapse` | Expand/collapse all tree nodes |
| `Find` / `X` | Search (Enter) / clear search |
| Drag pane borders | Resize tree / properties / geometry |
| Drag in geometry | Pan · mouse wheel zoom · double-click reset |

## DXX file format

hsbCAD's DXX is a text-based DXF-like format with UTF-8 BOM, CRLF line endings,
and two-line attribute pairs:

```
START
ProjectInfo
70MAV                  ← group code + tag name
1                      ← value
ProjectName            ← tag (no numeric prefix)
My Project             ← value
...
END
ProjectInfo
```

Key group codes: `70` (integer), `5` (handle/UUID), `10`/`11` (coordinate), `13`
(vector), `40` (real), `41` (bulge). ZIP-compressed JSON uses `\ZIP:H4sIAAAA...`
values.
