// DCompCompositionBackend.cpp — see header. Real DirectComposition backend.

#include "DCompCompositionBackend.h"
#include "../animation/Animation.h"     // MakeOffsetSweep
#include "../graphics/DCompHost.h"
#include "../graphics/D2DContext.h"

namespace fluent {

namespace {
const char* kTag = "CompBackend";
} // namespace

// ---------------------------------------------------------------------------
// DCompCompositionVisual
// ---------------------------------------------------------------------------

DCompCompositionVisual::DCompCompositionVisual(ComPtr<IDCompositionVisual2> visual,
                                               IDCompositionDevice2* device,
                                               D2DContext* d2d)
    : visual_(std::move(visual)), device_(device), d2d_(d2d) {
    // IDCompositionVisual3 (opacity) is optional; QI and keep it if present.
    visual_.As(&visual3_);
}

DCompCompositionVisual::~DCompCompositionVisual() {
    // Drop the surface first, then the visuals. The parent (container or root)
    // is expected to have removed this child already; if not, releasing our ref
    // is still safe — DComp keeps the node alive until the parent's next commit.
    surface_.Reset();
    rotateTransform_.Reset();
    visual3_.Reset();
    visual_.Reset();
}

void DCompCompositionVisual::SetOffset(float x, float y) {
    if (!visual_) return;
    // A raw float replaces any running animation on that axis (DComp semantics).
    visual_->SetOffsetX(x);
    visual_->SetOffsetY(y);
}

void DCompCompositionVisual::SetOpacity(float opacity) {
    if (visual3_) visual3_->SetOpacity(opacity);
}

void DCompCompositionVisual::SetClip(float left, float top, float right,
                                     float bottom) {
    if (!visual_) return;
    HRESULT hr = visual_->SetClip(D2D1::RectF(left, top, right, bottom));
    if (FAILED(hr)) Trace(kTag, "SetClip FAILED", hr);
}

void DCompCompositionVisual::ClearClip() {
    if (visual_) visual_->SetClip(static_cast<IDCompositionClip*>(nullptr));
}

void DCompCompositionVisual::StartOffsetSweep(const SweepSpec& spec) {
    if (!visual_ || !device_) return;
    if (ComPtr<IDCompositionAnimation> anim = MakeOffsetSweep(
            device_, spec.minX, spec.maxX, spec.cycleSec, spec.phaseDeg)) {
        visual_->SetOffsetX(anim.Get());  // compositor-thread animation
    } else {
        visual_->SetOffsetX(spec.minX);   // fallback: park at the left edge
        TraceMsg(kTag, "StartOffsetSweep: MakeOffsetSweep null (parked)");
    }
}

void DCompCompositionVisual::StartOffsetYTween(float fromPx, float toPx,
                                               double durationSec) {
    if (!visual_ || !device_) return;
    if (ComPtr<IDCompositionAnimation> anim =
            MakeOffsetTween(device_, fromPx, toPx, durationSec)) {
        visual_->SetOffsetY(anim.Get());  // compositor-thread decelerate tween
    } else {
        visual_->SetOffsetY(toPx);        // fallback: jump to target
        TraceMsg(kTag, "StartOffsetYTween: MakeOffsetTween null (jumped)");
    }
}

void DCompCompositionVisual::StartOpacityBlink(double halfPeriodSec) {
    if (!visual3_ || !device_) return;  // opacity needs IDCompositionVisual3
    if (ComPtr<IDCompositionAnimation> anim = MakeBlink(device_, halfPeriodSec)) {
        visual3_->SetOpacity(anim.Get());  // compositor-thread square wave
    } else {
        visual3_->SetOpacity(1.0f);       // fallback: a solid, non-blinking caret
        TraceMsg(kTag, "StartOpacityBlink: MakeBlink null (solid caret)");
    }
}

void DCompCompositionVisual::StartRotationSweep(double rps) {
    if (!visual_ || !device_) return;
    // Create the rotate transform on first use; it persists across multiple sweeps.
    if (!rotateTransform_) {
        HRESULT hr = device_->CreateRotateTransform(rotateTransform_.GetAddressOf());
        if (FAILED(hr)) {
            Trace(kTag, "CreateRotateTransform FAILED", hr);
            return;  // fallback: no rotation
        }
        // Center point: middle of the current surface (or 0,0 if no surface yet).
        // Updated in DrawSurface whenever the size changes.
        const float cx = surfaceW_ * 0.5f;
        const float cy = surfaceH_ * 0.5f;
        rotateTransform_->SetCenterX(cx);
        rotateTransform_->SetCenterY(cy);
        visual_->SetTransform(rotateTransform_.Get());
    }

    if (ComPtr<IDCompositionAnimation> anim = MakeRotationSweep(device_, rps)) {
        rotateTransform_->SetAngle(anim.Get());  // compositor-thread continuous rotation
    } else {
        rotateTransform_->SetAngle(0.0f);        // fallback: freeze at 0°
        TraceMsg(kTag, "StartRotationSweep: MakeRotationSweep null (frozen)");
    }
}

void DCompCompositionVisual::StopAnimation(CompositionProperty property) {
    if (!visual_) return;
    // Freeze the property at a static value (drops the animation). OffsetX and
    // Rotation are the active cases; OffsetY and Opacity exist for interface completeness.
    switch (property) {
        case CompositionProperty::OffsetX: visual_->SetOffsetX(0.0f); break;
        case CompositionProperty::OffsetY: visual_->SetOffsetY(0.0f); break;
        case CompositionProperty::Opacity: if (visual3_) visual3_->SetOpacity(1.0f); break;
        case CompositionProperty::Rotation:
            if (rotateTransform_) rotateTransform_->SetAngle(0.0f);
            break;
    }
}

bool DCompCompositionVisual::DrawSurface(uint32_t pixelW, uint32_t pixelH,
                                         const DrawCallback& draw) {
    if (!visual_ || !device_ || pixelW == 0 || pixelH == 0) return false;

    // (Re)create the surface when the size changed or it does not exist yet.
    //
    // NOTE ON COST: during a resize drag the requested size changes every frame, so
    // this branch is taken every frame — a fresh VRAM allocation plus the release of
    // the previous surface, per frame, per composited control.
    if (!surface_ || pixelW != surfaceW_ || pixelH != surfaceH_) {
        HRESULT hr = device_->CreateSurface(
            pixelW, pixelH, DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_ALPHA_MODE_PREMULTIPLIED, surface_.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            Trace(kTag, "CreateSurface FAILED", hr);
            surfaceW_ = surfaceH_ = 0;
            return false;
        }
        surfaceW_ = pixelW;
        surfaceH_ = pixelH;
        // A rotation started before the first draw (or before a resize) was centered
        // on the old surface; keep the pivot at the middle of the new one so the
        // content spins in place instead of orbiting a corner.
        if (rotateTransform_) {
            rotateTransform_->SetCenterX(surfaceW_ * 0.5f);
            rotateTransform_->SetCenterY(surfaceH_ * 0.5f);
        }
    }

    ComPtr<ID2D1DeviceContext> target;
    POINT off = {};
    // BeginDraw is where the CPU can be made to wait on the GPU: it maps a region of
    // the surface for CPU writes, which cannot proceed while the compositor is still
    // reading the previous contents.
    HRESULT hr = surface_->BeginDraw(nullptr, IID_PPV_ARGS(target.GetAddressOf()), &off);
    if (FAILED(hr)) {
        Trace(kTag, "surface BeginDraw FAILED", hr);
        return false;
    }
    // Translate to the surface origin (the atlas offset DComp hands back), then
    // let the caller draw. The caller clears/fills within [0,0,pixelW,pixelH].
    target->SetTransform(D2D1::Matrix3x2F::Translation(
        static_cast<float>(off.x), static_cast<float>(off.y)));
    if (draw) draw(target.Get(), static_cast<float>(off.x), static_cast<float>(off.y));
    surface_->EndDraw();
    hr = visual_->SetContent(surface_.Get());
    if (FAILED(hr)) { Trace(kTag, "SetContent FAILED", hr); return false; }
    return true;
}

void DCompCompositionVisual::AddChild(ICompositionVisual* child) {
    if (!visual_ || !child) return;
    auto* impl = static_cast<DCompCompositionVisual*>(child);
    // FALSE + NULL reference = render ABOVE existing children (the flag is
    // inverted from its name with a NULL reference; TRUE would hide it at the
    // bottom). Matches DCompHost::AddChildVisual and the PoC.
    HRESULT hr = visual_->AddVisual(impl->Raw(), FALSE, nullptr);
    if (FAILED(hr)) Trace(kTag, "AddChild AddVisual FAILED", hr);
}

void DCompCompositionVisual::RemoveChild(ICompositionVisual* child) {
    if (!visual_ || !child) return;
    auto* impl = static_cast<DCompCompositionVisual*>(child);
    visual_->RemoveVisual(impl->Raw());
}

// ---------------------------------------------------------------------------
// DCompCompositionBackend
// ---------------------------------------------------------------------------

DCompCompositionBackend::DCompCompositionBackend(DCompHost* host, D2DContext* d2d,
                                                 std::function<void()> requestCommit)
    : host_(host), d2d_(d2d), requestCommit_(std::move(requestCommit)) {}

std::unique_ptr<ICompositionVisual> DCompCompositionBackend::CreateVisual() {
    if (!host_) return nullptr;
    // Read the device live: a device-loss rebuild swaps DCompHost's device in
    // place, and controls recreate their visuals in OnDeviceRestored, so always
    // build against the current one.
    IDCompositionDevice2* device = host_->Device();
    if (!device) return nullptr;
    ComPtr<IDCompositionVisual2> visual;
    HRESULT hr = device->CreateVisual(visual.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        Trace(kTag, "CreateVisual FAILED", hr);
        return nullptr;
    }
    return std::make_unique<DCompCompositionVisual>(std::move(visual), device, d2d_);
}

void DCompCompositionBackend::AddToRoot(ICompositionVisual* visual) {
    if (!host_ || !visual) return;
    auto* impl = static_cast<DCompCompositionVisual*>(visual);
    host_->AddChildVisual(impl->Raw());
}

void DCompCompositionBackend::RemoveFromRoot(ICompositionVisual* visual) {
    if (!host_ || !visual) return;
    auto* impl = static_cast<DCompCompositionVisual*>(visual);
    host_->RemoveChildVisual(impl->Raw());
}

void DCompCompositionBackend::RequestCommit() {
    if (requestCommit_) requestCommit_();
}

} // namespace fluent
