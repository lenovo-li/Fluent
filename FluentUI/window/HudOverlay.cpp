// HudOverlay.cpp — HUD thread: private D3D/D2D/DWrite/DComp stack, ~10 Hz redraw.
// Design and threading contract: HudOverlay.h. Data and liveness logic: HudFeed.h.

#include "HudOverlay.h"

#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <string>

#include "../diagnostics/DebugHud.h"
#include "../fl_common.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dcomp.lib")

namespace fluent {

namespace {
const char* kTag = "HudOverlay";

// Redraw + ping period. 10 Hz is as fast as anyone reads changing digits; faster
// only adds HUD-thread work and UI-thread ping wake-ups.
constexpr DWORD kTickMs = 100;
// After a failed device build or draw, wait this long before rebuilding, so a
// persistent failure costs one attempt per second, not ten. Each attempt still
// traces its failing call (FL_RETURN_IF_FAILED), which is the HRESULT a user's
// DebugView capture needs.
constexpr double kRetryMs = 1000.0;

// Panel geometry (DIP). The panel is sized from the measured text, not a constant:
// line widths depend on the font and on the numbers (a fixed 300 DIP panel clipped
// the tick/rsz/draw line, ~315 DIP in 11 DIP Consolas). The size only ever grows,
// so the panel does not jitter as digits change and the surface is reallocated a
// handful of times per session rather than per tick.
constexpr float kMinPanelW = 200.0f;
constexpr float kMinPanelH = 60.0f;
constexpr float kMargin = 8.0f;
constexpr float kPad = 6.0f;

// Everything here belongs to the HUD thread and never leaves it.
struct GpuState {
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID2D1Factory1> d2dFactory;
    ComPtr<ID2D1Device> d2dDevice;
    ComPtr<IDCompositionDesktopDevice> dcomp;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual2> visual;
    ComPtr<IDCompositionSurface> surface;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<IDWriteTextFormat> format;
    UINT surfaceW = 0, surfaceH = 0;  // pixels; 0 = no surface yet
    float panelW = kMinPanelW, panelH = kMinPanelH;  // DIP, grow-only
    int lastX = INT_MIN, lastY = INT_MIN;

    bool Valid() const { return target != nullptr; }
    void Reset() { *this = GpuState{}; }
};

HRESULT CreateGpu(GpuState& g, HWND hwnd) {
    // BGRA support is required for D2D interop. No debug layer: the HUD must not be
    // the reason a Debug build fails to start on a machine without Graphics Tools.
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                   nullptr, 0, D3D11_SDK_VERSION, &g.d3d, nullptr, nullptr);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
                               D3D11_SDK_VERSION, &g.d3d, nullptr, nullptr);
    FL_RETURN_IF_FAILED(kTag, hr);

    ComPtr<IDXGIDevice> dxgi;
    FL_RETURN_IF_FAILED(kTag, g.d3d.As(&dxgi));
    D2D1_FACTORY_OPTIONS opts = {};
    FL_RETURN_IF_FAILED(kTag, D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                                __uuidof(ID2D1Factory1), &opts,
                                                reinterpret_cast<void**>(g.d2dFactory.GetAddressOf())));
    FL_RETURN_IF_FAILED(kTag, g.d2dFactory->CreateDevice(dxgi.Get(), &g.d2dDevice));

    // From the D2D device so surface BeginDraw hands back an ID2D1DeviceContext —
    // same reason as DCompHost::Initialize.
    FL_RETURN_IF_FAILED(kTag, DCompositionCreateDevice2(g.d2dDevice.Get(),
                                                        IID_PPV_ARGS(&g.dcomp)));
    // topmost = TRUE: the slot above the window content (DCompHost takes FALSE).
    FL_RETURN_IF_FAILED(kTag, g.dcomp->CreateTargetForHwnd(hwnd, TRUE, &g.target));
    FL_RETURN_IF_FAILED(kTag, g.dcomp->CreateVisual(&g.visual));
    FL_RETURN_IF_FAILED(kTag, g.target->SetRoot(g.visual.Get()));

    // An isolated factory: the shared one is the UI thread's.
    FL_RETURN_IF_FAILED(kTag, DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED,
                                                  __uuidof(IDWriteFactory),
                                                  reinterpret_cast<IUnknown**>(g.dwrite.GetAddressOf())));
    FL_RETURN_IF_FAILED(kTag, g.dwrite->CreateTextFormat(
                                  L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                  DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                  11.0f, L"en-us", &g.format));
    g.format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    TraceMsg(kTag, "initialized (topmost DComp target, own thread)");
    return S_OK;
}

