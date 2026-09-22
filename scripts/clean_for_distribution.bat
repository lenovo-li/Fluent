@echo off
REM ============================================================================
REM clean_for_distribution.bat - 双击入口
REM
REM 实际逻辑在 clean_for_distribution.ps1，这里只负责调起它。
REM 用 PowerShell 是因为需要算目录体积、并用 git 核对歧义目录是不是产物,
REM 这两件事批处理做不好。
REM
REM 双击本文件即可运行，默认清理仓库根（本脚本在 scripts\ 下，会自动往上一层）。
REM
REM 命令行用法（从仓库根执行，更灵活）:
REM   .\scripts\clean_for_distribution.ps1 -DryRun     只预览，不删除
REM   .\scripts\clean_for_distribution.ps1 -Yes        跳过确认
REM   .\scripts\clean_for_distribution.ps1 -KeepLogs   保留 *.log / *.tmp
REM   .\scripts\clean_for_distribution.ps1 -Path D:\other\project   清理别的项目
REM ============================================================================

setlocal

where pwsh >nul 2>&1
if %errorlevel%==0 (
    pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0clean_for_distribution.ps1" %*
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0clean_for_distribution.ps1" %*
)

echo.
pause
