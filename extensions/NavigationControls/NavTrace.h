// NavTrace.h — trace output for the NavigationControls library.
//
//   NAV_TRACEF("[NavigationControls.NavRail] select %d\n", index);
//
// Same switch as FluentUI (FLUENTUI_ENABLE_TRACE in fl_common.h): on in Debug,
// off in Release, overridable from the build. The caller writes the whole line,
// prefix and "\n" included, so the existing "[NavigationControls.<Control>]"
// filters keep working. When off, the call sits in an `if constexpr (false)`
// branch like FL_TRACEF: arguments stay type-checked but are never evaluated.
#pragma once

#include "../../FluentUI/fl_common.h"
#include <cstdarg>
#include <cstdio>

namespace fluent::nav_detail {
inline void NavTraceFmt(_Printf_format_string_ const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
}
}  // namespace fluent::nav_detail

#if FLUENTUI_ENABLE_TRACE
#  define NAV_TRACEF(...) ::fluent::nav_detail::NavTraceFmt(__VA_ARGS__)
#else
#  define NAV_TRACEF(...)                                                  \
    do {                                                                   \
        if constexpr (false) { ::fluent::nav_detail::NavTraceFmt(__VA_ARGS__); } \
    } while (0)
#endif
