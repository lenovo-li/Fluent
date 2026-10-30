---
paths:
  - "**/*.vcxproj"
  - "**/*.vcxproj.filters"
  - "**/CMakeLists.txt"
  - "scripts/check_*.py"
---

### Project file registration

**Dual source lists**: New `.cpp` files must be registered in **both** build systems:
- `FluentUI/FluentUI.vcxproj` (`<ClCompile>` entries)
- `CMakeLists.txt` (`FLUENTUI_SOURCES` list)

Run `python scripts/check_all.py` to verify source list sync and Solution Explorer
tree consistency. This is fast, read-only, and does not require compilation. A
PostToolUse hook (`.claude/hooks/check-project-files.py`) runs it automatically
after an edit to any `.vcxproj`, `.filters` or `CMakeLists.txt`.

- **New test files must be registered in `tests/FluentUITests.vcxproj`** (and the
  matching `.filters`). Sources are listed one-by-one; there is no glob. Same for
  new library sources in `FluentUI/FluentUI.vcxproj`.
- **This applies to every project, including third-party headers.** Any source
  file on disk under a project's directory (`.h`/`.hpp`/`.cpp`, including
  vendored third-party headers like `FluentSettings/third_party/nlohmann/json.hpp`)
  must appear in that project's `.vcxproj`. Do not skip third-party files just
  because they are vendored — an unregistered header is invisible to Solution
  Explorer search and to "Go To" navigation.
- **Solution Explorer must mirror the folder layout on disk.** A file's
  `<Filter>` in the `.filters` file is exactly its directory relative to the
  project, with `\` separators — `controls/primitives/ButtonBase.h` gets
  `<Filter>controls\primitives</Filter>`, and a file at the project root
  (`fl_common.h`) gets no `<Filter>` element at all. Every nested directory also
  needs its own `<Filter Include="...">` declaration with a fresh
  `UniqueIdentifier` GUID; MSBuild does not create intermediate filters
  implicitly. This holds with zero exceptions across every registered entry,
  so keep it that way: Solution Explorer is how this codebase is browsed,
  and a filter that disagrees with the path makes `graphics/DCompHost.h` show up
  somewhere other than `graphics`. When a file moves on disk, update its
  `Include=` path *and* its `<Filter>` in the same edit.
