#include "FltkGeometryWidget.h"
#include "../colors.h"
#include <FL/fl_draw.H>
#include <FL/Fl.H>
#include <cairo.h>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace dxxviewer {

namespace {
constexpr double kPi = 3.14159265358979323846;
} // namespace

FltkGeometryWidget::FltkGeometryWidget(int x, int y, int w, int h, const char* label)
    : Fl_Widget(x, y, w, h, label)
{
    box(FL_FLAT_BOX);
    color(FL_BACKGROUND2_COLOR);
}

void FltkGeometryWidget::showNode(const dxx::DxxNode* node)
{
    m_node = node;
    resetView();
    redraw();
}

void FltkGeometryWidget::resetView()
{
    m_viewInitialized = false;
    m_panning = false;
}

FltkGeometryWidget::Viewport FltkGeometryWidget::computeViewport(
    const std::vector<dxx::Curve>& curves)
{
    Bounds b;
    for (const auto& c : curves)
        for (const auto& seg : c.segments)
            b.add(seg.pt.x, seg.pt.y);

    if (b.maxX - b.minX < 1e-10 || b.maxY - b.minY < 1e-10) return {};

    double pad = std::max(10.0, std::max(b.maxX - b.minX, b.maxY - b.minY) * 0.1);
    b.minX -= pad; b.minY -= pad; b.maxX += pad; b.maxY += pad;

    int w = this->w(), h = this->h();
    double sx = (w - 20.0) / (b.maxX - b.minX);
    double sy = (h - 20.0) / (b.maxY - b.minY);
    double scale = std::min(sx, sy);
    return {scale, w / 2.0 - (b.maxX + b.minX) / 2.0 * scale,
            h / 2.0 - (b.maxY + b.minY) / 2.0 * scale, w, h};
}

void FltkGeometryWidget::draw()
{
    fl_draw_box(FL_FLAT_BOX, x(), y(), w(), h(), color());
    fl_line_style(FL_SOLID, 0);
    fl_push_clip(x(), y(), w(), h());

    if (!m_node) {
        drawPlaceholder();
        fl_pop_clip();
        return;
    }

    auto curves = dxx::extractProfile2D(*m_node);
    if (curves.empty()) {
        auto color = dxx::extractNodeColor(*m_node);
        if (color) {
            drawColorSwatchColor(color->r, color->g, color->b);
        } else {
            drawNoGeometryMessage();
        }
        fl_pop_clip();
        return;
    }

    if (!m_viewInitialized) {
        Viewport fit = computeViewport(curves);
        if (fit.scale > 0) {
            m_scale = fit.scale;
            m_fitScale = fit.scale;
            m_offsetX = fit.ox;
            m_offsetY = fit.oy;
            m_viewInitialized = true;
        }
    }

    if (!m_viewInitialized) {
        drawNoGeometryMessage();
        fl_pop_clip();
        return;
    }

    Viewport vp{m_scale, m_offsetX, m_offsetY, w(), h()};

    Bounds geomBounds;
    drawCurvesCairo(curves, vp, geomBounds);
    drawDimensions(vp, geomBounds);

    fl_pop_clip();
}

int FltkGeometryWidget::handle(int event)
{
    switch (event) {
    case FL_PUSH:
        if (Fl::event_button() == FL_LEFT_MOUSE && m_viewInitialized) {
            m_panning = true;
            m_lastX = Fl::event_x() - x();
            m_lastY = Fl::event_y() - y();
            return 1;
        }
        if (Fl::event_button() == FL_LEFT_MOUSE && Fl::event_clicks()) {
            // double-click: reset view
            resetView();
            redraw();
            return 1;
        }
        return 1;

    case FL_DRAG:
        if (m_panning) {
            int cx = Fl::event_x() - x();
            int cy = Fl::event_y() - y();
            m_offsetX += cx - m_lastX;
            m_offsetY -= cy - m_lastY;  // screen y down -> world y up
            m_lastX = cx;
            m_lastY = cy;
            redraw();
            return 1;
        }
        return 0;

    case FL_RELEASE:
        if (Fl::event_button() == FL_LEFT_MOUSE) m_panning = false;
        return 1;

    case FL_MOUSEWHEEL: {
        if (!m_viewInitialized) return 1;
        double factor = std::pow(1.0015, -Fl::event_dy() * 120.0);
        int mx = Fl::event_x() - x();
        int my = Fl::event_y() - y();

        double wx = (mx - m_offsetX) / m_scale;
        double wy = (h() - my - m_offsetY) / m_scale;

        double newScale = std::clamp(m_scale * factor, m_fitScale * 0.02, m_fitScale * 100.0);
        m_scale = newScale;
        m_offsetX = mx - wx * m_scale;
        m_offsetY = (h() - my) - wy * m_scale;

        redraw();
        return 1;
    }

    default:
        return Fl_Widget::handle(event);
    }
}

