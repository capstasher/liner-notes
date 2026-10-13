@echo off
setlocal EnableExtensions
title LinerNotes uninstaller

net session >nul 2>&1
if errorlevel 1 (
  echo Requesting administrator rights...
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)

tasklist /FI "IMAGENAME eq wmplayer.exe" 2>nul | find /I "wmplayer.exe" >nul
if not errorlevel 1 (
  echo Windows Media Player is running. Close it, then press any key.
  pause >nul
)

set "SYSNATIVE=%SystemRoot%\System32"
if exist "%SystemRoot%\Sysnative\regsvr32.exe" set "SYSNATIVE=%SystemRoot%\Sysnative"
set "PF=%ProgramFiles%"
if defined ProgramW6432 set "PF=%ProgramW6432%"
set "DEST=%PF%\LinerNotes"

if exist "%SystemRoot%\SysWOW64\regsvr32.exe" (
  if exist "%DEST%\LinerNotes64.dll" "%SYSNATIVE%\regsvr32.exe" /u /s "%DEST%\LinerNotes64.dll"
  if exist "%DEST%\LinerNotes32.dll" "%SystemRoot%\SysWOW64\regsvr32.exe" /u /s "%DEST%\LinerNotes32.dll"
) else (
  if exist "%DEST%\LinerNotes32.dll" "%SYSNATIVE%\regsvr32.exe" /u /s "%DEST%\LinerNotes32.dll"
)

rd /s /q "%DEST%" 2>nul
if exist "%DEST%" echo Some files are still in use; reboot and delete "%DEST%" manually.

echo.
echo LinerNotes removed. Bye for now, from Noto.
echo Your settings (HKCU\Software\LinerNotes), log and
echo lookup cache (%LOCALAPPDATA%\LinerNotes) were left in place.
pause
