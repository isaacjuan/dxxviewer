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
#include <string>

namespace dxxviewer {

namespace {
const char* kOpenLabel = "Open";
const char* kReloadLabel = "Reload";
const char* kExpandLabel = "+ Expand";
const char* kCollapseLabel = "- Collapse";
const char* kFindLabel = "Find";
const char* kClearLabel = "X";
const char* kSendToAcadLabel = "-> AutoCAD";
const char* kDrawLabel = "Draw";
const int kToolbarH = 32;

// Arbitrary but fixed magic value identifying our WM_COPYDATA messages to a
// host ARX app. Defined independently on both sides - this project
// (dxxviewer) and D:\dev_jp\HsbChatPanelPoc's ChatDockPane.cpp - since they
// are separate repos with no shared header; keep the two in sync by hand if
// this ever changes.
const ULONG_PTR kAutoCadCommandMsgId = 0x4443584Aul; // 'DCXJ', just a tag
// Same idea, different tag: a curve-geometry payload (see
// sendGeometryToHost()) rather than a plain command string. Defined
// independently on HsbChatPanelPoc's side too (kDxxGeometryMsgId there).
const ULONG_PTR kAutoCadGeometryMsgId = 0x4447584Aul; // 'DGXJ', just a tag

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

// Sends `node`'s curve geometry (world-space, already tessellated - one line
// per curve, each point "x,y,z", points space-separated) to the host ARX app
// via WM_COPYDATA, tagged kAutoCadGeometryMsgId so the receiver can tell this
// apart from a plain command string. No-op (status dialog) if `node` has no
// curves under it, or if not currently embedded.
void sendGeometryToHost(Fl_Window* window, const dxx::DxxNode* node)
{
    if (!node) {
        fl_message_title("Draw in AutoCAD");
        fl_message("Nothing selected.");
        return;
    }
    std::vector<dxx::Curve> curves = dxx::extractCurves3D(*node);
    if (curves.empty()) {
        fl_message_title("Draw in AutoCAD");
        fl_message("No curve geometry in the current selection.");
        return;
    }

    HWND hSelf = fl_xid(window);
    HWND hParent = findHostOrWarn(window);
    if (NULL == hParent)
        return;

    std::wstring payload;
    for (const dxx::Curve& curve : curves) {
        std::vector<dxx::Point3D> pts = dxx::tessellateCurveWorld(curve);
        if (pts.size() < 2)
            continue;
        if (!payload.empty())
            payload += L'\n';
        for (size_t i = 0; i < pts.size(); ++i) {
            if (i > 0)
                payload += L' ';
            wchar_t buf[96];
            swprintf_s(buf, L"%g,%g,%g", pts[i].x, pts[i].y, pts[i].z);
            payload += buf;
        }
    }
    if (payload.empty()) {
        fl_message_title("Draw in AutoCAD");
        fl_message("No curve geometry in the current selection.");
        return;
    }

    COPYDATASTRUCT cds = {};
    cds.dwData = kAutoCadGeometryMsgId;
    cds.cbData = static_cast<DWORD>((payload.size() + 1) * sizeof(wchar_t));
    cds.lpData = const_cast<wchar_t*>(payload.c_str());
    ::SendMessage(hParent, WM_COPYDATA, reinterpret_cast<WPARAM>(hSelf), reinterpret_cast<LPARAM>(&cds));
}

const char* kHubHost = "127.0.0.1";
const unsigned short kHubPort = 8181;
const char* kHubTopic = "map";
const char* kElementCommandsTopic = "element_commands";
const char* kAppVersion = "1.0.0";
} // namespace

FltkMainWindow::FltkMainWindow(int x, int y, int w, int h, const char* label)
    : Fl_Group(x, y, w, h, label)
{
    buildLayout();
    end();
    updateTitle();

    m_hub = std::make_unique<HubClient>(kHubHost, kHubPort, kHubTopic,
        [this](std::string dxxText, std::string filename) {
            onMapReceived(std::move(dxxText), std::move(filename));
        },
        [this](bool connected) { onHubConnectionChanged(connected); });

    // Separate connection: the hub relays each topic's broadcasts unwrapped
    // with no topic tag, so one connection can only unambiguously belong to
    // one topic - raw mode, no DXX-shape detection (see HubClient).
    m_elementCommandsHub = std::make_unique<HubClient>(kHubHost, kHubPort, kElementCommandsTopic,
        [this](std::string rawMessage) {
            onElementCommandsReceived(std::move(rawMessage));
        },
        [this](bool connected) { onElementCommandsConnectionChanged(connected); });
}

FltkMainWindow::~FltkMainWindow() = default;

void FltkMainWindow::resize(int x, int y, int w, int h)
{
    Fl_Group::resize(x, y, w, h);
    // Keep the floating toolbar pinned to the top edge at a fixed height.
    if (m_toolbar)
        m_toolbar->resize(x, y, w, kToolbarH);
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

    // ── Toolbar row (floats on top of the tile's empty top strip) ───────────
    m_toolbar = new Fl_Flex(0, 0, w(), kToolbarH, Fl_Flex::ROW);
    m_toolbar->gap(6);

    Fl_Button* btnOpen = new Fl_Button(0, 0, 0, 0, kOpenLabel);
    Fl_Button* btnReload = new Fl_Button(0, 0, 0, 0, kReloadLabel);
    Fl_Button* btnExpand = new Fl_Button(0, 0, 0, 0, kExpandLabel);
    Fl_Button* btnCollapse = new Fl_Button(0, 0, 0, 0, kCollapseLabel);

    m_searchEdit = new Fl_Input(0, 0, 0, 0);
    m_searchEdit->tooltip("Search node names, properties and values");
    Fl_Button* btnFind = new Fl_Button(0, 0, 0, 0, kFindLabel);
    Fl_Button* btnClear = new Fl_Button(0, 0, 0, 0, kClearLabel);

    m_autocadCmdEdit = new Fl_Input(0, 0, 0, 0);
    m_autocadCmdEdit->tooltip("Command to send to the host ARX app (e.g. ATHELLO)");
    Fl_Button* btnSendToAcad = new Fl_Button(0, 0, 0, 0, kSendToAcadLabel);

    Fl_Button* btnDraw = new Fl_Button(0, 0, 0, 0, kDrawLabel);
    btnDraw->tooltip("Draw the selected node's curve geometry in AutoCAD");

    m_toolbar->fixed(btnOpen, 70);
    m_toolbar->fixed(btnReload, 70);
    m_toolbar->fixed(btnExpand, 90);
    m_toolbar->fixed(btnCollapse, 95);
    m_toolbar->fixed(btnFind, 60);
    m_toolbar->fixed(btnClear, 40);
    m_toolbar->fixed(m_autocadCmdEdit, 140);
    m_toolbar->fixed(btnSendToAcad, 90);
    m_toolbar->fixed(btnDraw, 60);
    // search edit absorbs remaining width
    m_toolbar->end();

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

    btnDraw->callback([](Fl_Widget* w, void* data) {
        auto* self = static_cast<FltkMainWindow*>(data);
        sendGeometryToHost(w->window(), self->m_selectedNode);
    }, this);
}

bool FltkMainWindow::openFile(const char* path)
{
    auto doc = dxx::parseFile(path ? path : "");
    if (!doc) {
        fl_alert("Failed to parse file:\n%s", path ? path : "");
        return false;
    }
    m_filePath = path ? path : "";
    m_fromMap = false;
    m_fromElementCommands = false;
    displayDocument(std::move(*doc));
    return true;
}

void FltkMainWindow::onMapReceived(std::string dxxText, std::string filename)
{
    auto doc = dxx::parseString(dxxText);
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
    else if (m_fromMap)
        title += " - " + (m_mapFilename.empty() ? std::string("map@8181") : m_mapFilename);
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

    m_meshCache.reset();
    if (node) {
        if (auto mesh = dxx::extractMeshBody(*node))
            m_meshCache = std::make_unique<dxx::MeshBody>(std::move(*mesh));
    }

    if (m_meshCache) {
        m_geom->hide();
        m_mesh->showMesh(m_meshCache.get());
        m_mesh->show();
    } else {
        m_mesh->showMesh(nullptr);
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
    if (node && node->name == "CURVE" && !dxx::extractCurves3D(*node).empty())
        sendGeometryToHost(window(), node);
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