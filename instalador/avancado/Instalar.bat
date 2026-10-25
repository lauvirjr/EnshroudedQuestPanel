@echo off
chcp 65001 >nul
title Painel de Missoes - Instalar
cd /d "%~dp0"
echo.
echo   Instalando o Painel de Missoes para Enshrouded...
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\instalador.ps1" instalar
if errorlevel 1 (
  echo.
  echo   A instalacao nao terminou. Confira a mensagem acima.
  echo   Se o jogo estiver aberto, feche e tente de novo.
)
echo.
pause
