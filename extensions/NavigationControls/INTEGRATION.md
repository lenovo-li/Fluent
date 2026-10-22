# Integrating NavigationControls into another solution

NavigationControls is a static library on top of `FluentUI.lib`. A consuming app
references both projects and adds the Fluent repo root to its include path.

## 1. Project references

Add both projects to your solution, then reference them from the app's `.vcxproj`
(paths relative to the app project; adjust to your layout):

```xml
<ItemGroup>
  <ProjectReference Include="..\..\Fluent\FluentUI\FluentUI.vcxproj">
    <Project>{dcce72dd-3d48-4f95-9746-89b4ec20677e}</Project>
  </ProjectReference>
  <ProjectReference Include="..\..\Fluent\extensions\NavigationControls\NavigationControls.vcxproj">
    <Project>{f9e3d8b2-4c5e-6f7a-8b9c-0d1e2f3a4b5c}</Project>
  </ProjectReference>
</ItemGroup>
```

Both libraries write to `$(SolutionDir)x64\$(Configuration)\`, so when they are built
from YOUR solution they land next to your app's output.

## 2. Compiler settings

Match the libraries, or the link fails on CRT / ABI mismatches:

| Setting | Value |
|---|---|
| Include directories | Fluent repo root (so `FluentUI/...` and `extensions/...` resolve) |
| Language standard | C++20 (`stdcpp20`), same as the libraries |
| Runtime library | `/MTd` Debug, `/MT` Release (static CRT) |
| Extra options | `/utf-8` |
| Platform | x64 only |

## 3. Linker settings

`ProjectReference` links `FluentUI.lib` and `NavigationControls.lib` automatically.
Add the system libraries FluentUI needs:

```
d2d1.lib;dwrite.lib;dcomp.lib;windowscodecs.lib
```

## 4. Manifest (DPI)

Declare Per-Monitor V2 in the app manifest, as `examples/Minimal/app.manifest` does.
All control geometry is in DIPs; FluentUI scales per window and handles
`WM_DPICHANGED`.

## 5. Code

```cpp
#include <FluentUI/FluentUI.h>
#include "extensions/NavigationControls/NavigationControls.h"
```

Usage, event subscription rules and behavior notes are in `README.md`.

## Checklist when something does not work

- **Handler never fires**: the `Subscription` returned by `Subscribe` was discarded.
  Store it as a member (or in a `SubscriptionBag`).
- **Overlay does not cover the rail**: it must be the LAST child of the root grid and
  span every column (`SetCell(overlay, 0, 0, 1, columnCount)`).
- **Icons render as boxes**: the glyph is missing from Segoe MDL2 Assets (Win10). Use a
  codepoint present in both icon fonts.
- **LNK2038 RuntimeLibrary mismatch**: the app is not on the static CRT.
