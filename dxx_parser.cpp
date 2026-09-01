#include "dxx_parser.h"

#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <cmath>

namespace dxx {

std::string GzipDecompress(const std::string& compressed);

namespace {

std::string base64Decode(const std::string& in) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (c == '=') break;
        const char* p = strchr(table, c);
        if (!p) continue;
        val = (val << 6) + static_cast<int>(p - table);
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string trim(std::string s) {
    size_t start = 0;
    while (start < s.size() && static_cast<unsigned char>(s[start]) <= ' ') ++start;
    size_t end = s.size();
    while (end > start && static_cast<unsigned char>(s[end - 1]) <= ' ') --end;
    return s.substr(start, end - start);
}

struct CodeAndTag {
    std::string code;
    std::string tag;
};

CodeAndTag splitCodeLine(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
    return {line.substr(0, i), line.substr(i)};
}

std::vector<std::string> splitLines(std::istream& stream) {
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
    }
    return lines;
}

std::vector<std::string> readLines(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return {};
    return splitLines(file);
}

class Parser {
public:
    explicit Parser(std::vector<std::string> lines)
        : lines_(std::move(lines)), pos_(0) {}

    DxxNode parseRoot() {
        pos_ = 0;
        if (!atEnd() && lines_[pos_].size() >= 3
            && static_cast<unsigned char>(lines_[pos_][0]) == 0xEF
            && static_cast<unsigned char>(lines_[pos_][1]) == 0xBB
            && static_cast<unsigned char>(lines_[pos_][2]) == 0xBF) {
            lines_[pos_] = lines_[pos_].substr(3);
            if (lines_[pos_].empty()) ++pos_;
        }

        DxxNode root;
        root.name = "ROOT";
        parseContent(root);
        return root;
    }

private:
    std::string peek() const {
        return pos_ < lines_.size() ? lines_[pos_] : std::string{};
    }
    std::string read() {
        std::string result;
        if (pos_ < lines_.size()) result = std::move(lines_[pos_++]);
        return result;
    }
    bool atEnd() const { return pos_ >= lines_.size(); }

    // Depth is capped so a maliciously or accidentally deeply nested file
    // can't exhaust the call stack. Beyond the cap we stop descending but
    // keep consuming input, so a pathological file degrades to a flatter
    // (mis-nested) tree instead of crashing the process.
    static constexpr int kMaxDepth = 1000;

    void parseContent(DxxNode& node, int depth = 0) {
        while (!atEnd()) {
            std::string codeLine = peek();

            if (codeLine == "END") {
                read();
                if (!atEnd()) read();
                break;
            }
            if (codeLine == "START") {
                read();
                std::string name = read();
                DxxNode child;
                child.name = name;
                child.lineStart = static_cast<int>(pos_ + 1);
                if (depth < kMaxDepth) parseContent(child, depth + 1);
                node.children.push_back(std::move(child));
            } else if (codeLine == "OBJECT") {
                read();
                std::string typeName = read();
                DxxNode child;
                child.name = typeName;
                child.lineStart = static_cast<int>(pos_ + 1);
                if (depth < kMaxDepth) parseContent(child, depth + 1);
                node.children.push_back(std::move(child));
            } else {
                auto [code, tag] = splitCodeLine(read());
                std::string valueLine = read();

                std::string key = code + tag;
                if (key.empty() && !valueLine.empty()) key = valueLine;

                if (valueLine.size() > 500) {
                    if (valueLine.size() > 5 && valueLine.substr(0, 5) == "\\ZIP:") {
                        std::string b64 = valueLine.substr(5);
                        std::string raw = base64Decode(b64);
                        std::string decompressed = GzipDecompress(raw);
                        if (!decompressed.empty()) {
                            if (!node.zipData.empty()) node.zipData += "\n\n";
                            node.zipData += "--- " + key + " ---\n";
                            node.zipData += decompressed;
                            valueLine = "[ZIP: " + std::to_string(decompressed.size()) + " bytes decompressed]";
                        } else {
                            valueLine = "[ZIP: " + std::to_string(raw.size()) + " bytes raw, decompress failed]";
                        }
                    } else {
                        valueLine.clear();
                    }
                }

                node.properties.push_back({std::move(key), std::move(valueLine)});
            }
        }
    }

