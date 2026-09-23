cmake_minimum_required(VERSION 3.20)
project(dxxviewer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Vendored Lua 5.5 (settings.lua loader) — exclude the two standalone mains.
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua/*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|luac)\\.c$")

add_library(lua_settings STATIC ${LUA_SOURCES})
target_include_directories(lua_settings PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua)
if(MSVC)
    target_compile_options(lua_settings PRIVATE /W0)
else()
    target_compile_options(lua_settings PRIVATE -w)
endif()

# Command-line tool: parses a .dxx and writes preview.svg.
add_executable(dxxviewer
    main.cpp
    dxx_parser.cpp
    gzip_decompress.cpp
    settings.cpp
)
target_include_directories(dxxviewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(dxxviewer PRIVATE /W4 /utf-8)
else()
    target_compile_options(dxxviewer PRIVATE -Wall -Wextra -pedantic)
endif()
target_link_libraries(dxxviewer PRIVATE lua_settings)

# Ship settings.lua next to the exe (cwd/exe-dir search still applies).
add_custom_command(TARGET dxxviewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/settings.lua
        $<TARGET_FILE_DIR:dxxviewer>/settings.lua)
