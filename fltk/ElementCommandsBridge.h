#pragma once

#include "../dxx_parser.h"
#include <optional>
#include <string>

namespace dxxviewer {

// Parses a raw "element_commands" topic broadcast (JSON text, see
// hsbWebSocketRvt) and, if it's a "selection_parameters" message, builds a
// synthetic dxx::DxxDocument so it can be displayed through the existing
// tree/properties panels exactly like a real DXX document. Returns nullopt
// for messages that aren't selection_parameters (e.g. element_parameters/
// element_map/element_parameter_set replies - not handled by this push-only
// viewer) or that fail to parse as JSON.
//
// Resulting shape (see dxxviewer/AGENTS.md's ElementCommandsBridge section
// if present, or the design notes in the feature commit): a root node named
// "Selection" with one child per selected element, named "Element <id>",
// carrying the element's flattened Revit parameters as properties plus (if
// present) a "mapError" property or a "Map" child subtree built recursively
// from the element's "map" JSON object (nested objects become child nodes,
// string/number/bool/null leaves become properties on the current node).
[[nodiscard]] std::optional<dxx::DxxDocument> buildDocumentFromElementCommands(const std::string& json);

} // namespace dxxviewer