    std::vector<std::string> lines_;
    size_t pos_;
};

void collectCurves(const DxxNode& node, Curve& currentCurve,
                   std::vector<Curve>& result, int depth = 0) {
    if (depth > dxx::kMaxNodeDepth) return;
    if (node.name == "CURVE") {
        currentCurve = Curve{};
        currentCurve.normal.x = node.getDouble("13NORMALX");
        currentCurve.normal.y = node.getDouble("13NORMALY");
        currentCurve.normal.z = node.getDouble("13NORMALZ");

        auto parseDouble = [](const std::string& s) -> double {
            char* end = nullptr;
            double d = std::strtod(s.c_str(), &end);
            if (end && *end == '\0') return d;
            try { return static_cast<double>(std::stoi(s)); }
            catch (...) { return 0.0; }
        };

        size_t segIdx = 0;
        for (const auto& [code, val] : node.properties) {
            if (code == "11PTX") {
                if (segIdx >= currentCurve.segments.size())
                    currentCurve.segments.resize(segIdx + 1);
                currentCurve.segments[segIdx].pt.x = parseDouble(val);
            } else if (code == "11PTY") {
                if (segIdx < currentCurve.segments.size())
                    currentCurve.segments[segIdx].pt.y = parseDouble(val);
            } else if (code == "11PTZ") {
                if (segIdx < currentCurve.segments.size())
                    currentCurve.segments[segIdx].pt.z = parseDouble(val);
            } else if (code == "41BULGE") {
                if (segIdx < currentCurve.segments.size())
                    currentCurve.segments[segIdx].bulge = parseDouble(val);
                ++segIdx;
            }
        }

        result.push_back(currentCurve);
        return;
    }

    for (const auto& child : node.children) {
        collectCurves(child, currentCurve, result, depth + 1);
    }

    if (!node.children.empty()) {
        Vec3D org, vx{1, 0, 0}, vy{0, 1, 0};
        std::string entryName;

        double ox = node.getDouble("13PTORGX");
        double oy = node.getDouble("13PTORGY");
        double oz = node.getDouble("13PTORGZ");
        if (ox != 0 || oy != 0 || oz != 0) org = {ox, oy, oz};

        double vxx = node.getDouble("13VECXX");
        double vxy = node.getDouble("13VECXY");
        double vxz = node.getDouble("13VECXZ");
        if (vxx != 0 || vxy != 0 || vxz != 0) vx = {vxx, vxy, vxz};

        double vyx = node.getDouble("13VECYX");
        double vyy = node.getDouble("13VECYY");
        double vyz = node.getDouble("13VECYZ");
        if (vyx != 0 || vyy != 0 || vyz != 0) vy = {vyx, vyy, vyz};

        entryName = node.getString("EntryName");
        for (const auto& child : node.children) {
            if (!entryName.empty()) break;
            entryName = child.getString("EntryName");
        }

        for (auto& c : result) {
            if (!c.entryName.empty()) continue;
            c.origin = org;
            c.vecX = vx;
            c.vecY = vy;
            c.entryName = entryName;
        }
    }
}

} // anonymous namespace

namespace {

std::optional<DxxDocument> buildDocument(std::vector<std::string> lines) {
    if (lines.empty()) return std::nullopt;

    Parser parser(std::move(lines));
    DxxDocument doc;
    doc.root = parser.parseRoot();

    doc.curves.clear();
    Curve temp;
    for (const auto& child : doc.root.children) {
        collectCurves(child, temp, doc.curves, 0);
    }
    return doc;
}

} // anonymous namespace

