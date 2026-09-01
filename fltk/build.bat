@echo off
rem ==========================================================================
rem DXX Viewer - FLTK GUI build script (MinGW g++)
rem Links the FLTK 1.4.5 static libraries installed to C:\Users\jissi\fltk-install
rem Reuses the shared dxxviewer core (parser, gzip, format utils) - no duplication.
rem Run from anywhere: this script cd's to its own directory first.
rem ==========================================================================
setlocal
cd /d "%~dp0"

set FLTK_ROOT=C:\Users\jissi\fltk-install
set CAIRO_ROOT=C:\msys64\mingw64
set CXX=g++
set OUT=build\dxxviewer-fltk.exe

where g++ >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: g++ not found in PATH. Install MinGW via scoop install mingw.
    exit /b 1
)

if not exist build mkdir build

%CXX% -std=c++20 -Wall -Wextra ^
    fltk_main.cpp ^
    FltkMainWindow.cpp ^
    FltkTreePanel.cpp ^
    FltkPropertiesPanel.cpp ^
    FltkGeometryWidget.cpp ^
    HubClient.cpp ^
    ..\dxx_parser.cpp ^
    ..\gzip_decompress.cpp ^
    -I. ^
    -I.. ^
    -I%FLTK_ROOT%\include ^
    -I%CAIRO_ROOT%\include\cairo ^
    -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 ^
    -L%FLTK_ROOT%\lib ^
    -L%CAIRO_ROOT%\lib ^
    -static-libgcc -static-libstdc++ ^
    -mwindows ^
    -lfltk -lfltk_images -lfltk_png -lfltk_z ^
    -lcairo ^
    -lgdiplus -lole32 -luuid -lcomctl32 -lws2_32 -lwinspool -ldwmapi ^
    -o %OUT%

if %errorlevel% neq 0 (
    echo.
    echo Build failed!
    exit /b 1
)

rem Cairo is linked dynamically; copy its runtime DLLs next to the exe.
for %%D in (libcairo-2.dll libpixman-1-0.dll libfontconfig-1.dll libfreetype-6.dll libexpat-1.dll libharfbuzz-0.dll libglib-2.0-0.dll libgraphite2.dll libpcre2-8-0.dll libbrotlicommon.dll libbrotlidec.dll libbz2-1.dll libintl-8.dll libiconv-2.dll libpng16-16.dll zlib1.dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll) do (
    if exist "%CAIRO_ROOT%\bin\%%D" copy /y "%CAIRO_ROOT%\bin\%%D" build\ >nul
)

echo.
echo Build succeeded: %OUT%
endlocal