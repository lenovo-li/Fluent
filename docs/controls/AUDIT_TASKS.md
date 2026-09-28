# 控件文档审计与修复任务清单

审计日期：2026-09-24　　基线提交：`54380e2`（`fix/scroll-jitter`）

## 这份文档是什么

对 `docs/controls/` 下 26 份控件文档逐条核对头文件后得出的**可执行修复清单**。
每条都给出文档行号与源码中的正确形式，可直接照做。修完即可删除本文件。

核对方法：以头文件/`.cpp` 为准，逐个 API 签名、继承层次、事件名、主题 token 名、
脏标记等级、行为描述比对。所有断言均经 grep 复核，**不含未验证的推断**。

## 结论摘要

| 项 | 数量 |
|---|---|
| 已审计文档 | 26 份（全部） |
| 发现问题的文档 | 12 份 |
| 完全一致的文档 | 14 份 |
| 完全缺失文档的控件 | 26 个 |
| 需改源码注释（非文档） | 2 处 |
| 需用户决策 | 1 处 |

问题高度集中在早期写的几份：`Button.md`（8 个错，示例整体编不过）、
`Border.md`（整张 token 表不成立）、`GridSplitter.md`（整节行为与实现相反）、
`ScrollViewer.md`（引用已删除的组件）。

**质量良好、无需改动的 14 份**：TextBlock、TextBox、TextArea、RichTextBox、
ComboBox（仅 1 处失效链接）、ListBox、DataGrid、Image、MenuFlyout、Grid、Chart、
Metric、StackPanel（仅 1 处错）、GridSplitter 的签名部分。

其中 `MenuFlyout.md` 是全库范例：正确使用 `push_back(std::move(...))`、
明确标注拷贝已 delete、解释了 Event 为何堆分配。新写文档可参照它而非 TEMPLATE.md。

---

## 优先级 0：先修 TEMPLATE.md（会自我复制的错误源）

**为什么排最前**：README 指示「复制 `TEMPLATE.md` 为新控件文档」，而模板自身有 4 处
编不过的代码。`Button.md` 的同类错误极可能就是从它抄来的。修模板的收益高于修任何
单份文档——不修，以后每份新文档都会继承这些错。

`docs/controls/TEMPLATE.md`：

| 行 | 错误 | 正确形式 |
|---|---|---|
| 109 | `Application app;` | `Application app(hInstance);`（`Application.h:75` 只有 `explicit Application(HINSTANCE)`） |
| 113 | `window->SetSize({800, 600});` | `window->SetClientSize(800, 600);`（`Window.h:82`） |
| 119 | `window->SetContent(std::move(control));` | `window->SetRoot(std::move(control));`（`Window.h:170`） |
| 120 | `window->Show();` | `window->Show(app);`（`Window.h:66`，首参 `Application&` 无默认值） |

改完后建议在模板顶部加一句：示例代码必须能编译，参照 `MenuFlyout.md` 的写法。

---

## 优先级 1：行为描述与实现相反

这类最危险——照文档写出来的代码会是错的，且编译器不会报错。

### 1.1 GridSplitter.md — 整节行为已过期

起因：`54380e2`（2026-09-24）改了拖动的写回契约，文档未同步。

当前实现（`GridSplitter.cpp:161-181` 的 `writeBack`）：拖动**按原单位写回**，
Star↔Star 重分配星权重且权重和守恒；Star 对 Pixel/Auto 时 Star 保持 Star；
只有 Auto 被钉成 Pixel。头注释 `GridSplitter.h:1-21` 已是最新，可作为改写依据。

| 行 | 现文 | 处置 |
|---|---|---|
| 126 | 「此时相邻轨道已经是 `Pixel` 定义，可以直接从 Grid 读出 DIP 数值存盘」 | 删除此论断。改为：持久化应读 `Grid::ResolvedColumnSizes()`（已解析的 DIP），**不要**假设 `ColumnAt()` 返回 Pixel——Star 轨道拖动后仍是 Star，读到的是权重 |
| 184 | 示例注释「DragCompleted 时轨道已是 Pixel 定义」 | 同上改写。示例代码本身用 `ResolvedColumnSizes()`，**路径正确，只需改注释** |
| 198-205 | 整节「拖动后 Star 轨道变成 Pixel」，含「永久变成 Pixel」「不再按比例重新分配」「想保持比例只能应用自己换算回 Star」 | **整节重写**，按上述三条规则说明。并说明 Auto 是唯一被冻结的情况，及其原因（Auto 尺寸由内容决定，无可写回参数） |

