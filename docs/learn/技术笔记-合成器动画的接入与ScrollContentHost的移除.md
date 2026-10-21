# 合成器动画的接入与 ScrollContentHost 的移除

> **问题本质**：`ICompositionBackend` 让动画跑在 DirectComposition 合成器线程上，UI 线程卡住也不掉帧。
> 代价是：合成器视觉是一棵**独立于元素树**的视觉树，元素树上自动成立的东西（裁剪、透明度、可见性、
> 设备生命周期、DPI）在那边全部要手工同步。
>
> 本文记录接入它需要处理的每一件事（以 ProgressBar 的不确定态扫动为例），以及合成器驱动滚动
> （ScrollContentHost）被整体移除的原因。API 规则本身见 `composition/ICompositionBackend.h`，
> 裁剪与坐标系细节见《技术笔记-合成器视觉的裁剪与坐标系》。

---

## 一、什么时候值得用

只在"UI 线程繁忙时动画仍必须平滑"的场合用。目前唯一的生产用户是 ProgressBar 的不确定态扫动：
它的用途就是"程序正忙时告诉用户还活着"，恰恰是 UI 线程最可能卡住的时刻。

普通的悬停/按下过渡、滚动，都走 UI 线程的 `AnimatedValue` + `OnAnimationTick`，不要为了"更平滑"
引入合成器视觉——接入成本见下文，远高于一个 tick。

## 二、接入清单（ProgressBar 实际处理的每一项）

一个使用合成器视觉的控件，需要覆盖下面这些钩子。漏掉任何一个都对应一个已出现过的 bug。

| 时机 | 必须做的事 | 漏掉的后果 |
|---|---|---|
| `OnAttachedToTree` | 取 `Window()->Composition()`，**为 null 则退回 UI 线程路径**；创建视觉并 `AddToRoot` | 无合成环境或设备丢失时崩溃 |
| `OnDetachedFromTree` | `StopSweep` + `RemoveFromRoot` + 销毁，趁宿主和设备还有效 | 视觉残留在根上，悬空 |
| `OnBoundsChanged` | 按新几何重设位置/裁剪，**相位连续**地重拟合动画范围 | 调整大小时扫动段跳变 |
| `OnVisibilityChanged` / `OnAncestorVisibilityChanged` | 不可见时停止并移除 | 父容器隐藏后扫动仍画在原处 |
| `OnThemeChanged` | 强制重画（尺寸没变时 `SetGeometryPx` 不会重画） | 切主题后颜色不变 |
| `OnDpiChanged` | 按新 DPI 重算像素几何 | 跨屏后尺寸错误 |
| `OnDeviceLost` | 只丢弃，不要碰已死的设备 | 设备丢失时访问失效对象 |
| `OnDeviceRestored` | 重建并重启 | 恢复后扫动消失 |

另外三个不明显的点：

- **坐标是物理像素。** 合成器视觉是像素空间，控件的 DIP 边界要乘 `Context().dpiScale` 再传。
- **几何取裁剪后的矩形，不是 `bounds_`。** 在 ScrollPanel 里 `bounds_` 可能远在视口外（实测
  y=-506），而合成器视觉不受容器的 D2D 裁剪约束。用 `WindowClippedBounds()`；
  **矩形为空时整个跳过**，并把"启动动画"的请求挂起（`sweepStartPending_`）等到有真实几何时再执行，
  否则请求被吞掉，扫动永远不启动。
- **祖先透明度不会自动到达。** 父级的 `SetOpacity` 折进的是 `DrawingContext`，到不了合成器视觉。
  需要的话把 `EffectiveOpacity()` 推给 `ICompositionVisual::SetOpacity`（ProgressBar 目前没做）。

## 三、往合成器表面绘制：只能预乘变换

`DrawSurface` 的回调拿到的表面是共享图集里的一块 tile，tile 原点每次绘制都可能变，传入的变换已经
包含这个平移。**必须 `GetTransform` 后 `mine * base` 预乘，绝不能直接 `SetTransform`**——覆盖会
丢掉图集平移，绘制落到别的 tile 上，随 tile 重新分配而闪烁。窗口 DIP → tile 的映射用
`SurfaceTransformFromWindowDip()`。

## 四、为什么单元测试抓不到这些

`tests/framework/FakeCompositionBackend.h` 让合成行为能在无窗口、无 GPU 的环境下测试，但它给绘制
回调传的是 **null dc 和零偏移**。所以：

- 变换覆盖类 bug（第三节）在假后端上不会出现任何症状；
- 像素是否画对、是否闪烁、是否平滑，只能在真机上看。

假后端能验证的是**生命周期与调用顺序**：何时创建、何时移除、何时启动/停止、是否在 null 时退回。
新控件的测试应该覆盖第二节表格里的每一行。

## 五、ScrollContentHost：合成器驱动滚动，已移除

### 5.1 它做了什么

把视口外加一圈余量的内容画进一个 DComp 表面，滚动时只让合成器平移这个表面，于是短距离滚动与
UI 线程帧率无关。TextArea 和 TreeView 曾经用它；当时这两个控件有"composited 模式"，在该模式下
`Render()` 直接返回，透明度、命中测试都要单独处理（命中测试要读合成器上的实际偏移，而不是目标偏移）。

### 5.2 为什么移除

持续出现**白屏**：滚动 tween 偶尔让表面整块变空白。多次尝试复现，根因始终没有定位。合成器驱动滚动
被整体移除，TextArea 和 TreeView 改回只在 UI 线程滚动（通过 `ScrollViewer`）。代码里一些注释仍提到
ScrollContentHost，那是历史引用，类本身已不存在。

### 5.3 如果将来重新引入

有一个失败模式是已经弄清楚的：**tween 在提交时就把表面原点烘焙进去了。** 所以在 tween 运行期间
重新设定原点（re-base），必须拆成两次 `Commit()`：

1. 先在旧坐标下冻结当前位置并提交；
2. 再重设原点、启动新 tween 并提交。

合成一次提交会让内容跳动一个"原点差"。但要注意：**这个修复本身没有治好白屏**，白屏是另一个未知原因。
重新引入前，应先能稳定复现白屏，否则会重走一遍同样的路。
