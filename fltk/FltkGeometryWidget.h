#pragma once
#include <FL/Fl_Widget.H>
#include "../dxx_parser.h"
#include <optional>
#include <vector>

namespace dxxviewer {

// Interactive 2D geometry preview: fits the curves of the selected node,
// draws bulge arcs + dimensions (anti-aliased via Cairo), and supports mouse
// drag to pan, wheel to zoom, double-click to reset view.
class FltkGeometryWidget : public Fl_Widget {
public:
    FltkGeometryWidget(int x, int y, int w, int h, const char* label = nullptr);

    // Sets the node whose geometry is previewed (null clears it).
    void showNode(const dxx::DxxNode* node);

protected:
    void draw() FL_OVERRIDE;
    int handle(int event) FL_OVERRIDE;

private:
    struct Viewport {
        double scale = 0;
        double ox = 0;
        double oy = 0;
        int w = 0;
        int h = 0;
    };

    struct Bounds {
        double minX = 0, minY = 0, maxX = 0, maxY = 0;
        bool valid = false;
        void add(double x, double y) {
            if (!valid) { minX = maxX = x; minY = maxY = y; valid = true; }
            else { minX = std::min(minX, x); maxX = std::max(maxX, x);
                   minY = std::min(minY, y); maxY = std::max(maxY, y); }
        }
    };

    void resetView();
    void drawPlaceholder();
    void drawNoGeometryMessage();
    void drawColorSwatchColor(int r, int g, int b);
    void drawCurvesCairo(const std::vector<dxx::Curve>& curves, const Viewport& vp, Bounds& bb);
    void drawDimensions(const Viewport& vp, const Bounds& bb);
    Viewport computeViewport(const std::vector<dxx::Curve>& curves);

    const dxx::DxxNode* m_node = nullptr;
    bool m_viewInitialized = false;
    double m_scale = 0;
    double m_fitScale = 0;
    double m_offsetX = 0;
    double m_offsetY = 0;
    bool m_panning = false;
    int m_lastX = 0, m_lastY = 0;
    std::vector<unsigned char> m_rgbBuffer;
};

} // namespace dxxviewer