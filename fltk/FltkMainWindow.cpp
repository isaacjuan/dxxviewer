// WM_COPYDATA / SendMessage for the "-> AutoCAD" button below - windows.h
// before the FL includes, matching fltk_main.cpp's own ordering.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "FltkMainWindow.h"
#include "FltkTreePanel.h"
#include "FltkPropertiesPanel.h"
#include "FltkGeometryWidget.h"
#include "FltkMeshWidget.h"
#include "HubClient.h"
#include "ElementCommandsBridge.h"
#include "settings.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Flex.H>
#include <FL/Fl_Tile.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/fl_ask.H>
#include <FL/platform.H> // fl_xid() - this window's real HWND, for WM_COPYDATA

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>

namespace dxxviewer {

namespace {

// Runs `fn` on the FLTK main thread via Fl::awake, from any thread - same
// mechanism HubClient.cpp's own postToMain uses for the equivalent hub-
// receive handoff, duplicated here (not shared) since it's a tiny, generic
// trampoline, not worth wiring a new shared header for.
void postToMain(std::function<void()> fn)
{
    auto* payload = new std::function<void()>(std::move(fn));
    Fl::awake([](void* v) {
        auto* p = static_cast<std::function<void()>*>(v);
        (*p)();
        delete p;
    }, payload);
}
const char* kOpenLabel = "Open";
const char* kReloadLabel = "Reload";
const char* kExpandLabel = "+ Expand";
const char* kCollapseLabel = "- Collapse";
const char* kFindLabel = "Find";
const char* kClearLabel = "X";
const char* kSendToAcadLabel = "-> AutoCAD";
const char* kDrawLabel = "Draw";
// Two stacked rows, not one - at a narrow width (e.g. ~850px, this app's
// own docked-panel target inside HsbChatPanelPoc) one row's worth of fixed-
// width buttons (Open/Reload/.../Draw, ~715px) left the search box only a
// few pixels and clipped the AutoCAD command box/"-> AutoCAD"/"Draw"
// buttons off the right edge entirely - confirmed visually, not a guess.
// kToolbarH (both rows together) is what every content-area calculation in
// this file already keys off, so splitting the row doesn't need those
// call sites to change, only the toolbar's own construction below.
const int kToolbarRowH = 32;
const int kToolbarH = kToolbarRowH * 2;

// Arbitrary but fixed magic value identifying our WM_COPYDATA messages to a
// host ARX app. Defined independently on both sides - this project
// (dxxviewer) and D:\dev_jp\HsbChatPanelPoc's ChatDockPane.cpp - since they
// are separate repos with no shared header; keep the two in sync by hand if
// this ever changes.
const ULONG_PTR kAutoCadCommandMsgId = 0x4443584Aul; // 'DCXJ', just a tag
// Curve geometry no longer travels this way - see sendGeometryToHost(),
// which now publishes over hsbWebSocketHub's "acad_geometry" topic instead
// (ChatDockPane's own kDxxGeometryMsgId/WM_COPYDATA handling for it is
// unaffected but no longer fed by this project).

// Looks up the HWND this window is currently reparented into (HsbChatPanelPoc's
// ChatDockPane, if launched via its "Geometry" button). Returns NULL - after
// showing a status dialog - if not currently embedded, e.g. when
// dxxviewer-fltk.exe is run standalone for development.
HWND findHostOrWarn(Fl_Window* window)
{
    HWND hSelf = fl_xid(window);
    HWND hParent = hSelf ? ::GetParent(hSelf) : NULL;
    if (NULL == hParent) {
        fl_message_title("Send to AutoCAD");
        fl_message("Not running embedded in an ARX host (no parent window) -"
                   " launch this via HsbChatPanelPoc's Geometry button.");
    }
    return hParent;
}

// Sends `text` to whichever window this one is currently reparented into via
// WM_COPYDATA. No-op (findHostOrWarn already showed a status dialog) if not
// currently embedded.
void sendCommandToHost(Fl_Window* window, const char* text)
{
    HWND hSelf = fl_xid(window);
    HWND hParent = findHostOrWarn(window);
    if (NULL == hParent)
        return;

    // Naive byte->wchar_t widening, not a real UTF-8 decode - fine here since
    // AutoCAD command names are plain ASCII (e.g. "ATHELLO"); would mangle
    // anything outside that range.
    std::wstring wtext(text, text + std::strlen(text));
    COPYDATASTRUCT cds = {};
    cds.dwData = kAutoCadCommandMsgId;
    cds.cbData = static_cast<DWORD>((wtext.size() + 1) * sizeof(wchar_t));
    cds.lpData = const_cast<wchar_t*>(wtext.c_str());
    ::SendMessage(hParent, WM_COPYDATA, reinterpret_cast<WPARAM>(hSelf), reinterpret_cast<LPARAM>(&cds));
}

// hsbWebSocketHub endpoint + topics — loaded from settings.lua (settings.h);
// defaults match the former kHubHost/kHubPort/k*Topic hardcodes. Accessors
// below keep the call sites reading like the old constants.
const std::string& hubHost() { return settings().hubHost; }
unsigned short hubPort() { return settings().hubPort; }
const std::string& geometryTopic() { return settings().topicGeometry; }

// Shows a "Draw in AutoCAD" status dialog with `message`, unless
// silentOnFailure - the title is always the same, only the body differs per
// caller/failure reason. Shared by sendGeometryToHost/sendMeshToHost/
// drawSelectionToHost, which between them have several such dialogs (button
// click gets one, auto-draw-on-selection passes silentOnFailure=true so
// browsing the tree - or running with no hub up - doesn't pop one on every
// single click).
void showDrawMessage(bool silentOnFailure, const char* message)
{
    if (silentOnFailure)
        return;
    fl_message_title("Draw in AutoCAD");
    fl_message("%s", message);
}

// Builds "[x,y,z],[x,y,z],..." (no enclosing brackets) from `points` - the
// inner-tuple-list fragment shared by sendGeometryToHost's own "points"
// array and sendMeshToHost's own per-face "curve" arrays.
std::string joinPointsAsJsonTuples(const std::vector<dxx::Point3D>& points)
{
    std::string out;
    for (size_t i = 0; i < points.size(); ++i) {
        if (i > 0)
            out += ",";
        char buf[96];
        snprintf(buf, sizeof(buf), "[%g,%g,%g]", points[i].x, points[i].y, points[i].z);
        out += buf;
    }
    return out;
}

// Publishes `node`'s curve geometry (world-space, already tessellated) to
// hsbWebSocketHub's "acad_geometry" topic, one publish per curve (each as
// {"points":[[x,y,z],...]}) - the WebSocket equivalent of this function's
// original WM_COPYDATA implementation. Deliberately NOT WM_COPYDATA: a hub
// publish works whether this window is embedded in HsbChatPanelPoc or
// running fully standalone, unlike the old GetParent()-based check. Shows a
// status dialog on "nothing selected"/"no curve geometry"/publish failure,
// UNLESS silentOnFailure (used by the auto-draw-on-selection call below, so
// browsing the tree with no hub running doesn't pop a dialog on every click -
// same reasoning that already made the "no curve geometry" case silent for
// that caller specifically).
void sendGeometryToHost(const dxx::DxxNode* node, bool silentOnFailure = false)
{
    if (!node) {
        showDrawMessage(silentOnFailure, "Nothing selected.");
        return;
    }
    std::vector<dxx::Curve> curves = dxx::extractCurves3D(*node);
    if (curves.empty()) {
        showDrawMessage(silentOnFailure, "No curve geometry in the current selection.");
        return;
    }

    bool anySent = false;
    bool anyPublishFailed = false;
    for (const dxx::Curve& curve : curves) {
        std::vector<dxx::Point3D> pts = dxx::tessellateCurveWorld(curve);
        if (pts.size() < 2)
            continue;

        std::string data = "{\"points\":[" + joinPointsAsJsonTuples(pts) + "]}";

        if (PublishToHub(hubHost(), hubPort(), geometryTopic(), data))
            anySent = true;
        else
            anyPublishFailed = true;
    }

    if (!anySent) {
        showDrawMessage(silentOnFailure, anyPublishFailed
            ? "Failed to publish curve to the AutoCAD hub (is hsbWebSocketHub running?)."
            : "No curve geometry in the current selection.");
    }
}

// Builds the {"block":[[[x,y,z],...],[[x,y,z],...],...]} JSON for `mesh` -
// all faces together in ONE message, not one {"points":[...]} publish per
// face. This is what makes the receiving side (HsbChatPanelPoc's
// DrawWorldBlock, see that project's own HubProtocol.h/extractBlockLines)
// draw the whole mesh as a single new AutoCAD block: one pick selects the
// entire MassElement, not one polyline at a time - the whole reason for this
// shape (2026-09-07, per explicit request: "make every MassElement into a
// block in order to select individually"). Still a wireframe-per-face
// representation via the already-working AcDb3dPolyline pipeline, not a real
// solid/mesh AutoCAD entity: AutoCAD's own mesh entity classes turned out not
// to be usable here - AcDbPolyFaceMesh has no header at all in the installed
// ObjectARX 2026 SDK, and AcDbSubDMesh has a header (dbSubD.h) but its
// constructor/setSubDMesh aren't exported by any DLL in the installed
// AutoCAD 2026 (confirmed via `dumpbin /exports` across the whole install
// tree - acdb25.dll/accore.dll neither one has it, despite the header
// existing).
//
// A face's own index list (dxx::MeshBody::faces, 0-based into
// mesh.vertices) sometimes repeats its first index at the end (explicitly
// closing the loop, e.g. "0,1,2,3,0") - stripped here since
// DrawWorldBlock's own AcDb3dPolyline already closes the loop itself
// (connecting the last point back to the first, same as every curve
// already published this way); an internal repeated index (a
// self-touching/bridged boundary - e.g. an L-shaped or multiply-connected
// face) is passed through as-is, same vertex walk FltkMeshWidget's own
// GL_POLYGON already renders for that face.
//
// Returns nullopt if `mesh` has no usable face data (nothing to publish) -
// shared by sendMeshToHost (single mesh) and drawSelectionToHost (every mesh/
// beam under a selection) so both use the exact same stripping/validity
// logic instead of drifting apart.
std::optional<std::string> buildMeshBlockJson(const dxx::MeshBody& mesh)
{
    std::string data = "{\"block\":[";
    bool anyFace = false;
    for (const std::vector<int>& face : mesh.faces) {
        std::vector<int> loop = face;
        if (loop.size() >= 2 && loop.front() == loop.back())
            loop.pop_back();
        if (loop.size() < 2)
            continue;

        std::vector<dxx::Point3D> facePoints;
        facePoints.reserve(loop.size());
        bool validIndices = true;
        for (int idx : loop) {
            if (idx < 0 || static_cast<size_t>(idx) >= mesh.vertices.size()) {
                validIndices = false;
                break;
            }
            facePoints.push_back(mesh.vertices[static_cast<size_t>(idx)]);
        }
        if (!validIndices)
            continue;
        std::string curve = "[" + joinPointsAsJsonTuples(facePoints) + "]";

        if (anyFace)
            data += ",";
        data += curve;
        anyFace = true;
    }
    data += "]}";
    return anyFace ? std::make_optional(std::move(data)) : std::nullopt;
}

void sendMeshToHost(const dxx::MeshBody& mesh, bool silentOnFailure = false)
{
    if (mesh.vertices.empty() || mesh.faces.empty()) {
        showDrawMessage(silentOnFailure, "No mesh geometry in the current selection.");
        return;
    }

    std::optional<std::string> data = buildMeshBlockJson(mesh);
    if (!data) {
        showDrawMessage(silentOnFailure, "No usable face data in the current selection.");
        return;
    }

    if (!PublishToHub(hubHost(), hubPort(), geometryTopic(), *data)) {
        showDrawMessage(silentOnFailure, "Failed to publish mesh to the AutoCAD hub (is hsbWebSocketHub running?).");
    }
}

// Draws EVERYTHING found anywhere under `node`, not just one nearest/cached
// match: every curve (extractCurves3D already walks the whole subtree), plus
// every real mesh (extractAllMeshBodies) and every GenBeam box
// (extractAllGenBeamBoxes) - each published as its own hub message/AutoCAD
// block. This is what lets the "Draw" button draw a whole container (e.g.
// the document root, or any node with several MassElement/GenBeam
// descendants) in one click, not just a single selected leaf - real DXX
// coordinates are absolute, so nothing needs re-projecting to combine them.
// Deliberately only wired to the explicit "Draw" button, not to
// onNodeSelected's auto-draw-on-selection: that path intentionally keeps its
// own narrower single-element behavior (see onNodeSelected), so just
// browsing the tree can't accidentally fire dozens of hub publishes from one
// click on a container node.
void drawSelectionToHost(const dxx::DxxNode* node, bool silentOnFailure = false)
{
    if (!node) {
        showDrawMessage(silentOnFailure, "Nothing selected.");
        return;
    }

    bool anySent = false;
    bool anyPublishFailed = false;

    for (const dxx::Curve& curve : dxx::extractCurves3D(*node)) {
        std::vector<dxx::Point3D> pts = dxx::tessellateCurveWorld(curve);
        if (pts.size() < 2)
            continue;
        std::string data = "{\"points\":[" + joinPointsAsJsonTuples(pts) + "]}";
        if (PublishToHub(hubHost(), hubPort(), geometryTopic(), data))
            anySent = true;
        else
            anyPublishFailed = true;
    }

    auto publishAllMeshes = [&](const std::vector<dxx::MeshBody>& meshes) {
        for (const dxx::MeshBody& mesh : meshes) {
            std::optional<std::string> data = buildMeshBlockJson(mesh);
            if (!data)
                continue;
            if (PublishToHub(hubHost(), hubPort(), geometryTopic(), *data))
                anySent = true;
            else
                anyPublishFailed = true;
        }
    };
    publishAllMeshes(dxx::extractAllMeshBodies(*node));
    publishAllMeshes(dxx::extractAllGenBeamBoxes(*node));

    if (!anySent) {
        showDrawMessage(silentOnFailure, anyPublishFailed
            ? "Failed to publish geometry to the AutoCAD hub (is hsbWebSocketHub running?)."
            : "No curve, mesh, or beam geometry in the current selection.");
    }
}

const char* kAppVersion = "1.1.0";
} // namespace

FltkMainWindow::FltkMainWindow(int x, int y, int w, int h, const char* label)
    : Fl_Group(x, y, w, h, label)
{
    buildLayout();
    end();
    updateTitle();

    // First settings() call loads settings.lua (once) — hub endpoint/topics.
    const Settings& cfg = settings();
    m_hub = std::make_unique<HubClient>(cfg.hubHost, cfg.hubPort, cfg.topicMap,
        [this](std::optional<dxx::DxxDocument> doc, std::string filename) {
            onMapReceived(std::move(doc), std::move(filename));
        },
        [this](bool connected) { onHubConnectionChanged(connected); });

    // Separate connection: the hub relays each topic's broadcasts unwrapped
    // with no topic tag, so one connection can only unambiguously belong to
    // one topic - raw mode, no DXX-shape detection (see HubClient).
    m_elementCommandsHub = std::make_unique<HubClient>(cfg.hubHost, cfg.hubPort,
        cfg.topicElementCommands,
        [this](std::string rawMessage) {
            onElementCommandsReceived(std::move(rawMessage));
        },
        [this](bool connected) { onElementCommandsConnectionChanged(connected); });
}

FltkMainWindow::~FltkMainWindow() = default;

void FltkMainWindow::resize(int x, int y, int w, int h)
{
    Fl_Group::resize(x, y, w, h);
    // Keep the two floating toolbar rows pinned to the top edge at their
    // own fixed heights - two independent sibling Fl_Flex rows (not one
    // nested inside an outer Fl_Flex::COLUMN - simpler to reason about, and
    // reuses the exact single-row construction/resize shape this file
    // already had before there were two rows, rather than introducing a
    // new nested-Fl_Flex shape untested elsewhere in this project).
    if (m_toolbarRow1)
        m_toolbarRow1->resize(x, y, w, kToolbarRowH);
    if (m_toolbarRow2)
        m_toolbarRow2->resize(x, y + kToolbarRowH, w, kToolbarRowH);
}

void FltkMainWindow::buildLayout()
{
    // ── Content tile (drag-resizable), anchored at (0,0) ────────────────────
    // Fl_Tile's size_range-mode resize assumes the tile sits at the origin, so
    // it spans the full area and its children start below the toolbar. The
    // toolbar is a sibling that floats over the tile's empty top strip.
    Fl_Tile* content = new Fl_Tile(0, 0, w(), h());

    int contentH = h() - kToolbarH;
    m_tree = new FltkTreePanel(0, kToolbarH, 350, contentH, "Document");
    m_tree->onNodeSelected = [this](const dxx::DxxNode* node) { onNodeSelected(node); };
    content->size_range(m_tree, 180, 120);

    int propsH = contentH / 2;
    m_props = new FltkPropertiesPanel(350, kToolbarH, w() - 350, propsH, "Properties");
    m_props->align(FL_ALIGN_TOP | FL_ALIGN_LEFT);
    content->size_range(m_props, 120, 80);

    // Fl_Table's constructor leaves the "current group" on its internal
    // Fl_Scroll, so reset it to the tile before adding the next child.
    content->begin();

    // Geometry pane: the 2D profile widget and the 3D mesh widget occupy the
    // exact same rect inside a plain Fl_Group (not the tile directly - Fl_Tile's
    // drag-resize hit-testing assumes non-overlapping children); onNodeSelected
    // shows whichever one applies to the current selection and hides the other.
    // Both spanning the group's full rect means Fl_Group's default resize
    // (preserving each child's margin to the group's original edges - zero on
    // all four sides here) keeps them sized to the group with no extra code.
    int geomX = 350, geomY = kToolbarH + propsH, geomW = w() - 350, geomH = contentH - propsH;
    m_geomHost = new Fl_Group(geomX, geomY, geomW, geomH);
    m_geomHost->begin();
    m_geom = new FltkGeometryWidget(geomX, geomY, geomW, geomH, "Geometry");
    m_geom->align(FL_ALIGN_TOP | FL_ALIGN_LEFT);
    m_mesh = new FltkMeshWidget(geomX, geomY, geomW, geomH);
    m_mesh->hide();
    m_geomHost->end();
    m_geomHost->resizable(m_geom);
    content->size_range(m_geomHost, 120, 80);

    content->end();
    content->resizable(m_geomHost);
    resizable(content);

    // ── Toolbar (floats on top of the tile's empty top strip) ───────────────
    // Two stacked rows (see kToolbarH's own comment on why), as two
    // independent sibling Fl_Flex widgets rather than one nested inside an
    // outer Fl_Flex::COLUMN - simpler to reason about, and reuses the exact
    // single-row construction shape this file's own original one-row
    // toolbar already used, rather than introducing a new nested-Fl_Flex
    // shape untested elsewhere in this project. Row 1 is document/tree
    // browsing (Open/Reload/Expand/Collapse/search), row 2 is the AutoCAD
    // interop controls (command box/"-> AutoCAD"/"Draw").
    m_toolbarRow1 = new Fl_Flex(0, 0, w(), kToolbarRowH, Fl_Flex::ROW);
    m_toolbarRow1->gap(6);

    Fl_Button* btnOpen = new Fl_Button(0, 0, 0, 0, kOpenLabel);
    Fl_Button* btnReload = new Fl_Button(0, 0, 0, 0, kReloadLabel);
    Fl_Button* btnExpand = new Fl_Button(0, 0, 0, 0, kExpandLabel);
    Fl_Button* btnCollapse = new Fl_Button(0, 0, 0, 0, kCollapseLabel);

    m_searchEdit = new Fl_Input(0, 0, 0, 0);
    m_searchEdit->tooltip("Search node names, properties and values");
    Fl_Button* btnFind = new Fl_Button(0, 0, 0, 0, kFindLabel);
    Fl_Button* btnClear = new Fl_Button(0, 0, 0, 0, kClearLabel);

    m_toolbarRow1->fixed(btnOpen, 70);
    m_toolbarRow1->fixed(btnReload, 70);
    m_toolbarRow1->fixed(btnExpand, 90);
    m_toolbarRow1->fixed(btnCollapse, 95);
    m_toolbarRow1->fixed(btnFind, 60);
    m_toolbarRow1->fixed(btnClear, 40);
    // search edit absorbs remaining width
    m_toolbarRow1->end();

    m_toolbarRow2 = new Fl_Flex(0, kToolbarRowH, w(), kToolbarRowH, Fl_Flex::ROW);
    m_toolbarRow2->gap(6);

    m_autocadCmdEdit = new Fl_Input(0, 0, 0, 0);
    m_autocadCmdEdit->tooltip("Command to send to the host ARX app (e.g. ATHELLO)");
    Fl_Button* btnSendToAcad = new Fl_Button(0, 0, 0, 0, kSendToAcadLabel);

    Fl_Button* btnDraw = new Fl_Button(0, 0, 0, 0, kDrawLabel);
    btnDraw->tooltip("Draw every curve/mesh/beam found under the selected node in AutoCAD");

    m_toolbarRow2->fixed(btnSendToAcad, 90);
    m_toolbarRow2->fixed(btnDraw, 60);
    // AutoCAD command edit absorbs remaining width - previously fixed at a
    // cramped 140px; now that it's not squeezed onto the same row as every
    // tree-browsing button, there's no reason not to let it use the space.
    m_toolbarRow2->end();

    // ── Callbacks ────────────────────────────────────────────────────────────
    btnOpen->callback([](Fl_Widget*, void* data) {
        auto* self = static_cast<FltkMainWindow*>(data);
        Fl_Native_File_Chooser chooser(Fl_Native_File_Chooser::BROWSE_FILE);
        chooser.title("Open DXX File");
        chooser.filter("DXX Files\t*.dxx\nAll Files\t*");
        if (!self->m_filePath.empty())
            chooser.directory(self->m_filePath.c_str());
        if (chooser.show() == 0)
            self->openFile(chooser.filename());
    }, this);

    btnReload->callback([](Fl_Widget*, void* data) {
        auto* self = static_cast<FltkMainWindow*>(data);
        if (!self->m_filePath.empty())
            self->openFile(self->m_filePath.c_str());
    }, this);

    btnExpand->callback([](Fl_Widget*, void* data) {
        static_cast<FltkMainWindow*>(data)->m_tree->expandAllItems();
    }, this);

    btnCollapse->callback([](Fl_Widget*, void* data) {
        static_cast<FltkMainWindow*>(data)->m_tree->collapseAllItems();
    }, this);

    btnFind->callback([](Fl_Widget*, void* data) {
        static_cast<FltkMainWindow*>(data)->doSearch();
    }, this);

    btnClear->callback([](Fl_Widget*, void* data) {
        static_cast<FltkMainWindow*>(data)->clearSearch();
    }, this);

    m_searchEdit->callback([](Fl_Widget*, void* data) {
        static_cast<FltkMainWindow*>(data)->doSearch();
    }, this);
    m_searchEdit->when(FL_WHEN_ENTER_KEY);

    btnSendToAcad->callback([](Fl_Widget* w, void* data) {
        auto* self = static_cast<FltkMainWindow*>(data);
        const char* text = self->m_autocadCmdEdit->value();
        if (text && *text)
            sendCommandToHost(w->window(), text);
    }, this);
    m_autocadCmdEdit->when(FL_WHEN_ENTER_KEY);
    m_autocadCmdEdit->callback(btnSendToAcad->callback(), this);

    btnDraw->callback([](Fl_Widget*, void* data) {
        auto* self = static_cast<FltkMainWindow*>(data);
        drawSelectionToHost(self->m_selectedNode);
    }, this);
}

void FltkMainWindow::openFile(const char* path)
{
    if (!path || !*path) return;

    // Parsing (dxx::parseFile) runs on a detached background thread, not
    // here - a large file's parse cost would otherwise freeze the whole
    // window for however long it takes. Only the already-built
    // optional<DxxDocument> crosses back to the main thread (via
    // postToMain), same off-thread-parse/main-thread-display split
    // HubClient uses for a hub-received map. `this` outliving the thread is
    // safe: FltkMainWindow lives for the whole app run, far longer than one
    // file parse.
    std::string pathStr = path;
    std::thread([this, pathStr]() {
        auto doc = dxx::parseFile(pathStr);
        postToMain([this, doc = std::move(doc), pathStr]() mutable {
            if (!doc) {
                fl_alert("Failed to parse file:\n%s", pathStr.c_str());
                return;
            }
            m_filePath = pathStr;
            m_fromMap = false;
            m_fromElementCommands = false;
            displayDocument(std::move(*doc));
        });
    }).detach();
}

void FltkMainWindow::onMapReceived(std::optional<dxx::DxxDocument> doc, std::string filename)
{
    if (!doc) {
        std::fprintf(stderr, "dxxviewer: failed to parse map received from hub\n");
        return;
    }
    m_filePath.clear();
    m_fromMap = true;
    m_mapFilename = std::move(filename);
    m_fromElementCommands = false;
    displayDocument(std::move(*doc));
}

void FltkMainWindow::onElementCommandsReceived(std::string rawMessage)
{
    // Not a selection_parameters message (e.g. an element_parameters/
    // element_map/element_parameter_set reply not handled by this push-only
    // viewer), or a parse failure - leave whatever is currently displayed
    // alone, mirroring how the "map" hub client only acts on messages it
    // successfully decodes.
    auto doc = buildDocumentFromElementCommands(rawMessage);
    if (!doc) return;

    size_t count = doc->root.children.size();
    if (count == 0)
        m_elementCommandsSummary = "Revit selection (none)";
    else
        m_elementCommandsSummary = "Revit selection (" + std::to_string(count) +
            (count == 1 ? " element)" : " elements)");

    m_filePath.clear();
    m_fromMap = false;
    m_fromElementCommands = true;
    displayDocument(std::move(*doc));
}

void FltkMainWindow::onHubConnectionChanged(bool connected)
{
    m_hubConnected = connected;
    updateTitle();
}

void FltkMainWindow::onElementCommandsConnectionChanged(bool connected)
{
    m_elementCommandsHubConnected = connected;
    updateTitle();
}

void FltkMainWindow::updateTitle()
{
    std::string title = std::string("DXX Viewer ") + kAppVersion;
    if (m_fromElementCommands)
        title += " - " + m_elementCommandsSummary;
    else if (m_fromMap) {
        if (m_mapFilename.empty()) {
            title += " - map@" + std::to_string(hubPort());
        } else {
            title += " - " + m_mapFilename;
        }
    }
    else if (!m_filePath.empty())
        title += " - " + m_filePath;
    title += m_hubConnected ? "  [hub: connected]" : "  [hub: offline]";
    title += m_elementCommandsHubConnected ? "  [elements: connected]" : "  [elements: offline]";
    if (window())
        window()->copy_label(title.c_str());
}

void FltkMainWindow::displayDocument(dxx::DxxDocument doc)
{
    onNodeSelected(nullptr); // drop any pointers into the document being replaced
    m_doc = std::make_unique<dxx::DxxDocument>(std::move(doc));
    m_tree->resetSearch();
    updateTitle();
    populateTree();
}

void FltkMainWindow::populateTree()
{
    if (!m_doc) return;
    m_tree->populate(*m_doc);
}

void FltkMainWindow::onNodeSelected(const dxx::DxxNode* node)
{
    m_selectedNode = node;
    m_props->fill(node);

    // The local 3D preview shows EVERYTHING under the selection, not just one
    // nearest match - selecting a container node (e.g. the document root)
    // previously only ever previewed the first mesh/beam encountered, which
    // read as an incomplete/broken preview once extractAllMeshBodies/
    // extractAllGenBeamBoxes made it possible to gather all of them.
    // Kept as two SEPARATE merged caches (not one combined MeshBody) so
    // FltkMeshWidget can render real meshes (walls/sheets) semi-transparent
    // while keeping GenBeam boxes opaque - the whole point of gathering every
    // element under a selection is to see the beams that would otherwise be
    // hidden inside a wall's solid.
    m_wallMeshCache.reset();
    m_beamMeshCache.reset();
    if (node) {
        std::vector<dxx::MeshBody> walls = dxx::extractAllMeshBodies(*node);
        if (!walls.empty())
            m_wallMeshCache = std::make_unique<dxx::MeshBody>(dxx::mergeMeshBodies(walls));

        std::vector<dxx::MeshBody> beams = dxx::extractAllGenBeamBoxes(*node);
        if (!beams.empty())
            m_beamMeshCache = std::make_unique<dxx::MeshBody>(dxx::mergeMeshBodies(beams));
    }

    if (m_wallMeshCache || m_beamMeshCache) {
        m_geom->hide();
        m_mesh->showMeshes(m_wallMeshCache.get(), m_beamMeshCache.get());
        m_mesh->show();
    } else {
        m_mesh->showMeshes(nullptr, nullptr);
        m_mesh->hide();
        m_geom->show();
        m_geom->showNode(node);
    }
    // FltkMeshWidget is a real native child window (Fl_Gl_Window), unlike the
    // plain-widget FltkGeometryWidget - hiding/showing it doesn't automatically
    // repaint whichever sibling now occupies its screen area, so force it.
    m_geomHost->redraw();

    // Auto-draw: selecting a CURVE leaf with actual point data sends it
    // straight to AutoCAD via the same pipeline the "Draw" button uses
    // (sendGeometryToHost) - no separate click needed for the common case of
    // just browsing curves in the tree (including a live "map" broadcast's
    // synthetic CURVE nodes - see ElementCommandsBridge). Checking
    // extractCurves3D up front (rather than just calling sendGeometryToHost
    // outright) is what keeps this silent for an empty CURVE node instead of
    // popping its "No curve geometry" dialog on every such tree click - that
    // dialog makes sense for a deliberate Draw click, not an automatic one.
    // silentOnFailure=true for the same reason: browsing the tree with no
    // hub running (a legitimate standalone-dev scenario now that this no
    // longer needs HsbChatPanelPoc's embedding) shouldn't pop a dialog on
    // every single CURVE click either.
    //
    // Same idea, added for mesh selections (e.g. a MassElement/SimpleBody):
    // deliberately does NOT reuse m_wallMeshCache/m_beamMeshCache any more -
    // those now hold EVERYTHING merged under the selection (for the local
    // preview above), and auto-firing on every tree click has to stay
    // narrow, or selecting a
    // container node while just browsing (e.g. clicking the root) would
    // silently publish one giant combined block to AutoCAD. Re-runs the
    // original single-nearest search instead, so auto-draw still only ever
    // fires for a genuinely single mesh/beam - the "draw everything under
    // this selection" behavior is reserved for an explicit "Draw" click
    // (drawSelectionToHost). Also deliberately does NOT broaden the CURVE
    // branch's own literal-name gate to match (kept exactly as before, not
    // "any ancestor with a nested curve", to avoid changing existing
    // curve-auto-draw behavior at all).
    if (node && node->name == "CURVE" && !dxx::extractCurves3D(*node).empty()) {
        sendGeometryToHost(node, /*silentOnFailure=*/true);
    } else if (node) {
        if (auto single = dxx::extractMeshBody(*node))
            sendMeshToHost(*single, /*silentOnFailure=*/true);
        else if (auto singleBeam = dxx::extractGenBeamBox(*node))
            sendMeshToHost(*singleBeam, /*silentOnFailure=*/true);
    }
}

void FltkMainWindow::doSearch()
{
    if (!m_doc) return;
    const char* term = m_searchEdit->value();
    if (!term || !*term) return;

    if (!m_tree->searchNext(term)) {
        fl_message_title("Search");
        fl_message("Not found: %s", term);
    }
}

void FltkMainWindow::clearSearch()
{
    m_searchEdit->value("");
    m_tree->resetSearch();
}

} // namespace dxxviewer