// One HUD frame: (re)allocate the surface if the DPI changed, draw panel + text,
// place the visual at the window's top-right below the caption, commit.
HRESULT DrawTick(GpuState& g, HWND hwnd, const std::wstring& text, float titleBarDip) {
    const UINT dpi = GetDpiForWindow(hwnd);
    const float s = (dpi ? dpi : 96) / 96.0f;

    // Lay out once (unbounded, no wrap) and grow the panel to fit it.
    ComPtr<IDWriteTextLayout> layout;
    FL_RETURN_IF_FAILED(kTag, g.dwrite->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                                                         g.format.Get(), 4096.0f, 4096.0f, &layout));
    DWRITE_TEXT_METRICS tm = {};
    FL_RETURN_IF_FAILED(kTag, layout->GetMetrics(&tm));
    g.panelW = (std::max)(g.panelW, std::ceil(tm.widthIncludingTrailingWhitespace) + 2 * kPad);
    g.panelH = (std::max)(g.panelH, std::ceil(tm.height) + 2 * kPad);
    const UINT w = static_cast<UINT>(g.panelW * s + 0.5f);
    const UINT h = static_cast<UINT>(g.panelH * s + 0.5f);

    if (w != g.surfaceW || h != g.surfaceH) {
        g.surface.Reset();
        FL_RETURN_IF_FAILED(kTag, g.dcomp->CreateSurface(w, h, DXGI_FORMAT_B8G8R8A8_UNORM,
                                                         DXGI_ALPHA_MODE_PREMULTIPLIED,
                                                         &g.surface));
        FL_RETURN_IF_FAILED(kTag, g.visual->SetContent(g.surface.Get()));
        g.surfaceW = w;
        g.surfaceH = h;
    }

    ComPtr<ID2D1DeviceContext> dc;
    POINT atlas = {};
    FL_RETURN_IF_FAILED(kTag, g.surface->BeginDraw(nullptr, IID_PPV_ARGS(&dc), &atlas));
    dc->SetTransform(D2D1::Matrix3x2F::Scale(s, s) *
                     D2D1::Matrix3x2F::Translation(static_cast<float>(atlas.x),
                                                   static_cast<float>(atlas.y)));
    dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);  // premultiplied target
    dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    ComPtr<ID2D1SolidColorBrush> brush;
    HRESULT hr = dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.6f), &brush);
    if (SUCCEEDED(hr)) {
        dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0, 0, g.panelW, g.panelH), 4, 4),
                                 brush.Get());
        brush->SetColor(D2D1::ColorF(0.6f, 1.0f, 0.6f, 1.0f));
        dc->DrawTextLayout(D2D1::Point2F(kPad, kPad), layout.Get(), brush.Get());
    }
    dc.Reset();
    const HRESULT endHr = g.surface->EndDraw();
    FL_RETURN_IF_FAILED(kTag, hr);
    FL_RETURN_IF_FAILED(kTag, endHr);

    // Visual offsets are pixels relative to the client origin.
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    const int x = rc.right - static_cast<int>(w) - static_cast<int>(kMargin * s + 0.5f);
    const int y = static_cast<int>((kMargin + titleBarDip) * s + 0.5f);
    if (x != g.lastX || y != g.lastY) {
        g.visual->SetOffsetX(static_cast<float>(x));
        g.visual->SetOffsetY(static_cast<float>(y));
        g.lastX = x;
        g.lastY = y;
    }
    FL_RETURN_IF_FAILED(kTag, g.dcomp->Commit());
    return S_OK;
}

