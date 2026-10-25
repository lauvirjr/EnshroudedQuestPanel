@echo off
chcp 65001 >nul
title Painel de Missoes - Atualizar
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0atualizar.ps1"
