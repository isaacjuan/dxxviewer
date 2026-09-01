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
// at a time. Also runs a HubClient that subscribes to hsbWebSocketHub's
// "map" topic (127.0.0.1:8181) so a document can arrive live over the
// network instead of only from disk.
class FltkMainWindow : public Fl_Group {
public:
    explicit FltkMainWindow(int x, int y, int w, int h, const char* label = nullptr);
    ~FltkMainWindow();

    // Parses and displays `path`. Returns false (and shows a dialog) if parse fails.
    bool openFile(const char* path);

protected:
    void resize(int x, int y, int w, int h) FL_OVERRIDE;

private:
    void buildLayout();
    void onNodeSelected(const dxx::DxxNode* node);
    void doSearch();
    void clearSearch();
    void populateTree();
    void onMapReceived(std::string dxxText, std::string filename);
    void onHubConnectionChanged(bool connected);
    void updateTitle();

    FltkTreePanel*       m_tree = nullptr;
    FltkPropertiesPanel* m_props = nullptr;
    Fl_Group*            m_geomHost = nullptr;
    FltkGeometryWidget*  m_geom = nullptr;
    FltkMeshWidget*      m_mesh = nullptr;
    Fl_Flex*             m_toolbar = nullptr;
    Fl_Input*            m_searchEdit = nullptr;

    std::unique_ptr<dxx::DxxDocument> m_doc;
    std::unique_ptr<dxx::MeshBody> m_meshCache;
    std::string m_filePath;
    bool m_fromMap = false;
    std::string m_mapFilename;
    bool m_hubConnected = false;
    std::unique_ptr<HubClient> m_hub;
};

} // namespace dxxviewer