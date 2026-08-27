#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""Pack a C/C++ project into one Markdown file.

The generated Markdown contains:
- Project overview and file tree
- The complete text of every included file
- Optional line numbers
- Include dependencies and reverse references
- Lightweight symbol index
- Skipped-file report

Examples:
  python pack_cpp_project_full.py "C:\\Users\\me\\Project\\Fluent"
  python pack_cpp_project_full.py . -o project_bundle.md
  python pack_cpp_project_full.py . --no-line-numbers
  python pack_cpp_project_full.py . --include-json-hpp
  python pack_cpp_project_full.py . --all-text-files
  python pack_cpp_project_full.py . --max-file-kb 0

Notes:
- --max-file-kb 0 means no per-file size limit.
- By default, common build/generated/binary/secret files are excluded.
- --all-text-files includes every decodable text file, even if its extension is
  not in the default extension list.
"""

from __future__ import annotations

import argparse
import datetime as dt
import fnmatch
import hashlib
import os
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any


DEFAULT_EXTENSIONS = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".inc",
    ".m", ".mm", ".rc", ".def", ".idl",
    ".vcxproj", ".filters", ".props", ".targets", ".sln",
    ".cmake", ".txt", ".md", ".json", ".xml", ".manifest", ".natvis",
    ".py", ".ps1", ".bat", ".cmd", ".sh", ".yml", ".yaml", ".toml",
}

DEFAULT_EXCLUDE_DIRS = {
    ".git", ".svn", ".hg", ".vs", ".idea", ".vscode",
    "Debug", "Release", "RelWithDebInfo", "MinSizeRel",
    "debug", "release", "build", "Build", "out", "bin", "obj",
    "x64", "x86", "arm", "arm64", "packages", "node_modules",
    "CMakeFiles", "_deps", "generated", "Generated", "__pycache__",
}

DEFAULT_EXCLUDE_GLOBS = {
    "*.user", "*.suo", "*.ncb", "*.sdf", "*.opensdf", "*.VC.db", "*.VC.VC.opendb",
    "*.pdb", "*.obj", "*.o", "*.lib", "*.a", "*.dll", "*.exe", "*.ilk", "*.pch",
    "*.idb", "*.ipch", "*.tlog", "*.lastbuildstate", "*.cache", "*.log",
    "*.png", "*.jpg", "*.jpeg", "*.gif", "*.ico", "*.bmp", "*.webp",
    "*.pdf", "*.zip", "*.7z", "*.rar",
    ".env", ".env.*", "*.pem", "*.key", "*.pfx", "*.p12",
}

LANGUAGE_BY_EXT = {
    ".c": "c", ".h": "cpp", ".cc": "cpp", ".cpp": "cpp", ".cxx": "cpp",
    ".hh": "cpp", ".hpp": "cpp", ".hxx": "cpp", ".inl": "cpp", ".inc": "cpp",
    ".m": "objective-c", ".mm": "objective-cpp", ".rc": "rc", ".def": "text",
    ".idl": "cpp", ".vcxproj": "xml", ".filters": "xml", ".props": "xml",
    ".targets": "xml", ".sln": "text", ".cmake": "cmake", ".txt": "text",
    ".md": "markdown", ".json": "json", ".xml": "xml", ".manifest": "xml",
    ".natvis": "xml", ".py": "python", ".ps1": "powershell", ".bat": "bat",
    ".cmd": "bat", ".sh": "bash", ".yml": "yaml", ".yaml": "yaml", ".toml": "toml",
}

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.MULTILINE)

SYMBOL_PATTERNS = [
    (
        "type",
        re.compile(
            r'^\s*(?:template\s*<[^;{}]*>\s*)?'
            r'(?:class|struct|enum(?:\s+class)?)\s+([A-Za-z_]\w*)',
            re.MULTILINE,
        ),
    ),
    (
        "macro",
        re.compile(r'^\s*#\s*define\s+([A-Za-z_]\w*)', re.MULTILINE),
    ),
    (
        "function",
        re.compile(
            r'^\s*(?!if\b|for\b|while\b|switch\b|catch\b)'
            r'(?:[\w:<>&*~,[\]\s]+\s+)?'
            r'([A-Za-z_]\w*(?:::\w+)*)\s*'
            r'\([^;{}]*\)\s*'
            r'(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:final\s*)?\{',
            re.MULTILINE,
        ),
    ),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="将 C/C++ 项目完整汇总为一个 Markdown 文件")
    parser.add_argument("root", nargs="?", default=".", help="项目根目录，默认当前目录")
    parser.add_argument("-o", "--output", default="project_bundle.md", help="输出 Markdown 路径")
    parser.add_argument(
        "--max-file-kb",
        type=int,
        default=0,
        help="单文件大小上限，单位 KB；0 表示不限制，默认 0",
    )
    parser.add_argument(
        "--extensions",
        nargs="*",
        default=[],
        help="额外扩展名，如 .cu .cuh；不会替代默认扩展名",
    )
    parser.add_argument("--exclude-dir", action="append", default=[], help="额外排除目录，可重复")
    parser.add_argument("--exclude", action="append", default=[], help="额外排除 glob，可重复")
    parser.add_argument("--include-json-hpp", action="store_true", help="包含第三方大型 json.hpp")
    parser.add_argument("--all-text-files", action="store_true", help="包含所有可解码文本文件，不按扩展名过滤")
    parser.add_argument("--no-line-numbers", action="store_true", help="文件内容不添加行号")
    parser.add_argument("--include-hidden", action="store_true", help="包含隐藏目录和隐藏文件")
    parser.add_argument("--no-analysis", action="store_true", help="不生成 include 关系和符号索引，只输出树和完整文件内容")
    return parser.parse_args()


def normalize_extension(value: str) -> str:
    value = value.strip().lower()
    if not value:
        return value
    return value if value.startswith(".") else "." + value


def is_hidden_relative(path: Path, root: Path) -> bool:
    return any(part.startswith(".") for part in path.relative_to(root).parts)


def excluded(
    path: Path,
    root: Path,
    excluded_dirs: set[str],
    globs: set[str],
    include_hidden: bool,
) -> bool:
    rel = path.relative_to(root)
    if not include_hidden and is_hidden_relative(path, root):
        return True
    if any(part in excluded_dirs for part in rel.parts[:-1]):
        return True
    rel_posix = rel.as_posix()
    return any(
        fnmatch.fnmatch(path.name, pattern) or fnmatch.fnmatch(rel_posix, pattern)
        for pattern in globs
    )


def decode_text(data: bytes) -> tuple[str, str]:
    if b"\x00" in data[:8192]:
        raise UnicodeError("检测到 NUL 字节，可能是二进制文件")
    for encoding in ("utf-8-sig", "utf-8", "gb18030", "utf-16", "cp1252"):
        try:
            return data.decode(encoding), encoding
        except UnicodeDecodeError:
            continue
    return data.decode("utf-8", errors="replace"), "utf-8(replace)"


def collect_files(
    root: Path,
    output: Path,
    extensions: set[str],
    excluded_dirs: set[str],
    globs: set[str],
    include_hidden: bool,
    max_bytes: int | None,
    skip_json_hpp: bool,
    all_text_files: bool,
) -> tuple[list[dict[str, Any]], list[tuple[str, str]]]:
    files: list[dict[str, Any]] = []
    skipped: list[tuple[str, str]] = []

    for base, dirs, names in os.walk(root):
        base_path = Path(base)
        dirs[:] = sorted(
            d for d in dirs
            if d not in excluded_dirs and (include_hidden or not d.startswith("."))
        )

        for name in sorted(names):
            path = base_path / name
            rel = path.relative_to(root).as_posix()

            try:
                if path.resolve() == output:
                    continue
            except OSError:
                pass

            if excluded(path, root, excluded_dirs, globs, include_hidden):
                continue

            suffix = path.suffix.lower()
            is_cmake_lists = name.lower() == "cmakelists.txt"
            if not all_text_files and suffix not in extensions and not is_cmake_lists:
                continue

            if skip_json_hpp and name.lower() == "json.hpp":
                skipped.append((rel, "默认跳过大型第三方单头文件 json.hpp；使用 --include-json-hpp 可包含"))
                continue

            try:
                size = path.stat().st_size
                if max_bytes is not None and size > max_bytes:
                    skipped.append((rel, f"文件过大: {size / 1024:.1f} KB"))
                    continue

                data = path.read_bytes()
                text, encoding = decode_text(data)
                files.append(
                    {
                        "path": path,
                        "rel": rel,
                        "text": text.replace("\r\n", "\n").replace("\r", "\n"),
                        "encoding": encoding,
                        "size": size,
                        "sha256": hashlib.sha256(data).hexdigest()[:12],
                    }
                )
            except (OSError, UnicodeError) as error:
                skipped.append((rel, str(error)))

    files.sort(key=lambda item: item["rel"].lower())
    skipped.sort(key=lambda item: item[0].lower())
    return files, skipped


def make_tree(files: list[dict[str, Any]]) -> str:
    tree: dict[str, Any] = {}
    for file_info in files:
        node = tree
        for part in file_info["rel"].split("/"):
            node = node.setdefault(part, {})

    lines: list[str] = []

    def walk(node: dict[str, Any], prefix: str = "") -> None:
        names = sorted(node, key=lambda name: (not bool(node[name]), name.lower()))
        for index, name in enumerate(names):
            last = index == len(names) - 1
            connector = "└── " if last else "├── "
            lines.append(prefix + connector + name)
            if node[name]:
                walk(node[name], prefix + ("    " if last else "│   "))

    walk(tree)
    return "\n".join(lines)


def resolve_include(source_rel: str, target: str, known: set[str]) -> str | None:
    target = target.replace("\\", "/")
    source_dir = Path(source_rel).parent
    candidates = []
    if source_dir.as_posix() != ".":
        candidates.append((source_dir / target).as_posix())
    candidates.append(Path(target).as_posix())

    for candidate in candidates:
        if candidate in known:
            return candidate

    suffix_matches = [path for path in known if path == target or path.endswith("/" + target)]
    return suffix_matches[0] if len(suffix_matches) == 1 else None


def analyze(files: list[dict[str, Any]]):
    known = {f["rel"] for f in files}
    includes: defaultdict[str, list[str]] = defaultdict(list)
    reverse: defaultdict[str, list[str]] = defaultdict(list)
    external: defaultdict[str, list[tuple[str, bool]]] = defaultdict(list)
    symbols: list[tuple[str, str, str, int]] = []

    for file_info in files:
        rel = file_info["rel"]
        text = file_info["text"]

        for delimiter, target in INCLUDE_RE.findall(text):
            resolved = resolve_include(rel, target, known)
            if resolved:
                includes[rel].append(resolved)
                reverse[resolved].append(rel)
            else:
                external[rel].append((target, delimiter == "<"))

        for kind, pattern in SYMBOL_PATTERNS:
            for match in pattern.finditer(text):
                line = text.count("\n", 0, match.start()) + 1
                symbols.append((match.group(1), kind, rel, line))

    for mapping in (includes, reverse):
        for key in mapping:
            mapping[key] = sorted(set(mapping[key]), key=str.lower)

    for key in external:
        external[key] = sorted(set(external[key]), key=lambda item: item[0].lower())

    symbols = sorted(set(symbols), key=lambda item: (item[0].lower(), item[2].lower(), item[3]))
    return includes, reverse, external, symbols


def safe_fence(text: str) -> str:
    longest = max((len(match.group(0)) for match in re.finditer(r"`+", text)), default=0)
    return "`" * max(3, longest + 1)


def numbered(text: str) -> str:
    lines = text.split("\n")
    width = max(4, len(str(len(lines))))
    return "\n".join(f"{index:>{width}}  {line}" for index, line in enumerate(lines, 1))


def markdown_language(path: str) -> str:
    name = Path(path).name.lower()
    if name == "cmakelists.txt":
        return "cmake"
    return LANGUAGE_BY_EXT.get(Path(path).suffix.lower(), "text")


def write_markdown(
    root: Path,
    output: Path,
    files: list[dict[str, Any]],
    skipped: list[tuple[str, str]],
    use_line_numbers: bool,
    include_analysis: bool,
) -> None:
    total_bytes = sum(file_info["size"] for file_info in files)
    now = dt.datetime.now().astimezone().isoformat(timespec="seconds")

    if include_analysis:
        includes, reverse, external, symbols = analyze(files)
    else:
        includes = reverse = external = {}
        symbols = []

    with output.open("w", encoding="utf-8", newline="\n") as out:
        out.write("# C/C++ 项目完整汇总\n\n")
        out.write("> 自动生成文件。每个已收录文本文件的完整内容均列在“文件完整内容”章节中。\n\n")
        out.write("## 项目概览\n\n")
        out.write(f"- 根目录：`{root}`\n")
        out.write(f"- 生成时间：`{now}`\n")
        out.write(f"- 收录文件：**{len(files)}**\n")
        out.write(f"- 源文件总大小：**{total_bytes / 1024:.1f} KB**\n")
        out.write(f"- 跳过文件：**{len(skipped)}**\n")
        out.write(f"- 行号：**{'开启' if use_line_numbers else '关闭'}**\n\n")

        out.write("## 文件结构\n\n")
        out.write("```text\n")
        out.write(root.name + "/\n")
        tree = make_tree(files)
        if tree:
            out.write(tree + "\n")
        out.write("```\n\n")

        out.write("## 文件索引\n\n")
        for index, file_info in enumerate(files, 1):
            out.write(
                f"{index}. `{file_info['rel']}` "
                f"({file_info['size']} bytes, {file_info['encoding']}, sha256:{file_info['sha256']})\n"
            )
        out.write("\n")

        if include_analysis:
            out.write("## 项目内部引用关系\n\n")
            if includes:
                for source in sorted(includes, key=str.lower):
                    out.write(f"- `{source}`\n")
                    for target in includes[source]:
                        out.write(f"  - includes → `{target}`\n")
            else:
                out.write("- 未发现可解析的项目内部 include。\n")
            out.write("\n")

            out.write("## 反向引用\n\n")
            if reverse:
                for target in sorted(reverse, key=str.lower):
                    refs = ", ".join(f"`{source}`" for source in reverse[target])
                    out.write(f"- `{target}` ← {refs}\n")
            else:
                out.write("- 未发现反向引用。\n")
            out.write("\n")

            out.write("## 外部或未解析的 include\n\n")
            if external:
                for source in sorted(external, key=str.lower):
                    values = []
                    for target, is_system in external[source]:
                        values.append(f"`<{target}>`" if is_system else f"`\"{target}\"`")
                    out.write(f"- `{source}`：{', '.join(values)}\n")
            else:
                out.write("- 无。\n")
            out.write("\n")

            out.write("## 轻量符号索引\n\n")
            if symbols:
                for name, kind, rel, line in symbols:
                    out.write(f"- **{name}** ({kind}) — `{rel}:{line}`\n")
            else:
                out.write("- 未提取到符号。\n")
            out.write("\n")

        if skipped:
            out.write("## 跳过的文件\n\n")
            for rel, reason in skipped:
                out.write(f"- `{rel}`：{reason}\n")
            out.write("\n")

        out.write("# 文件完整内容\n\n")
        for index, file_info in enumerate(files, 1):
            rel = file_info["rel"]
            text = numbered(file_info["text"]) if use_line_numbers else file_info["text"]
            fence = safe_fence(text)
            language = markdown_language(rel)

            out.write(f"## {index}. `{rel}`\n\n")
            out.write(f"- 编码：`{file_info['encoding']}`\n")
            out.write(f"- 大小：`{file_info['size']}` bytes\n")
            out.write(f"- SHA-256 前缀：`{file_info['sha256']}`\n\n")
            out.write(f"{fence}{language}\n")
            out.write(text)
            if not text.endswith("\n"):
                out.write("\n")
            out.write(f"{fence}\n\n")


def main() -> int:
    args = parse_args()
    root = Path(args.root).expanduser().resolve()
    if not root.is_dir():
        print(f"错误：项目目录不存在：{root}", file=sys.stderr)
        return 2

    output = Path(args.output).expanduser()
    if not output.is_absolute():
        output = root / output
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    extensions = set(DEFAULT_EXTENSIONS)
    extensions.update(normalize_extension(ext) for ext in args.extensions if normalize_extension(ext))
    excluded_dirs = set(DEFAULT_EXCLUDE_DIRS) | set(args.exclude_dir)
    globs = set(DEFAULT_EXCLUDE_GLOBS) | set(args.exclude)
    max_bytes = None if args.max_file_kb <= 0 else args.max_file_kb * 1024

    files, skipped = collect_files(
        root=root,
        output=output,
        extensions=extensions,
        excluded_dirs=excluded_dirs,
        globs=globs,
        include_hidden=args.include_hidden,
        max_bytes=max_bytes,
        skip_json_hpp=not args.include_json_hpp,
        all_text_files=args.all_text_files,
    )

    write_markdown(
        root=root,
        output=output,
        files=files,
        skipped=skipped,
        use_line_numbers=not args.no_line_numbers,
        include_analysis=not args.no_analysis,
    )

    print(f"完成：{output}")
    print(f"收录文件：{len(files)}")
    print(f"跳过文件：{len(skipped)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