补充建议：第 215-221 行只讲了尺寸快照，可补一句定义快照（`defBeforeStart_` /
`defAfterStart_`，`GridSplitter.h:120-124`）——它是按原单位写回的正确性前提。

### 1.2 Button.md — 脏标记写反，等于把已修的 bug 写回文档

| 行 | 现文 | 正确形式 |
|---|---|---|
| 57 | 「**脏标记**: Render」 | `Measure`。`Button.h:51` 是 `SetProperty(kind_, k, DirtyFlags::Measure)` |
| 244 | 「`SetKind` 仅触发 Render（只改变颜色，不影响布局）」 | 必须是 Measure。`Button.h:39-50` 用一整段解释原因：`Kind::Accent` 画 SemiBold，同一字符串更宽，desired 宽度会变。旧注释声称 Render-only 是**已修正的 bug** |

改写时把「为什么是 Measure」的理由写进去，否则后人可能再改回来。

### 1.3 Border.md — 默认值与 token 表均不成立

`Border.cpp` 里**一次 `Theme()` 调用都没有**（已 grep 确认为 0 次）。

| 行 | 现文 | 正确形式 |
|---|---|---|
| 102 | 「未调用 SetBackground / SetBorderColor 时走主题默认值」 | 默认画的是**透明**。`Border.h:107-110`：`hasBg_ = false` / `hasBorder_ = false`，Render 直接跳过填充与描边（`Border.cpp:53,63`）；`borderThickness_` 与 `cornerRadius_` 默认均为 0。头注释 `Border.h:24-50` 原话：「Unset (the default) paints no fill (transparent)」 |
| 117-120 | 整张 token 表（`cardFill` / `controlStrokeDefault` / `spacing.borderWidth` / `spacing.cornerRadiusNormal`） | **整表删除**。这四个 token 名在 `ThemeTokens.h` 中确实存在，但 Border 不读它们。改为一句说明：Border 不读主题，颜色完全由调用方提供，未提供即透明 |

顺带补两个漏掉的 API：`ClearBackground()`（`Border.h:51`）、`Child()`（`Border.h:43`）。

### 1.4 三份文档共有的 Margin 错误

`Border.md:88`、`Grid.md:142`、`InfoBar.md:202` 都写了「Border / GroupBox / Viewbox /
Expander 一致忽略子元素 Margin」。**这句一半是错的**：

- `Border` 与 `GroupBox` **尊重** Margin——两者都走 `FrameworkElement::ComputeArrangeRect`
  （`Border.cpp:45`、`GroupBox.cpp:122`），该函数首先做 `slot - child->Margin()`。
  `Border.cpp:43-44` 的注释明说：「Before this change Border ignored them; now it
  behaves like Panel does.」
- 只有 `Viewbox`（`Viewbox.cpp:91` 直接 Arrange natural size）与 `Expander`
  （`Expander.cpp:166` 直接 `content_->Arrange(contentRect)`）真的忽略。

连带处置：`Border.md:92-94` 那段「`child->SetMargin(Thickness{20})` 无效！」的示例
**必须删除或改成相反结论**——Border 会照做。

### 1.5 Rating.md — 注意：要改的是源码注释，不是文档

**文档是对的，不要动。** `Rating.md:128`「没有半星字形」正确：`Rating.cpp:17-18`
只定义了 `kFilledStar = U+2605` 与 `kEmptyStar = U+2606`，`.cpp:91` 的
`halfFills = (shown - whole) >= 0.5f` 让边界字形在小数 ≥0.5 时**整颗点亮**。

**需修的是 `Rating.h:14-15`**：头注释写「render as a half-filled glyph when the
fraction reaches 0.5」，声称了一个不存在的半星字形。应改为「0.5 及以上进位为整颗实心」。

文档端可选补充：`Rating.md:128` 前半句「可以存 3.5」易被读成截断，可明确写成
3.4 → 3 颗、3.6 → 4 颗（与 `Rating.h:14-17` 的举例一致）。

### 1.6 InfoBar.h — 同样是源码注释问题