std::optional<DxxDocument> parseFile(const std::string& filepath) {
    return buildDocument(readLines(filepath));
}

std::optional<DxxDocument> parseString(const std::string& content) {
    std::istringstream stream(content);
    return buildDocument(splitLines(stream));
}

std::vector<Curve> extractCurves(const DxxNode& node) {
    std::vector<Curve> curves;
    walkNode(node, [&](const DxxNode& n) {
        if (n.name == "CURVE" && !n.properties.empty()) {
            Curve c; c.vecX = {1, 0, 0}; c.vecY = {0, 1, 0};
            size_t idx = 0;
            for (const auto& [code, val] : n.properties) {
                if (code == "11PTX") {
                    if (idx >= c.segments.size()) c.segments.resize(idx + 1);
                    c.segments[idx].pt.x = std::strtod(val.c_str(), nullptr);
                } else if (code == "11PTY" && idx < c.segments.size()) {
                    c.segments[idx].pt.y = std::strtod(val.c_str(), nullptr);
                } else if (code == "41BULGE" && idx < c.segments.size()) {
                    c.segments[idx].bulge = std::strtod(val.c_str(), nullptr);
                    ++idx;
                }
            }
            if (!c.segments.empty()) curves.push_back(c);
        }
    });
    return curves;
}

std::vector<Curve> extractCurves3D(const DxxNode& node) {
    std::vector<Curve> curves;
    Curve temp;
    collectCurves(node, temp, curves, 0);
    return curves;
}

namespace {

constexpr double kPi = 3.14159265358979323846;

Vec3D normalized(Vec3D v) {
    double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-12) return {0, 0, 0};
    return {v.x / len, v.y / len, v.z / len};
}

Vec3D crossProduct(const Vec3D& a, const Vec3D& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

} // anonymous namespace

std::vector<Point3D> tessellateCurveWorld(const Curve& curve, int stepsPerCircle) {
    std::vector<Point3D> out;
    if (curve.segments.size() < 2) return out;

    Vec3D normal = normalized(curve.normal);
    if (normal.x == 0 && normal.y == 0 && normal.z == 0)
        normal = normalized(crossProduct(curve.vecX, curve.vecY));

    auto toWorld = [&](double lx, double ly, double lz) -> Point3D {
        return {
            curve.origin.x + lx * curve.vecX.x + ly * curve.vecY.x + lz * normal.x,
            curve.origin.y + lx * curve.vecX.y + ly * curve.vecY.y + lz * normal.y,
            curve.origin.z + lx * curve.vecX.z + ly * curve.vecY.z + lz * normal.z,
        };
    };

    // Appends the arc/line from `prev` to `seg` (endpoint only if straight,
    // else tessellated) using the exact bulge-angle relation
    // bulge = tan(includedAngle / 4), so the sweep is signed and exact —
    // no need to disambiguate direction from an atan2 difference.
    auto appendSegment = [&](const CurveSegment& prev, const CurveSegment& seg) {
        double dx = seg.pt.x - prev.pt.x, dy = seg.pt.y - prev.pt.y;
        double dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 1e-10 || std::abs(prev.bulge) < 1e-10) {
            out.push_back(toWorld(seg.pt.x, seg.pt.y, seg.pt.z));
            return;
        }
        double b = prev.bulge;
        double mx = (prev.pt.x + seg.pt.x) / 2.0, my = (prev.pt.y + seg.pt.y) / 2.0;
        double nx = -dy / dist, ny = dx / dist;
        double co = dist * (1.0 - b * b) / (4.0 * b);
        double acx = mx + nx * co, acy = my + ny * co;
        double r = std::sqrt((prev.pt.x - acx) * (prev.pt.x - acx) + (prev.pt.y - acy) * (prev.pt.y - acy));
        double a1 = std::atan2(prev.pt.y - acy, prev.pt.x - acx);
        double sweep = 4.0 * std::atan(b);

        int steps = std::max(2, (int)std::ceil(std::abs(sweep) / (2.0 * kPi) * stepsPerCircle));
        for (int i = 1; i <= steps; ++i) {
            double t = double(i) / steps;
            double a = a1 + sweep * t;
            double lz = prev.pt.z + (seg.pt.z - prev.pt.z) * t;
            out.push_back(toWorld(acx + r * std::cos(a), acy + r * std::sin(a), lz));
        }
    };

    out.push_back(toWorld(curve.segments[0].pt.x, curve.segments[0].pt.y, curve.segments[0].pt.z));
    for (size_t i = 1; i < curve.segments.size(); ++i)
        appendSegment(curve.segments[i - 1], curve.segments[i]);
    appendSegment(curve.segments.back(), curve.segments.front()); // close the loop

    return out;
}

