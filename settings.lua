-- settings.lua — runtime config for dxxviewer (CLI + FLTK GUI)
-- Searched next to the exe first, then the working directory.
-- Delete this file (or any key) to fall back to the built-in defaults
-- from colors.h / the previous compile-time constants.
--
-- Colors: "#rrggbb", "#rgb", or integer 0xRRGGBB.

return {
    -- Qualitative palette for distinct curves (geometry preview + SVG legend)
    curve_palette = {
        "#1F77B4", "#FF7F0E", "#2CA02C", "#D62728", "#9467BD",
        "#8C564B", "#E377C2", "#7F7F7F", "#BC9D22", "#17BECF",
        "#393B79", "#5254A3", "#6B6ECF", "#9C9EDE", "#637939",
        "#8CA252", "#B5CF6B", "#CEDB9C", "#8C6D31", "#BD9E39",
    },

    -- Tree label color per nesting depth (cycles)
    tree_depth_palette = {
        "#000000", "#00468C", "#007850", "#B45000",
        "#7800A0", "#0064A0", "#A02828",
    },

    -- hsbWebSocketHub endpoint (subscribe + publish)
    hub = {
        host = "127.0.0.1",
        port = 8181,
    },

    topics = {
        map = "map",
        element_commands = "element_commands",
        geometry = "acad_geometry",
    },
}
