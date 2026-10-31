// Slider.h — Fluent horizontal slider with animated fill.
//
// A track + filled portion + draggable thumb. The fill width and thumb position
// animate to the current value via the host's per-frame tick. Arrow keys step
// the value; dragging sets it immediately.
//
// Slider is a RangeBase: RangeBase owns min_/max_/value_ and the
// coerce pipeline; Slider adds the step grid (CoerceValue override), the keyboard
// and drag input, the animated fill (animFill_), and the ValueChanged event.
#pragma once

#include "primitives/RangeBase.h"
#include "../animation/AnimatedValue.h"
#include "../base/Event.h"

namespace fluent {

class Slider : public RangeBase {
public:
    // UI Automation peer (automation/RangeAndContainerPeers.h).
    std::unique_ptr<automation::AutomationPeer> CreateAutomationPeer() override;
    enum class Orientation { Horizontal, Vertical };

    Slider() { SetFocusable(true); }

    // Step is Slider-specific (ProgressBar has no concept of step).
    void SetStep(float v) { step_ = std::max(0.0001f, v); }
    float Step() const { return step_; }

    // Orientation: Horizontal (default) lays the track left-to-right with the thumb
    // moving horizontally; Vertical lays it top-to-bottom with the thumb moving
    // vertically. Affects Measure, input mapping, and rendering.
    void SetOrientation(Orientation o) { SetProperty(orientation_, o, DirtyFlags::Measure); }
    Orientation GetOrientation() const { return orientation_; }

    // Aliases for the WPF-style Min/Max names the demo uses.
    void SetMin(float v) { SetMinimum(v); }
    void SetMax(float v) { SetMaximum(v); }

    // Fired when the value changes (drag, arrow keys, or SetValue). Payload is the
    // new value. Replaces SetOnChange(std::function<void(float)>).
    //
    // During a drag, ValueChanging fires for every intermediate position, while
    // ValueChanged fires ONLY when the drag completes (pointer release). Keyboard
    // navigation and programmatic SetValue fire ValueChanged immediately (no drag).
    Event<Slider, float>& ValueChanged() { return valueChanged_; }

    // Fired during a drag for every intermediate value as the thumb moves. NOT fired
    // for keyboard navigation or programmatic SetValue — those are not previews, they
    // are the committed value, so they skip straight to ValueChanged.
    //
    // Use to update a live preview (volume control, brightness) while leaving the
    // final commit (write to settings, raise app-level PropertyChanged) to
    // ValueChanged. Without this, every pixel the thumb drags would flush settings.
    Event<Slider, float>& ValueChanging() { return valueChanging_; }

    void OnKeyDownRouted(KeyEventArgs& e) override;

    bool WantsAnimationTick() const override;
    // Adopt the initial state outright on first layout rather than easing into it,
    // so a control built already-set does not animate the first time it is shown.
    void SnapAnimationsToSettledState() override { animFill_.SetImmediate(value_); }
    void OnAnimationTick(float dtSec) override;

    void Render(const DrawingContext& dc) override;
    void Measure(float availW, float availH) override;

    // The thumb circle + focus ring are drawn CENTERED on the track and, while
    // dragging/focused, extend beyond the slider's layout bounds (a thin track
    // but a fat thumb+ring). Partial redraw clips + clears only the dirty rect,
    // so a bounds-sized dirty rect would clip the thumb (flat-topped "capsule")
    // and leave residue where the ring overflowed a previous frame.
    float VisualOverflowDip() const override;

    // Drag semantics (routed): press captures the pointer and jumps the value to
    // the click position; move updates while captured; release drops capture.
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerReleased(PointerEventArgs& e) override;

protected:
    // RangeBase hook: fire the typed event. Which event depends on whether a drag is
    // in flight — mid-drag values are a preview (ValueChanging) and the commit is
    // deferred to the release, everything else is already committed.
    void OnValueChanged(float /*old*/, float newVal) override {
        if (dragging_) {
            valueChanging_.Raise(*this, newVal);
            // Remember that the drag moved the value, so the release knows whether it
            // owes a ValueChanged. A press that never moves must not fire one.
            dragChangedValue_ = true;
        } else {
            valueChanged_.Raise(*this, newVal);
        }
    }
    // RangeBase hook: clamp + step-snap.
    float CoerceValue(float v) const override;

    void OnStateChanged() override { Invalidate(); }
    void OnFocusChanged() override { Invalidate(); }

    // --- Extension points for derived classes --------------------------------
    //
    // A LogSlider / DecibelSlider overrides CoerceValue (above) to snap to its own
    // grid, and OnValueChanged to react — calling Slider::OnValueChanged first so
    // ValueChanging/ValueChanged keep firing. These read-only accessors let such a
    // subclass tell a drag from a keyboard/programmatic change and paint a custom
    // tick scale against the same track geometry the thumb and the hit-test use.
    // The geometry stays computed here (not stored) so a subclass cannot make the
    // painted track and the pointer mapping disagree.

    // True between pointer press and release. Inside OnValueChanged this is
    // exactly the "mid-drag preview" condition Slider itself branches on.
    bool IsDragging() const { return dragging_; }
    // The eased value currently painted (lags value_ while animating). Not named
    // AnimatedValue(): a member function of that name hides the AnimatedValue TYPE
    // inside the class scope and breaks the animFill_ declaration below.
    float DisplayedValue() const { return static_cast<float>(animFill_); }
    // Track endpoints in window DIPs along the slider's axis (x for Horizontal,
    // y for Vertical), and the track's cross-axis centerline.
    float TrackStart() const {
        return orientation_ == Orientation::Horizontal ? TrackLeft() : TrackTop();
    }
    float TrackEnd() const {
        return orientation_ == Orientation::Horizontal ? TrackRight() : TrackBottom();
    }
    float TrackCenterline() const {
        return orientation_ == Orientation::Horizontal ? TrackY() : TrackX();
    }
    // Map a value to its position on the track axis (window DIPs) — the inverse of
    // the pointer mapping, so ticks drawn at ValueToTrackPos(v) line up with where
    // a click lands v. Unclamped: out-of-range values extrapolate.
    float ValueToTrackPos(float v) const;

private:
    float NormalizedAnim() const;
    float TrackLeft()  const;
    float TrackRight() const;
    float TrackY()     const;
    float TrackTop()   const;
    float TrackBottom() const;
    float TrackX()     const;
    float ValueFromX(float dipX) const;
    float ValueFromY(float dipY) const;
    void  StepBy(float delta);

    Orientation orientation_ = Orientation::Horizontal;
    float step_ = 1.0f;
    AnimatedValue animFill_{0.0f};  // eased fill, same units as value_
    bool  dragging_ = false;
    bool  dragChangedValue_ = false;  // true when a drag has moved the value (owes a commit event on release)
    Event<Slider, float> valueChanged_;
    Event<Slider, float> valueChanging_;
};

} // namespace fluent
