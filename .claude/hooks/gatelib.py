# Shared helpers for the completion-gate hooks (stop-gate.py, record-review.py).
#
# The gate answers one question when Claude tries to end a turn: for the code
# currently changed in the working tree, has each file been built, reviewed, and
# had its documentation either updated or explicitly judged unaffected?
#
# Review evidence lives in .claude/review-stamps.json (gitignored via .claude/*).
# It is keyed to HEAD: once the change is committed, HEAD moves, the stamps are
# discarded, and the next change starts from nothing. A stamp records the file
# list at the moment the reviewer finished, not file contents, so fixing a
# review finding in an already-reviewed file does not force another full review.
# A substantive rewrite after review is the reviewer's instructions to handle,
# not this gate's.
import json
import os
import subprocess
from pathlib import Path

ROOT = Path(os.environ.get("CLAUDE_PROJECT_DIR")
            or Path(__file__).resolve().parents[2])
STAMPS = ROOT / ".claude" / "review-stamps.json"

# Code that needs build + review when it changes.
CODE_DIRS = ("FluentUI/", "extensions/", "FluentSettings/", "tests/",
             "FluentUIDemo/", "demos/", "examples/")
CODE_EXTS = (".h", ".hpp", ".cpp")


def git(*args: str) -> str:
    r = subprocess.run(["git", *args], cwd=ROOT, capture_output=True,
                       text=True, encoding="utf-8", errors="replace",
                       timeout=10)
    return r.stdout if r.returncode == 0 else ""


def head() -> str:
    return git("rev-parse", "HEAD").strip()


def changed_files() -> list[str]:
    """Modified, added, renamed and untracked files, repo-relative, '/'-separated.
    Deleted files are dropped: there is nothing left to build or document."""
    out = []
    for line in git("status", "--porcelain", "-uall").splitlines():
        if len(line) < 4 or "D" in line[:2]:
            continue
        path = line[3:].strip().strip('"')
        if " -> " in path:  # rename: keep the new name
            path = path.split(" -> ", 1)[1]
        out.append(path.replace("\\", "/"))
    return out


def is_code(path: str) -> bool:
    return path.startswith(CODE_DIRS) and path.endswith(CODE_EXTS)


def load_stamps() -> dict:
    empty = {"head": head(), "reviewed": [], "doc_audited": [],
             "doc_not_needed": {}}
    try:
        data = json.loads(STAMPS.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return empty
    return data if data.get("head") == empty["head"] else empty


def save_stamps(data: dict) -> None:
    data["head"] = head()
    STAMPS.write_text(json.dumps(data, ensure_ascii=False, indent=2),
                      encoding="utf-8")
