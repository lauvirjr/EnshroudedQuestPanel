@echo off
chcp 65001 >nul
title Painel de Missoes - Estado
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0instalador.ps1" estado
echo.
pause
