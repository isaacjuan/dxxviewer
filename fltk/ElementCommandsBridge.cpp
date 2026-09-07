#include "ElementCommandsBridge.h"
#include "../third_party/json.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace dxxviewer {

namespace {

using nlohmann::json;

// Splits "(a,b,c)" starting at/after `pos` into its three comma-separated
// tokens (as raw text, not parsed doubles - see ParsedPLine below for why),
// advancing `pos` past the closing ')'. Returns false if no well-formed
// 3-tuple is found.
bool parseTuple3Tokens(const std::string& text, size_t& pos,
                       std::string& a, std::string& b, std::string& c) {
    size_t open = text.find('(', pos);
    if (open == std::string::npos) return false;
    size_t close = text.find(')', open);
    if (close == std::string::npos) return false;

    std::string inner = text.substr(open + 1, close - open - 1);
    auto trim = [](std::string s) {
        size_t b0 = s.find_first_not_of(" \t");
        if (b0 == std::string::npos) return std::string();
        size_t b1 = s.find_last_not_of(" \t");
        return s.substr(b0, b1 - b0 + 1);
    };

    std::vector<std::string> parts;
    size_t start = 0;
    for (int i = 0; i < 2; ++i) {
        size_t comma = inner.find(',', start);
        if (comma == std::string::npos) return false;
        parts.push_back(trim(inner.substr(start, comma - start)));
        start = comma + 1;
    }
    parts.push_back(trim(inner.substr(start)));

    a = parts[0];
    b = parts[1];
    c = parts[2];
    pos = close + 1;
    return true;
}

// A Phoenix/BeamRDB "CURVE" map entry's value is this object's own
// ToString(), not DXX text: PLine(normal, Point3dCollection[n]{(x,y,z),...},
// DoubleCollection[n]{bulge,...}). Point3dCollection is already absolute
// world-space 3D coordinates (unlike DXX's own local-plane-relative CURVE
// convention), so these map straight onto dxx_parser.cpp's 11PTX/11PTY/
// 11PTZ/41BULGE properties with an identity origin/basis (collectCurves'
// default when no ancestor sets 13PTORG*/13VECX*/13VECY*) - no coordinate
// transform needed. Point/normal components are kept as their original text
// tokens rather than round-tripped through strtod+reformat, so no precision
// is lost beyond what dxx_parser.cpp's own strtod parsing already costs when
// it later reads these properties back for rendering.
struct ParsedPLine {
    std::string normalX, normalY, normalZ;
    std::vector<std::array<std::string, 3>> points;
    std::vector<std::string> bulges;
};

std::optional<ParsedPLine> parsePLine(const std::string& text) {
    size_t pos = text.find("PLine(");
    if (pos == std::string::npos) return std::nullopt;
    pos += 6;

    ParsedPLine result;
    if (!parseTuple3Tokens(text, pos, result.normalX, result.normalY, result.normalZ))
        return std::nullopt;

    size_t collPos = text.find("Point3dCollection", pos);
    if (collPos == std::string::npos) return std::nullopt;
    size_t braceOpen = text.find('{', collPos);
    size_t braceClose = (braceOpen == std::string::npos) ? std::string::npos : text.find('}', braceOpen);
    if (braceOpen == std::string::npos || braceClose == std::string::npos) return std::nullopt;

    for (size_t p = braceOpen + 1;;) {
        size_t nextParen = text.find('(', p);
        if (nextParen == std::string::npos || nextParen > braceClose) break;

        std::array<std::string, 3> pt;
        size_t tuplePos = nextParen;
        if (!parseTuple3Tokens(text, tuplePos, pt[0], pt[1], pt[2])) break;
        result.points.push_back(std::move(pt));
        p = tuplePos;
    }
    if (result.points.empty()) return std::nullopt;

    size_t dcPos = text.find("DoubleCollection", braceClose);
    if (dcPos != std::string::npos) {
        size_t dcOpen = text.find('{', dcPos);
        size_t dcClose = (dcOpen == std::string::npos) ? std::string::npos : text.find('}', dcOpen);
        if (dcOpen != std::string::npos && dcClose != std::string::npos) {
            std::string inner = text.substr(dcOpen + 1, dcClose - dcOpen - 1);
            for (size_t start = 0; start <= inner.size();) {
                size_t comma = inner.find(',', start);
                std::string tok = (comma == std::string::npos) ? inner.substr(start)
                                                                : inner.substr(start, comma - start);
                size_t b0 = tok.find_first_not_of(" \t");
                if (b0 != std::string::npos) {
                    size_t b1 = tok.find_last_not_of(" \t");
                    result.bulges.push_back(tok.substr(b0, b1 - b0 + 1));
                }
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        }
    }

    return result;
}

// Builds a DXX-native CURVE node (property order matters: dxx_parser.cpp's
// collectCurves/extractCurves/extractProfile2D all finalize a point on
// 41BULGE, so each point's four properties must appear together in exactly
// this X/Y/Z/BULGE order) so the existing curve-extraction/rendering
// pipeline picks this up with zero changes, exactly as it would a real DXX
// file's CURVE node.
dxx::DxxNode buildCurveNode(const ParsedPLine& pline) {
    dxx::DxxNode curveNode;
    curveNode.name = "CURVE";
    curveNode.properties.emplace_back("13NORMALX", pline.normalX);
    curveNode.properties.emplace_back("13NORMALY", pline.normalY);
    curveNode.properties.emplace_back("13NORMALZ", pline.normalZ);

    for (size_t i = 0; i < pline.points.size(); ++i) {
        curveNode.properties.emplace_back("11PTX", pline.points[i][0]);
        curveNode.properties.emplace_back("11PTY", pline.points[i][1]);
        curveNode.properties.emplace_back("11PTZ", pline.points[i][2]);
        curveNode.properties.emplace_back("41BULGE", i < pline.bulges.size() ? pline.bulges[i] : std::string("0"));
    }
    return curveNode;
}

// Converts an arbitrary JSON leaf value to the string a DxxNode property
// value holds. Objects/arrays never reach here (buildMapNode recurses into
// objects instead of calling this on them) but are handled defensively via
// dump() rather than asserting.
std::string jsonLeafToString(const json& v) {
    if (v.is_null()) return std::string();
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
    if (v.is_number_float()) return v.dump();
    return v.dump();
}

// Recursively fills `node`'s properties/children from a JSON object shaped
// like the Revit "map" tree (see hsbWebSocketRvt's BuildMapJson/HsbMap):
// a nested object value becomes a child DxxNode (named after the key,
// populated recursively); any other value (string/number/bool/null) becomes
// a (key, stringValue) property on the CURRENT node.
void buildMapNode(dxx::DxxNode& node, const json& mapObj) {
    if (!mapObj.is_object()) return;
    for (auto it = mapObj.begin(); it != mapObj.end(); ++it) {
        const json& value = it.value();
        if (value.is_object()) {
            dxx::DxxNode child;
            child.name = it.key();
            buildMapNode(child, value);
            node.children.push_back(std::move(child));
        } else if (it.key() == "CURVE" && value.is_string()) {
            // A "CURVE" key's value is Phoenix's own PLine(...) text, not a
            // plain string to display - parse it into a real DXX CURVE node
            // (see buildCurveNode) so the existing geometry preview can
            // render it, same as a real DXX file's CURVE node. Falls back to
            // the flat string property if it doesn't parse as PLine, so
            // nothing is silently dropped.
            if (auto pline = parsePLine(value.get<std::string>())) {
                node.children.push_back(buildCurveNode(*pline));
            } else {
                node.properties.emplace_back(it.key(), jsonLeafToString(value));
            }
        } else {
            node.properties.emplace_back(it.key(), jsonLeafToString(value));
        }
    }
}

dxx::DxxNode buildElementNode(const json& element) {
    dxx::DxxNode node;
    long long elementId = element.value("elementId", static_cast<long long>(0));
    node.name = "Element " + std::to_string(elementId);

    if (element.contains("found") && element["found"].is_boolean() && !element["found"].get<bool>())
        node.properties.emplace_back("found", "false");

    if (auto it = element.find("parameters"); it != element.end() && it->is_array()) {
        for (const auto& p : *it) {
            if (!p.is_object()) continue;
            std::string name = p.value("name", std::string());
            if (name.empty()) continue;
            std::string value = p.contains("value") ? jsonLeafToString(p.at("value")) : std::string();
            node.properties.emplace_back(name, value);
            if (auto st = p.find("storageType"); st != p.end())
                node.properties.emplace_back(name + " [storageType]", jsonLeafToString(*st));
            if (auto ro = p.find("isReadOnly"); ro != p.end())
                node.properties.emplace_back(name + " [isReadOnly]", jsonLeafToString(*ro));
        }
    }

    // map/mapError are documented as mutually exclusive (mapError replaces
    // map on failure), but handled independently here rather than
    // else-if'd, so a producer that somehow sends both still shows both
    // rather than silently dropping one.
    if (auto it = element.find("mapError"); it != element.end() && it->is_string())
        node.properties.emplace_back("mapError", it->get<std::string>());

    if (auto it = element.find("map"); it != element.end() && it->is_object()) {
        dxx::DxxNode mapNode;
        mapNode.name = "Map";
        buildMapNode(mapNode, *it);
        node.children.push_back(std::move(mapNode));
    }

    return node;
}

} // namespace

std::optional<dxx::DxxDocument> buildDocumentFromElementCommands(const std::string& jsonText) {
    json root;
    try {
        root = json::parse(jsonText);
    } catch (const std::exception&) {
        return std::nullopt;
    }

    if (!root.is_object()) return std::nullopt;
    if (root.value("type", std::string()) != "selection_parameters") return std::nullopt;

    dxx::DxxDocument doc;
    doc.root.name = "Selection";

    if (auto it = root.find("elements"); it != root.end() && it->is_array()) {
        for (const auto& element : *it) {
            if (!element.is_object()) continue;
            doc.root.children.push_back(buildElementNode(element));
        }
    }

    return doc;
}

} // namespace dxxviewer
