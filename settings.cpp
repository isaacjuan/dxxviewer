#include "settings.h"
#include "colors.h"

#include <lua.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace dxxviewer {

namespace {

namespace fs = std::filesystem;

std::string exeDir()
{
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return {};
    return fs::path(buf).parent_path().string();
#else
    return {};
#endif
}

// "#rrggbb" / "#rgb" or integer 0xRRGGBB → 0xRRGGBB.
bool parseColorValue(lua_State* L, int idx, uint32_t& out)
{
    idx = lua_absindex(L, idx);
    if (lua_isinteger(L, idx)) {
        const lua_Integer v = lua_tointeger(L, idx);
        if (v < 0 || v > 0xFFFFFF)
            return false;
        out = static_cast<uint32_t>(v);
        return true;
    }
    if (lua_type(L, idx) != LUA_TSTRING)
        return false;
    const char* str = lua_tostring(L, idx);
    if (!str || str[0] != '#')
        return false;
    try {
        const std::string s(str + 1);
        if (s.size() == 3) { // #rgb → #rrggbb
            std::string e;
            e.reserve(6);
            for (char c : s) {
                e.push_back(c);
                e.push_back(c);
            }
            out = static_cast<uint32_t>(std::stoul(e, nullptr, 16));
            return true;
        }
        if (s.size() != 6)
            return false;
        out = static_cast<uint32_t>(std::stoul(s, nullptr, 16));
        return true;
    } catch (...) {
        return false;
    }
}

// Reads array field `key` from the table at stack top. Leaves table on stack.
// Replaces `out` only if a non-empty valid palette was read.
void readPalette(lua_State* L, const char* key, std::vector<uint32_t>& out,
                 const char* file)
{
    lua_getfield(L, -1, key);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    std::vector<uint32_t> tmp;
    const lua_Integer n = luaL_len(L, -1);
    for (lua_Integer i = 1; i <= n; ++i) {
        lua_rawgeti(L, -1, i);
        uint32_t rgb = 0;
        if (!parseColorValue(L, -1, rgb)) {
            std::fprintf(stderr, "%s: bad color at %s[%lld]\n", file, key,
                         static_cast<long long>(i));
            lua_pop(L, 2); // element + palette
            return;
        }
        tmp.push_back(rgb);
        lua_pop(L, 1);
    }
    lua_pop(L, 1); // palette
    if (!tmp.empty())
        out = std::move(tmp);
}

void readString(lua_State* L, const char* field, std::string& out)
{
    lua_getfield(L, -1, field);
    if (lua_isstring(L, -1))
        out = lua_tostring(L, -1);
    lua_pop(L, 1);
}

// Applies a `return { ... }` table at stack top. Leaves table on stack.
void applyRootTable(lua_State* L, Settings& s, const char* file)
{
    readPalette(L, "curve_palette", s.curvePalette, file);
    readPalette(L, "tree_depth_palette", s.treeDepthPalette, file);

    lua_getfield(L, -1, "hub");
    if (lua_istable(L, -1)) {
        readString(L, "host", s.hubHost);
        lua_getfield(L, -1, "port");
        if (lua_isinteger(L, -1)) {
            const lua_Integer port = lua_tointeger(L, -1);
            if (port > 0 && port <= 65535)
                s.hubPort = static_cast<unsigned short>(port);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "topics");
    if (lua_istable(L, -1)) {
        readString(L, "map", s.topicMap);
        readString(L, "element_commands", s.topicElementCommands);
        readString(L, "geometry", s.topicGeometry);
    }
    lua_pop(L, 1);
}

Settings defaultSettings()
{
    Settings s;
    s.curvePalette.assign(std::begin(kCurveColorPalette),
                          std::end(kCurveColorPalette));
    s.treeDepthPalette.assign(std::begin(kTreeDepthColorPalette),
                              std::end(kTreeDepthColorPalette));
    s.hubHost = "127.0.0.1";
    s.hubPort = 8181;
    s.topicMap = "map";
    s.topicElementCommands = "element_commands";
    s.topicGeometry = "acad_geometry";
    return s;
}

struct Loaded {
    Settings value;
    std::string path; // empty = defaults only
};

Loaded load()
{
    Loaded out;
    out.value = defaultSettings();

    // cwd first (dev: edit project-root settings.lua without a rebuild),
    // then next to the exe (deployed layout).
    std::vector<fs::path> candidates;
    candidates.emplace_back("settings.lua");
    const std::string dir = exeDir();
    if (!dir.empty())
        candidates.push_back(fs::path(dir) / "settings.lua");

    for (const auto& p : candidates) {
        std::error_code ec;
        if (!fs::exists(p, ec))
            continue;

        std::ifstream f(p, std::ios::binary);
        if (!f)
            continue;
        std::ostringstream ss;
        ss << f.rdbuf();
        const std::string src = ss.str();
        const std::string name = p.string();

        lua_State* L = luaL_newstate();
        if (!L)
            continue;
        luaL_openlibs(L);

        if (luaL_loadbuffer(L, src.data(), src.size(), name.c_str()) !=
                LUA_OK ||
            lua_pcall(L, 0, 1, 0) != LUA_OK) {
            std::fprintf(stderr, "%s\n", lua_tostring(L, -1));
            lua_close(L);
            break; // file exists but is broken — keep defaults
        }

        if (lua_istable(L, -1)) {
            applyRootTable(L, out.value, name.c_str());
            out.path = name;
        } else {
            std::fprintf(stderr, "%s: expected `return { ... }`\n",
                         name.c_str());
        }
        lua_close(L);
        break; // first existing candidate wins
    }
    return out;
}

const Loaded& loaded()
{
    static const Loaded one = load();
    return one;
}

} // namespace

const Settings& settings()
{
    return loaded().value;
}

const std::string& settingsPath()
{
    return loaded().path;
}

} // namespace dxxviewer
