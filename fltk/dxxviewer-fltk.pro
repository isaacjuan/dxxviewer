# ============================================================================
# DXX Viewer — FLTK GUI (qmake project for Qt Creator)
#
# Open this .pro in Qt Creator with a MinGW-w64 kit. It mirrors fltk/build.bat:
# links FLTK 1.4.5 (static) and Cairo (dynamic). Run build.bat once to copy the
# Cairo DLLs next to the exe, or add C:\msys64\mingw64\bin to PATH.
# ============================================================================

CONFIG -= qt          # plain C++ app — no Qt linkage
CONFIG += c++20
CONFIG -= app_bundle

TEMPLATE = app
TARGET = dxxviewer-fltk

FLTK_ROOT  = C:/Users/jissi/fltk-install
CAIRO_ROOT = C:/msys64/mingw64

INCLUDEPATH += $$PWD $$PWD/.. $$FLTK_ROOT/include $$CAIRO_ROOT/include/cairo

SOURCES += \
    fltk_main.cpp \
    FltkMainWindow.cpp \
    FltkTreePanel.cpp \
    FltkPropertiesPanel.cpp \
    FltkGeometryWidget.cpp \
    HubClient.cpp \
    ../dxx_parser.cpp \
    ../gzip_decompress.cpp

HEADERS += \
    FltkMainWindow.h \
    FltkTreePanel.h \
    FltkPropertiesPanel.h \
    FltkGeometryWidget.h \
    HubClient.h \
    ../dxx_parser.h \
    ../colors.h

LIBS += -L$$FLTK_ROOT/lib -L$$CAIRO_ROOT/lib
LIBS += -lfltk -lfltk_images -lfltk_png -lfltk_z
LIBS += -lcairo
LIBS += -lgdiplus -lole32 -luuid -lcomctl32 -lws2_32 -lwinspool -ldwmapi

DEFINES += _LARGEFILE_SOURCE _LARGEFILE64_SOURCE _FILE_OFFSET_BITS=64
QMAKE_LFLAGS += -static-libgcc -static-libstdc++ -mwindows
