@echo off
REM Convenience wrapper so the demo can be started from cmd.exe as well.
REM PowerShell is the supported entry point; this just forwards to it.
REM
REM   demo\run-demo.bat
REM   demo\run-demo.bat -Stage cir
REM   demo\run-demo.bat -Stage all -Pause

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-demo.ps1" %*
exit /b %ERRORLEVEL%