`InfoBar.h:12` 说自己是「a plain FrameworkElement in the visual tree」，
但 `InfoBar.h:40` 实际是 `class InfoBar : public Control`。
`InfoBar.md:20` 沿用了这个错误措辞，而同份文档 `:31` 的继承图写 `Control → InfoBar`
（后者才对）——文档自相矛盾，根源在头注释。

处置：改 `InfoBar.h:12` 的措辞（意思是「参与常规布局、不是弹窗」，但不该说成
FrameworkElement），再统一 `InfoBar.md:20`。

---

## 优先级 2：示例代码编不过

### 2.1 Button.md 最小示例（26 份文档中唯一整体编不过的）

| 行 | 错误 | 正确形式 |
|---|---|---|
| 130 | `Application app;` | `Application app(hInstance);`（`Application.h:75`） |
| 148 | `window->Show();` | `window->Show(app);`（`Window.h:66`） |
| 157, 208 | `Orientation::Vertical` / `Orientation::Horizontal` | `StackPanel::Orientation::...`。全库 5 个 `Orientation` 全是嵌套枚举（StackPanel / WrapPanel / GridSplitter / Slider / Separator），**没有全局作用域的 Orientation** |
| 188-193 | `menu->SetItems({ MenuItem{L"Open"}, ... })` | 两处非法：(a) `MenuItem` 声明了 `MenuItem() = default` 与移动构造，C++20 下**不是 aggregate**，`MenuItem{L"Open"}` 不合法；(b) 拷贝构造 `= delete`（`MenuFlyout.h:71`），无法从 initializer_list 构造 vector。**照 `MenuFlyout.md:21-47` 改写**：逐个 `MenuItem m; m.text = ...; items.push_back(std::move(m));`，分隔线用 `MenuItem::Sep()` |
| 134-135 | 注释「窗口尺寸不是一个 setter：它通过 `Show()` 的 WindowState 传入」 | 事实错误。`Window.h:82` 有 `SetClientSize(float, float)`，另有 `SetAutoSize(bool)`。`WindowState` 是可选的**恢复**参数，不是设尺寸的唯一途径 |

### 2.2 ScrollViewer.md 示例

| 行 | 错误 | 正确形式 |
|---|---|---|
| 211-218 | `void MyList::OnMouseWheel(int delta, bool shiftKey)` | 元素级应重写 `UIElement::OnPointerWheelChanged(PointerEventArgs& e)`。`OnMouseWheel` 确实存在，但在 `NativeWindowHost.h:414`，签名是 `(float dipX, float dipY, int delta)`，是**窗口级**虚函数，与此处用法无关 |
| 214 | 示例用 `AnimateBy` 做水平滚动 | 与本文档 `:115` 和 `:260-261` 自相矛盾——水平轴无 tween（`ScrollViewer.h:157-161` 确认），应直接 `SetOffsetX` |
| 38-39 | 声明了显式构造函数 `ScrollViewer();` | 头文件无此声明，是隐式默认构造。文中描述的默认值本身与成员初值一致（`ScrollViewer.h:149,172`），只需删掉这段签名 |
| 189 | `sv->SetBounds(RectDip{...})` | 技术上可编译（`UIElement.h:57` 为 public），但绕过 Measure/Arrange 协议。建议改为交给父 Panel 布局 |
| 274 | 「完全可见时 35% alpha」 | 不准确。35% 是 `keepVisibleWhenOverflow_` 的最小可见度下限（`.cpp:341`）；拇指实际 alpha 是 `base * vis`，base 在 idle 为 0.32（亮）/0.40（暗）、hover 0.55、drag 0.65（`.cpp:357,374`） |

### 2.3 StackPanel.md

- `:57` `void ClearChildren();` → 不存在，真名是 `Panel::Clear()`（`Panel.h:52`）

---

## 优先级 3：主题 token 表错误

### 3.1 token 名不存在

`Button.md:276-277`：`controlFill` → `controlFillDefault`；`controlStroke` →
`controlStrokeDefault`（`ThemeTokens.h:79-82`）。另：Standard 边框实际走
`EffectiveBorderBrush()`，其无参回退才是 `controlStrokeDefault`（`Control.cpp:146`）。

### 3.2 文档说读 token，实际是硬编码字面值

