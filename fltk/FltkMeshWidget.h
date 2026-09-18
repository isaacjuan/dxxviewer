#pragma once
#include <FL/Fl_Gl_Window.H>
#include "../dxx_parser.h"

namespace dxxviewer {

// Interactive 3D preview for up to two MeshBody objects (SimpleBody
// vertexList + faceList geometry / GenBeam boxes - see dxx::extractMeshBody /
// dxx::extractGenBeamBox), rendered with legacy/fixed-function OpenGL via
// FLTK's Fl_Gl_Window. Deliberately not a general 3D renderer: a flat-shaded
// (one directional light, no textures) fill under the wireframe edges, no
// hidden-line removal beyond the depth buffer - the goal is just to make an
// otherwise entirely-unrendered solid body visible and orientable, the 3D
// counterpart of FltkGeometryWidget's 2D profile view (same interaction
// vocabulary: left-drag to rotate here vs. pan there, wheel to zoom,
// double-click to reset). 'P'/'S' keys (click the view first to give it
// keyboard focus) toggle perspective-vs-orthographic and shaded-vs-
// wireframe-only; current state is hinted in the bottom-left.
//
// `walls` renders semi-transparent (depth writes off) and `beams` opaque, so
// selecting a container with both (e.g. the document root) lets GenBeam
// boxes show through the wall solids around them instead of being hidden
// inside them - the whole point of previewing everything under a selection
// at once (see FltkMainWindow::onNodeSelected).
class FltkMeshWidget : public Fl_Gl_Window {
public:
    FltkMeshWidget(int x, int y, int w, int h, const char* label = nullptr);

    // Sets the meshes to preview (either or both may be null to clear).
    // Caller owns `walls`/`beams` and must keep them alive at least until the
    // next showMeshes() call.
    void showMeshes(const dxx::MeshBody* walls, const dxx::MeshBody* beams);

protected:
    void draw() FL_OVERRIDE;
    int handle(int event) FL_OVERRIDE;

private:
    void resetView();
    void fitView();
    void setupProjection();
    void drawShadedFaces(const dxx::MeshBody& mesh, float r, float g, float b, float alpha);
    void drawWireframeEdges(const dxx::MeshBody& mesh);
    void drawHint();

    const dxx::MeshBody* m_wallMesh = nullptr;
    const dxx::MeshBody* m_beamMesh = nullptr;
    double m_centerX = 0, m_centerY = 0, m_centerZ = 0;
    double m_radius = 1.0;
    double m_yaw = 35.0;
    double m_pitch = 25.0;
    double m_zoom = 1.0;
    bool m_dragging = false;
    bool m_perspective = true;
    bool m_shaded = true;
    int m_lastX = 0, m_lastY = 0;
};

} // namespace dxxviewer
