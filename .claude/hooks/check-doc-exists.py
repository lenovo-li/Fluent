#!/usr/bin/env python3
# PreToolUse hook: block edits to FluentUI controls that have no documentation.
#
# Matches Edit/Write targeting FluentUI/controls/*.{cpp,h} or FluentUI/layout/*.{cpp,h}.
# Checks whether docs/controls/<Name>.md exists. If not, exit 2 (block the edit) and
# explain. Exempt internal classes (TextEditBase, ToggleVisuals, etc.) are allowed.
#
# WHY: A control can compile and work but ship undocumented. This catches it at
# write time instead of at release time.

import json
import os
import sys
from pathlib import Path

_ROOT = Path(os.environ.get("CLAUDE_PROJECT_DIR")
             or Path(__file__).resolve().parents[2])
# Single source of truth for the exemption list: scripts/check_docs_coverage.py.
# A second copy here would drift the first time someone exempts a helper class.
sys.path.insert(0, str(_ROOT / "scripts"))
from check_docs_coverage import EXEMPT_CONTROLS  # noqa: E402


def main() -> int:
    try:
        payload = json.loads(sys.stdin.buffer.read().decode("utf-8"))
    except (json.JSONDecodeError, ValueError) as e:
        # Guard fails closed: if we can't parse the target, refuse the write.
        sys.stderr.write(
            f"Blocked: JSON parse failed ({e}); write refused.\n")
        return 2

    tool_input = payload.get("tool_input") or {}
    path = (tool_input.get("file_path") or
            tool_input.get("notebook_path") or "")
    if not path:
        # Some tools (MultiEdit) might have different field names; log it.
        sys.stderr.write(
            f"Debug: no file_path in tool_input. Keys: {list(tool_input.keys())}\n")
        return 0
    normalized = path.replace("\\", "/")

    # Only act on FluentUI/controls/ and FluentUI/layout/ edits.
    if not (("/FluentUI/controls/" in normalized or
             "/FluentUI/layout/" in normalized) and
            (normalized.endswith(".cpp") or normalized.endswith(".h"))):
        return 0

    # Extract control name.
    stem = Path(path).stem
    if stem in EXEMPT_CONTROLS:
        return 0  # internal class; no doc required

    # Check docs/controls/<Name>.md exists.
    doc_path = _ROOT / "docs" / "controls" / f"{stem}.md"

    if doc_path.exists():
        return 0  # doc exists; allow the edit

    sys.stderr.write(
        f"Blocked: {stem} has no documentation.\n"
        f"  Missing: docs/controls/{stem}.md\n"
        f"  Template: docs/controls/TEMPLATE.md\n\n"
        f"Before editing this control, create its documentation with at least:\n"
        f"  ## 概述, ## 继承层次, ## 公开 API\n\n"
        f"If this is an internal helper that should not have public docs, "
        f"add it to EXEMPT_CONTROLS in scripts/check_docs_coverage.py.\n")
    return 2


if __name__ == "__main__":
    sys.exit(main())
