#!/usr/bin/env python3
# scripts/check_docs_coverage.py — verify that every control has documentation.
#
# Exit code 0 = all checks passed. Non-zero = number of failures.
#
# Usage (from repo root):
#   python scripts/check_docs_coverage.py
#   python scripts/check_docs_coverage.py --verbose
#
# WHAT THIS CHECKS:
# 1. Every .h in FluentUI/controls/ and FluentUI/layout/ has docs/controls/<Name>.md
#    (except exempted internal classes)
# 2. Every .h in extensions/*/ is mentioned in that extension's README.md
# 3. Control docs have required sections: ## 公开 API, ## 概述, ## 继承层次
#
# WHY: Documentation drift is silent — a new control ships undocumented, or a
# control is renamed but its doc stays stale. This catches it at check_all.py time
# instead of months later when someone asks "where's the ProgressBar doc?"

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Internal classes, base classes, or types merged into another control's doc:
# they don't need their own top-level docs/controls/<Name>.md.
EXEMPT_CONTROLS = {
    "TextEditBase",       # base class for TextBox/TextArea/PasswordBox
    "ToggleVisuals",      # internal helper for ToggleSwitch/CheckBox/RadioButton
    "ProgressSweepVisual",# internal compositor visual for ProgressBar
    "RichTextDocument",   # documented inside RichTextBox.md
    "RepeatButton",       # primitive; no public construction
    "ContentDialog",      # base for MessageDialog; user doc is MessageDialog.md
    "Separator",          # trivial; may merge into a "Primitives" doc later
    "Panel",              # abstract base; layout containers have their own docs
}

# Exact H2 headings from docs/controls/TEMPLATE.md. Matched as whole lines so
# "## 公开 API 变更" doesn't count. Kept strict on purpose: synonyms ("## API",
# "## 何时用") were how two docs drifted off the template and lost their
# inheritance section without anyone noticing.
REQUIRED_SECTIONS = [
    "## 概述",
    "## 继承层次",
    "## 公开 API",
]


def check_fluentui_controls(verbose: bool) -> list[str]:
    """Check FluentUI/controls/*.h and layout/*.h have docs/controls/<Name>.md."""
    failures = []
    docs_dir = REPO_ROOT / "docs" / "controls"

    for subdir in ["controls", "layout"]:
        control_dir = REPO_ROOT / "FluentUI" / subdir
        if not control_dir.exists():
            continue

        for header in sorted(control_dir.glob("*.h")):
            stem = header.stem
            if stem in EXEMPT_CONTROLS:
                if verbose:
                    print(f"  SKIP {subdir}/{stem}.h (exempt)")
                continue

            doc_path = docs_dir / f"{stem}.md"
            if not doc_path.exists():
                failures.append(
                    f"{subdir}/{stem}.h has no docs/controls/{stem}.md")
            elif verbose:
                print(f"  OK   {subdir}/{stem}.h -> docs/controls/{stem}.md")

    return failures


def check_doc_sections(verbose: bool) -> list[str]:
    """Check that existing control docs have required sections."""
    failures = []
    docs_dir = REPO_ROOT / "docs" / "controls"

    for doc in sorted(docs_dir.glob("*.md")):
        if doc.name in ("README.md", "TEMPLATE.md", "inheritance-guide.md"):
            continue

        # utf-8-sig: several docs start with a BOM.
        content = doc.read_text(encoding="utf-8-sig", errors="replace")
        headings = {line.rstrip() for line in content.splitlines()
                    if line.startswith("## ")}
        missing = [sec for sec in REQUIRED_SECTIONS if sec not in headings]

        if missing:
            failures.append(
                f"docs/controls/{doc.name} missing sections: {', '.join(missing)}")
        elif verbose:
            print(f"  OK   docs/controls/{doc.name} has all required sections")

    return failures


def check_extension_readmes(verbose: bool) -> list[str]:
    """Check extensions/*/README.md mentions every .h in that extension."""
    failures = []
    extensions_dir = REPO_ROOT / "extensions"

    if not extensions_dir.exists():
        return failures

    for ext_dir in sorted(extensions_dir.iterdir()):
        if not ext_dir.is_dir():
            continue

        readme = ext_dir / "README.md"
        if not readme.exists():
            # Extension dir without README is a finding, but we only report
            # missing coverage for headers, so skip if there are no headers.
            headers = list(ext_dir.glob("*.h"))
            if headers:
                failures.append(
                    f"extensions/{ext_dir.name}/ has {len(headers)} headers but no README.md")
            continue

        readme_text = readme.read_text(encoding="utf-8", errors="replace")

        for header in sorted(ext_dir.glob("*.h")):
            stem = header.stem
            # Check if the control name appears in the README (loose match:
            # markdown heading, link, bold, or just the name as a word).
            # This is heuristic; a future version could parse the README structure.
            pattern = rf"\b{re.escape(stem)}\b"
            if not re.search(pattern, readme_text, re.IGNORECASE):
                failures.append(
                    f"extensions/{ext_dir.name}/{stem}.h not mentioned in README.md")
            elif verbose:
                print(f"  OK   extensions/{ext_dir.name}/{stem}.h in README.md")

    return failures


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that every control has documentation.")
    parser.add_argument("--verbose", action="store_true",
                        help="Print every passing check")
    args = parser.parse_args()

    # Output contains Chinese headings. Piped stdout on Windows defaults to the
    # ANSI code page (cp936), which Claude Code and hooks then decode as UTF-8
    # and print as mojibake. Force UTF-8 so the failure list is readable.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    all_failures = []

    print("=== FluentUI controls/layout documentation coverage")
    all_failures.extend(check_fluentui_controls(args.verbose))

    print("\n=== Control documentation required sections")
    all_failures.extend(check_doc_sections(args.verbose))

    print("\n=== Extension library README coverage")
    all_failures.extend(check_extension_readmes(args.verbose))

    if all_failures:
        print("\n=== FAILURES")
        for i, failure in enumerate(all_failures, 1):
            print(f"  {i}. {failure}")
        print(f"\nFAILED - {len(all_failures)} issue(s) found")
        return len(all_failures)

    print("\nOK - all documentation coverage checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
