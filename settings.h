#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dxxviewer {

// Runtime settings loaded from settings.lua (externalized constants that
// used to be compile-time). Defaults match the previous hardcodes / colors.h
// and stay in effect if the file is missing or fails to load.
struct Settings {
    std::vector<uint32_t> curvePalette;      // 0xRRGGBB, non-empty
    std::vector<uint32_t> treeDepthPalette;  // 0xRRGGBB, non-empty
    // hsbWebSocketHub endpoint for all WS subscribe/publish traffic
    std::string hubHost;
    unsigned short hubPort = 0;
    std::string topicMap;
    std::string topicElementCommands;
    std::string topicGeometry;
};

// First call loads settings.lua once (next to the exe, then cwd) and caches
// the result for the process lifetime. Safe from any thread after first use;
// call once from main() before any worker threads start.
const Settings& settings();

// Path of the settings file that was actually loaded, or empty if defaults.
const std::string& settingsPath();

} // namespace dxxviewer
