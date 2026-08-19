#include "dxx_parser.h"
#include "colors.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <unordered_map>

using namespace dxx;

namespace {

struct BBox {
    double minX = 1e100, minY = 1e100;
    double maxX = -1e100, maxY = -1e100;

    void add(double x, double y) {
        if (x < minX) minX = x;
        if (y < minY) minY = y;
        if (x > maxX) maxX = x;
        if (y > maxY) maxY = y;
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
    v.reserve(dxxviewer::kCurveColorCount);
    for (uint32_t rgb : dxxviewer::kCurveColorPalette) {
        char buf[8];
        snprintf(buf, sizeof(buf), "#%06x", rgb);
        v.push_back(buf);
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
    double dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 1e-10) return;
    double sagitta = bulge * dist / 2.0;

    double midX = (p1.x + p2.x) / 2.0;
    double midY = (p1.y + p2.y) / 2.0;
    double normX = -dy / dist;
    double normY = dx / dist;

    double centerOffset = sagitta - dist * bulge / 2.0;
    double cx = midX + normX * centerOffset;
    double cy = midY + normY * centerOffset;
    double r = std::sqrt((p1.x - cx) * (p1.x - cx) + (p1.y - cy) * (p1.y - cy));

    int sweep = bulge > 0 ? 0 : 1;
    out << "A " << r << " " << r << " 0 0 " << sweep << " "
        << p2.x << " " << p2.y << " ";
}

BBox ComputeSvgBBox(const DxxDocument& doc) {
    BBox bbox;
    for (const auto& curve : doc.curves)
        for (const auto& seg : curve.segments)
            bbox.add(worldTransform(seg.pt, curve.origin, curve.vecX, curve.vecY).x,
                     worldTransform(seg.pt, curve.origin, curve.vecX, curve.vecY).y);
    if (!bbox.valid()) {
        std::cerr << "Warning: no valid geometry bounding box found.\n";
        bbox = {-100, -100, 100, 100};
    }
    double pad = std::max(20.0, std::max(bbox.width(), bbox.height()) * 0.05);
    bbox.minX -= pad; bbox.minY -= pad;
    bbox.maxX += pad; bbox.maxY += pad;
    return bbox;
}

void WriteSvgLegend(std::ostream& f, const BBox& bbox,
                    const std::unordered_map<std::string, int>& nameColorMap) {
    double legendX = bbox.minX + 10;
    double legendY = bbox.maxY - 10;
    f << "<g transform=\"scale(1, -1)\">\n";
    int li = 0;
    for (const auto& [name, idx] : nameColorMap) {
        std::string color = curveColors[idx % curveColors.size()];
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

    f << std::fixed << std::setprecision(3);
    f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    f << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
      << "width=\"" << bbox.width() << "\" height=\"" << bbox.height() << "\" "
      << "viewBox=\"" << bbox.minX << " " << bbox.minY << " "
      << bbox.width() << " " << bbox.height() << "\">\n";
    f << "<rect width=\"100%\" height=\"100%\" fill=\"#1a1a2e\"/>\n";
    f << "<g transform=\"scale(1, -1)\">\n";

    std::unordered_map<std::string, int> nameColorMap;
    int nextColor = 0;
    int curveIdx = 0;
    for (const auto& curve : doc.curves) {
        if (curve.segments.size() < 2) { ++curveIdx; continue; }

        std::string color;
        if (!curve.entryName.empty()) {
            auto it = nameColorMap.find(curve.entryName);
            if (it == nameColorMap.end()) {
                nameColorMap[curve.entryName] = nextColor;
                color = curveColors[nextColor % curveColors.size()];
                ++nextColor;
            } else {
                color = curveColors[it->second % curveColors.size()];
            }
        } else {
            color = curveColors[curveIdx % curveColors.size()];
        }

        f << "<path d=\"";
        for (size_t i = 0; i < curve.segments.size(); ++i) {
            const auto& seg = curve.segments[i];
            Point2D p = worldTransform(seg.pt, curve.origin, curve.vecX, curve.vecY);
            if (i == 0) {
                f << "M " << p.x << " " << p.y << " ";
            } else {
                const auto& prevSeg = curve.segments[i - 1];
                Point2D prevP = worldTransform(prevSeg.pt, curve.origin, curve.vecX, curve.vecY);
                arcToSvgPath(f, prevP, p, prevSeg.bulge, false);
            }
        }
        f << "Z\" fill=\"none\" stroke=\"" << color
          << "\" stroke-width=\"1\" opacity=\"0.85\"/>\n";
        ++curveIdx;
    }
    f << "</g>\n";

    WriteSvgLegend(f, bbox, nameColorMap);
    f << "</svg>\n";
    f.close();
    std::cout << "SVG written to: " << outputPath << "\n";
    std::cout << "  Curves: " << doc.curves.size() << "\n";
    std::cout << "  Named entities: " << nameColorMap.size() << "\n";
}

void printSummary(const DxxDocument& doc) {
    std::cout << "\nTotal curves extracted: " << doc.curves.size() << "\n";
    int named = 0;
    for (size_t i = 0; i < doc.curves.size(); ++i) {
        const auto& c = doc.curves[i];
        if (!c.entryName.empty()) ++named;
        if (i < 5) {
            std::cout << "  [" << i << "] \"" << c.entryName << "\" "
                      << c.segments.size() << " segs\n";
        }
    }
    std::cout << "  Named entities: " << named << "\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
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
