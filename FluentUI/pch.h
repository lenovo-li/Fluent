// pch.h — precompiled header for FluentUI library
//
// Contains stable system headers that are included by most .cpp files but rarely
// change. The first compilation of this header takes 2-3 seconds; subsequent
// compilations of every .cpp file reuse the cached .pch and save 0.5-1 second each.
//
// DO include: <memory>, <algorithm>, <d2d1.h>, <wrl/client.h> — used everywhere.
// DO NOT include: project headers like UIElement.h — they change frequently and
// would force recompiling the entire PCH.

#pragma once

// These MUST be defined before <windows.h> is pulled in below. The .vcxproj /
// CMakeLists also define them project-wide, but the PCH is compiled first and
// standalone, so it cannot rely on that: without NOMINMAX, windows.h defines
// min/max as macros and every std::min / std::max in the codebase fails with
// "error C2589: illegal token on right side of '::'".
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

// Standard library — containers, algorithms, smart pointers
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <cmath>
#include <optional>

// Windows SDK — Direct2D, DirectWrite, DirectComposition, WRL
#include <windows.h>
#include <d2d1_1.h>
#include <d2d1_3.h>
#include <dwrite.h>
#include <dwrite_3.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <dcomp.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
