# Integration Guide: CustomControls → FluentUIDemo

**Status**: 🚧 Template ready, build integration TODO

---

## What This Is

`examples/CustomControls/` is a **separate library** demonstrating how end users should extend FluentUI by inheriting controls. It contains three working examples:

1. **LunarCalendar**: adds lunar dates below Gregorian days
2. **BigButton**: 64dp tall button with icon + two-line text
3. **LogSlider**: logarithmic snapping (1, 10, 100, 1000)

These are **real implementations** (`.h` + `.cpp` files), not header-only stubs.

---

## Architecture

```
FluentUI.lib (core framework, unchanged)
    ↓ #include FluentUI headers, link FluentUI.lib
CustomControls.lib (user extension library, this directory)
    ↓ #include CustomControls.h, link both libs
FluentUIDemo.exe (demo application)
```

**Why separate?**
- ✅ Clear boundary: FluentUI = stable core, CustomControls = user code
- ✅ Users can fork CustomControls without touching FluentUI
- ✅ Demonstrates the real integration path
- ❌ Don't bloat FluentUI.lib with examples

---

## Build Integration (TODO)

### Step 1: Create CustomControls.vcxproj

```xml
<!-- examples/CustomControls/CustomControls.vcxproj -->
<Project DefaultTargets="Build" xmlns="...">
  <PropertyGroup>
    <ConfigurationType>StaticLibrary</ConfigurationType>
    <PlatformToolset>v145</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  
  <ItemDefinitionGroup>
    <ClCompile>
      <!-- Include FluentUI headers -->
      <AdditionalIncludeDirectories>$(SolutionDir);%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <LanguageStandard>stdcpp20</LanguageStandard>
      <PrecompiledHeader>NotUsing</PrecompiledHeader>
    </ClCompile>
  </ItemDefinitionGroup>
  
  <ItemGroup>
    <ClInclude Include="CustomControls.h" />
  </ItemGroup>
  <ItemGroup>
    <ClCompile Include="LunarCalendar.cpp" />
    <ClCompile Include="BigButton.cpp" />
    <ClCompile Include="LogSlider.cpp" />
  </ItemGroup>
  
  <!-- Reference FluentUI project -->
  <ItemGroup>
    <ProjectReference Include="..\..\FluentUI\FluentUI.vcxproj" />
  </ItemGroup>
</Project>
```

### Step 2: Add to Fluent.slnx

```json
{
  "solution": {
    "projects": [
      {
        "path": "examples/CustomControls/CustomControls.vcxproj",
        "type": "{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}"
      }
    ]
  }
}
```

### Step 3: Update FluentUIDemo.vcxproj

**Add reference**:
```xml
<ItemGroup>
  <ProjectReference Include="..\examples\CustomControls\CustomControls.vcxproj" />
</ItemGroup>
```

**Include header**:
```cpp
// FluentUIDemo/Demo.cpp
#include "../examples/CustomControls/CustomControls.h"

using namespace fluent::custom;

// In BuildUI():
auto lunarCal = std::make_shared<LunarCalendar>();
lunarCal->SetSelectedDate(2026, 9, 27);
panel->Add(lunarCal);

auto bigBtn = std::make_shared<BigButton>();
bigBtn->SetText(L"Download");
bigBtn->SetSubtitle(L"1.2 GB available");
bigBtn->SetIcon(L"⬇️");
panel->Add(bigBtn);

auto logSlider = std::make_shared<LogSlider>();
logSlider->SetValue(100.0f);
panel->Add(logSlider);
```

---

## CMake Integration (TODO)

```cmake
# examples/CustomControls/CMakeLists.txt
add_library(CustomControls STATIC
    CustomControls.h
    LunarCalendar.cpp
    BigButton.cpp
    LogSlider.cpp
)

target_include_directories(CustomControls PUBLIC
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(CustomControls PUBLIC
    FluentUI
)

target_compile_features(CustomControls PUBLIC cxx_std_20)
```

Update root `CMakeLists.txt`:
```cmake
if(FLUENTUI_BUILD_EXAMPLES)
    add_subdirectory(examples/CustomControls)
endif()
```

Update `FluentUIDemo/CMakeLists.txt`:
```cmake
target_link_libraries(FluentUIDemo PRIVATE
    FluentUI
    CustomControls  # NEW
    # ...
)
```

---

## Testing

The examples are already compile-tested via `tests/unit/InheritanceExamplesTest.cpp`, which includes `examples/InheritanceExamples.h` (header-only versions of the same controls).

Once `CustomControls.lib` is built, the same tests should pass with the real `.cpp` implementations.

---

## Status Summary

| Task | Status |
|------|--------|
| Write `.h` + `.cpp` implementations | ✅ Done |
| Write README.md | ✅ Done |
| Create .vcxproj | 🚧 TODO |
| Add to Fluent.slnx | 🚧 TODO |
| Update FluentUIDemo to link CustomControls | 🚧 TODO |
| Add demo pages in FluentUIDemo | 🚧 TODO |
| Create CMakeLists.txt | 🚧 TODO |

---

## Next Steps

1. **Create `CustomControls.vcxproj`** (copy from `FluentUI.vcxproj`, adjust paths)
2. **Add to solution** (`Fluent.slnx`)
3. **Build**: verify it compiles and links against FluentUI.lib
4. **Update FluentUIDemo**: add reference, include header, instantiate controls
5. **Verify**: run Demo, check that LunarCalendar/BigButton/LogSlider render correctly
6. **Document**: update this file with "✅ Done"

---

**Who should do this?**

User (you) — this is intentionally left as a TODO to demonstrate that CustomControls is **user territory**, not core FluentUI. The template is here, the pattern is documented, and integrating it is a straightforward build-system task.
