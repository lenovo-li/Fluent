#!/usr/bin/env python3
# SubagentStop hook (matcher: fluent-reviewer|fluent-doc-auditor). When a review
# agent finishes, record which changed files it covered so stop-gate.py can tell
# that the review actually ran, instead of trusting the main agent's word.
#
# fluent-reviewer  -> every changed code file is marked "reviewed".
# fluent-doc-auditor -> every changed file is marked "doc_audited", and each
#   "DOC-NOT-NEEDED: <Name> — <reason>" line in its final message is recorded as
#   a judgment that <Name>'s doc needs no edit (a pure bug fix with no API or
#   behavior change). The auditor makes that call, not the agent that wrote the
#   code, which is the point: the writer is the one most tempted to skip docs.
#
# Never blocks: a recording failure must not stop a review from returning.
import json
import re
import sys

from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gatelib  # noqa: E402

try:
    payload = json.loads(sys.stdin.buffer.read().decode("utf-8"))
except ValueError:
    sys.exit(0)

agent = (payload.get("agent_type") or "").split(":")[-1]
changed = gatelib.changed_files()
stamps = gatelib.load_stamps()

if agent == "fluent-reviewer":
    stamps["reviewed"] = sorted(set(stamps["reviewed"]) |
                                {p for p in changed if gatelib.is_code(p)})
elif agent == "fluent-doc-auditor":
    stamps["doc_audited"] = sorted(set(stamps["doc_audited"]) | set(changed))
    message = payload.get("last_assistant_message") or ""
    for name, reason in re.findall(
            r"^\s*DOC-NOT-NEEDED:\s*([A-Za-z0-9_]+)\s*[—:-]+\s*(.+)$",
            message, re.MULTILINE):
        stamps["doc_not_needed"][name] = reason.strip()
else:
    sys.exit(0)

try:
    gatelib.save_stamps(stamps)
except OSError:
    pass
sys.exit(0)
