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
3. **Plan + documentation framework.** If the item count can be unbounded, design
   the virtualized path first. Pick the `DirtyFlags` level and `VisualOverflowDip()`
   value. If anything is ambiguous or consequential, ask the user before writing
   code. Create `docs/controls/<Name>.md` from `TEMPLATE.md` with:
   - filled overview, inheritance hierarchy, header path
   - empty API section marked "to be filled during implementation"
   Add a line to `docs/controls/README.md` if it is a new control.
4. **Implement** in this conversation. Register every new file in its
   `.vcxproj` + `.filters` (+ `CMakeLists.txt` for `FluentUI/`); the PostToolUse
   hook runs `check_all.py` on project-file edits. Add tests under `tests/`.
   After each public/protected member is written, add its signature and one-line
   purpose to the doc API section (keep the doc current as you go, not batch at end).
5. **Verify** — run `/verify` (or delegate to `fluent-builder`). Fix and repeat
   until Debug + Release are clean and the suite is green apart from known
   environment failures. Update the checkpoint.
6. **Mutation check.** Break the fix or feature deliberately, have
   `fluent-builder` rebuild and run the relevant filter, confirm the expected
   assertion fails, then restore. If Device Guard blocks the rebuilt binary,
   record it as attempted, not verified.
7. **Review + Doc audit in parallel** — delegate both read-only agents at the same
   time: `fluent-reviewer` on the diff, `fluent-doc-auditor` on docs and
   registration. Fix CONFIRMED findings; check PLAUSIBLE ones yourself before
   acting. The SubagentStop hook records their completion for the Stop hook.
8. **Complete documentation.** API section should already have every member from
   step 4. Add code examples, fill performance characteristics (O(visible),
   DirtyFlags, WantsAnimationTick, compositor path), fill UI Automation, and split
   what was verified (mutation test caught it) from what needs hardware (ghosting,
   smoothness). For an optimization record what was slow, the fix, which assertion
   dies when reverted, and what was NOT verified (frame cost needs the F12 HUD).
9. **Final verify** with `/verify` if anything changed after step 5, then report:
   what changed, verification actually run, and what the user must check on
   real hardware and exactly where to look. Do not commit unless asked.
