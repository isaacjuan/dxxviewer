#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <unordered_map>
#include <optional>
#include <functional>

namespace dxx {

struct DxxNode {
    std::string name;
    std::vector<std::pair<std::string, std::string>> properties;
    std::vector<DxxNode> children;
    std::string zipData;
    int lineStart = 0;
    int lineEnd = 0;

    [[nodiscard]] std::string getString(std::string_view key, const std::string& def = "") const {
        return getImpl<std::string>(key, def, [](const std::string& v) { return v; });
    }
    [[nodiscard]] double getDouble(std::string_view key, double def = 0.0) const {
        return getImpl<double>(key, def, [](const std::string& v) {
            char* end = nullptr;
            double d = std::strtod(v.c_str(), &end);
            if (end && *end == '\0' && end != v.c_str()) return d;
            try { return static_cast<double>(std::stoi(v)); }
            catch (...) { return 0.0; }
        });
    }
    [[nodiscard]] int getInt(std::string_view key, int def = 0) const {
        return getImpl<int>(key, def, [](const std::string& v) {
            try { return std::stoi(v); }
            catch (...) { return 0; }
        });
    }

private:
    template<typename T, typename Conv>
    [[nodiscard]] T getImpl(std::string_view key, T def, Conv&& conv) const {
        for (const auto& [k, v] : properties)
            if (k == key) return conv(v);
        return def;
    }
};

// Caps recursion depth so a maliciously or accidentally deeply nested
// document can't exhaust the call stack. Real DXX documents never nest
// anywhere close to this deep.
constexpr int kMaxNodeDepth = 1000;

template<typename F>
void walkNode(const DxxNode& node, F&& visitor, int depth = 0) {
    if (depth > kMaxNodeDepth) return;
    visitor(node);
    for (const auto& child : node.children)
        walkNode(child, std::forward<F>(visitor), depth + 1);
}

template<typename F>
auto findInNode(const DxxNode& node, F&& visitor, int depth = 0)
    -> decltype(visitor(node)) {
    if (depth > kMaxNodeDepth) return {};
    if (auto r = visitor(node)) return r;
    for (const auto& child : node.children)
        if (auto r = findInNode(child, std::forward<F>(visitor), depth + 1))
            return r;
    return {};
}

struct Point2D {
    double x = 0;
    double y = 0;
};

struct Point3D {
    double x = 0;
    double y = 0;
    double z = 0;
};

struct Vec3D {
    double x = 0;
    double y = 0;
    double z = 0;
};

struct CurveSegment {
    Point3D pt;
    double bulge = 0;
};

struct Curve {
    Vec3D normal;
    Vec3D origin;
    Vec3D vecX;
    Vec3D vecY;
    std::vector<CurveSegment> segments;
    std::string entryName;
    int colorIndex = 0;
};

struct DxxDocument {
    DxxNode root;
    std::vector<Curve> curves;
};

std::optional<DxxDocument> parseFile(const std::string& filepath);

// Collects every "CURVE" node found anywhere under `node` (at any depth)
// into local-coordinate Curve segments, reading 11PTX/11PTY/41BULGE.
// Used by the interactive geometry preview in both GUIs — shared here so
// the two don't drift out of sync with each other.
[[nodiscard]] std::vector<Curve> extractCurves(const DxxNode& node);

// Like extractCurves, but preserves each curve's 3D placement
// (origin/vecX/vecY/normal, read from the nearest ancestor's
// 13PTORG*/13VECX*/13VECY*/13NORMAL* properties) instead of flattening
// everything onto one local plane. Used by the 3D wireframe preview.
[[nodiscard]] std::vector<Curve> extractCurves3D(const DxxNode& node);

// Projects every CURVE found under `node` onto its defining plane (normal
// read from 13VECN*/13NORMAL*) so an extrusion profile can be drawn as a
// true, undistorted 2D cross-section. Segments come back in projected 2D
// (x,y) with bulges preserved — projection onto the profile plane is a
// rotation, so arcs keep their exact shape.
[[nodiscard]] std::vector<Curve> extractProfile2D(const DxxNode& node);

// An RGB color as a neutral triple, so consumers can convert to whatever
// color type they need (Fl_Color / cairo RGB) without sharing logic.
struct NodeColor {
    int r = 0;
    int g = 0;
    int b = 0;
};

// Finds the first descendant of `node` that carries 70R/70G/70B properties
// and returns its RGB color, for the geometry-preview color swatch.
[[nodiscard]] std::optional<NodeColor> extractNodeColor(const DxxNode& node);

// Tessellates a curve's bulge arcs into straight segments and places each
// point in world space via origin + x*vecX + y*vecY + z*normal, returning a
// single closed polyline. For renderers (the 3D view) that need flat 3D
// points rather than local 2D coordinates plus arc primitives.
[[nodiscard]] std::vector<Point3D> tessellateCurveWorld(const Curve& curve, int stepsPerCircle = 48);

} // namespace dxx
