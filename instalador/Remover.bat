@echo off
chcp 65001 >nul
title Painel de Missoes - Remover
cd /d "%~dp0"
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0instalador.ps1" remover
echo.
pause
