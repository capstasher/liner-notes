@echo off
setlocal EnableExtensions
title LinerNotes installer

rem --- elevate (registration writes to HKLM) ---
net session >nul 2>&1
if errorlevel 1 (
  echo Requesting administrator rights...
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)

rem --- locate the DLLs: next to this script, or ..\dist in the source tree ---
set "SRCDIR=%~dp0"
if not exist "%SRCDIR%LinerNotes64.dll" set "SRCDIR=%~dp0..\dist\"
if not exist "%SRCDIR%LinerNotes32.dll" (
  echo Could not find LinerNotes32.dll / LinerNotes64.dll next to this script.
  goto :fail
)

tasklist /FI "IMAGENAME eq wmplayer.exe" 2>nul | find /I "wmplayer.exe" >nul
if not errorlevel 1 (
  echo Windows Media Player is running. Close it, then press any key.
  pause >nul
)

rem --- work out native vs 32-bit paths, even if launched from a 32-bit cmd ---
set "SYSNATIVE=%SystemRoot%\System32"
if exist "%SystemRoot%\Sysnative\regsvr32.exe" set "SYSNATIVE=%SystemRoot%\Sysnative"
set "PF=%ProgramFiles%"
if defined ProgramW6432 set "PF=%ProgramW6432%"
set "DEST=%PF%\LinerNotes"
set "IS64=0"
if exist "%SystemRoot%\SysWOW64\regsvr32.exe" set "IS64=1"

if not exist "%DEST%" mkdir "%DEST%" || goto :fail
copy /Y "%SRCDIR%LinerNotes32.dll" "%DEST%\" >nul || goto :fail
if "%IS64%"=="1" copy /Y "%SRCDIR%LinerNotes64.dll" "%DEST%\" >nul || goto :fail

if "%IS64%"=="1" (
  echo Registering 64-bit plug-in...
  "%SYSNATIVE%\regsvr32.exe" /s "%DEST%\LinerNotes64.dll" || goto :fail
  echo Registering 32-bit plug-in...
  "%SystemRoot%\SysWOW64\regsvr32.exe" /s "%DEST%\LinerNotes32.dll" || goto :fail
) else (
  echo Registering 32-bit plug-in...
  "%SYSNATIVE%\regsvr32.exe" /s "%DEST%\LinerNotes32.dll" || goto :fail
)

echo.
echo Installed to "%DEST%".
echo Noto is ready! Start Windows Media Player and pop a CD in.
echo Her window: Tools ^> Options ^> Plug-ins ^> Background ^> LinerNotes ^> Properties.
pause
exit /b 0

:fail
echo.
echo Installation failed (error %errorlevel%).
pause
exit /b 1
