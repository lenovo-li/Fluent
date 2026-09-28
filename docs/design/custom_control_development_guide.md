# 完全自定义控件开发指南

**日期**: 2026-09-27  
**状态**: ✅ 已验证，CustomControls.lib 编译成功

---

## 核心问题回答

### 用户能否基于 FluentUI 开发完全自定义的控件（带动画）？

**✅ 完全可以！** 而且有**两种方式**：

---

## 方式一：继承具体控件（视觉调整）

适用场景：基于现有控件添加功能或修改外观。

### 示例：LunarCalendar（继承 Calendar）

```cpp
class LunarCalendar : public Calendar {
protected:
    void RenderCell(const DrawingContext& dc, const CellInfo& cell) override {
        // 1. 调用基类，复用标准渲染
        Calendar::RenderCell(dc, cell);
        
        // 2. 在日期下方加农历
        std::wstring lunar = GetLunarDate(cell.year, cell.month, cell.day);
        dc.DrawText(lunar.c_str(), ...);
    }
};
```

**你能做什么**：
- ✅ 读取内部状态（通过 `protected` 访问器：`CellWidth()`, `HoverCell()`）
- ✅ 复用部分渲染（调用 `RenderCell()`, `RenderBackground()`）
- ✅ 访问几何辅助（`CellRect()`, `ValueToTrackPos()`）
- ✅ 使用主题令牌（`Theme().colors`, `EffectiveForeground()`）

**限制**：
- ❌ 不能修改 `private` 成员（如 `Calendar::cellW_` 仍是 private）
- ❌ 不能改变基类的布局算法（除非完全重写 `Measure`）

---

## 方式二：从 Control 基类构建（完全自定义）

适用场景：创建全新的控件，不依赖任何现有控件。

### 示例：SpinnerControl（旋转脉冲加载指示器）

```cpp
class SpinnerControl : public Control {
public:
    void Start();
    void Stop();
    
    // 自定义布局
    void Measure(float availW, float availH) override {
        float w = IsAuto(Width()) ? diameter_ : Width();
        SetDesired(SizeDip(w, diameter_));
    }
    
    // 注册动画
    bool WantsAnimationTick() const override { return running_; }
    
    // 每帧回调（60fps）
    void OnAnimationTick(float dtSec) override {
        rotation_ += speed_ * 2.0f * kPi * dtSec;  // 旋转
        pulse_.Approach(pulseTarget, dtSec, 0.15f);  // 脉冲
        Invalidate();
    }
    
    // 自定义渲染
    void Render(const DrawingContext& dc) override {
        // 绘制 8 个旋转的圆弧，渐变透明度
        for (int i = 0; i < 8; ++i) {
            float angle = rotation_ + (2*pi/8) * i;
            float opacity = 1.0f - (i / 8.0f);
            dc.DrawLine(arcStart, arcEnd, color, 3.0f);
        }
    }

private:
    bool running_ = true;
    float rotation_ = 0.0f;
    AnimatedValue pulse_{1.0f};  // 帧率无关的平滑动画
};
```

**你能做什么**：
- ✅ **完全自定义动画**：`WantsAnimationTick()` 每帧返回 true → 宿主调用 `OnAnimationTick(dtSec)`
- ✅ **AnimatedValue 辅助**：帧率无关的指数逼近（就像 ToggleSwitch 的滑动旋钮）
- ✅ **Direct2D 绘制**：通过 `DrawingContext` 调用所有 D2D 图元（线、矩形、圆、路径、文本）
- ✅ **主题集成**：`EffectiveForeground()`, `EffectiveBackground()`, `Theme().colors`
- ✅ **输入处理**：override `OnPointerPressed`, `OnKeyDownRouted`, `OnPointerMoved`
- ✅ **焦点管理**：`SetFocusable(true)`, `IsFocused()`, 绘制焦点环
- ✅ **布局参与**：`Measure()` 报告期望尺寸，`Arrange()` 接收最终位置

**与 ToggleSwitch/Button 用的是同一套基础设施**：
- `AnimatedValue` - 同一个辅助类
- `WantsAnimationTick` / `OnAnimationTick` - 同一套宿主帧循环
- `DrawingContext` - 同一个 Direct2D 包装器
- `Theme()` - 同一套主题令牌

---

## 动画机制详解

### 1. AnimatedValue（帧率无关的平滑动画）

```cpp
AnimatedValue rotation_{0.0f};

void OnAnimationTick(float dtSec) {
    float target = someCalculation();
    rotation_.Approach(target, dtSec, tau);  // tau = 时间常数（秒）
    
    // 使用：直接转换为 float
    float current = static_cast<float>(rotation_);
    dc.DrawSomething(..., current);
}

bool WantsAnimationTick() const override {
    return rotation_.Animating(target, 0.001f);  // 未到达目标时返回 true
}
```

**特点**：
- 指数逼近（exp easing），不是线性
- 帧率无关：60fps 和 30fps 看起来一样平滑
- 自动停止：到达目标后 `WantsAnimationTick()` 返回 false，宿主停止调用

### 2. 宿主帧循环

