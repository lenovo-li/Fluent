#!/usr/bin/env python
# PostToolUse hook (Edit|Write): after an edit to a .vcxproj, .vcxproj.filters or
# CMakeLists.txt, run scripts/check_all.py and, if it fails, exit 2 so Claude Code
# shows the checker output to the model. The tool call has already happened; this
# only surfaces the problem in the same turn instead of at the next build.
#
# Why a script instead of an `if` filter in settings.json: `if` takes exactly one
# permission rule, so covering three file kinds x two tools would need six hook
# entries. Filtering on the path here keeps it to one, and check_all.py is ~0.4 s.
import json
import os
import subprocess
import sys

WATCHED_SUFFIXES = (".vcxproj", ".vcxproj.filters")
WATCHED_NAMES = ("cmakelists.txt",)


def main() -> int:
    try:
        payload = json.loads(sys.stdin.buffer.read().decode("utf-8"))
    except (json.JSONDecodeError, ValueError):
        return 0
    path = (payload.get("tool_input") or {}).get("file_path") or ""
    lower = path.replace("\\", "/").lower()
    name = lower.rsplit("/", 1)[-1]
    if not (lower.endswith(WATCHED_SUFFIXES) or name in WATCHED_NAMES):
        return 0

    root = os.environ.get("CLAUDE_PROJECT_DIR") or payload.get("cwd") or "."
    result = subprocess.run(
        [sys.executable, os.path.join(root, "scripts", "check_all.py")],
        cwd=root, capture_output=True, text=True)
    if result.returncode == 0:
        return 0
    sys.stderr.write(
        f"check_all.py failed after editing {path}. Fix before building:\n")
    sys.stderr.write(result.stdout[-4000:])
    sys.stderr.write(result.stderr[-1000:])
    return 2


if __name__ == "__main__":
    sys.exit(main())