| 文档 | 行 | 现文 | 事实 |
|---|---|---|---|
| Slider.md | 218 | 未填充轨道槽 = `controlFillDefault` | `Slider.cpp:196` 是 `EffectiveBackground(th.dark ? ColorF(1,1,1,0.18f) : ColorF(0,0,0,0.12f))`，硬编码回退，不读 token |
| ProgressBar.md | 178 | 未填充轨道槽 = `controlFillDefault` | `ProgressBar.cpp:64` 同类硬编码（0.16f / 0.10f） |
| Slider.md | 219 | 旋钮描边 = `controlStrokeDefault` | `.cpp:217` 是 `EffectiveBorderBrush(pal.accent)`，回退是 **accent** |
| Hyperlink.md | 170 | `textSecondary` 用于禁用态 | `Hyperlink.cpp` 中**无任何禁用态配色分支**（grep 无命中）。这可能是实现缺口，但文档描述的行为不存在 |
| NumericUpDown.md | 166 | `controlFillHover` = 输入区底色 | 输入区只用 `EffectiveBackground(pal.controlFillDefault)`（`.cpp:134`）；`controlFillHover`/`Pressed` 是**箭头按钮**的填充（`.cpp:160`）。另漏了 `focusStroke`（`.cpp:127` 的 `DrawFocusRing`）与 `textSecondary`（`.cpp:178/181/195`） |
| Rating.md | 142-145 | token 表 | 漏 `accentHover`（悬停预览，`.cpp:76`）与 `textSecondary`（禁用，`.cpp:78`）。`:145` 写「`spacing` 系列」偏含糊，实际只用 `spacing.spacingXSmall` |

### 3.3 需用户决策：focusStroke 的实现层不一致

`RadioButton.md:144`、`ToggleSwitch.md:184`、`Slider.md:220` 都把焦点环标为
`focusStroke`，但这三个控件**自己画环，用的是 `accent`**：
`RadioButton.cpp:106-110`、`ToggleSwitch.cpp:135`、`Slider.cpp:224`。

只有走 `styling/FocusVisual.h::DrawFocusRing` 的控件才真正读 `focusStroke`：
Button、CheckBox、Rating、NumericUpDown。

**这是实现层的真实不一致，不是文档笔误。** 两条路，需你定：

- **A. 照实改文档** — 三份文档标成 `accent`。省事，但把不一致固化下来；
  用户改 `focusStroke` 时会发现一半控件不响应。
- **B. 先统一实现** — 让自绘焦点环的控件也读 `focusStroke`，再统一文档。
  注意 `ThemeTokens.h:201` 写明 `accent` 的 setter 会连带设置 `focusStroke`，
  所以默认外观不变，但需在真机确认。属行为变更，要跑测试。

倾向 B：`focusStroke` 存在的意义就是让焦点环可单独定制，一半控件不读它等于这个
token 半残。但这是行为改动，请你确认。

---

## 优先级 4：私有成员被当作公开 API

这类会让人写出编不过的代码，且错误信息（access violation）不易联想到文档。

| 文档 | 行 | 问题 |
|---|---|---|
| CheckBox.md | 55 | `void Toggle();` 列在公开 API 下 → `ToggleButton.h:44` 是 **protected**，应用代码调不到。公开的只有 `SetChecked` / `IsChecked` |
| ToggleSwitch.md | 99-104 | `TrackW()` / `TrackH()` 列为「几何 API」→ `ToggleSwitch.h:62-63` 是 **private**。「轨道 40×20 固定」这个结论可以保留，但不该以公开 API 形式呈现 |
| Grid.md | 147 | 说 `ResolveTracks`「可单独测试」→ 它是 private static 且无 friend 声明，headless 测试无法直接调用 |

---

## 优先级 5：失效链接与指向已删除的组件

### 5.1 ScrollContentHost 已删除但仍被当作现存组件

该组件于 2026-08-31（`92f13bc`）移除，`FluentUI/composition/` 现仅有
`DCompCompositionBackend.{h,cpp}` / `ICompositionBackend.h` / `OverlaySignature.h`
（已确认目录内容）。仍在引用它的地方：

- **`docs/api-surface.md:92`** — 把 `composition/ScrollContentHost.h` 列为现存文件，
  描述「Overscan scrolling engine.」**必须删除此行**（这不在 controls/ 目录，容易漏）
