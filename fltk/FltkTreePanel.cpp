#include "FltkTreePanel.h"
#include "../colors.h"
#include <FL/fl_draw.H>
#include <algorithm>
#include <cctype>

namespace dxxviewer {

namespace {

// Matches a node's display name AND every property key/value (case-insensitive).
bool nodeMatches(const dxx::DxxNode* node, const std::string& termLower) {
    if (!node) return false;
    for (const auto& [k, v] : node->properties) {
        std::string kl = k, vl = v;
        std::transform(kl.begin(), kl.end(), kl.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        std::transform(vl.begin(), vl.end(), vl.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (kl.find(termLower) != std::string::npos) return true;
        if (vl.find(termLower) != std::string::npos) return true;
    }
    return false;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

} // namespace

FltkTreePanel::FltkTreePanel(int x, int y, int w, int h, const char* label)
    : Fl_Tree(x, y, w, h, label)
{
    showroot(1);
    connectorstyle(FL_TREE_CONNECTOR_SOLID);
    selectmode(FL_TREE_SELECT_SINGLE);
    callback(treeCallback, this);
}

void FltkTreePanel::setItemColor(Fl_Tree_Item* item, int depth)
{
    uint32_t rgb = kTreeDepthColorPalette[depth % kTreeDepthColorCount];
    item->labelcolor(fl_rgb_color((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF));
}

void FltkTreePanel::populate(const dxx::DxxDocument& doc)
{
    clear();
    root_label("DXX Document");
    Fl_Tree_Item* rootItem = root();
    if (rootItem) {
        rootItem->user_data((void*)&doc.root);
        setItemColor(rootItem, 0);
        open(rootItem);
    }
    populateNode(rootItem, doc.root, 0);
    redraw();
}

void FltkTreePanel::populateNode(Fl_Tree_Item* parent, const dxx::DxxNode& node, int depth)
{
    for (const auto& child : node.children) {
        std::string label = child.name.empty() ? "(unnamed)" : child.name;
        if (!child.properties.empty())
            label += " [" + std::to_string(child.properties.size()) + "]";
        if (!child.children.empty())
            label += " >";

        Fl_Tree_Item* item = parent ? add(parent, label.c_str()) : add(label.c_str());
        if (!item) continue;
        item->user_data((void*)&child);
        setItemColor(item, depth + 1);
        if (depth < 2) open(item);

        populateNode(item, child, depth + 1);
    }
}

const dxx::DxxNode* FltkTreePanel::selectedNode()
{
    Fl_Tree_Item* item = first_selected_item();
    if (!item) return nullptr;
    return reinterpret_cast<const dxx::DxxNode*>(item->user_data());
}

bool FltkTreePanel::itemMatches(Fl_Tree_Item* item, const std::string& termLower)
{
    if (!item) return false;
    const char* lbl = item->label();
    if (lbl && toLower(lbl).find(termLower) != std::string::npos) return true;
    auto* node = reinterpret_cast<const dxx::DxxNode*>(item->user_data());
    return nodeMatches(node, termLower);
}

Fl_Tree_Item* FltkTreePanel::findMatch(const char* term, Fl_Tree_Item* after)
{
    if (!term || !*term) return nullptr;
    std::string termLower = toLower(term);

    std::vector<Fl_Tree_Item*> items;
    Fl_Tree_Item* item = first();
    while (item) {
        items.push_back(item);
        item = item->next();
    }

    size_t start = 0;
    if (after) {
        auto it = std::find(items.begin(), items.end(), after);
        if (it != items.end()) start = (size_t)(it - items.begin()) + 1;
    }

    for (size_t i = 0; i < items.size(); ++i) {
        Fl_Tree_Item* candidate = items[(start + i) % items.size()];
        if (candidate == after) continue;
        if (itemMatches(candidate, termLower)) return candidate;
    }
    return nullptr;
}

bool FltkTreePanel::searchNext(const char* term)
{
    Fl_Tree_Item* hit = findMatch(term, m_lastSearchHit);
    if (!hit) {
        m_lastSearchHit = nullptr;
        return false;
    }
    m_lastSearchHit = hit;
    for (Fl_Tree_Item* p = hit->parent(); p; p = p->parent())
        open(p);
    select(hit, 1);
    show_item(hit);
    redraw();
    return true;
}

void FltkTreePanel::resetSearch()
{
    m_lastSearchHit = nullptr;
}

void FltkTreePanel::expandAllItems()
{
    Fl_Tree_Item* item = first();
    while (item) {
        open(item);
        item = item->next();
    }
}

void FltkTreePanel::collapseAllItems()
{
    Fl_Tree_Item* item = first();
    while (item) {
        if (!item->is_root()) close(item);
        item = item->next();
    }
}

void FltkTreePanel::treeCallback(Fl_Widget*, void* data)
{
    auto* self = static_cast<FltkTreePanel*>(data);
    if (!self || !self->onNodeSelected) return;
    if (self->callback_reason() != FL_TREE_REASON_SELECTED) return;
    self->onNodeSelected(self->selectedNode());
}

} // namespace dxxviewer