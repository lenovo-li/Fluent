#!/usr/bin/env bash
# sync_to_public.sh - 将私有开发库同步到公开代码库
# 
# 用法：
#   ./scripts/sync_to_public.sh [public_repo_path]
#
# 功能：
#   1. 复制代码文件到临时目录
#   2. 排除私有文档 (docs/, .claude/, AGENTS.md, CLAUDE.md)
#   3. 创建/更新公开仓库的 git 历史

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRIVATE_REPO="$(cd "$SCRIPT_DIR/.." && pwd)"
PUBLIC_REPO="${1:-$PRIVATE_REPO/../Fluent-public}"

# 需要同步到公开仓库的文件/目录
INCLUDE_PATTERNS=(
    "FluentUI/"
    "FluentUIDemo/"
    "FluentUIBench/"
    "FluentSettings/"
    "tests/"
    "examples/"
    "scripts/"
    "cmake/"
    "CMakeLists.txt"
    "Fluent.slnx"
    "LICENSE"
    ".gitignore"
    ".gitattributes"
    ".editorconfig"
    "CONTRIBUTING.public.md"
    "README.public.md"
)

# 排除的私有内容（注释，实际通过 INCLUDE_PATTERNS 白名单控制）
# 以下内容不会被同步：
# - docs/              开发文档和进度记录
# - .claude/           AI 助手工作目录
# - .github/           CI 配置（可能暴露内部工作流）
# - AGENTS.md          AI 助手配置
# - CLAUDE.md          开发约束文档
# - DISTRIBUTION_GUIDE.md, clean_for_distribution.bat, pack_cpp_project.py
# - .vs/, x64/, *.user, *.suo  构建产物

echo "================================================"
echo "同步私有开发库 -> 公开代码库"
echo "================================================"
echo "私有库: $PRIVATE_REPO"
echo "公开库: $PUBLIC_REPO"
echo ""

# 检查是否需要创建 README.public.md
if [[ ! -f "$PRIVATE_REPO/README.public.md" ]]; then
    echo "警告: 未找到 README.public.md"
    echo "建议创建一个面向公众的 README，排除内部开发信息"
    echo ""
fi

# 创建临时目录
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

echo "复制文件到临时目录..."
cd "$PRIVATE_REPO"

# 复制包含的文件
for pattern in "${INCLUDE_PATTERNS[@]}"; do
    if [[ -e "$pattern" ]]; then
        parent_dir="$TEMP_DIR/$(dirname "$pattern")"
        mkdir -p "$parent_dir"
        cp -r "$pattern" "$parent_dir/"
        echo "  ✓ $pattern"
    fi
done

# 重命名公开版文件
if [[ -f "$TEMP_DIR/README.public.md" ]]; then
    mv "$TEMP_DIR/README.public.md" "$TEMP_DIR/README.md"
    echo "  ✓ README.public.md -> README.md"
else
    echo "  ⚠ 缺少 README.public.md"
fi

if [[ -f "$TEMP_DIR/CONTRIBUTING.public.md" ]]; then
    mv "$TEMP_DIR/CONTRIBUTING.public.md" "$TEMP_DIR/CONTRIBUTING.md"
    echo "  ✓ CONTRIBUTING.public.md -> CONTRIBUTING.md"
else
    echo "  ⚠ 缺少 CONTRIBUTING.public.md"
fi

echo ""
echo "清理构建产物..."
cd "$TEMP_DIR"
find . -type d -name "x64" -exec rm -rf {} + 2>/dev/null || true
find . -type d -name ".vs" -exec rm -rf {} + 2>/dev/null || true
find . -type f -name "*.user" -delete 2>/dev/null || true
find . -type f -name "*.obj" -delete 2>/dev/null || true
find . -type f -name "*.pdb" -delete 2>/dev/null || true
find . -type f -name "*.idb" -delete 2>/dev/null || true
find . -type f -name "*.ilk" -delete 2>/dev/null || true

# 移除不该公开的脚本（会暴露私有库结构）
rm -f scripts/sync_to_public.sh
echo "  ✓ 移除 sync_to_public.sh"

echo ""
echo "清理 AI 相关痕迹..."

# 清理代码注释中的 CLAUDE.md 和 .claude/ 引用
find . -type f \( -name "*.h" -o -name "*.cpp" -o -name "*.py" \) -exec sed -i \
    -e 's|CLAUDE\.md|project documentation|g' \
    -e 's|\.claude/[a-zA-Z0-9_/-]*\.md|internal documentation|g' \
    -e 's|\.claude/plans|internal design notes|g' \
    {} +

# 清理 .gitignore 中的 .claude 相关行
if [[ -f ".gitignore" ]]; then
    sed -i \
        -e '/Claude Code local config/d' \
        -e '/^\.claude\//d' \
        ".gitignore"
fi

echo "  ✓ 移除代码注释中的 CLAUDE.md / .claude/ 引用"
echo "  ✓ 清理 .gitignore"

# 最终扫描：确认没有 AI 痕迹残留
echo ""
echo "扫描残留..."
LEAKS=$(grep -rniE 'claude|anthropic|copilot|chatgpt|openai|hermes|co-authored-by' . \
    --exclude-dir=.git 2>/dev/null || true)
if [[ -n "$LEAKS" ]]; then
    echo "错误: 发现 AI 相关内容残留，同步中止:"
    echo "$LEAKS" | head -20
    exit 1
fi
echo "  ✓ 无残留"

echo ""
echo "准备公开仓库..."

# 复制 temp -> 目标，包含点文件（cp 的 * 不匹配点开头条目，用 tar 管道）
copy_tree_into() {
    local dest="$1"
    ( cd "$TEMP_DIR" && tar cf - . ) | ( cd "$dest" && tar xf - )
}

if [[ -d "$PUBLIC_REPO/.git" ]]; then
    echo "公开仓库已存在，更新内容..."
    cd "$PUBLIC_REPO"

    if [[ -n "$(git status --porcelain)" ]]; then
        echo ""
        echo "错误: 公开仓库有未提交的变更，同步会覆盖它们。"
        echo "请先 commit 或 stash:"
        git status --short
        exit 1
    fi

    # 删除所有受版本控制的内容（保留 .git）
    find . -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +

    copy_tree_into "$PUBLIC_REPO"

    echo ""
    echo "变更摘要:"
    git status --short

else
    echo "初始化新的公开仓库..."
    mkdir -p "$PUBLIC_REPO"
    cd "$PUBLIC_REPO"

    copy_tree_into "$PUBLIC_REPO"

    git init -q
    git add -A

    echo ""
    echo "文件已准备，待提交:"
    git status --short | head -20
    echo "  ... 共 $(git diff --cached --name-only | wc -l) 个文件"
fi

echo ""
echo "================================================"
echo "下一步操作:"
echo "================================================"
echo "1. 检查公开仓库内容:"
echo "   cd $PUBLIC_REPO"
echo "   git status"
echo ""
echo "2. 提交变更:"
echo "   git add ."
echo "   git commit -m \"Sync from private development repo\""
echo ""
echo "3. 首次推送到 GitHub（如果仓库不存在）:"
echo "   gh repo create lenovo-li/Fluent-public --public --source=. --remote=origin"
echo "   git push -u origin master"
echo ""
echo "4. 后续推送:"
echo "   git push origin master"
echo ""
