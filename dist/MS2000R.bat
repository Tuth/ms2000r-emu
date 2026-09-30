@echo off
rem MS2000R emulator - standalone GUI, no log (the console output goes to NUL).
rem Keep this file beside ms2000_emulator.exe. The ROMs (flash.bin, full FW\boot-362.ms2000.bin, optional
rem hd44780_a00.bin) go in the same folder, or in a folder above it, or wherever MS2K_HOME points.
rem What it writes there: thin_gui.ini (settings), ms2000.nvram, the flash state after a WRITE, recordings\*.wav.
cd /d "%~dp0"
start "MS2000R" /min cmd /c "ms2000_emulator.exe --gui > NUL 2>&1"