- `ScrollViewer.md:287` — 链接到不存在的 `ScrollContentHost.md`，**删链接而非补文件**
- `ScrollViewer.md:116-117` — 提到 `EffectiveOffset()`，全库只剩注释，无定义。
  作为历史说明可保留，但要注明「已移除」，否则读者会以为有这个 API
- `ScrollViewer.md:107,131` — 「（compositor 用）」「暴露给缓存覆盖层 surface 的
  compositor host」描述的是已被移除的消费者。`HExpandFactor()` / `Visibility()` /
  `ExpandFactor()` 方法本身仍存在（`ScrollViewer.h:71,109,110`），只是已无 compositor
  host 在用

### 5.2 失效的 .md 链接（点击即 404）

| 来源 | 目标 | 处置 |
|---|---|---|
| Button.md:303-314 | `RepeatButton.md`、`ToggleButton.md`、`../primitives/ButtonBase.md` | 改纯文本或补文档 |
| ComboBox.md | `AutoSuggestBox.md` | 仓库无 AutoSuggestBox 类，**删链接** |
| ScrollViewer.md:285 | `TreeView.md` | 建议补文档（见优先级 6） |
| ScrollViewer.md:287 | `ScrollContentHost.md` | 删（组件已不存在） |
| README.md、TEMPLATE.md、Button.md 共 10 处 | `../ELEMENT_MODEL.md`、`../LAYOUT_GUIDE.md`、`../THEMING_GUIDE.md`、`../PERFORMANCE_BEST_PRACTICES.md` | `docs/` 下这四个文件**都不存在**。要么删链接，要么明确列为待写 |

### 5.3 其他不存在的引用

- `ScrollViewer.md:278` 提到 `ListView` → 仓库无此类，只有 `ListBox`
- `README.md:72` 列 `ColorPicker` 为控件 → 全库无此类（仅 README 与
  `MenuFlyout.md:388` 的一句反例提到）
- `README.md` 把 `ToolTip` 列为控件 → 实为 `services/TooltipService.h`，不是控件

---

## 优先级 6：README.md 自身的元数据错误

README 是文档入口，它的错误影响面最大。

| 问题 | 事实 |
|---|---|
| 声称「本目录目前有 21 份控件文档」 | 实际 26 份控件文档（+ README + TEMPLATE = 28 个 .md） |
| 标「文档待写」但**文件其实存在** | `Image.md`、`MenuFlyout.md`、`GridSplitter.md` 三份都在——应改成正常链接 |
| 有文档但 README 完全没列 | `RichTextBox.md`、`ScrollViewer.md` |
| 列了不存在的控件 | `ColorPicker`（删）、`ToolTip`（改为指向 TooltipService，或移出控件清单） |
| 新控件未收录 | `LogViewer`（高吞吐日志控件，README 完全没提，连「文档待写」都没有） |

另有一处过期注解：`InfoBar.md:37-38` 指责 `TEMPLATE.md` 与 `Button.md` 写了不存在的
`FluentUI::Controls` 命名空间。**两处均已修正为 `fluent`**，这条注解现在在指责一个
不存在的问题，应删除。

---

## 优先级 7：文档滞后于源码改动

源码已改但文档未同步的（日期取 git 最后提交）。这些文档的签名核对是通过的，
但**可能漏掉了新增的 API 或行为**，建议逐份复查一遍改动内容：

| 控件 | 源码 | 文档 |
|---|---|---|
| GridSplitter | 2026-09-24 | 2026-09-22（已在优先级 1.1 处理） |
| DataGrid | 2026-09-23 | 2026-08-27 |
| ListBox | 2026-09-23 | 2026-08-27 |
| ScrollViewer | 2026-09-23 | 2026-09-22 |
| Chart | 2026-09-22 | 2026-08-27 |
| TextArea | 2026-09-22 | 2026-08-27 |
| TextBlock | 2026-09-22 | 2026-08-27 |

复查方法：`git log --oneline <文档日期>..HEAD -- FluentUI/controls/<控件>.h` 看改了什么。

---

## 优先级 8：完全缺失文档的 26 个控件

工作量最大的一节，建议分批做，不要一次铺开。已排除纯基类与辅助类型
（`Panel`、`TextEditBase`、`ToggleVisuals`、`ProgressSweepVisual`、
`RichTextDocument`、以及 `primitives/` 下 5 个）。

