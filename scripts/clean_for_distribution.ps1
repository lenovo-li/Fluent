<#
.SYNOPSIS
    清理项目中的编译产物与 IDE 缓存，用于拷贝/分发前瘦身。

.DESCRIPTION
    判断标准：只删「一条构建命令就能重新生成」的东西。
    源码、项目文件、文档、版本库（.git）、Claude 配置（.claude）一律不动。

    分两档处理：
      A 档  确定是产物，直接删（x64\、.vs\、*.obj、*.user 等）
      B 档  名字有歧义的目录（bin\、build\、out\、Release\ 等）—— 这些名字
            也可能是真源码目录。若当前是 git 仓库，先用 git 确认目录内没有
            被跟踪的文件，确认是产物才删；有跟踪文件就跳过并报告。

    默认先扫描并列出清单+体积，等你确认后才动手。

.PARAMETER Path
    要清理的目录。默认为脚本所在目录的上一层（即仓库根），
    因为本脚本放在 scripts\ 子目录里。

.PARAMETER DryRun
    只扫描列出，不删除。

.PARAMETER Yes
    跳过确认提示，直接删除。用于脚本化调用。

.PARAMETER KeepLogs
    保留 *.log / *.tmp 文件。默认会删。

.EXAMPLE
    .\clean_for_distribution.ps1
    扫描脚本所在目录，列出清单后询问是否删除。

.EXAMPLE
    .\clean_for_distribution.ps1 -DryRun
    只看会删什么，不实际删除。

.EXAMPLE
    .\clean_for_distribution.ps1 -Path D:\some\project -Yes
    清理指定目录，不询问。
#>

