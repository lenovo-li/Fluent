# 双仓库工作流程

本项目采用**私有开发库 + 公开代码库**的双仓库架构：

## 仓库划分

### 私有开发库（当前仓库）
- 位置：`D:\project\Fluent`
- 远程：`lenovo-li/Fluent`（私有）
- 内容：完整项目
  - 代码：`FluentUI/`、`tests/`、`examples/` 等
  - 文档：`docs/`（技术笔记、设计文档、路线图）
  - 开发记录：`.claude/`（任务状态、进度报告、分析文档）
  - 开发配置：`AGENTS.md`、`CLAUDE.md`

### 公开代码库
- 位置：`D:\project\Fluent-public`（首次同步时创建）
- 远程：`lenovo-li/Fluent-public`（公开）
- 内容：仅代码
  - 代码：`FluentUI/`、`tests/`、`examples/`
  - 构建：`CMakeLists.txt`、`Fluent.slnx`、`scripts/`
  - 基础文档：`README.md`（公开版）、`LICENSE`、`CONTRIBUTING.md`

## 日常工作流

### 1. 开发（私有库）

正常在私有库工作，自由提交：

```bash
cd /d/project/Fluent

# 修改代码
git add FluentUI/controls/Button.cpp
git commit -m "优化 Button 按下动画"

# 更新长期文档
git add docs/learn/button-animation.md
git commit -m "记录 Button 动画优化方案"

# 推送到私有库
git push origin master
```

### 2. 同步到公开库

当你完成一个功能/修复，想公开时：

```bash
# 运行同步脚本（默认同步到 ../Fluent-public）
./scripts/sync_to_public.sh

# 脚本会自动：
# - 复制代码文件到 ../Fluent-public
# - 排除 docs/, .claude/, AGENTS.md, CLAUDE.md, .github/
# - 使用 README.public.md 和 CONTRIBUTING.public.md 作为公开版
# - 清理构建产物（x64/, .vs/, *.obj）
# - 替换代码注释中的 AI 相关引用：
#     CLAUDE.md → project documentation
#     .claude/xxx.md → internal documentation
#     .claude/plans → internal design notes
# - 扫描残留，确保无 AI 痕迹泄露

# 进入公开库检查
cd ../Fluent-public
git status

# 验证 AI 痕迹已清理
grep -rniE 'claude|\.claude|anthropic' --include="*.h" --include="*.cpp" . | wc -l
# 应该输出 0

# 提交变更
git add .
git commit -m "Feature: 优化 Button 按下动画"

# 推送到公开库
git push origin master
```

### 3. 首次发布设置

第一次同步时需要创建公开仓库：

```bash
# 运行同步脚本
./scripts/sync_to_public.sh

# 进入公开库
cd ../Fluent-public

# 创建 GitHub 远程仓库（需要 gh CLI）
gh repo create lenovo-li/Fluent-public --public --source=. --remote=origin

# 推送
git add .
git commit -m "Initial public release"
git push -u origin master
```

或在 GitHub 网页手动创建仓库，然后：

```bash
cd ../Fluent-public
git remote add origin https://github.com/lenovo-li/Fluent-public.git
git add .
git commit -m "Initial public release"
git push -u origin master
```

## 同步策略

### 何时同步

- **功能完成后**：新控件、新特性已测试通过
- **重要修复后**：bug 修复、性能优化
- **不同步**：
  - 正在进行的实验（未完成）
  - 内部讨论和设计草案
  - 个人开发笔记和进度记录

### 同步内容

✅ **包含**
- 源代码（`FluentUI/`、`tests/`、`examples/`）
- 构建配置（`CMakeLists.txt`、`Fluent.slnx`、`scripts/check_*.py`）
- 开源文档（`README.public.md` → `README.md`、`CONTRIBUTING.public.md` → `CONTRIBUTING.md`、`LICENSE`）
- 项目配置（`.editorconfig`、`.gitignore`、`.gitattributes`）

❌ **排除**
- `docs/` —— 技术笔记、设计文档、路线图（包含开发进度）
- `.claude/` —— 任务状态、进度报告、分析文档
- `.github/` —— CI 配置（可能暴露工作流）
- `AGENTS.md`、`CLAUDE.md` —— AI 开发助手配置
- `scripts/sync_to_public.sh` —— 同步脚本自己（会暴露双仓库架构）
- `DISTRIBUTION_GUIDE.md`、`clean_for_distribution.bat`、`pack_cpp_project.py` —— 发布脚本
- 构建产物（`x64/`、`.vs/`、`*.obj`、`*.pdb`）

🔄 **自动转换**
- 代码注释中的引用自动替换：
  - `CLAUDE.md` → `project documentation`
  - `.claude/xxx.md` → `internal documentation`
  - `.claude/plans` → `internal design notes`
- `.gitignore` 中的 `.claude/` 行自动移除
- 同步后自动扫描残留，发现 AI 痕迹会中止并报告位置

## 自定义同步

如果需要调整同步内容，编辑 `scripts/sync_to_public.sh`：

```bash
# 添加要包含的文件/目录
INCLUDE_PATTERNS=(
    "FluentUI/"
    "新目录/"        # <-- 添加这里
    ...
)

# 添加要排除的内容
EXCLUDE_PATTERNS=(
    "docs/"
    "新的私有目录/"  # <-- 添加这里
    ...
)
```

## 注意事项

1. **单向同步**：私有库 → 公开库，公开库不回流到私有库
2. **检查敏感信息**：同步前确保代码中没有 API 密钥、个人路径等
3. **README 维护**：`README.public.md` 是公开版，移除内部开发信息
4. **提交独立**：私有库和公开库的提交历史完全独立
5. **备份**：同步脚本会覆盖公开库内容，首次使用前建议备份

## 故障排除

### 脚本执行失败

```bash
# 确保脚本可执行
chmod +x scripts/sync_to_public.sh

# 如果 bash 不可用，检查 git-bash 安装
which bash
```

### 公开库冲突

如果公开库有未提交变更：

```bash
cd ../Fluent-public
git status

# 备份变更
git stash

# 重新同步
cd ../Fluent
./scripts/sync_to_public.sh
```

### 同步后发现敏感信息

```bash
cd ../Fluent-public

# 删除敏感提交（本地）
git reset --hard HEAD~1

# 如果已推送，需要强制推送（危险！）
git push --force origin master
```

## 工具需求

- Git（已安装）
- Bash（git-bash，已安装）
- gh CLI（可选，用于创建远程仓库）

安装 gh CLI：

```bash
# Windows (Chocolatey)
choco install gh

# Windows (Scoop)
scoop install gh

# 或从 https://cli.github.com/ 下载
```

## 维护清单

定期检查：

- [ ] `README.public.md` 是否包含私有信息
- [ ] 同步脚本的包含/排除列表是否最新
- [ ] 公开库的 LICENSE 是否正确
- [ ] 公开库的 CONTRIBUTING.md 是否符合开源规范
