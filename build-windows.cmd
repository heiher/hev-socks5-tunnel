@echo off
rem ============================================================================
rem  build-windows.cmd - Native MSVC/Windows build for hev-socks5-tunnel
rem
rem  Make-based counterpart to build-apple.sh: builds with NMake + MSVC
rem  (cl / lib / link / ml64), no CMake required.
rem
rem  Prerequisites:
rem    - Visual Studio 2022 (17.5+) with the x64 v143 toolset.
rem      Run from a VS2022 Developer Command Prompt so cl.exe, lib.exe,
rem      link.exe, ml64.exe and nmake.exe are available.
rem      (_Generic needs MSVC 19.35+.)
rem
rem  Outputs:
rem    build-msvc\dist\bin\  - hev-socks5-tunnel.exe, hev-socks5-tunnel.dll, wintun.dll
rem    build-msvc\dist\lib\  - hev-socks5-tunnel-static.lib, hev-socks5-tunnel.lib (import)
rem    build-msvc\dist\include\ - public header (hev-main.h)
rem
rem  Usage:
rem    build-windows.cmd [--debug] [--static-crt] [--clean] [--help]
rem    Extra args after -- are forwarded to nmake, e.g.:
rem      build-windows.cmd -- DEBUG=1
rem ============================================================================
setlocal EnableExtensions
cd /d "%~dp0"

set NMAKE_ARGS=

:parse_args
if "%~1"=="" goto :args_done
if /I "%~1"=="--debug" ( set NMAKE_ARGS=%NMAKE_ARGS% DEBUG=1 & shift & goto :parse_args )
if /I "%~1"=="--static-crt" ( set NMAKE_ARGS=%NMAKE_ARGS% STATIC_CRT=1 & shift & goto :parse_args )
if /I "%~1"=="--clean" ( nmake /F Makefile.msvc clean & exit /b %ERRORLEVEL% )
if /I "%~1"=="--help" goto :usage
if /I "%~1"=="-h" goto :usage
if "%~1"=="--" ( shift & goto :nmake_passthrough )
echo ERROR: unknown argument %~1
goto :usage
:args_done

where nmake >nul 2>nul || ( echo ERROR: nmake.exe not found. Run from a VS2022 Developer Command Prompt. & exit /b 1 )
where cl >nul 2>nul || ( echo ERROR: cl.exe not found. Run from a VS2022 Developer Command Prompt. & exit /b 1 )
where ml64 >nul 2>nul || ( echo ERROR: ml64.exe not found. Run from a VS2022 Developer Command Prompt. & exit /b 1 )
if not exist Makefile.msvc ( echo ERROR: Makefile.msvc not found in %CD%. & exit /b 1 )

echo === nmake /F Makefile.msvc %NMAKE_ARGS% ===
nmake /F Makefile.msvc %NMAKE_ARGS% || exit /b 1

echo.
echo Build finished.
echo   binaries: build-msvc\dist\bin
echo   libraries: build-msvc\dist\lib
echo   headers:   build-msvc\dist\include
exit /b 0

:nmake_passthrough
where nmake >nul 2>nul || ( echo ERROR: nmake.exe not found. Run from a VS2022 Developer Command Prompt. & exit /b 1 )
nmake /F Makefile.msvc %NMAKE_ARGS% %* || exit /b 1
exit /b 0

:usage
echo Usage: %~nx0 [--debug] [--static-crt] [--clean] [-- ...nmake args...]
exit /b 1
