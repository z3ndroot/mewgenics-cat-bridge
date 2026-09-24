@echo off
rem Unloads cat_bridge.dll from the running game (optional: closing the game does the same).
python "%~dp0tools\cat_bridge_dev.py" eject
pause
