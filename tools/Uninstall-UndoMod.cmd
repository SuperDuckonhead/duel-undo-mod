@echo off
setlocal
chcp 65001 >nul
if not exist "%~dp0undo-mod\Uninstall-UndoMod.ps1" (
  echo 缺少卸载工具：undo-mod\Uninstall-UndoMod.ps1
  echo 未删除任何文件。
  pause
  exit /b 1
)
rem Leave the batch context before PowerShell removes this launcher.
(goto) 2>nul & (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0undo-mod\Uninstall-UndoMod.ps1" -Interactive
  if errorlevel 1 (
    echo.
    pause
    exit /b 1
  )
  echo.
  pause
  exit /b 0
)
