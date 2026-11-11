@echo off
rem Dark Cloud Co-op: builds the mod on this PC into your Chronicle folder's mods\Dark Cloud Co-op (see setup\setup.ps1
rem for every step). Needs 64-bit Windows 10 or 11, an internet connection, Chronicle at the commit this release names (or
rem none: it sets Chronicle up), and your own Dark Cloud disc image (NTSC, US 1.02) when Chronicle has no game data yet.
rem (Windows PowerShell finds its own modules: a PowerShell 7 module path inherited from where it was started breaks it.)
set "PSModulePath="
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup\setup.ps1"
set CODE=%ERRORLEVEL%
if not defined DCO_SETUP_YES pause
exit /b %CODE%
