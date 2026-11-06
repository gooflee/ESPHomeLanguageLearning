@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-Firmware.ps1" -Run %*
