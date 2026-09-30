@echo off
rem MS2000R emulator - standalone GUI with a log: ms2000_gui.log beside this file (for bug reports).
cd /d "%~dp0"
start "MS2000R" /min cmd /c "ms2000_emulator.exe --gui > ms2000_gui.log 2>&1"
