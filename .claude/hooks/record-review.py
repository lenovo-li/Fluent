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


def _last_text(payload: dict) -> str:
    """Return the final text the agent produced.

    Agents that use SubagentHandback deliver their report as the ``message``
    field inside the tool call, not as a bare assistant text message, so
    ``last_assistant_message`` is empty for them.  Read the agent's own JSONL
    transcript and pull the last SubagentHandback message if present; fall
    back to ``last_assistant_message`` for agents that end with a text reply.
    """
    transcript_path = payload.get("agent_transcript_path") or ""
    if transcript_path:
        try:
            lines = Path(transcript_path).read_text(encoding="utf-8",
                                                     errors="replace").splitlines()
            for raw in reversed(lines):
                try:
                    entry = json.loads(raw)
                except json.JSONDecodeError:
                    continue
                content = entry.get("message", {}).get("content", [])
                if not isinstance(content, list):
                    continue
                for block in content:
                    if (block.get("type") == "tool_use"
                            and block.get("name") == "SubagentHandback"):
                        msg = (block.get("input") or {}).get("message") or ""
                        if msg:
                            return msg
        except OSError:
            pass
    return payload.get("last_assistant_message") or ""


if agent == "fluent-reviewer":
    stamps["reviewed"] = sorted(set(stamps["reviewed"]) |
                                {p for p in changed if gatelib.is_code(p)})
elif agent == "fluent-doc-auditor":
    stamps["doc_audited"] = sorted(set(stamps["doc_audited"]) | set(changed))
    message = _last_text(payload)
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
