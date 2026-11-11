@echo off
rem Dark Cloud Co-op: sets up what Steam joins need (Steam running, Valve's steam_api64.dll, Spacewar), asking before each
rem change. Only when you run it: the launcher and the game never download or install anything. See launcher\steam-setup.ps1.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0launcher\steam-setup.ps1"
if not defined DCO_STEAM_SETUP_ANSWERS pause
