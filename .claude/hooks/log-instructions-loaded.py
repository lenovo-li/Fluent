#!/usr/bin/env python
# InstructionsLoaded hook: append one line per CLAUDE.md / .claude/rules/*.md load
# to .claude/instructions-load.log, so you can see which rule loaded, why
# (session_start / path_glob_match / nested_traversal / include / compact) and
# which file access triggered it.
#
# Why this exists: /context lists only the instruction files loaded at launch.
# Path-scoped rules load later, when a matching file is read, and never appear
# there, so without a log there is no way to check that their paths: globs work.
#
# The log lives under .claude/ but is not in the .gitignore negation list, so it
# stays local. It is never read by Claude; delete it whenever it gets long.
import datetime
import json
import os
import sys

try:
    payload = json.load(sys.stdin)
except (json.JSONDecodeError, ValueError):
    sys.exit(0)

root = os.environ.get("CLAUDE_PROJECT_DIR") or payload.get("cwd") or "."


def rel(path):
    if not path:
        return "-"
    try:
        return os.path.relpath(path, root).replace("\\", "/")
    except ValueError:  # different drive on Windows
        return path.replace("\\", "/")


fields = [
    datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
    (payload.get("session_id") or "-")[:8],
    payload.get("agent_type") or "main",
    payload.get("load_reason") or "-",
    rel(payload.get("file_path")),
    "trigger=" + rel(payload.get("trigger_file_path")),
]
try:
    with open(os.path.join(root, ".claude", "instructions-load.log"), "a",
              encoding="utf-8") as f:
        f.write("  ".join(fields) + "\n")
except OSError:
    pass
sys.exit(0)
