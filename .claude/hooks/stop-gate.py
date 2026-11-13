#!/usr/bin/env python3
# Stop hook: the completion gate. When Claude tries to end a turn with code
# changed in the working tree, it must have (1) kept docs in sync, (2) a passing
# check_all.py, (3) a build newer than every changed source, and (4) a review by
# fluent-reviewer and fluent-doc-auditor. Any miss exits 2, which keeps the turn
# going with the list of what is missing.
#
# Doc sync rule: a changed FluentUI/{controls,layout}/<Name>.{h,cpp} needs
# docs/controls/<Name>.md changed too; a changed extensions/<Lib>/* source needs
# extensions/<Lib>/README.md changed. The escape hatch for a pure bug fix with no
# API or behavior change is a "DOC-NOT-NEEDED: <Name> — <reason>" line from
# fluent-doc-auditor (recorded by record-review.py). The writer cannot waive it.
#
# Loop guard: when stop_hook_active is set, Claude already got this list once and
# kept working. Blocking forever would hang auto mode on something it cannot fix
# from here (Device Guard blocking the test binary, a missing toolset), so the
# second stop goes through and the final report must name each unmet gate.
#
# A turn with no working-tree changes (questions, research) is never gated, so
# pre-existing doc debt reported by check_all.py does not block conversation.
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gatelib  # noqa: E402

sys.path.insert(0, str(gatelib.ROOT / "scripts"))
from check_docs_coverage import EXEMPT_CONTROLS  # noqa: E402

TEST_EXES = ("x64/Debug/FluentUITests.exe", "x64/Release/FluentUITests.exe")


def doc_issues(changed: list[str], waived: dict) -> list[str]:
    changed_set = set(changed)
    out = []
    for p in changed:
        parts = p.split("/")
        if not gatelib.is_code(p):
            continue
        name = Path(p).stem
        if name in EXEMPT_CONTROLS or name in waived:
            continue
        if parts[0] == "FluentUI" and len(parts) == 3 and \
                parts[1] in ("controls", "layout"):
            doc = f"docs/controls/{name}.md"
        elif parts[0] == "extensions" and len(parts) >= 3:
            doc = f"extensions/{parts[1]}/README.md"
        else:
            continue
        if doc not in changed_set:
            out.append(f"  - {p} 已改，{doc} 未改")
    return sorted(set(out))


def stale_build(code: list[str]) -> list[str]:
    newest = max((gatelib.ROOT / p).stat().st_mtime for p in code)
    out = []
    for exe in TEST_EXES:
        f = gatelib.ROOT / exe
        if not f.exists() or f.stat().st_mtime < newest:
            out.append(f"  - {exe} 早于最新改动的源文件")
    return out


def main() -> int:
    try:
        payload = json.loads(sys.stdin.buffer.read().decode("utf-8"))
    except ValueError:
        return 0
    if payload.get("stop_hook_active"):
        return 0
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

    changed = gatelib.changed_files()
    code = [p for p in changed if gatelib.is_code(p)]
    if not code:
        return 0
    stamps = gatelib.load_stamps()

    issues = []
    docs = doc_issues(changed, stamps.get("doc_not_needed", {}))
    if docs:
        issues.append("文档未同步（先更新文档；若确实无需改，由 "
                      "fluent-doc-auditor 输出 DOC-NOT-NEEDED 行）:\n"
                      + "\n".join(docs))

    r = subprocess.run([sys.executable, "scripts/check_all.py"],
                       cwd=gatelib.ROOT, capture_output=True, text=True,
                       encoding="utf-8", errors="replace", timeout=60)
    if r.returncode != 0:
        tail = r.stdout[r.stdout.find("=== summary"):]
        issues.append("check_all.py 未通过:\n" + tail.strip())

    stale = stale_build(code)
    if stale:
        issues.append("构建过期，运行 /verify（Debug + Release + 测试）:\n"
                      + "\n".join(stale))

    for key, agent in (("reviewed", "fluent-reviewer"),
                       ("doc_audited", "fluent-doc-auditor")):
        missing = [p for p in code if p not in set(stamps.get(key, []))]
        if missing:
            issues.append(f"{agent} 未覆盖 {len(missing)} 个改动文件，"
                          f"例如 {missing[0]}。两个审查可并行启动。")

    if not issues:
        return 0
    sys.stderr.write("完成门禁未通过:\n\n" + "\n\n".join(issues) + "\n")
    return 2


if __name__ == "__main__":
    sys.exit(main())
