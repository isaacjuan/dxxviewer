#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>

#include <cstdio>

#include "FltkMainWindow.h"

namespace {

// Read Windows' live accent color (0xAARRGGBB) and apply it as FLTK's
// selection color, the way the fltk-example gallery does.
void applyWindowsAccent() {
    DWORD color = 0;
    if (DwmGetColorizationColor(&color, nullptr) == S_OK) {
        Fl::set_color(FL_SELECTION_COLOR,
                      (uchar)((color >> 16) & 0xFF),
                      (uchar)((color >> 8) & 0xFF),
                      (uchar)(color & 0xFF));
    }
}

} // namespace

int main(int argc, char* argv[]) {
    // Required once, before Fl::run(), so HubClient's background thread can
    // deliver received maps to the GUI via Fl::awake().
    Fl::lock();

    Fl::scheme("oxy");
    Fl::get_system_colors();
    Fl::set_font(FL_HELVETICA, "Segoe UI");
    Fl::set_font(FL_HELVETICA_BOLD, "Segoe UI Bold");
    Fl::set_font(FL_HELVETICA_ITALIC, "Segoe UI Italic");
    Fl::set_font(FL_HELVETICA_BOLD_ITALIC, "Segoe UI Bold Italic");
    applyWindowsAccent();

    Fl_Double_Window window(1180, 700, "DXX Viewer");
    dxxviewer::FltkMainWindow mainWindow(0, 0, 1180, 700);
    mainWindow.end();

    window.resizable(&mainWindow);
    window.end();
    window.show();

    if (argc > 1) {
        mainWindow.openFile(argv[1]);
    }

    return Fl::run();
}