#include "FltkMeshWidget.h"

#include <FL/Fl.H>
#include <FL/gl.h>
#include <FL/glu.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace dxxviewer {

FltkMeshWidget::FltkMeshWidget(int x, int y, int w, int h, const char* label)
    : Fl_Gl_Window(x, y, w, h, label)
{
    // FL_MULTISAMPLE requests a multisample-capable pixel format (hardware
    // MSAA) if the driver supports one; falls back to non-multisampled
    // silently otherwise. Combined with GL_LINE_SMOOTH in draw() for the
    // wireframe's own per-line coverage antialiasing.
    mode(FL_RGB | FL_DEPTH | FL_DOUBLE | FL_MULTISAMPLE);
}

void FltkMeshWidget::showMeshes(const dxx::MeshBody* walls, const dxx::MeshBody* beams)
{
    m_wallMesh = walls;
    m_beamMesh = beams;
    resetView();
    redraw();
}

void FltkMeshWidget::resetView()
{
    m_yaw = 35.0;
    m_pitch = 25.0;
    m_zoom = 1.0;
    fitView();
}

void FltkMeshWidget::fitView()
{
    m_centerX = m_centerY = m_centerZ = 0.0;
    m_radius = 1.0;

    double minX = 1e300, minY = 1e300, minZ = 1e300;
    double maxX = -1e300, maxY = -1e300, maxZ = -1e300;
    bool any = false;
    auto accumulate = [&](const dxx::MeshBody* mesh) {
        if (!mesh) return;
        for (const auto& v : mesh->vertices) {
            minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
            minY = std::min(minY, v.y); maxY = std::max(maxY, v.y);
            minZ = std::min(minZ, v.z); maxZ = std::max(maxZ, v.z);
            any = true;
        }
    };
    accumulate(m_wallMesh);
    accumulate(m_beamMesh);
    if (!any) return;

    m_centerX = (minX + maxX) / 2.0;
    m_centerY = (minY + maxY) / 2.0;
    m_centerZ = (minZ + maxZ) / 2.0;
    m_radius = std::max({maxX - minX, maxY - minY, maxZ - minZ, 1.0}) / 2.0;
}

void FltkMeshWidget::draw()
{
    if (!valid()) {
        glViewport(0, 0, w(), h());
        glEnable(GL_DEPTH_TEST);

        // GL_MULTISAMPLE (0x809D, core since GL 1.3) isn't in the OpenGL 1.1
        // header Windows ships - defining the enum ourselves needs no extension
        // loader since the pixel format (FL_MULTISAMPLE above) already created
        // the multisample buffer; this just turns sampling on for it.
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif
        glEnable(GL_MULTISAMPLE);
        glEnable(GL_LINE_SMOOTH);
        glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        valid(1);
    }

    glClearColor(0.96f, 0.96f, 0.96f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    bool hasWalls = m_wallMesh && !m_wallMesh->vertices.empty() && !m_wallMesh->faces.empty();
    bool hasBeams = m_beamMesh && !m_beamMesh->vertices.empty() && !m_beamMesh->faces.empty();
    if (!hasWalls && !hasBeams) {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, w(), 0, h(), -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        const char* msg = "No mesh geometry";
        gl_color(FL_DARK3);
        gl_font(FL_HELVETICA, 12);
        gl_draw(msg, (w() - static_cast<int>(gl_width(msg))) / 2, h() / 2);
        return;
    }

    setupProjection();

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    if (m_perspective)
        glTranslated(0, 0, -(m_radius * 3.0 / m_zoom));
    glRotated(m_pitch, 1, 0, 0);
    glRotated(m_yaw, 0, 1, 0);
    glTranslated(-m_centerX, -m_centerY, -m_centerZ);

    // Beams (opaque) drawn first so their color/depth are already in the
    // buffers when the walls' semi-transparent pass reads/blends against
    // them - walls skip the depth WRITE (not the depth test) so they never
    // occlude a beam drawn after them, while still being correctly hidden
    // behind whatever's already opaque in front of them.
    if (m_shaded) {
        if (hasBeams) drawShadedFaces(*m_beamMesh, 0.80f, 0.58f, 0.32f, 1.0f);
        if (hasWalls) {
            // Alpha kept quite low (not just "a bit see-through"): a wall's
            // own near AND far face both get drawn (no face culling - face
            // winding isn't guaranteed consistent, see drawShadedFaces'
            // two-sided lighting), so looking through a wall box stacks TWO
            // translucent layers, compounding to roughly 1-(1-a)^2 effective
            // opacity - 0.35 alone looked barely more transparent than solid
            // once doubled.
            glDepthMask(GL_FALSE);
            drawShadedFaces(*m_wallMesh, 0.58f, 0.70f, 0.83f, 0.15f);
            glDepthMask(GL_TRUE);
        }
    }
    if (hasBeams) drawWireframeEdges(*m_beamMesh);
    if (hasWalls) drawWireframeEdges(*m_wallMesh);
    drawHint();
}

void FltkMeshWidget::setupProjection()
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    double aspect = h() > 0 ? static_cast<double>(w()) / h() : 1.0;

    if (m_perspective) {
        // Camera sits at distance camDist from the object's center (pushed
        // back by the modelview translate in draw()); near/far bound a
        // generous margin around it since m_radius is only an approximate
        // bounding-box radius.
        double camDist = m_radius * 3.0 / m_zoom;
        double nearP = std::max(camDist - m_radius * 5.0, camDist * 0.01);
        double farP = camDist + m_radius * 10.0;
        gluPerspective(40.0, aspect, nearP, farP);
    } else {
        double viewSize = m_radius * 2.2 / m_zoom;
        if (aspect >= 1.0)
            glOrtho(-viewSize * aspect, viewSize * aspect, -viewSize, viewSize, -m_radius * 10, m_radius * 10);
        else
            glOrtho(-viewSize, viewSize, -viewSize / aspect, viewSize / aspect, -m_radius * 10, m_radius * 10);
    }
}

