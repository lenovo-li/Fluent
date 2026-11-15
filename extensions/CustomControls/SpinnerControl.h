// SpinnerControl.h — a completely custom animated control
#pragma once
#include "../../FluentUI/core/Control.h"
#include "../../FluentUI/animation/AnimatedValue.h"
#include "../../FluentUI/composition/ICompositionBackend.h"
#include <memory>

namespace fluent::custom {

class SpinnerControl : public Control {
public:
    SpinnerControl();
    void Start();
    void Stop();
    bool IsRunning() const { return running_; }

    void SetSpeed(float rps) { speed_ = rps; if (CompositorSpinning()) UpdateSpinSpeed(); }
    float Speed() const { return speed_; }

    void SetDiameter(float dip) { diameter_ = dip; InvalidateMeasure(); }
    float Diameter() const { return diameter_; }

    void Measure(float availW, float availH) override;
    void Render(const DrawingContext& dc) override;
    bool WantsAnimationTick() const override;
    void OnAnimationTick(float dtSec) override;

protected:
    // UI Automation peer (defined in this library's .cpp, on the public automation
    // headers: the extension path for UIA, see inheritance-guide.md).
    std::unique_ptr<automation::AutomationPeer> CreateAutomationPeer() override;

    // Compositor rotation lifecycle: keep the visual created, positioned, redrawn,
    // and torn down in step with the tree/device.
    void OnAttachedToTree() override;
    void OnDetachedFromTree() override;
    void OnBoundsChanged() override;
    void OnThemeChanged() override;
    void OnDpiChanged(float dpiScale) override;
    void OnDeviceLost() override;
    void OnDeviceRestored() override;
    void OnVisibilityChanged(bool visible) override;
    void OnAncestorVisibilityChanged() override;

private:
    // True when the spin is running on the compositor (running + attached +
    // a composition device was available). When false, running mode falls back
    // to the UI-thread spin.
    bool CompositorSpinning() const { return compositorActive_; }
    // Reconcile the spin visual with the current mode/attachment/device: create +
    // start it when it should run, stop + detach it otherwise. Idempotent.
    void UpdateSpinMode();
    // Push current bounds/DPI/theme into the spin visual (reposition, redraw the
    // ring surface). Called on attach / arrange / DPI / theme / device-restored.
    void SyncSpinGeometry();
    // Redraw the arc ring into the visual's surface.
    HRESULT DrawRing();
    // Start the rotation at speed_, or hold it still when speed_ <= 0.
    void ApplySpinSpeed();
    // Update the rotation speed (rps) on a running compositor spin.
    void UpdateSpinSpeed();

    bool running_ = true;
    float speed_ = 1.0f;         // revolutions per second
    float diameter_ = 32.0f;
    float rotation_ = 0.0f;      // UI-thread fallback: current angle
    AnimatedValue pulse_{1.0f};  // UI-thread fallback: scale pulse
    bool compositorActive_ = false;
    // Two visuals, because a rotating visual cannot clip itself: its clip rotates
    // with it. The container sits at the window-clipped bounds (so a spinner
    // scrolled half out of a ScrollPanel is cut at the viewport edge, which the D2D
    // clip does not do for composition visuals) and clips; the ring is its child at
    // the full, unclipped bounds and carries the rotate transform.
    std::unique_ptr<ICompositionVisual> spinContainer_;
    std::unique_ptr<ICompositionVisual> spinnerVisual_;  // the rotating ring
    uint32_t ringWpx_ = 0, ringHpx_ = 0;  // size of the last drawn ring surface
    bool ringRedrawPending_ = true;       // theme/first draw: redraw even if same size
    D2D1_COLOR_F lastRingColor_ = D2D1::ColorF(0, 0, 0, -1.0f); // cached; -1 = uninitialized
    float lastOpacity_ = -1.0f;           // cached EffectiveOpacity; -1 = uninitialized
};

} // namespace fluent::custom
