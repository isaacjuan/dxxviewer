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

**Known environment issue (found 2026-09-03, not yet fixed)**: `build.bat` calls
plain `g++`, which on this machine currently resolves to a broken
scoop-installed MinGW (`C:\Users\jissi\scoop\apps\mingw\...` ahead of
`C:\msys64\mingw64\bin` on `PATH`) - its `crt2.o` fails to link anything
(`undefined reference to '_gnu_exception_handler'`), unrelated to this
project's own code (reproduces on a clean checkout too). Build with
`C:\msys64\mingw64\bin\g++.exe` on `PATH` instead (already installed here for
Cairo) until `PATH` ordering or the scoop install itself is fixed - `build.bat`
itself hasn't been changed to hardcode a toolchain path, so this still needs
doing manually per shell session until resolved at the environment level.

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
    Toolbar also has a small input + `"-> AutoCAD"` button
    (`sendCommandToHost`, top of `FltkMainWindow.cpp`): sends the typed text
    to this window's real Win32 parent via `WM_COPYDATA`, tagged with a magic
    `dwData` value (`kAutoCadCommandMsgId`) that must match the receiving
    side's own copy of the same constant. Only does anything when this window
    has actually been reparented into a host - i.e. launched via
    `D:\dev_jp\HsbChatPanelPoc`'s "Geometry" button, not standalone (no parent
    window then, so it just shows a status message). See that project's
    `ChatDockPane::OnCopyData`/`kDxxCommandMsgId` for the receiving side.
    A `"Draw"` button next to it sends the *currently selected* node's curve
    geometry: `dxx::extractCurves3D` + `dxx::tessellateCurveWorld` per curve
    (both pre-existing - no new geometry math). `FltkMainWindow::m_selectedNode`
    tracks the selection (set in `onNodeSelected`) so the button always draws
    whatever is selected *right now*.
    **Draw now publishes over the hub, not `WM_COPYDATA` (2026-09-07)**:
    `sendGeometryToHost` (`FltkMainWindow.cpp`) was rewritten to publish
    `{"points":[[x,y,z],...]}` - one publish per curve - to
    `hsbWebSocketHub`'s `"acad_geometry"` topic instead of sending
    `WM_COPYDATA` to this window's Win32 parent, per the same request that
    drove the equivalent change in `hsbMapExplorerWs`'s own
    `AutoCadBridge.cs` (see that fork's `CLAUDE.md`). `HubClient` gained a
    `PublishToHub(host, port, topic, jsonData)` free function
    (`HubClient.h/.cpp`) for this - a one-shot connect/handshake/send/close,
    reusing the same hand-rolled Winsock2 WS framing helpers (`sendFrame`,
    `sendAll`, `recvExact`, `makeWebSocketKey`) `HubClient::run()`'s own
    persistent subscribe loop already has, rather than a second WS
    implementation. Old `dwData`/`kAutoCadGeometryMsgId`/`GetParent()`
    plumbing removed entirely (not kept as a fallback) - a hub publish works
    identically whether this window is embedded in `HsbChatPanelPoc` or
    running fully standalone; `ChatDockPane` already had a third
    `HsbWsBridge` connection subscribed to exactly this topic (see that
    project's `kGeometryTopic`/`OnAcadGeometryMessage`), so nothing on the
    receiving side needed to change. `"-> AutoCAD"`'s own `WM_COPYDATA`
    channel (`sendCommandToHost`/`kAutoCadCommandMsgId`) is untouched - only
    curve-drawing moved to the hub.
    **Auto-draw on selection (2026-09-07)**: `onNodeSelected` also calls
    `sendGeometryToHost` itself, not just the `"Draw"` button - whenever the
    newly-selected node's own `name == "CURVE"` and
    `dxx::extractCurves3D(*node)` is non-empty (i.e. it actually has point
    data, not just an empty/malformed `CURVE` block). This covers a live
    "map" broadcast's synthetic `CURVE` nodes too (see the
    `element_commands`/`ElementCommandsBridge` section above), since those
    are real DXX-native `CURVE` nodes by the time they reach the tree - no
    separate wiring needed. Deliberately checks `extractCurves3D` up front
    rather than just calling `sendGeometryToHost` unconditionally: that
    function pops a "No curve geometry" dialog when it finds none, which is
    fine for a deliberate button click but would be an annoying no-op dialog
    on every empty-`CURVE` tree click if it fired automatically. This call
    also passes `silentOnFailure=true` (a parameter the button click leaves
    `false`) so a failed hub publish - e.g. running standalone with no hub up,
    a real scenario now that this no longer needs `HsbChatPanelPoc`'s
    embedding - doesn't pop a dialog on every single `CURVE` tree click
    either. The `"Draw"` button itself still works unchanged for redrawing
    the current selection on demand (e.g. after the receiving side's
    `ChatDockPane` document changed).

    **Verified working end-to-end**: subscribed to `acad_geometry` via
    `wsget`, published a synthetic `selection_parameters` message (one
    element, one `CURVE` map entry as `PLine((0,0,1),
    Point3dCollection[3]{(0,0,0),(10,0,0),(10,10,0)},
    DoubleCollection[3]{0,0,0})`) via `sendws --topic element_commands`,
    and confirmed the `CURVE` leaf auto-selected, auto-drew, and `wsget`
    received `{"points":[[0,0,0],[10,0,0],[10,10,0],[0,0,0]]}` (the closing
    point back to the origin comes from `tessellateCurveWorld` itself,
    unchanged - not something this change affects) - process stayed alive
    throughout, running fully standalone (no `HsbChatPanelPoc` host).

    **Mesh selections (e.g. a `MassElement`/`SimpleBody`) draw too
    (2026-09-07)** - `sendMeshToHost` publishes a mesh's faces to the SAME
    `"acad_geometry"` topic curves already use, as a wireframe-per-face
    representation via the already-working `AcDb3dPolyline` pipeline,
    **not** a real solid/mesh AutoCAD entity. That was the first attempt
    (an `AcDbSubDMesh`, ObjectARX's modern mesh entity), abandoned after a
    real SDK/linkage dead end: `AcDbPolyFaceMesh` has no header at all in
    the installed ObjectARX 2026 SDK, and `AcDbSubDMesh` (`dbSubD.h`) has a
    header but its constructor/`setSubDMesh` aren't exported by *any* DLL
    in the installed AutoCAD 2026 - confirmed via `dumpbin /exports`
    recursively across the entire install tree, not just `acdb25.dll`/
    `accore.dll` (`HsbChatPanelPoc`'s own attempt at wiring this up hit a
    `LNK2019` on exactly these symbols; reverted rather than left half-
    working).

    `dxx::MeshBody::faces` (0-based indices into `mesh.vertices`) sometimes
    repeats its first index at the end, explicitly closing the loop (e.g.
    `0,1,2,3,0`) - stripped before publishing, since the receiving side's
    `AcDb3dPolyline` already closes the loop itself (same convention every
    curve published this way already uses). An *internal* repeated index (a
    self-touching/bridged boundary - e.g. an L-shaped or multiply-connected
    face, seen in real data - see below) is passed through as-is, same
    vertex walk `FltkMeshWidget`'s own `GL_POLYGON` already renders for that
    face.

    Wired into both the `"Draw"` button and auto-draw-on-selection via a
    small dispatcher, `drawSelectionToHost(node, meshCache,
    silentOnFailure)`: tries `extractCurves3D` first (unchanged), else
    falls back to `meshCache` (the exact `FltkMainWindow::m_meshCache` the
    preview pane itself just resolved via `extractMeshBody` - not a second
    recursive search), else a "nothing to draw" dialog. Auto-draw's own
    curve branch keeps its exact original `node->name == "CURVE"` gate
    unchanged (deliberately NOT broadened to "any ancestor with a nested
    curve", to avoid changing existing curve-auto-draw behavior); the mesh
    branch (`else if (m_meshCache) sendMeshToHost(...)`) reuses that same
    "did the preview pane find a mesh" check, so selecting an ancestor like
    `MassElement` (not just the literal `SimpleBody` container) auto-draws,
    matching what the local 3D preview already shows for that same
    selection.

    **One AutoCAD block per mesh, not loose polylines (2026-09-07)** - per
    explicit follow-up request ("make every MassElement into a block in
    order to select individually"): `sendMeshToHost` was rewritten again to
    publish ONE message per mesh, not one `{"points":[...]}` publish per
    face - `{"block":[[[x,y,z],...],[[x,y,z],...],...]}`, every face
    together. `HsbChatPanelPoc`'s receiving side (same `"acad_geometry"`
    connection, no new topic) now checks for this shape first
    (`extractBlockLines`, in that project's `HubProtocol.h/.cpp`) before
    falling back to the existing single-curve `"points"` shape - the two
    are unambiguous (different JSON keys) so there's no risk of
    misdetecting a plain curve publish. `DrawWorldBlock`
    (`HsbChatPanelPoc\GeometryDraw.h/.cpp`) draws each face's
    `AcDb3dPolyline` into a **new, uniquely-named `AcDbBlockTableRecord`**
    (`HsbMesh_1`, `HsbMesh_2`, ... - a process-lifetime counter) instead of
    model space directly, then inserts ONE `AcDbBlockReference` for it (at
    the origin, no transform - every polyline's own vertices are already
    absolute world coordinates) - so a pick anywhere on the mesh selects
    the *whole* block reference as one entity, not one polyline/face at a
    time. Verified these ObjectARX APIs are actually exported this time
    (after the `AcDbSubDMesh` lesson) via `dumpbin /linkermember:1` (not
    plain `/symbols`, which - also a lesson learned - doesn't enumerate an
    import lib's archive members by default and had made the earlier
    `AcDbSubDMesh` search look emptier than it needed to, though that
    particular conclusion held up under the corrected method too):
    `AcDbBlockReference`'s point+id constructor, `AcDbBlockTableRecord`'s
    public no-arg constructor, and `AcDbSymbolTable::add` (inherited by
    `AcDbBlockTable`) are all genuinely exported by `acdb25.lib`.

    **Verified working end-to-end against real production data** (not a
    synthetic test payload): a real hsbcad export,
    `GTT-BHOMES_WIP_TM_detached\...\R25_V29-Akron - Mechanical_detached.dxx`
    (~9.8MB, 1M+ lines, 659 `MassElement` nodes) - confirmed the file
    contains genuine `SimpleBody`/`vertexList`/`faceList` blocks directly
    under `MassElement` (one instance: 16 vertices, 11 faces, including a
    real self-touching face - `4,5,6,7,8,9,10,11,8,7,4` - and a real
    trailing-closing-duplicate face - `0,1,2,3,0`). GUI automation via
    simulated clicks proved unreliable here (a DPI-scaling mismatch between
    `GetWindowRect`'s reported coordinates and where a click actually
    landed - confirmed by a click consistently selecting the wrong tree
    row), so verified instead with a throwaway headless test program
    (compiled directly against this project's own `dxx_parser.cpp`, no
    FLTK) that parsed the real file, ran `extractMeshBody` on the first
    real `MassElement`, and replicated `sendMeshToHost`'s exact
    JSON-building logic - `wsget` (subscribed to `acad_geometry`) confirmed
    the single combined `"block"` message arrived with all 11 faces
    correctly stripped/formatted (e.g. the self-touching face arriving as
    10 points, 11 minus the trailing duplicate), with real, sane
    coordinates (register/duct-scale, consistent ~6.35mm Z-offset matching
    that element's own `"12x6 FD"` register data). `HubProtocol.cpp`'s
    `extractBlockLines` was verified by careful manual trace against this
    exact captured payload (bracket-depth tracking through multiple
    same-message curves, confirming a short/degenerate curve is correctly
    filtered). **Confirmed working inside a real AutoCAD session** too
    (user-tested, 2026-09-07): picking anywhere on the drawn mesh selects
    the whole `HsbMesh_N` block as one entity, as intended - including
    after `HsbChatPanelPoc`'s follow-up switch to fan-triangulated
    `AcDbFace` triangles instead of `AcDb3dPolyline` outlines (see that
    project's own `README.md`), which only changed how the receiving side
    draws this same wire format, not this project's own publishing code.
  - `FltkTreePanel` (`Fl_Tree`) — tree population + search + selection.
  - `FltkPropertiesPanel` (`Fl_Table_Row`) — property/value inspector.
  - `FltkGeometryWidget` (`Fl_Widget`) — 2D profile preview rendered with
    **Cairo** (anti-aliased) into an image surface, blitted via `fl_draw_image`;
    pan/zoom + dimension annotations.
  - `FltkMeshWidget` (`Fl_Gl_Window`) — 3D preview of a `MeshBody` (fixed-
    function/legacy OpenGL, no textures/hidden-line-removal beyond the depth
    buffer); orbit via left-drag, zoom via wheel, double-click to reset,
    mirroring the 2D widget's interaction vocabulary. Two independent view
    toggles, both defaulted on, `P`/`S` keys (click the view first for
    keyboard focus - `FL_FOCUS`/`FL_UNFOCUS` accepted, `Fl::focus(this)`
    called on `FL_PUSH`), current state hinted bottom-left every frame:
    - **Perspective vs. orthographic** (`m_perspective`) — `setupProjection()`
      picks `gluPerspective` (camera pushed back from the object by a
      zoom-scaled `glTranslated` in `draw()`, before the existing
      rotate-around-center transform) or the original `glOrtho`.
    - **Shaded vs. wireframe-only** (`m_shaded`) — `drawShadedFaces()` renders
      each face as a `GL_POLYGON` with a flat per-face normal (Newell's
      method - tolerant of a slightly non-planar or concave real-world face,
      unlike a plain 3-point cross product) under one directional light
      (`GL_LIGHTING`/`GL_LIGHT0`, two-sided), offset back via
      `GL_POLYGON_OFFSET_FILL` so `drawWireframeEdges()` - always drawn,
      regardless of this toggle - stays z-fight-free on top of it.
    Antialiased via `FL_MULTISAMPLE` (requests a multisample-capable pixel
    format; the mode flag alone doesn't turn sampling on, so `draw()` also
    calls `glEnable(GL_MULTISAMPLE)` - manually `#define`d, since Windows'
    OpenGL 1.1 header predates it - plus `GL_LINE_SMOOTH` with alpha blending
    for the wireframe's own line edge coverage antialiasing) - falls back to
    non-multisampled silently if the driver has no such pixel format. Being an
    `Fl_Gl_Window` makes it a real native child window, not a plain widget
    drawn into the parent surface like `FltkGeometryWidget` - see the gotcha
    below.
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
  - **`element_commands` topic / live Revit selection (2026-09-03)** — a
    SECOND `HubClient` instance (`FltkMainWindow::m_elementCommandsHub`, its
    own connection - the hub relays broadcasts unwrapped with no topic tag,
    so one connection can only unambiguously belong to one topic, same
    reasoning as `hsbWebSocketRvt`'s own `TopicSubscription`) subscribes to
    `element_commands`, published by the Revit add-in `hsbWebSocketRvt`
    (`C:\HSBCAD\DEFAULT\beamapprevit\Phoenix\hsbWebSocketRvt`) - JSON, not DXX
    text, pushed automatically whenever the Revit user's selection changes.
    `HubClient` gained a second, raw-mode constructor overload for this (no
    DXX `{filename,content_base64}`/bare-string shape detection - just the
    unwrapped message text verbatim); the original `"map"`-topic
    constructor/behavior is untouched.
    `ElementCommandsBridge.h/.cpp` (uses vendored `third_party/json.hpp`,
    nlohmann::json - this project had no JSON parser before) converts a
    `selection_parameters` message into a synthetic `dxx::DxxDocument`
    displayed through the SAME tree/properties panels a real `.dxx` file
    uses, no new UI: root `"Selection"` → one `"Element <id>"` child per
    selected entity (flattened Revit `parameters` as properties, plus
    `<name> [storageType]`/`<name> [isReadOnly]` sibling properties) →
    optional `"mapError"` property and/or a recursive `"Map"` child built
    from the element's `map` JSON tree (nested objects → child nodes,
    scalar/string/null leaves → properties on the current node). Wired into
    `FltkMainWindow` via a shared `displayDocument()` helper also used by
    `openFile`/`onMapReceived`; title shows `"Revit selection (N elements)"`
    with its own `[elements: connected|offline]` badge.
    **`CURVE` map entries get special-cased, not left as flat strings**: a
    BeamRDB `"CURVE"` map value is Phoenix's own object `ToString()`, not DXX
    text - `PLine(normal, Point3dCollection[n]{(x,y,z),...},
    DoubleCollection[n]{bulge,...})`. `ElementCommandsBridge.cpp`'s
    `parsePLine`/`buildCurveNode` parse this and emit a real DXX-native
    `CURVE` node (`11PTX`/`11PTY`/`11PTZ`/`41BULGE` per point, in that exact
    order - `dxx_parser.cpp`'s `collectCurves` finalizes a point on
    `41BULGE`, plus `13NORMALX/Y/Z`) instead of a property, so the existing,
    completely unmodified `dxx::extractProfile2D`/`extractCurves3D` pick it
    up exactly like a real DXX file's `CURVE` node - no new geometry code.
    Works with zero coordinate transform because `Point3dCollection`'s
    points are already absolute world-space 3D (unlike DXX's own
    local-plane-relative convention), and `collectCurves` already defaults
    to an identity origin/basis (`{0,0,0}`/`{1,0,0}`/`{0,1,0}`) when no
    ancestor sets `13PTORG*`/`13VECX*`/`13VECY*` - which none of these
    synthetic nodes do. Verified against real sample payloads via a
    throwaway scratch program (not committed): `extractProfile2D` (the
    function `FltkGeometryWidget` actually calls) correctly projects a real
    wall/panel outline onto its plane, producing a clean rectangle with Z
    flattened to 0 as expected. Numeric tokens are kept as original text
    (not round-tripped through `strtod`+reformat) to avoid a second,
    unnecessary precision-loss point beyond `dxx_parser.cpp`'s own eventual
    `strtod` read. Falls back to a flat string property if a `"CURVE"`
    value doesn't parse as `PLine(...)`, so nothing is silently dropped.
    **Bug found and fixed (2026-09-07)**: this node's `13NORMALX/Y/Z`
    (the `PLine`'s own plane normal - genuinely needed by
    `extractProfile2D`'s projection, so it can't just be omitted) was also
    being picked up by `tessellateCurveWorld`'s final world-reconstruction
    step (`origin + lx*vecX + ly*vecY + lz*normal`) - but this node's
    points are already absolute (unlike a real DXX `CURVE`'s local-plane-
    relative ones), so reapplying that normal there reprojected them through
    the wrong basis, corrupting (flattening/skewing) anything not lying flat
    in the XY plane. Confirmed via drawing a real 3D-coordinate `CURVE`
    element through `HsbChatPanelPoc`'s new `acad_geometry` WS channel and
    seeing it come out flattened in AutoCAD. Fixed in `tessellateCurveWorld`
    itself (`dxx_parser.cpp`), not here: when a curve's origin/vecX/vecY are
    still exactly `collectCurves`' identity defaults (true for these
    synthetic nodes, since no ancestor sets `13PTORG*`/`13VECX*`/`13VECY*`),
    the world-reconstruction normal is forced to identity `(0,0,1)` instead
    of trusting `curve.normal` - safe for real DXX files too, since an
    identity vecX/vecY frame geometrically requires a consistent normal to
    already be `(0,0,1)`.
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
