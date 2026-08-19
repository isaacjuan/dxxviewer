#pragma once
#include <FL/Fl_Table_Row.H>
#include "../dxx_parser.h"

namespace dxxviewer {

// Property inspector: 2-column (Property/Value) read-only table.
// Data lives in the panel; draw_cell() renders it from the DxxNode held
// by the panel rather than through Fl_Table's callback-based data model.
class FltkPropertiesPanel : public Fl_Table_Row {
public:
    explicit FltkPropertiesPanel(int x, int y, int w, int h, const char* label = nullptr);

    // Replaces the contents with `node`'s properties (or clears if null).
    void fill(const dxx::DxxNode* node);

protected:
    void draw_cell(TableContext context, int R, int C, int X, int Y, int W, int H) FL_OVERRIDE;

private:
    const dxx::DxxNode* m_node = nullptr;
};

} // namespace dxxviewer