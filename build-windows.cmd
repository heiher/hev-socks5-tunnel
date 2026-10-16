@echo off
rem Native MSVC/Windows build wrapper. Use -Arch x64 (default), x86 or
rem arm64; see Makefile.msvc / build-windows.ps1.
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-windows.ps1" %*
exit /b %ERRORLEVEL%