std::vector<Curve> extractProfile2D(const DxxNode& node) {
    Vec3D normal = normalized({node.getDouble("13VECNX"),
                               node.getDouble("13VECNY"),
                               node.getDouble("13VECNZ")});
    if (normal.x == 0 && normal.y == 0 && normal.z == 0) {
        walkNode(node, [&](const DxxNode& n) {
            if (normal.x != 0 || normal.y != 0 || normal.z != 0) return;
            if (n.name == "CURVE") {
                normal = normalized({n.getDouble("13NORMALX"),
                                     n.getDouble("13NORMALY"),
                                     n.getDouble("13NORMALZ")});
            }
        });
    }
    if (normal.x == 0 && normal.y == 0 && normal.z == 0)
        normal = {0, 0, 1}; // default: XY plane

    // Orthonormal 2D basis lying in the profile plane.
    Vec3D ref = (std::abs(normal.z) < 0.9) ? Vec3D{0, 0, 1} : Vec3D{0, 1, 0};
    Vec3D u = normalized(crossProduct(ref, normal));
    if (u.x == 0 && u.y == 0 && u.z == 0)
        u = normalized(crossProduct({1, 0, 0}, normal));
    Vec3D v = crossProduct(normal, u);

    Vec3D org{node.getDouble("11PTORGX"),
              node.getDouble("11PTORGY"),
              node.getDouble("11PTORGZ")};

    std::vector<Curve> curves;
    walkNode(node, [&](const DxxNode& n) {
        if (n.name != "CURVE" || n.properties.empty()) return;
        Curve c;
        c.normal = normal;
        size_t idx = 0;
        for (const auto& [code, val] : n.properties) {
            if (code == "11PTX") {
                if (idx >= c.segments.size()) c.segments.resize(idx + 1);
                c.segments[idx].pt.x = std::strtod(val.c_str(), nullptr);
            } else if (code == "11PTY" && idx < c.segments.size()) {
                c.segments[idx].pt.y = std::strtod(val.c_str(), nullptr);
            } else if (code == "11PTZ" && idx < c.segments.size()) {
                c.segments[idx].pt.z = std::strtod(val.c_str(), nullptr);
            } else if (code == "41BULGE" && idx < c.segments.size()) {
                c.segments[idx].bulge = std::strtod(val.c_str(), nullptr);
                ++idx;
            }
        }
        for (auto& seg : c.segments) {
            double px = seg.pt.x - org.x, py = seg.pt.y - org.y, pz = seg.pt.z - org.z;
            double nx = px * u.x + py * u.y + pz * u.z;
            double ny = px * v.x + py * v.y + pz * v.z;
            seg.pt = {nx, ny, 0.0};
        }
        if (!c.segments.empty()) curves.push_back(std::move(c));
    });
    return curves;
}

std::optional<NodeColor> extractNodeColor(const DxxNode& node) {
    return findInNode(node, [](const DxxNode& n) -> std::optional<NodeColor> {
        int r = n.getInt("70R", -1), g = n.getInt("70G", -1), b = n.getInt("70B", -1);
        if (r < 0 || g < 0 || b < 0) return std::nullopt;
        return NodeColor{r, g, b};
    });
}

} // namespace dxx