数字为头文件行数，是工作量的粗略指标。

### 第一批：被其他文档引用或最常用（建议先做这 6 个）

| 控件 | 行数 | 为什么优先 |
|---|---|---|
| `TreeView` | 242 | 被 `ScrollViewer.md:285` 链接，当前是 404 |
| `ScrollPanel` | 268 | `StackPanel.md:109` 指引读者「外面套 ScrollPanel」，却无任何 API 记录 |
| `LogViewer` | 276 | 新增的高吞吐日志控件（10k 行/s 目标），README 连提及都没有 |
| `TabControl` | 268 | 常用导航控件 |
| `MenuBar` | 97 | 与已有 `MenuFlyout.md` 配套（MenuBar 用 `SetItemsBorrow`） |
| `ToolBar` | 108 | CommandBar 风格，溢出进 flyout |

### 第二批：对话框与常用输入

| 控件 | 行数 |
|---|---|
| `Expander` | 225 |
| `MessageDialog` | 51 |
| `ContentDialog` | 69 |
| `PasswordBox` | 33 |
| `Calendar` | 163 |
| `DatePicker` | 77 |

### 第三批：布局容器（README 已全部标为「文档待写」）

| 控件 | 行数 |
|---|---|
| `GroupBox` | 137 |
| `Viewbox` | 118 |
| `Canvas` | 117 |
| `UniformGrid` | 71 |
| `WrapPanel` | 59 |
| `DockPanel` | 48 |

### 第四批：小控件

| 控件 | 行数 |
|---|---|
| `RepeatButton` | 68（被 `Button.md` 链接） |
| `Separator` | 45 |
| `StatusBar` | 41 |

合计约 2600 行头文件需通读。**写作要求**：
- 参照 `MenuFlyout.md` 的风格，**不要**照抄 `TEMPLATE.md`（先修模板，见优先级 0）
- 每个 API 签名必须从头文件复制，不要凭印象写
- token 表必须 grep `.cpp` 确认真的读了该 token
- 脏标记等级必须查 `SetProperty(...)` 的第三个参数
- 写完在 `README.md` 对应类别下加链接，并更新文档总数

---

## 建议执行顺序

1. **优先级 0**（TEMPLATE.md，4 行改动）— 阻止错误继续复制
2. **优先级 1**（行为写反，6 项）— 危害最大，其中 1.5 / 1.6 改的是源码注释
3. **优先级 2**（示例编不过，3 份文档）
4. **优先级 3**（token，6 份文档）— 但 3.3 需先等你对 focusStroke 拍板
5. **优先级 4-6**（私有 API、失效链接、README 元数据）— 机械性修改，可一次做完
6. **优先级 7**（滞后复查，7 份）
7. **优先级 8**（补 26 份文档）— 按批次单独排期

1-6 项都是纯文档改动，不影响编译。**例外**：优先级 1.5（`Rating.h`）、1.6
（`InfoBar.h`）改的是头文件注释，以及 3.3 方案 B 会动实现代码——这三项之后需要
按仓库惯例跑 MSBuild Debug + Release 与 `x64/Debug/FluentUITests.exe`。

---

## 审计范围与未覆盖项（诚实说明）

**已验证**：26 份文档中约 150 个 API 签名逐个 grep 比对；继承层次对照实际基类；
token 名对照 `ThemeTokens.h` 并在 `.cpp` 中确认是否真的读取；脏标记对照
`SetProperty` 调用；所有 .md 链接的目标文件存在性。

**未验证**：

- **示例代码未实际编译过。** 「编不过」的判断基于 API 签名比对（如
  `Application` 无默认构造、`MenuItem` 拷贝已 delete），推理可靠，但没有真正
  把示例投进编译器。补文档时应实际编译验证。
- **未运行构建或测试。** 本次是纯文档审计，未改任何代码，因此没有跑
  MSBuild 或测试套件。优先级 1.5 / 1.6 / 3.3-B 落地后必须补跑。
- **视觉正确性无法从此处验证。** `focusStroke` 统一（3.3-B）若实施，焦点环的实际
  观感需要在真机上确认——headless 测试看不到像素。
- **优先级 7 的 7 份文档只核了签名**，未逐条比对源码改动内容，可能仍有遗漏的新 API。