void FltkGeometryWidget::drawPlaceholder()
{
    fl_color(140, 140, 140);
    fl_font(FL_HELVETICA, 12);
    fl_draw("Select a node with geometry to preview", x(), y(), w(), h(), FL_ALIGN_CENTER);
}

void FltkGeometryWidget::drawNoGeometryMessage()
{
    fl_color(FL_DARK3);
    fl_font(FL_HELVETICA, 12);
    std::string msg = "No geometry in \"";
    msg += m_node && !m_node->name.empty() ? m_node->name : "(unnamed)";
    msg += "\"";
    fl_draw(msg.c_str(), x(), y(), w(), h(), FL_ALIGN_CENTER);
}

void FltkGeometryWidget::drawColorSwatchColor(int r, int g, int b)
{
    int w = this->w(), h = this->h();
    int sz = std::min(w, h) / 3;
    int sx = x() + (w - sz) / 2, sy = y() + (h - sz) / 2 - 20;

    fl_color(FL_WHITE);
    fl_line_style(FL_SOLID,2);
    fl_draw_box(FL_FLAT_BOX, sx, sy, sz, sz, fl_rgb_color(r, g, b));
    fl_rect(sx, sy, sz, sz, FL_WHITE);

    char buf[128];
    snprintf(buf, sizeof(buf), "R:%d  G:%d  B:%d  (#%02X%02X%02X)", r, g, b, r, g, b);
    fl_color(FL_BLACK);
    fl_font(FL_HELVETICA, 12);
    fl_draw(buf, x(), sy + sz + 10, w, 30, FL_ALIGN_CENTER);

    std::string nt = "Color: ";
    nt += m_node ? m_node->name : "";
    fl_draw(nt.c_str(), x(), sy - 30, w, 25, FL_ALIGN_CENTER | FL_ALIGN_BOTTOM);
}

void FltkGeometryWidget::drawCurvesCairo(const std::vector<dxx::Curve>& curves,
                                         const Viewport& vp, Bounds& bb)
{
    int w = this->w(), h = this->h();
    if (w <= 0 || h <= 0) return;

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return;
    }
    cairo_t* cr = cairo_create(surface);

    // Background = widget color.
    uchar br = 0, bg = 0, bbcol = 0;
    Fl::get_color(color(), br, bg, bbcol);
    cairo_set_source_rgb(cr, br / 255.0, bg / 255.0, bbcol / 255.0);
    cairo_paint(cr);

    // World -> widget-pixel transform (y flipped).
    auto sx = [&](double wx) { return wx * vp.scale + vp.ox; };
    auto sy = [&](double wy) { return vp.h - (wy * vp.scale + vp.oy); };

    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_width(cr, 1.0);

    for (size_t ci = 0; ci < curves.size(); ++ci) {
        const dxx::Curve& curve = curves[ci];
        if (curve.segments.size() < 2) continue;

        uint32_t rgb = kCurveColorPalette[ci % kCurveColorCount];
        cairo_set_source_rgb(cr, ((rgb >> 16) & 0xFF) / 255.0,
                                 ((rgb >> 8) & 0xFF) / 255.0,
                                 (rgb & 0xFF) / 255.0);

        cairo_new_path(cr);
        const dxx::CurveSegment& first = curve.segments[0];
        bb.add(first.pt.x, first.pt.y);
        cairo_move_to(cr, sx(first.pt.x), sy(first.pt.y));

        for (size_t i = 1; i < curve.segments.size(); ++i) {
            const dxx::CurveSegment& seg = curve.segments[i];
            const dxx::CurveSegment& prev = curve.segments[i - 1];
            bb.add(seg.pt.x, seg.pt.y);

            if (std::fabs(prev.bulge) < 1e-10) {
                cairo_line_to(cr, sx(seg.pt.x), sy(seg.pt.y));
                continue;
            }

            double dx = seg.pt.x - prev.pt.x, dy = seg.pt.y - prev.pt.y;
            double dist = std::sqrt(dx * dx + dy * dy);
            if (dist < 1e-10) continue;
            double b = prev.bulge;
            double mx = (prev.pt.x + seg.pt.x) / 2.0, my = (prev.pt.y + seg.pt.y) / 2.0;
            double nx = -dy / dist, ny = dx / dist;
            double co = dist * (1.0 - b * b) / (4.0 * b);
            double acx = mx + nx * co, acy = my + ny * co;
            double r = std::sqrt((prev.pt.x - acx) * (prev.pt.x - acx) +
                                 (prev.pt.y - acy) * (prev.pt.y - acy));
            double a1 = std::atan2(prev.pt.y - acy, prev.pt.x - acx);
            double sweep = 4.0 * std::atan(b);
            int steps = std::max(2, (int)std::ceil(std::fabs(sweep) / (2.0 * kPi) * 48.0));
            for (int s = 1; s <= steps; ++s) {
                double t = (double)s / steps;
                double a = a1 + sweep * t;
                cairo_line_to(cr, sx(acx + r * std::cos(a)), sy(acy + r * std::sin(a)));
            }
        }
        cairo_close_path(cr);
        cairo_stroke(cr);
    }

    // Blit the RGBA/BGRX cairo buffer to the widget as RGB.
    int stride = cairo_image_surface_get_stride(surface);
    const unsigned char* data = cairo_image_surface_get_data(surface);
    m_rgbBuffer.resize((size_t)w * h * 3);
    for (int y = 0; y < h; ++y) {
        const unsigned char* src = data + (size_t)y * stride;
        unsigned char* dst = m_rgbBuffer.data() + (size_t)y * w * 3;
        for (int x = 0; x < w; ++x) {
            dst[x * 3 + 0] = src[x * 4 + 2]; // R
            dst[x * 3 + 1] = src[x * 4 + 1]; // G
            dst[x * 3 + 2] = src[x * 4 + 0]; // B
        }
    }
    fl_draw_image(m_rgbBuffer.data(), x(), y(), w, h, 3, w * 3);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

