﻿@echo off
chcp 65001 >nul
REM ============================================================================
REM clean_for_distribution.bat - 清理 FluentUI 项目用于分发
REM
REM 删除构建产物和 IDE 缓存，保留源代码、文档、版本库和工作状态。
REM 不依赖 git，可在导出的源码包（无 .git 目录）里直接使用。
REM
REM 判断标准：只删「一条编译命令就能重新生成」的东西。
REM .git\ 和 .claude\ 都不满足这个标准，所以都不动。
REM
REM 注意：MSBuild 会在每个项目目录下生成自己的 x64\ 中间目录，
REM 所以必须递归删除，只删根目录那一个会漏掉约 1 GB。
REM ============================================================================

setlocal enabledelayedexpansion

echo ========================================
echo FluentUI 项目清理工具
echo ========================================
echo.
echo 将删除以下内容：
echo   - 所有 x64\ 目录       (递归，含各子项目的中间目录)
echo   - .vs\                 (Visual Studio 缓存)
echo   - TestPicture\         (测试图片)
echo   - *.user / *.suo       (用户配置)
echo   - *.tmp / *.log        (临时文件与日志)
echo.
echo 保留以下内容：
echo   - 所有源代码           (.h / .cpp)
echo   - 项目文件             (.vcxproj / .slnx / CMakeLists.txt)
echo   - 文档与脚本           (docs\ / scripts\ / tools\)
echo   - .git\                (版本库，本脚本不动它)
echo   - .claude\             (项目配置、任务状态、会话缓存)
echo.

choice /C YN /M "确认继续清理吗"
if errorlevel 2 goto :abort

echo.
echo [1/5] 递归删除所有 x64\ 构建目录
set "count=0"
for /d /r %%d in (x64) do (
    if exist "%%d" (
        rd /s /q "%%d" 2>nul
        if not exist "%%d" (
            set /a count+=1
            echo       已删除 %%d
        )
    )
)
if !count!==0 (
    echo       没有找到 x64\ 目录，跳过
) else (
    echo       共删除 !count! 个目录
)

echo.
echo [2/5] 删除 Visual Studio 缓存 .vs\
if exist ".vs" (
    rd /s /q ".vs" 2>nul
    echo       已删除 .vs\
) else (
    echo       .vs\ 不存在，跳过
)

REM ---------------------------------------------------------------------------
REM 本脚本刻意不动 .claude\ 。
REM
REM 那里放的是项目级的 Claude Code 数据：settings.json（权限、钩子）、
REM TASK_STATE.md（长任务断点）、以及会话产出的分析笔记。这些是工作状态，
REM 重建不出来，删掉就没了 —— 和 x64\ 那种一条编译命令就能重新生成的
REM 产物性质完全不同。
REM
REM 它已被 .gitignore 忽略，所以既不会进版本库，也不会被 git archive
REM 打进分发包。想手动排除时用压缩工具的 exclude 即可。
REM ---------------------------------------------------------------------------

echo.
echo [3/5] 删除测试图片目录 TestPicture\
if exist "TestPicture" (
    rd /s /q "TestPicture" 2>nul
    echo       已删除 TestPicture\
) else (
    echo       TestPicture\ 不存在，跳过
)

echo.
echo [4/5] 删除用户配置文件 (*.user, *.suo)
set "count=0"
for /r %%f in (*.user *.suo) do (
    if exist "%%f" (
        del /f /q "%%f" 2>nul
        set /a count+=1
    )
)
if !count!==0 (
    echo       没有找到，跳过
) else (
    echo       共删除 !count! 个文件
)

echo.
echo [5/5] 删除临时文件与日志 (*.tmp, *.log, SessionLog*.txt)
set "count=0"
for /r %%f in (*.tmp *.log SessionLog*.txt) do (
    if exist "%%f" (
        del /f /q "%%f" 2>nul
        set /a count+=1
    )
)
if !count!==0 (
    echo       没有找到，跳过
) else (
    echo       共删除 !count! 个文件
)

echo.
echo ========================================
echo 清理完成
echo ========================================
echo.
echo 剩余内容大小：
for /f "delims=" %%s in ('powershell -NoProfile -Command "$b=(Get-ChildItem -Recurse -File -Force -EA SilentlyContinue ^| Measure-Object Length -Sum).Sum; '{0:N1} MB' -f ($b/1MB)"') do echo       %%s
echo.
echo 打包方式：
echo   带版本历史   - 直接压缩整个目录
echo   仅源码       - git archive -o FluentUI.zip HEAD
echo.
echo 重新构建：
echo   msbuild Fluent.slnx -p:Configuration=Release -p:Platform=x64
echo.
pause
goto :eof

:abort
echo.
echo 已取消，未删除任何文件。
echo.
pause
