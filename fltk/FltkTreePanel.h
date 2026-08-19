#pragma once
#include <FL/Fl_Tree.H>
#include <FL/Fl_Tree_Item.H>
#include "../dxx_parser.h"
#include <functional>

namespace dxxviewer {

// Tree browser for DXX node hierarchy. Each Fl_Tree_Item carries its
// const dxx::DxxNode* in user_data() (the root item stores &doc.root).
// Labels follow the convention "Name [N props] >".
class FltkTreePanel : public Fl_Tree {
public:
    explicit FltkTreePanel(int x, int y, int w, int h, const char* label = nullptr);

    // Rebuilds the tree from a parsed document (clears the previous one).
    void populate(const dxx::DxxDocument& doc);

    // Selected node, or nullptr if nothing is selected.
    const dxx::DxxNode* selectedNode();

    // Search: matches node names AND property keys/values across the whole
    // tree regardless of expand state. If `after` is given, resumes past it
    // and wraps around to the start. Returns the matched item or nullptr.
    Fl_Tree_Item* findMatch(const char* term, Fl_Tree_Item* after = nullptr);

    // Advances the search from the previous hit (wrapping around), opens the
    // hit's ancestors, selects and scrolls to it. Returns false if no match.
    bool searchNext(const char* term);

    // Clears the "last hit" so the next searchNext() starts from the top.
    void resetSearch();

    // Expands/collapses every item under the root.
    void expandAllItems();
    void collapseAllItems();

    // Client callback fired on selection.
    std::function<void(const dxx::DxxNode*)> onNodeSelected;

private:
    void populateNode(Fl_Tree_Item* parent, const dxx::DxxNode& node, int depth);
    static bool itemMatches(Fl_Tree_Item* item, const std::string& termLower);
    static void setItemColor(Fl_Tree_Item* item, int depth);

    static void treeCallback(Fl_Widget* w, void* data);

    Fl_Tree_Item* m_lastSearchHit = nullptr;
};

} // namespace dxxviewer