void FltkGeometryWidget::drawDimensions(const Viewport& vp, const Bounds& bb)
{
    if (!bb.valid) return;

    double padding = (bb.maxY - bb.minY) * 0.08;
    double geoW = bb.maxX - bb.minX, geoH = bb.maxY - bb.minY;
    double dimY = bb.minY - padding, dimX = bb.maxX + padding;

    auto toScreen = [&](double wx, double wy) -> std::pair<int, int> {
        return {x() + static_cast<int>(std::lround(wx * vp.scale + vp.ox)),
                y() + static_cast<int>(std::lround(vp.h - (wy * vp.scale + vp.oy)))};
    };

    fl_color(120, 120, 120);
    fl_line_style(FL_SOLID,1);

    double gap = 6.0, arrowSz = 5.0;
    int lay = toScreen(0, dimY).second;
    if (lay > y() + vp.h - 10) lay = y() + vp.h - 10;
    int lx1 = toScreen(bb.minX, 0).first, lx2 = toScreen(bb.maxX, 0).first;
    int lyExt = toScreen(0, bb.minY).second + (int)gap;

    fl_line(lx1, lay, lx2, lay);
    fl_line(lx1, lyExt, lx1, lay);
    fl_line(lx2, lyExt, lx2, lay);

    fl_begin_polygon();
    fl_vertex(lx1, lay); fl_vertex(lx1 + arrowSz, lay - arrowSz); fl_vertex(lx1 + arrowSz, lay + arrowSz);
    fl_end_polygon();
    fl_begin_polygon();
    fl_vertex(lx2, lay); fl_vertex(lx2 - arrowSz, lay - arrowSz); fl_vertex(lx2 - arrowSz, lay + arrowSz);
    fl_end_polygon();

    char wb[64];
    snprintf(wb, sizeof(wb), "%.1f mm", geoW);
    int labelY = (lay > y() + vp.h - 30) ? lay - 20 : lay + 4;
    fl_font(FL_HELVETICA, 10);
    fl_draw(wb, lx1 + 4, labelY, lx2 - lx1 - 8, 16, FL_ALIGN_CENTER);

    int rax = toScreen(dimX, 0).first;
    if (rax > x() + vp.w - 10) rax = x() + vp.w - 10;
    int ray1 = toScreen(0, bb.minY).second, ray2 = toScreen(0, bb.maxY).second;
    int rxExt = toScreen(bb.maxX, 0).first + (int)gap;

    fl_line(rax, ray1, rax, ray2);
    fl_line(rxExt, ray1, rax, ray1);
    fl_line(rxExt, ray2, rax, ray2);

    fl_begin_polygon();
    fl_vertex(rax, ray1); fl_vertex(rax - arrowSz, ray1 - arrowSz); fl_vertex(rax + arrowSz, ray1 - arrowSz);
    fl_end_polygon();
    fl_begin_polygon();
    fl_vertex(rax, ray2); fl_vertex(rax - arrowSz, ray2 + arrowSz); fl_vertex(rax + arrowSz, ray2 + arrowSz);
    fl_end_polygon();

    snprintf(wb, sizeof(wb), "%.1f mm", geoH);
    int lx = (rax + 86 > x() + vp.w) ? rax - 86 : rax + 6;
    int topY = std::min(ray1, ray2);
    int hh = std::abs(ray2 - ray1);
    if (hh < 12) { topY = lay; hh = 12; }
    fl_draw(wb, lx, topY, 80, hh, FL_ALIGN_CENTER);
}

} // namespace dxxviewer
