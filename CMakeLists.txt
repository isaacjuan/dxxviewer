cmake_minimum_required(VERSION 3.20)
project(dxxviewer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Command-line tool: parses a .dxx and writes preview.svg.
add_executable(dxxviewer
    main.cpp
    dxx_parser.cpp
    gzip_decompress.cpp
)
target_include_directories(dxxviewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(dxxviewer PRIVATE -Wall -Wextra -pedantic)
