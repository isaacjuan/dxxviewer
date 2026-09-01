#pragma once
#include <FL/Fl_Gl_Window.H>
#include "../dxx_parser.h"

namespace dxxviewer {

// Interactive 3D wireframe preview for a MeshBody (SimpleBody vertexList +
// faceList geometry - see dxx::extractMeshBody), rendered with legacy/
// fixed-function OpenGL via FLTK's Fl_Gl_Window. Deliberately not a general
// 3D renderer: wireframe only, no shading/lighting/hidden-line removal -
// the goal is just to make an otherwise entirely-unrendered solid body
// visible and orientable, the 3D counterpart of FltkGeometryWidget's 2D
// profile view (same interaction vocabulary: left-drag to rotate here vs.
// pan there, wheel to zoom, double-click to reset).
class FltkMeshWidget : public Fl_Gl_Window {
public:
    FltkMeshWidget(int x, int y, int w, int h, const char* label = nullptr);

    // Sets the mesh to preview (null clears it). Caller owns `mesh` and
    // must keep it alive at least until the next showMesh() call.
    void showMesh(const dxx::MeshBody* mesh);

protected:
    void draw() FL_OVERRIDE;
    int handle(int event) FL_OVERRIDE;

private:
    void resetView();
    void fitView();

    const dxx::MeshBody* m_mesh = nullptr;
    double m_centerX = 0, m_centerY = 0, m_centerZ = 0;
    double m_radius = 1.0;
    double m_yaw = 35.0;
    double m_pitch = 25.0;
    double m_zoom = 1.0;
    bool m_dragging = false;
    int m_lastX = 0, m_lastY = 0;
};

} // namespace dxxviewer
