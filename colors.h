#pragma once
#include <cstdint>

namespace dxxviewer {

// Qualitative palette used to color distinct curves/entities in the geometry
// preview and the exported SVG (CLI), so the same curve reads as the same
// color everywhere. Packed 0xRRGGBB; each consumer converts to its own color
// type (Fl_Color / cairo RGB, "#rrggbb").
inline constexpr uint32_t kCurveColorPalette[] = {
    0x1F77B4, 0xFF7F0E, 0x2CA02C, 0xD62728, 0x9467BD,
    0x8C564B, 0xE377C2, 0x7F7F7F, 0xBC9D22, 0x17BECF,
    0x393B79, 0x5254A3, 0x6B6ECF, 0x9C9EDE, 0x637939,
    0x8CA252, 0xB5CF6B, 0xCEDB9C, 0x8C6D31, 0xBD9E39,
};
inline constexpr int kCurveColorCount =
    static_cast<int>(sizeof(kCurveColorPalette) / sizeof(kCurveColorPalette[0]));

// Text color per tree nesting depth, cycling every 7 levels.
inline constexpr uint32_t kTreeDepthColorPalette[] = {
    0x000000, 0x00468C, 0x007850, 0xB45000, 0x7800A0, 0x0064A0, 0xA02828,
};
inline constexpr int kTreeDepthColorCount =
    static_cast<int>(sizeof(kTreeDepthColorPalette) / sizeof(kTreeDepthColorPalette[0]));

} // namespace dxxviewer
