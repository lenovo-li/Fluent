// SpinnerControl.h — a completely custom animated control
#pragma once
#include "../../FluentUI/core/Control.h"
#include "../../FluentUI/animation/AnimatedValue.h"

namespace fluent::custom {

class SpinnerControl : public Control {
public:
    SpinnerControl();
    void Start();
    void Stop();
    bool IsRunning() const { return running_; }
    
    void SetSpeed(float rps) { speed_ = rps; }
    float Speed() const { return speed_; }
    
    void SetDiameter(float dip) { diameter_ = dip; InvalidateMeasure(); }
    float Diameter() const { return diameter_; }
    
    void Measure(float availW, float availH) override;
    void Render(const DrawingContext& dc) override;
    bool WantsAnimationTick() const override { return running_; }
    void OnAnimationTick(float dtSec) override;

private:
    bool running_ = true;
    float speed_ = 1.0f;
    float diameter_ = 32.0f;
    float rotation_ = 0.0f;
    AnimatedValue pulse_{1.0f};
};

} // namespace fluent::custom
