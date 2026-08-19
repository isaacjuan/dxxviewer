#include "FltkPropertiesPanel.h"
#include <FL/fl_draw.H>
#include <algorithm>
#include <string>

namespace dxxviewer {

namespace {
const char* kHeader[] = {"Property", "Value"};
const int kHeaderCols = 2;
} // namespace

FltkPropertiesPanel::FltkPropertiesPanel(int x, int y, int w, int h, const char* label)
    : Fl_Table_Row(x, y, w, h, label)
{
    type(SELECT_NONE);
    rows(0);
    cols(kHeaderCols);
    col_header(1);
    row_header(0);
    row_resize(0);
    col_resize(1);
    when(FL_WHEN_NEVER);

    col_width(0, 160);
    col_width(1, 300);
    col_header_height(24);
    row_height_all(20);
}

void FltkPropertiesPanel::fill(const dxx::DxxNode* node)
{
    m_node = node;
    rows(node ? (int)node->properties.size() : 0);
    redraw();
}

void FltkPropertiesPanel::draw_cell(TableContext context, int R, int C,
                                    int X, int Y, int W, int H)
{
    if (context == CONTEXT_STARTPAGE) {
        fl_font(FL_HELVETICA, 11);
        return;
    }

    if (context == CONTEXT_COL_HEADER) {
        fl_push_clip(X, Y, W, H);
        fl_draw_box(FL_THIN_UP_BOX, X, Y, W, H, FL_BACKGROUND2_COLOR);
        fl_color(FL_BLACK);
        fl_font(FL_HELVETICA_BOLD, 11);
        fl_draw(kHeader[C], X, Y, W, H, FL_ALIGN_LEFT | FL_ALIGN_INSIDE, nullptr, 0);
        fl_pop_clip();
        return;
    }

    if (context == CONTEXT_CELL) {
        const char* text = "";
        if (m_node && R >= 0 && R < (int)m_node->properties.size()) {
            const auto& [key, val] = m_node->properties[R];
            text = C == 0 ? key.c_str() : val.c_str();
        }

        fl_push_clip(X, Y, W, H);
        if (R % 2 == 1)
            fl_draw_box(FL_FLAT_BOX, X, Y, W, H, fl_color_average(FL_BACKGROUND2_COLOR, FL_WHITE, 0.4f));
        else
            fl_draw_box(FL_FLAT_BOX, X, Y, W, H, FL_BACKGROUND2_COLOR);

        fl_color(C == 0 ? FL_BLACK : FL_DARK3);
        fl_font(FL_HELVETICA, 11);
        if (text && *text)
            fl_draw(text, X + 4, Y, W - 8, H, FL_ALIGN_LEFT | FL_ALIGN_INSIDE, nullptr, 0);
        fl_pop_clip();
        return;
    }
}

} // namespace dxxviewer