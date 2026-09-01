@echo off
rem Copy the Cairo runtime DLL closure from a source bin dir to a destination dir.
rem Usage: copy_cairo_dlls.bat <src-bin-dir> <dst-dir>
if "%~1"=="" exit /b 1
if "%~2"=="" exit /b 1
for %%D in (libcairo-2.dll libpixman-1-0.dll libfontconfig-1.dll libfreetype-6.dll libexpat-1.dll libharfbuzz-0.dll libglib-2.0-0.dll libgraphite2.dll libpcre2-8-0.dll libbrotlicommon.dll libbrotlidec.dll libbz2-1.dll libintl-8.dll libiconv-2.dll libpng16-16.dll zlib1.dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll) do (
    if exist "%~1\%%D" copy /y "%~1\%%D" "%~2\" >nul
)