```
Host (NativeWindowHost)
  ↓ 每帧（DWM vsync，通常 60fps）
  ↓ 调用 WantsAnimationTick() 检查
  ↓ 是 → 调用 OnAnimationTick(dtSec)
  ↓ 控件更新状态 + Invalidate()
  ↓ 宿主重绘 → Render()
```

**你不需要创建定时器或线程**，只需：
1. `WantsAnimationTick()` 返回 true
2. `OnAnimationTick(dtSec)` 更新状态
3. 调用 `Invalidate()` 标记脏区

---

## 实际示例对比

| 控件 | 继承自 | 动画 | 用途 |
|------|--------|------|------|
| **LunarCalendar** | Calendar | 无 | 演示：继承具体控件，复用渲染 |
| **BigButton** | Button | 继承基类动画 | 演示：读取状态（`CurrentKind()`），自定义内容 |
| **LogSlider** | Slider | 继承基类动画 | 演示：重写 `CoerceValue()`，自定义刻度 |
| **SpinnerControl** | Control | ✅ 自定义动画 | 演示：从零构建，完全自定义渲染+动画 |

---

## 文件位置

### CustomControls 库（独立库，演示用户如何扩展 FluentUI）

```
examples/CustomControls/
├── CustomControls.h           # 公开 API
├── CustomControls.vcxproj     # Visual Studio 项目
├── LunarCalendar.cpp          # 继承 Calendar
├── BigButton.cpp              # 继承 Button
├── LogSlider.cpp              # 继承 Slider
├── SpinnerControl.h/.cpp      # 继承 Control（完全自定义）
├── README.md                  # 为何独立库优于源码树内示例
└── INTEGRATION.md             # 如何链接到 FluentUIDemo（TODO）
```

### 编译输出

```
x64/Debug/CustomControls.lib     # 用户库
x64/Debug/FluentUI.lib           # 核心框架（不变）
```

---

## 架构设计：独立库模式

```
FluentUI.lib (框架核心，72 MB lib → 1.8 MB exe)
    ↓ 用户 #include FluentUI 头文件，链接 FluentUI.lib
CustomControls.lib (用户扩展库，本示例)
    ↓ 用户 #include CustomControls.h，链接两个 lib
YourApp.exe (最终应用)
```

**为什么不把自定义控件放进 FluentUI 源码？**
- ✅ 边界清晰：FluentUI = 稳定核心，CustomControls = 用户代码
- ✅ FluentUI.lib 不会随示例膨胀
- ✅ 用户 fork CustomControls，不 fork FluentUI
- ✅ 演示真实集成路径

---

## 限制与约束

### ❌ 你不能做的

1. **不能绕过 DrawingContext**：所有绘制必须通过 `DrawingContext` 的类型化方法
   - 原因：透明度、脏区裁剪、draw-call 计数都在这里统一处理
   - 例外：宿主窗口镶边（title bar）可以访问原始 `ID2D1DeviceContext`

2. **不能访问其他控件的 private 成员**：即使在同一个库里
   - Button::kind_ 是 private，你的控件无法读取
   - 只能通过 protected API（如 `CurrentKind()`）访问

3. **不能在 Render() 里修改布局**：
   - `Measure` / `Arrange` 和 `Render` 是分离的阶段
   - `Render()` 里调用 `SetDesired()` 或 `InvalidateMeasure()` 会被忽略

4. **不能阻塞 UI 线程**：
   - `OnAnimationTick()` 每 16ms 调用一次（60fps），必须立即返回
   - 耗时操作要放后台线程或异步

### ✅ 你能做的（完整清单）

| 能力 | API | 示例 |
|------|-----|------|
| **动画** | `AnimatedValue`, `WantsAnimationTick`, `OnAnimationTick` | SpinnerControl 旋转+脉冲 |
| **绘制** | `DrawingContext` 的 11 个方法 | `DrawLine`, `FillEllipse`, `DrawText` |
| **主题** | `Theme().colors`, `Effective*()` | `EffectiveForeground()` |
| **输入** | `OnPointerPressed`, `OnKeyDownRouted` | Button 的点击 |
| **焦点** | `SetFocusable`, `IsFocused` | ToggleSwitch 的焦点环 |
| **布局** | `Measure`, `Arrange`, `Bounds()` | SpinnerControl 的自然尺寸 |
| **状态** | `State()`, `IsHovered`, `IsPressed` | Button 的 hover 高亮 |

---

## 下一步

1. **✅ 已完成**：CustomControls.lib 编译成功
2. **🚧 TODO**：集成到 FluentUIDemo，添加演示页面
3. **🚧 TODO**：真机验证 SpinnerControl 的动画流畅度

---

## 总结

**是的，用户完全可以基于你的库开发自定义控件（带动画）**，而且有两种路径：

1. **继承具体控件**（如 Calendar/Button/Slider） - Phase 1 已实现的 protected API
2. **从 Control 基类构建** - SpinnerControl 演示的完整路径

两种方式都：
- ✅ 使用相同的动画基础设施（`AnimatedValue`, frame tick）
- ✅ 使用相同的渲染接口（`DrawingContext`）
- ✅ 集成主题系统（`Theme()`, `Effective*（）`）
- ✅ 不需要修改 FluentUI 源码

**SpinnerControl 是完全自定义控件的模板**：从零开始，只依赖 Control 基类，完全自定义动画和渲染。