std::wstring BuildText(const HudSnapshot* snap, double pendingMs, double latencyMs,
                       double lastFrameAgoMs) {
    std::string ascii;
    if (snap) {
        HudPercentiles iv;
        iv.p50Ms = snap->interval.P50();
        iv.p95Ms = snap->interval.P95();
        iv.p99Ms = snap->interval.P99();
        iv.maxMs = snap->interval.Max();
        iv.sampleCount = static_cast<unsigned>(snap->interval.Count());
        HudCpuPercentiles cpu;
        cpu.p50Ms = snap->cpu.P50();
        cpu.p99Ms = snap->cpu.P99();
        cpu.maxMs = snap->cpu.Max();
        ascii = FormatHudLines(snap->stats, iv, cpu);
    } else {
        ascii = "waiting for first frame";
    }
    ascii += '\n';
    ascii += FormatHudStatusLine(pendingMs, latencyMs, lastFrameAgoMs);
    return std::wstring(ascii.begin(), ascii.end());  // ASCII only, safe widen
}

}  // namespace

void HudOverlay::Start(HWND hwnd) {
    if (Running() || !hwnd) return;
    hwnd_ = hwnd;
    ackSeq_.store(0, std::memory_order_relaxed);
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_) {
        Trace(kTag, "CreateEvent FAILED", HRESULT_FROM_WIN32(GetLastError()));
        return;
    }
    worker_ = std::thread([this] { ThreadMain(); });
}

void HudOverlay::Stop() {
    if (stopEvent_) SetEvent(stopEvent_);
    if (worker_.joinable()) worker_.join();
    if (stopEvent_) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
}

void HudOverlay::AcknowledgePing(WPARAM seq) {
    // Time first, then the sequence with release: a reader that sees the new
    // sequence (acquire) also sees the time that belongs to it.
    ackQpc_.store(QpcNow(), std::memory_order_relaxed);
    ackSeq_.store(static_cast<uint32_t>(seq), std::memory_order_release);
}

void HudOverlay::ThreadMain() {
    GpuState gpu;
    HudPingTracker ping;
    HudSnapshot snap;
    bool haveSnap = false;
    bool failureLogged = false;
    double retryAtMs = 0.0;  // no GPU rebuild before this time (backoff)

    for (;;) {
        if (!IsWindow(hwnd_)) break;
        const double nowMs = QpcToMs(QpcNow());

        // Liveness first, so a stall is measured even if drawing then fails.
        const uint32_t acked = ackSeq_.load(std::memory_order_acquire);
        ping.Observe(acked, QpcToMs(ackQpc_.load(std::memory_order_relaxed)));
        if (const uint32_t seq = ping.NextPing(nowMs)) {
            if (!PostMessageW(hwnd_, WM_FLUENT_HUD_PING, static_cast<WPARAM>(seq), 0))
                ping.CancelInFlight();
        }

        if (feed_.Read(snap)) haveSnap = true;
        const double lastFrameAgoMs =
            (haveSnap && snap.lastFrameEndQpc != 0) ? nowMs - QpcToMs(snap.lastFrameEndQpc) : -1.0;
        const std::wstring text =
            BuildText(haveSnap ? &snap : nullptr, ping.PendingMs(nowMs),
                      ping.HasLatency() ? ping.LastLatencyMs() : -1.0, lastFrameAgoMs);

        if (!gpu.Valid() && nowMs >= retryAtMs && FAILED(CreateGpu(gpu, hwnd_))) {
            gpu.Reset();
            retryAtMs = nowMs + kRetryMs;
        }
        if (gpu.Valid()) {
            if (FAILED(DrawTick(gpu, hwnd_, text, haveSnap ? snap.titleBarHeightDip : 0.0f))) {
                // Device loss or a resize race: drop everything, rebuild after the
                // backoff. Logged once per failure streak, not per attempt.
                if (!failureLogged) TraceMsg(kTag, "draw failed; rebuilding after backoff");
                failureLogged = true;
                gpu.Reset();
                retryAtMs = nowMs + kRetryMs;
            } else {
                failureLogged = false;
            }
        }

        if (WaitForSingleObject(stopEvent_, kTickMs) != WAIT_TIMEOUT) break;
    }
    // Releasing the target here, on its own thread, takes the HUD off the screen.
    gpu.Reset();
}

}  // namespace fluent