[CmdletBinding()]
param(
    # 默认清理仓库根（脚本在 scripts\ 下，所以要往上一层），
    # 而不是 $PSScriptRoot —— 那样会清理 scripts\ 自己。
    [string] $Path = (Split-Path -Parent $PSScriptRoot),
    [switch] $DryRun,
    [switch] $Yes,
    [switch] $KeepLogs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# 清理目标定义
# ---------------------------------------------------------------------------

# A 档：目录名唯一指向构建产物或 IDE 缓存，无歧义，直接删
$SafeDirs = @(
    'x64', 'x86', 'Win32', 'ARM', 'ARM64'   # Visual C++ 平台输出
    '.vs', 'ipch'                            # Visual Studio 缓存 / 预编译头
    '__pycache__', '.pytest_cache', '.mypy_cache', '.ruff_cache'
    'CMakeFiles'
    '.gradle'
)

# B 档：目录名可能是产物，也可能是源码。删前用 git 核对
$CheckedDirs = @(
    'bin', 'obj'                                      # .NET / 通用
    'Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel' # C++ / CMake 配置输出
    'build', 'out', 'dist'                            # 通用构建目录
    'target'                                          # Rust / Maven
    'node_modules'                                    # npm，可 install 还原
    'packages'                                        # NuGet，可 restore 还原
    'vendor'                                          # Go / PHP
    '.venv', 'venv'                                   # Python 虚拟环境
)

# 文件：按扩展名匹配，全部是可重新生成的中间产物或用户本地设置
$SafeFilePatterns = @(
    '*.obj', '*.o', '*.lib', '*.exp', '*.ilk', '*.pch', '*.idb'
    '*.iobj', '*.ipdb', '*.tlog', '*.lastbuildstate', '*.unsuccessfulbuild'
    '*.user', '*.suo', '*.aps', '*.ncb', '*.sdf', '*.opensdf'
    '*.VC.db', '*.VC.VC.opendb'
    '*.pyc', '*.pyo'
    'Thumbs.db', 'desktop.ini'
)

$LogFilePatterns = @('*.log', '*.tmp')

# 绝不进入的目录 —— 版本库和工作状态，重建不出来
$ProtectedDirs = @('.git', '.svn', '.hg', '.claude')

# ---------------------------------------------------------------------------
# 辅助函数
# ---------------------------------------------------------------------------

function Test-ProtectedPath {
    param([string] $FullPath, [string] $Root)

    $rel = $FullPath.Substring($Root.Length).TrimStart('\', '/')
    foreach ($seg in ($rel -split '[\\/]')) {
        if ($ProtectedDirs -contains $seg) { return $true }
    }
    return $false
}

function Get-PathSize {
    param([string] $FullPath)

    try {
        if (Test-Path -LiteralPath $FullPath -PathType Container) {
            $sum = (Get-ChildItem -LiteralPath $FullPath -Recurse -File -Force `
                        -ErrorAction SilentlyContinue |
                    Measure-Object -Property Length -Sum).Sum
            return [int64]($sum ?? 0)
        }
        return [int64](Get-Item -LiteralPath $FullPath -Force).Length
    } catch {
        return [int64]0
    }
}

function Format-Size {
    param([int64] $Bytes)

    if ($Bytes -ge 1GB) { return '{0:N2} GB' -f ($Bytes / 1GB) }
    if ($Bytes -ge 1MB) { return '{0:N1} MB' -f ($Bytes / 1MB) }
    if ($Bytes -ge 1KB) { return '{0:N0} KB' -f ($Bytes / 1KB) }
    return "$Bytes B"
}

# 判断某个歧义目录是不是构建产物。
# 依据：git 仓库里，产物目录被 .gitignore 忽略，不含任何被跟踪的文件。
# 若目录里有被跟踪的文件，说明它是源码目录（比如手写的 build 脚本目录），必须跳过。
function Test-IsBuildOutput {
    param([string] $DirPath, [bool] $IsGitRepo, [string] $Root)

    if (-not $IsGitRepo) {
        # 非 git 仓库无法核对，一律保留，由用户手动处理
        return @{ IsOutput = $false; Reason = '非 git 仓库，无法确认，已跳过' }
    }

    Push-Location -LiteralPath $Root
    try {
        # --error-unmatch 让「没有任何跟踪文件」以非零退出码表达
        $tracked = & git ls-files --error-unmatch -- $DirPath 2>$null
        $hasTracked = ($LASTEXITCODE -eq 0) -and ($tracked)

        if ($hasTracked) {
            $count = @($tracked).Count
            return @{ IsOutput = $false; Reason = "含 $count 个已跟踪文件，判定为源码目录" }
        }
        return @{ IsOutput = $true; Reason = '无已跟踪文件' }
    } finally {
        Pop-Location
    }
}

# ---------------------------------------------------------------------------
# 扫描
# ---------------------------------------------------------------------------

$Root = (Resolve-Path -LiteralPath $Path).Path

Write-Host ''
Write-Host '========================================' -ForegroundColor Cyan
Write-Host ' 项目清理工具' -ForegroundColor Cyan
Write-Host '========================================' -ForegroundColor Cyan
Write-Host ''
Write-Host "目标目录: $Root"

$IsGitRepo = $false
Push-Location -LiteralPath $Root
try {
    & git rev-parse --is-inside-work-tree 2>$null | Out-Null
    $IsGitRepo = ($LASTEXITCODE -eq 0)
} catch {
    $IsGitRepo = $false
} finally {
    Pop-Location
}

if ($IsGitRepo) {
    Write-Host "版本库:   git 仓库（歧义目录将用 git 核对）" -ForegroundColor DarkGray
} else {
    Write-Host "版本库:   非 git 仓库（歧义目录一律保留）" -ForegroundColor Yellow
}

$sizeBefore = Get-PathSize -FullPath $Root
Write-Host "当前体积: $(Format-Size $sizeBefore)"
Write-Host ''
Write-Host '正在扫描…' -ForegroundColor DarkGray

$toDelete = [System.Collections.Generic.List[object]]::new()
$skipped  = [System.Collections.Generic.List[object]]::new()

# --- 目录 ---
# 收集所有候选目录，按路径深度倒序处理，避免先删父目录导致子目录路径失效
$allDirs = Get-ChildItem -LiteralPath $Root -Recurse -Directory -Force -ErrorAction SilentlyContinue |
           Where-Object { -not (Test-ProtectedPath -FullPath $_.FullName -Root $Root) } |
           Sort-Object { $_.FullName.Length } -Descending

# 已登记待删的目录前缀，用于跳过其子目录（父目录删了子目录自然没了）
$claimedPrefixes = [System.Collections.Generic.List[string]]::new()

function Test-AlreadyClaimed {
    param([string] $FullPath)
    foreach ($p in $claimedPrefixes) {
        if ($FullPath.StartsWith($p, [StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    return $false
}

foreach ($dir in $allDirs) {
    $name = $dir.Name

    if ($SafeDirs -contains $name) {
        $toDelete.Add([pscustomobject]@{
            Type = 'Dir'; Path = $dir.FullName
            Size = (Get-PathSize -FullPath $dir.FullName); Note = '构建产物/IDE 缓存'
        })
        $claimedPrefixes.Add($dir.FullName + '\')
        continue
    }

    if ($CheckedDirs -contains $name) {
        $verdict = Test-IsBuildOutput -DirPath $dir.FullName -IsGitRepo $IsGitRepo -Root $Root
        if ($verdict.IsOutput) {
            $toDelete.Add([pscustomobject]@{
                Type = 'Dir'; Path = $dir.FullName
                Size = (Get-PathSize -FullPath $dir.FullName); Note = $verdict.Reason
            })
            $claimedPrefixes.Add($dir.FullName + '\')
        } else {
            $skipped.Add([pscustomobject]@{
                Path = $dir.FullName; Reason = $verdict.Reason
            })
        }
    }
}

# 过滤掉已被父目录覆盖的条目
$dirEntries = @($toDelete | Where-Object { $_.Type -eq 'Dir' })
$toDelete = [System.Collections.Generic.List[object]]::new()
foreach ($e in ($dirEntries | Sort-Object { $_.Path.Length })) {
    $covered = $false
    foreach ($kept in $toDelete) {
        if ($e.Path.StartsWith($kept.Path + '\', [StringComparison]::OrdinalIgnoreCase)) {
            $covered = $true; break
        }
    }
    if (-not $covered) { $toDelete.Add($e) }
}

# --- 文件 ---
$filePatterns = if ($KeepLogs) { $SafeFilePatterns } else { $SafeFilePatterns + $LogFilePatterns }

foreach ($pattern in $filePatterns) {
    Get-ChildItem -LiteralPath $Root -Recurse -File -Force -Filter $pattern -ErrorAction SilentlyContinue |
        Where-Object {
            -not (Test-ProtectedPath -FullPath $_.FullName -Root $Root) -and
            -not (Test-AlreadyClaimed -FullPath $_.FullName)
        } |
        ForEach-Object {
            $toDelete.Add([pscustomobject]@{
                Type = 'File'; Path = $_.FullName; Size = $_.Length; Note = $pattern
            })
        }
}

# ---------------------------------------------------------------------------
# 报告
# ---------------------------------------------------------------------------

Write-Host ''

if ($toDelete.Count -eq 0) {
    Write-Host '没有找到可清理的内容，项目已经是干净的。' -ForegroundColor Green
    Write-Host ''
    exit 0
}

$dirs  = @($toDelete | Where-Object { $_.Type -eq 'Dir'  })
$files = @($toDelete | Where-Object { $_.Type -eq 'File' })
$total = ($toDelete | Measure-Object -Property Size -Sum).Sum

if ($dirs.Count -gt 0) {
    Write-Host "将删除 $($dirs.Count) 个目录：" -ForegroundColor Yellow
    foreach ($d in ($dirs | Sort-Object Size -Descending)) {
        $rel = $d.Path.Substring($Root.Length).TrimStart('\')
        Write-Host ('  {0,10}  {1}' -f (Format-Size $d.Size), $rel)
        Write-Host ('              └─ {0}' -f $d.Note) -ForegroundColor DarkGray
    }
    Write-Host ''
}

if ($files.Count -gt 0) {
    $fileSize = ($files | Measure-Object -Property Size -Sum).Sum
    Write-Host "将删除 $($files.Count) 个文件（合计 $(Format-Size $fileSize)）：" -ForegroundColor Yellow
    $files | Group-Object Note | Sort-Object Count -Descending | ForEach-Object {
        Write-Host ('  {0,5} 个  {1}' -f $_.Count, $_.Name)
    }
    Write-Host ''
}

if ($skipped.Count -gt 0) {
    Write-Host "已跳过 $($skipped.Count) 个名字像产物、但实际不是的目录：" -ForegroundColor Cyan
    foreach ($s in $skipped) {
        $rel = $s.Path.Substring($Root.Length).TrimStart('\')
        Write-Host ('  {0}' -f $rel)
        Write-Host ('    └─ {0}' -f $s.Reason) -ForegroundColor DarkGray
    }
    Write-Host ''
}

Write-Host '保留：源码、项目文件、文档、.git\、.claude\' -ForegroundColor DarkGray
Write-Host ''
Write-Host "预计释放 $(Format-Size $total)，清理后约 $(Format-Size ($sizeBefore - $total))" -ForegroundColor Green
Write-Host ''

if ($DryRun) {
    Write-Host '[DryRun] 只是预览，未删除任何文件。' -ForegroundColor Cyan
    Write-Host ''
    exit 0
}

if (-not $Yes) {
    $answer = Read-Host '确认删除以上内容？(y/N)'
    if ($answer -notmatch '^[Yy]') {
        Write-Host ''
        Write-Host '已取消，未删除任何文件。' -ForegroundColor Yellow
        Write-Host ''
        exit 0
    }
}

# ---------------------------------------------------------------------------
# 执行
# ---------------------------------------------------------------------------

Write-Host ''
$okCount = 0
$failed = [System.Collections.Generic.List[object]]::new()

foreach ($item in $toDelete) {
    try {
        Remove-Item -LiteralPath $item.Path -Recurse -Force -ErrorAction Stop
        $okCount++
    } catch {
        $failed.Add([pscustomobject]@{ Path = $item.Path; Error = $_.Exception.Message })
    }
}

Write-Host "已删除 $okCount 项。" -ForegroundColor Green

if ($failed.Count -gt 0) {
    Write-Host ''
    Write-Host "$($failed.Count) 项删除失败（通常是文件被占用 —— 关闭 Visual Studio 或正在运行的程序后重试）：" -ForegroundColor Red
    foreach ($f in $failed) {
        $rel = $f.Path.Substring($Root.Length).TrimStart('\')
        Write-Host "  $rel" -ForegroundColor Red
        Write-Host "    └─ $($f.Error)" -ForegroundColor DarkGray
    }
}

$sizeAfter = Get-PathSize -FullPath $Root
Write-Host ''
Write-Host '========================================' -ForegroundColor Cyan
Write-Host (' 清理前: {0}' -f (Format-Size $sizeBefore))
Write-Host (' 清理后: {0}' -f (Format-Size $sizeAfter))
Write-Host (' 已释放: {0}' -f (Format-Size ($sizeBefore - $sizeAfter))) -ForegroundColor Green
Write-Host '========================================' -ForegroundColor Cyan
Write-Host ''
