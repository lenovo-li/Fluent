---
name: doc-sync
description: Bring the documentation of changed FluentUI controls back in line with their code — API section vs. header, behavior contracts, protected extension points, extension READMEs — then have fluent-doc-auditor confirm. Use after modifying an existing control, or when the Stop hook reports "文档未同步".
argument-hint: "[ControlName ...]  (default: every control changed in the working tree)"
---

Targets: $ARGUMENTS

If no names were given, take every control whose source appears in
`git status --short` under `FluentUI/controls/`, `FluentUI/layout/` or
`extensions/<Lib>/`. The doc for each is listed in the mapping table in
`.claude/agents/fluent-doc-auditor.md`.

For each target, in this conversation:

1. **Read both sides.** `git diff HEAD -- <its .h/.cpp>` and the whole doc. Do
   not write doc text from memory of the API; read the header.
2. **API section.** Make `## 公开 API` match the header's `public` and
   `protected` declarations: add new members with signature and purpose,
   update changed signatures and defaults, delete removed members. Events list
   their argument type and when they fire.
3. **Contracts.** If the diff changed behavior, update the sections that state
   it: which `DirtyFlags` a property invalidates, `VisualOverflowDip()`,
   virtualization, when `WantsAnimationTick()` goes false, the compositor path
   and its UI-thread fallback, keyboard and UI Automation behavior.
4. **Extension points.** A new, changed or removed `protected` member also goes
   into `docs/controls/inheritance-guide.md`; a rename or removal is written up
   as a breaking change for `extensions/`.
5. **Verification honesty.** Keep "verified by test X" separate from "needs the
   user on real hardware (where to look)". Do not claim smoothness or pixels.
6. **Lifetime rule.** No links to `docs/design/`, no commit SHAs, no plan labels.

If the change truly leaves every documented fact true (a bug fix restoring
documented behavior, an internal refactor), leave the doc alone; the auditor
decides that and emits `DOC-NOT-NEEDED`.

Then delegate to `fluent-doc-auditor` with the target list (in parallel with
`fluent-reviewer` if the code has not been reviewed yet). Fix its findings and
re-run it only if a finding changed a doc. Run `python scripts/check_all.py`
at the end.
