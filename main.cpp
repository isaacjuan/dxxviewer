#include "dxx_parser.h"
#include "colors.h"
#include "settings.h"

#include <fstream>
#include <iomanip>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>

using namespace dxx;

namespace {

struct BBox {
    double minX = 1e100, minY = 1e100;
    double maxX = -1e100, maxY = -1e100;

    void add(double x, double y) {
        minX = std::min( minX, x );
        minY = std::min( minY, y );
        maxX = std::max( maxX, x );
        maxY = std::max( maxY, y );
    }

    double width() const { return maxX - minX; }
    double height() const { return maxY - minY; }
    bool valid() const { return minX <= maxX && minY <= maxY; }
};

Point2D worldTransform(const Point3D& pt, const Vec3D& origin,
                       const Vec3D& vx, const Vec3D& vy) {
    return {
        origin.x + pt.x * vx.x + pt.y * vy.x,
        origin.y + pt.x * vx.y + pt.y * vy.y
    };
}

std::string htmlEscape(const std::string& s) {
    std::string r;
    r.reserve(s.size() * 6);  // Worst case: '&' becomes '&amp;' (5 chars)
    for (char c : s) {
        switch (c) {
            case '<': r += "&lt;"; break;
            case '>': r += "&gt;"; break;
            case '&': r += "&amp;"; break;
            case '"': r += "&quot;"; break;
            default: r += c;
        }
    }
    return r;
}

std::vector<std::string> curveColors = [] {
    std::vector<std::string> v;
    const auto& pal = dxxviewer::settings().curvePalette;
    v.reserve(pal.size());
    for (uint32_t rgb : pal) {
        char buf[8];
        snprintf(buf, sizeof(buf), "#%06x", rgb);
        v.push_back(buf);
    }
    if (v.empty()) { // settings() always fills defaults, but stay safe
        v.reserve(dxxviewer::kCurveColorCount);
        for (uint32_t rgb : dxxviewer::kCurveColorPalette) {
            char buf[8];
            snprintf(buf, sizeof(buf), "#%06x", rgb);
            v.push_back(buf);
        }
    }
    return v;
}();

void arcToSvgPath(std::ostream& out, const Point2D& p1, const Point2D& p2,
                  double bulge, bool first) {
    if (first) {
        out << "M " << p1.x << " " << p1.y << " ";
    }

    if (std::abs(bulge) < 1e-10) {
        out << "L " << p2.x << " " << p2.y << " ";
        return;
    }

    double dx = p2.x - p1.x;
    double dy = p2.y - p1.y;
    double distSq = dx * dx + dy * dy;
    if (distSq < 1e-20) return;  // 1e-10 squared

    double dist = std::sqrt(distSq);
    double invDist = 1.0 / dist;
    double sagitta = bulge * dist * 0.5;

    double midX = (p1.x + p2.x) * 0.5;
    double midY = (p1.y + p2.y) * 0.5;
    double centerOffset = sagitta * (2.0 - std::abs(bulge));

    double cx = midX - dy * invDist * centerOffset;
    double cy = midY + dx * invDist * centerOffset;

    double radSq = (p1.x - cx) * (p1.x - cx) + (p1.y - cy) * (p1.y - cy);

    out << "A " << std::sqrt(radSq) << " " << std::sqrt(radSq) << " 0 0 "
        << (bulge > 0 ? 0 : 1) << " " << p2.x << " " << p2.y << " ";
}

BBox ComputeSvgBBox(const DxxDocument& doc) {
    BBox bbox;
    for (const auto& curve : doc.curves) {
        for (const auto& seg : curve.segments) {
            Point2D p = worldTransform(seg.pt, curve.origin, curve.vecX, curve.vecY);
            bbox.add(p.x, p.y);
        }
    }
    if (!bbox.valid()) {
        std::cerr << "Warning: no valid geometry bounding box found.\n";
        bbox = {-100, -100, 100, 100};
    }
    double pad = std::max(20.0, std::max(bbox.width(), bbox.height()) * 0.05);
    bbox.minX -= pad; bbox.minY -= pad;
    bbox.maxX += pad; bbox.maxY += pad;
    return bbox;
}

std::string_view getColorForCurve(const std::string& entryName, int curveIdx,
                                    std::unordered_map<std::string, int>& nameColorMap,
                                    int& nextColor, const size_t colorCount) {
    if (!entryName.empty()) {
        auto [it, inserted] = nameColorMap.try_emplace(entryName, nextColor);
        if (inserted) ++nextColor;
        return curveColors[it->second % colorCount];
    }
    return curveColors[curveIdx % colorCount];
}

void writeSvgHeader(std::ostream& out, const BBox& bbox) {
    out << std::fixed << std::setprecision(3)
        << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        << "width=\"" << bbox.width() << "\" height=\"" << bbox.height() << "\" "
        << "viewBox=\"" << bbox.minX << " " << bbox.minY << " "
        << bbox.width() << " " << bbox.height() << "\">\n"
        << "<rect width=\"100%\" height=\"100%\" fill=\"#1a1a2e\"/>\n"
        << "<g transform=\"scale(1, -1)\">\n";
}

void WriteSvgLegend(std::ostream& f, const BBox& bbox,
                    const std::unordered_map<std::string, int>& nameColorMap) {
    double legendX = bbox.minX + 10;
    double legendY = bbox.maxY - 10;
    f << "<g transform=\"scale(1, -1)\">\n";
    int li = 0;
    const size_t colorCount = curveColors.size();
    for (const auto& [name, idx] : nameColorMap) {
        const std::string& color = curveColors[idx % colorCount];
        double ly = legendY - 20 - li * 14;
        f << "<rect x=\"" << legendX << "\" y=\"" << ly << "\" "
          << "width=\"10\" height=\"10\" fill=\"" << color << "\"/>\n";
        f << "<text x=\"" << (legendX + 14) << "\" y=\"" << (ly + 9) << "\" "
          << "fill=\"#ccc\" font-size=\"10\" font-family=\"monospace\">"
          << htmlEscape(name) << "</text>\n";
        ++li;
    }
    f << "</g>\n";
}

void renderSvg(const DxxDocument& doc, const std::string& outputPath) {
    BBox bbox = ComputeSvgBBox(doc);
    std::ofstream f(outputPath);
    if (!f.is_open()) {
        std::cerr << "Error: cannot write to " << outputPath << "\n";
        return;
    }

    writeSvgHeader(f, bbox);

    std::unordered_map<std::string, int> nameColorMap;
    int nextColor = 0;
    const size_t colorCount = curveColors.size();

    for (int curveIdx = 0; const auto& curve : doc.curves) {
        if (curve.segments.size() < 2) { ++curveIdx; continue; }

        std::string_view color = getColorForCurve(curve.entryName, curveIdx, nameColorMap, nextColor, colorCount);

        f << "<path d=\"";
        for (size_t i = 0; i < curve.segments.size(); ++i) {
            Point2D p = worldTransform(curve.segments[i].pt, curve.origin, curve.vecX, curve.vecY);
            if (i == 0) {
                f << "M " << p.x << " " << p.y << " ";
            } else {
                Point2D prevP = worldTransform(curve.segments[i - 1].pt, curve.origin, curve.vecX, curve.vecY);
                arcToSvgPath(f, prevP, p, curve.segments[i - 1].bulge, false);
            }
        }
        f << "Z\" fill=\"none\" stroke=\"" << color << "\" stroke-width=\"1\" opacity=\"0.85\"/>\n";
        ++curveIdx;
    }
    f << "</g>\n";

    WriteSvgLegend(f, bbox, nameColorMap);
    f << "</svg>\n";
    f.close();
    std::cout << "SVG written to: " << outputPath << "\n"
              << "  Curves: " << doc.curves.size() << "\n"
              << "  Named entities: " << nameColorMap.size() << "\n";
}

void printSummary(const DxxDocument& doc) {
    std::cout << "\nTotal curves extracted: " << doc.curves.size() << "\n";
    int named = 0;
    const size_t maxDisplay = std::min(size_t(5), doc.curves.size());
    for (size_t i = 0; i < doc.curves.size(); ++i) {
        const auto& c = doc.curves[i];
        if (!c.entryName.empty()) ++named;
        if (i < maxDisplay) {
            std::cout << "  [" << i << "] \"" << c.entryName << "\" "
                      << c.segments.size() << " segs\n";
        }
    }
    std::cout << "  Named entities: " << named << "\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    // Force settings.lua load once, before any palette use (static init
    // of curveColors also touches it, but this makes the load order explicit
    // and prints the path used).
    const auto& cfg = dxxviewer::settings();
    (void)cfg;

    std::string inputPath = R"(C:\Users\jissi\AppData\Roaming\hsbCAD\StandaloneFramingStyles.dxx)";
    std::string outputPath = "preview.svg";

    if (argc >= 2) {
        inputPath = argv[1];
    }
    if (argc >= 3) {
        outputPath = argv[2];
    }

    std::cout << "DXX Viewer\n";
    std::cout << "Input:  " << inputPath << "\n";
    std::cout << "Output: " << outputPath << "\n\n";

    std::cout << "Parsing...\n" << std::flush;
    auto doc = parseFile(inputPath);
    std::cout << "Parsing done.\n" << std::flush;

    if (!doc) {
        std::cerr << "Error: could not parse file: " << inputPath << "\n";
        return 1;
    }

    printSummary(*doc);
    std::cout << "\nGenerating SVG preview...\n";
    renderSvg(*doc, outputPath);

    return 0;
}