void FltkMeshWidget::drawShadedFaces(const dxx::MeshBody& mesh, float r, float g, float b, float alpha)
{
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    GLfloat lightDir[] = {0.4f, 0.6f, 1.0f, 0.0f}; // directional (w=0)
    glLightfv(GL_LIGHT0, GL_POSITION, lightDir);
    GLfloat ambient[] = {0.45f, 0.45f, 0.45f, 1.0f};
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 1); // faces can wind either way

    // Pushes the fill back slightly in depth so the wireframe pass (drawn
    // without offset, right after) never z-fights with the coplanar fill.
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);

    glColor4f(r, g, b, alpha);
    for (const auto& face : mesh.faces) {
        if (face.size() < 3) continue;

        // Flat per-face normal via Newell's method - robust to a slightly
        // non-planar or concave real-world face, unlike a plain 3-point
        // cross product.
        double nx = 0, ny = 0, nz = 0;
        size_t n = face.size();
        auto pointOf = [&](size_t i) -> const dxx::Point3D* {
            int idx = face[i];
            if (idx < 0 || static_cast<size_t>(idx) >= mesh.vertices.size()) return nullptr;
            return &mesh.vertices[static_cast<size_t>(idx)];
        };
        for (size_t i = 0; i < n; ++i) {
            const dxx::Point3D* a = pointOf(i);
            const dxx::Point3D* b = pointOf((i + 1) % n);
            if (!a || !b) continue;
            nx += (a->y - b->y) * (a->z + b->z);
            ny += (a->z - b->z) * (a->x + b->x);
            nz += (a->x - b->x) * (a->y + b->y);
        }
        double len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-12) { nx /= len; ny /= len; nz /= len; }

        glNormal3d(nx, ny, nz);
        glBegin(GL_POLYGON);
        for (size_t i = 0; i < n; ++i) {
            const dxx::Point3D* p = pointOf(i);
            if (p) glVertex3d(p->x, p->y, p->z);
        }
        glEnd();
    }

    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_LIGHTING);
    glDisable(GL_LIGHT0);
    glDisable(GL_COLOR_MATERIAL);
}

void FltkMeshWidget::drawWireframeEdges(const dxx::MeshBody& mesh)
{
    glColor3f(0.10f, 0.35f, 0.65f);
    for (const auto& face : mesh.faces) {
        glBegin(GL_LINE_LOOP);
        for (int idx : face) {
            if (idx < 0 || static_cast<size_t>(idx) >= mesh.vertices.size()) continue;
            const auto& p = mesh.vertices[static_cast<size_t>(idx)];
            glVertex3d(p.x, p.y, p.z);
        }
        glEnd();
    }
}

void FltkMeshWidget::drawHint()
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w(), 0, h(), -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);

    char hint[96];
    std::snprintf(hint, sizeof(hint), "P: %s view   S: shading %s",
                  m_perspective ? "perspective" : "orthographic",
                  m_shaded ? "on" : "off");
    gl_color(FL_DARK3);
    gl_font(FL_HELVETICA, 10);
    gl_draw(hint, 6, 6);

    glEnable(GL_DEPTH_TEST);
}

int FltkMeshWidget::handle(int event)
{
    switch (event) {
    case FL_FOCUS:
    case FL_UNFOCUS:
        return 1; // accept keyboard focus so P/S below can be reached

    case FL_PUSH:
        Fl::focus(this);
        if (Fl::event_button() == FL_LEFT_MOUSE && Fl::event_clicks()) {
            resetView();
            redraw();
            return 1;
        }
        if (Fl::event_button() == FL_LEFT_MOUSE) {
            m_dragging = true;
            m_lastX = Fl::event_x();
            m_lastY = Fl::event_y();
            return 1;
        }
        return 1;

    case FL_DRAG:
        if (m_dragging) {
            int cx = Fl::event_x(), cy = Fl::event_y();
            m_yaw += (cx - m_lastX) * 0.5;
            m_pitch += (cy - m_lastY) * 0.5;
            m_lastX = cx;
            m_lastY = cy;
            redraw();
            return 1;
        }
        return 0;

    case FL_RELEASE:
        m_dragging = false;
        return 1;

    case FL_MOUSEWHEEL:
        m_zoom = std::clamp(m_zoom * std::pow(1.0015, -Fl::event_dy() * 120.0), 0.05, 50.0);
        redraw();
        return 1;

    case FL_KEYDOWN: {
        int key = Fl::event_key();
        if (key == 'p' || key == 'P') { m_perspective = !m_perspective; redraw(); return 1; }
        if (key == 's' || key == 'S') { m_shaded = !m_shaded; redraw(); return 1; }
        return 0;
    }

    default:
        return Fl_Gl_Window::handle(event);
    }
}

} // namespace dxxviewer
