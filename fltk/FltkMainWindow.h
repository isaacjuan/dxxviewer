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

// Main window: toolbar (Open/Reload/Expand/Collapse/Search) above a
// horizontal split of tree | (properties + geometry). Owns the parsed
// document and wires tree selection to the two detail panels.
class FltkMainWindow : public Fl_Group {
public:
    explicit FltkMainWindow(int x, int y, int w, int h, const char* label = nullptr);

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

    FltkTreePanel*       m_tree = nullptr;
    FltkPropertiesPanel* m_props = nullptr;
    FltkGeometryWidget*  m_geom = nullptr;
    Fl_Flex*             m_toolbar = nullptr;
    Fl_Input*            m_searchEdit = nullptr;

    std::unique_ptr<dxx::DxxDocument> m_doc;
    std::string m_filePath;
};

} // namespace dxxviewer