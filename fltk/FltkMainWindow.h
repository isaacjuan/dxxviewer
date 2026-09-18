#pragma once
#include <FL/Fl_Group.H>
#include "../dxx_parser.h"
#include <memory>
#include <string>

class Fl_Input;
class Fl_Button;
class Fl_Flex;

namespace dxxviewer {

class FltkTreePanel;
class FltkPropertiesPanel;
class FltkGeometryWidget;
class FltkMeshWidget;
class HubClient;

// Main window: toolbar (Open/Reload/Expand/Collapse/Search) above a
// horizontal split of tree | (properties + geometry). Owns the parsed
// document and wires tree selection to the two detail panels - the 2D
// profile widget (FltkGeometryWidget) for CURVE-based nodes, or the 3D
// mesh widget (FltkMeshWidget) when the selected node contains a
// SimpleBody-style mesh (see dxx::extractMeshBody); only one is visible
// at a time. Also runs two HubClients against hsbWebSocketHub
// (127.0.0.1:8181): one on the "map" topic so a DXX document can arrive
// live over the network instead of only from disk, and one on the
// "element_commands" topic (raw mode - see HubClient) carrying JSON
// "selection_parameters" pushes from a Revit add-in, converted to a
// synthetic DxxDocument by ElementCommandsBridge and shown through the same
// tree/properties panels. The toolbar's "-> AutoCAD" input/button send an
// arbitrary command string back to a host ARX app via WM_COPYDATA, when this
// window has been reparented into one (see m_autocadCmdEdit). The "Draw"
// button instead sends every curve, mesh, and beam box found anywhere under
// the currently selected node over the hub (see m_selectedNode /
// drawSelectionToHost in the .cpp) - no reparenting needed for that one, and
// selecting a container node (e.g. the document root) draws everything
// underneath it in one click.

class FltkMainWindow : public Fl_Group {
public:
    explicit FltkMainWindow(int x, int y, int w, int h, const char* label = nullptr);
    ~FltkMainWindow();

    // Parses and displays `path`. Parsing runs on a background thread (same
    // off-main-thread handoff as a hub-received map, see HubClient) so a
    // large file doesn't block the UI while it loads; shows an alert dialog
    // (on the main thread, once parsing finishes) if it fails.
    void openFile(const char* path);

protected:
    void resize(int x, int y, int w, int h) FL_OVERRIDE;

private:
    void buildLayout();
    void onNodeSelected(const dxx::DxxNode* node);
    void doSearch();
    void clearSearch();
    void populateTree();
    void onMapReceived(std::optional<dxx::DxxDocument> doc, std::string filename);
    void onElementCommandsReceived(std::string rawMessage);
    void onHubConnectionChanged(bool connected);
    void onElementCommandsConnectionChanged(bool connected);
    void updateTitle();
    void displayDocument(dxx::DxxDocument doc);

    FltkTreePanel*       m_tree = nullptr;
    FltkPropertiesPanel* m_props = nullptr;
    Fl_Group*            m_geomHost = nullptr;
    FltkGeometryWidget*  m_geom = nullptr;
    FltkMeshWidget*      m_mesh = nullptr;
    // Two independent, sibling Fl_Flex rows (not one nested inside the
    // other) - see resize()'s own comment for why not nested.
    Fl_Flex*             m_toolbarRow1 = nullptr;
    Fl_Flex*             m_toolbarRow2 = nullptr;
    Fl_Input*            m_searchEdit = nullptr;
    // Sends whatever text is typed here to the host ARX app (HsbChatPanelPoc,
    // D:\dev_jp\HsbChatPanelPoc) via WM_COPYDATA, IF this window is currently
    // reparented into one (i.e. launched via its "Geometry" button, not
    // standalone) - see the "-> AutoCAD" button's callback in the .cpp.
    Fl_Input*            m_autocadCmdEdit = nullptr;

    // The tree's current selection, refreshed on every onNodeSelected() call -
    // read by the "Draw" button's callback so it always draws whatever is
    // selected right now, without needing FltkTreePanel to expose more than
    // its existing onNodeSelected callback. Never owns; points into m_doc.
    const dxx::DxxNode* m_selectedNode = nullptr;

    std::unique_ptr<dxx::DxxDocument> m_doc;
    // Kept as two separate merged caches, not one, so the 3D preview
    // (FltkMeshWidget::showMeshes) can render real meshes (walls/sheets)
    // semi-transparent while keeping GenBeam boxes opaque - see
    // onNodeSelected in the .cpp.
    std::unique_ptr<dxx::MeshBody> m_wallMeshCache;
    std::unique_ptr<dxx::MeshBody> m_beamMeshCache;
    std::string m_filePath;
    bool m_fromMap = false;
    std::string m_mapFilename;
    bool m_fromElementCommands = false;
    std::string m_elementCommandsSummary;
    bool m_hubConnected = false;
    bool m_elementCommandsHubConnected = false;
    std::unique_ptr<HubClient> m_hub;
    std::unique_ptr<HubClient> m_elementCommandsHub;
};

} // namespace dxxviewer