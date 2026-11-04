---
name: new-control
description: End-to-end workflow for adding or substantially changing a FluentUI control — research, implement, verify, review, document — delegating the heavy reading and building to project subagents.
argument-hint: "<ControlName> <what it should do>"
disable-model-invocation: true
---

Task: $ARGUMENTS

Run these steps in order. The main conversation plans, writes code and makes the
decisions; subagents read, build and review so their output stays out of this
context.

1. **Checkpoint.** Follow the `task-state` skill: create or update
   `.claude/TASK_STATE.md` with the objective, acceptance criteria and this plan.
2. **Research** — delegate to `fluent-explorer`: existing classes with this name or
   role, the closest control to model on, exact API signatures, applicable
   `.claude/rules/` constraints. Decide where it lives: a reusable custom control
   goes in an `extensions/` library, not `FluentUI/`. If an existing control
   already covers it, modify it in place; never delete a control the user wrote.
3. **Plan.** If the item count can be unbounded, design the virtualized path
   first. Pick the `DirtyFlags` level and `VisualOverflowDip()` value. If
   anything is ambiguous or consequential, ask the user before writing code.
4. **Implement** in this conversation. Register every new file in its
   `.vcxproj` + `.filters` (+ `CMakeLists.txt` for `FluentUI/`); the PostToolUse
   hook runs `check_all.py` on project-file edits. Add tests under `tests/`.
5. **Verify** — run `/verify` (or delegate to `fluent-builder`). Fix and repeat
   until Debug + Release are clean and the suite is green apart from known
   environment failures. Update the checkpoint.
6. **Mutation check.** Break the fix or feature deliberately, have
   `fluent-builder` rebuild and run the relevant filter, confirm the expected
   assertion fails, then restore. If Device Guard blocks the rebuilt binary,
   record it as attempted, not verified.
7. **Review** — delegate in parallel (both read-only): `fluent-reviewer` on the
   diff, `fluent-doc-auditor` on docs and registration. Fix CONFIRMED findings;
   check PLAUSIBLE ones yourself before acting.
8. **Document.** `docs/controls/<Name>.md` from `TEMPLATE.md` plus its line in
   `docs/controls/README.md`; for an optimization record what was slow, the fix,
   and which assertion dies when it is reverted.
9. **Final verify** with `/verify` if anything changed after step 5, then report:
   what changed, verification actually run, and what the user must check on
   real hardware and exactly where to look. Do not commit unless asked.
