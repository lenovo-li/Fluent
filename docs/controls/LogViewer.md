# LogViewer 控件

## 概述

只读的高吞吐日志查看器。每一行是若干**带样式的片段**（`TextSpan`：颜色、字号、字重、下划线），
所以一行里可以同时有红色的 `[ERROR]`、加粗的时间戳和青色带下划线的地址。目标是每秒追加
上万行、文档上亿行时仍然只为可见行付出成本。

没有插入符、不能编辑，但可以选择并复制文字。要编辑请用 `TextArea`（单一样式、虚拟化），
要富文本编辑请用 `RichTextBox`。

**典型使用场景**:
- 设备/服务的实时日志（`tail -f` 式尾随 + 环形上限）
- 带级别着色的诊断输出
- 需要从中复制错误信息的只读长文本

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ LogViewer
```

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/LogViewer.h`

---

## 公开 API

### 构造函数

```cpp
LogViewer();
```

默认可聚焦（Ctrl+C / Ctrl+A / Esc 要能送到它）、尾随开启、无行数上限、允许选择。
滚动条用白色墨水（日志底色默认是黑色，主题墨水在黑底上看不见），空闲 3 秒后才淡出。

### 嵌套类型与数据

```cpp
struct TextSpan {
    std::wstring text;
    D2D1_COLOR_F color = D2D1::ColorF(1.0f, 1.0f, 1.0f);
    float fontSize = 12.0f;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    bool underline = false;
};

struct LogTextPos { size_t line = 0; size_t column = 0; };  // 可比较：先比行再比列
```

`TextSpan`、`LogLine`、`LogTextPos` 都是 `namespace fluent` 级类型，不是嵌套类型。

### 内容

```cpp
void AppendLine(std::vector<TextSpan> spans);
void Clear();
void SetMaxLines(int maxLines);   int MaxLines() const;   // 0 = 不限
size_t LineCount() const;
```

- **`AppendLine` 是 O(1) 均摊**，环形缓冲已满时也是。新行的高度先用估计值
  （最大字号 × 1.2），第一次绘制时才实测。空的片段列表会被忽略。
- **`SetMaxLines`**：超出上限时丢弃最旧的行。负数按 0（不限）处理。降低上限立即生效。
  一次裁剪的成本是 O(被丢弃的行数)，与保留的行数无关。
- **裁剪会清掉选区**：行被重新编号后，旧的 (行, 列) 指向的已经是别的文字。

### 滚动与尾随

```cpp
void SetAutoScrollToTail(bool enable);   // 默认 true
bool AutoScrollToTail() const;
```

只有视图本来就在底部时，新行才把它带到新的底部；用户向上滚动后尾随暂停，滚回底部恢复。

### 选择与剪贴板

```cpp
void SetSelectionEnabled(bool enable);   bool SelectionEnabled() const;
bool HasSelection() const;
LogTextPos SelectionAnchor() const;      // 固定端
LogTextPos SelectionActiveEnd() const;   // Shift+键 / Shift+点击 移动的那一端
void SetSelectionPoint(LogTextPos p);    // 等同于一次普通点击
std::wstring SelectedText() const;       // 行之间用 '\n' 连接
void SelectAll();
void ClearSelection();
bool CopySelectionToClipboard() const;   // 无选区或剪贴板被占用时返回 false
```

双击选词、三击选整行，拖动时保持该粒度。关掉选择（`SetSelectionEnabled(false)`）会同时
清掉现有选区，并省下拖选时命中测试用的布局。

### 诊断接口（给测试用）

```cpp
float ContentExtentDip() const;               // 文档总高度（未测量的行用估计值）
float LineHeightDip(size_t index) const;
float LineTopDip(size_t index) const;         // 行顶的内容坐标
unsigned SpanLayoutsBuilt() const;            // 构造以来创建的片段布局数
size_t CachedLayoutLines() const;             // 当前持有缓存布局的行数
void PrepareVisibleLayoutsForTest();          // 做 Render 绘制前的那一步缓存准备
float CachedLineWidthForTest(size_t index) const;
```

这些是公开的，因为要守住的成本在像素上看不出来：一个每帧给每行重建布局的实现，画出来
和正确实现一模一样。计数器就是可观测量。

---

## 代码示例

### 最小示例

```cpp
#include "FluentUI/controls/LogViewer.h"

auto* log = panel->Add(std::make_unique<LogViewer>());
log->SetHeight(320.0f);           // 没有自然高度，见「使用注意事项」
log->SetMaxLines(100000);         // 环形上限
log->AppendLine({
    {L"[ERROR] ", D2D1::ColorF(D2D1::ColorF::Red), 12.0f, DWRITE_FONT_WEIGHT_BOLD},
    {L"Connection timeout at "},
    {L"192.168.1.1:8080", D2D1::ColorF(D2D1::ColorF::Cyan), 12.0f,
     DWRITE_FONT_WEIGHT_NORMAL, true},
});
```

---

## UI Automation

LogViewer 的 peer（`automation/DataPeers.h`）：

| 项 | 值 |
|---|---|
| ControlType | List |
| 模式 | Scroll（纵向） |
| 子节点 | 每一行一个 ListItem，Name = 该行文本（最多 1024 字符），ScrollItem 走新的公开 `ScrollLineIntoView` |
| 命中测试 | 新的公开 `LineIndexAt`，二分查找，O(log n) |

行按序号作 key（新的公开 `FirstLineSerial()` + 下标），`SetMaxLines` 从前面裁掉旧行时序号继续增长：客户端停在第 900 行时，前面的行掉出去不影响它的节点，被裁掉的行的节点被释放。新的公开 `LineText` / `LineRect` / `VerticalOffset` / `MaxVerticalOffset` / `SetVerticalOffset`（滚离底部会停止跟随尾部，与滚轮相同）。

**只暴露视口附近的行。** 客户端遍历子节点时，得到的是可见行加上下各一页（最多 256 行），不是全部。原因（实测）：Inspect 会列出焦点元素的全部兄弟节点，真实客户端每行约 0.45 ms，5 万行要约 22 秒，Inspect 卡死；改为窗口后同样的遍历是 6 行、1 ms。窗口外的行仍然存在：选中、焦点、ScrollItem 都能到达；滚动后窗口移动并发 ChildrenInvalidated。这与 WPF/WinUI 虚拟化列表在 UIA 树中的表现相同。窗口外的行可以用 ItemContainer 模式（`FindItemByProperty`，支持按 Name、按 SelectionItem.IsSelected、或取下一项，逐个下标比对名字、不为每行建节点）直接找到，再用该行的 VirtualizedItem 模式 `Realize()` 滚动到可见（投递执行，同 ScrollItem）。

代价：正在追加的日志每帧都在改变子节点：行数变化，或者到了 `SetMaxLines` 上限后行数不变但第一行的序号前进。有客户端监听时，两种情况都在每帧为它发一次 ChildrenInvalidated（合并后），每次检查最多 64 个可见行的名字；客户端离开后什么都不发，缓存在下一次行数变化时丢弃。新行不作为 live region 朗读（每秒上千行会让讲述人无法使用）。

惰性创建：只有 UIA 客户端访问到这个控件时才创建 peer，没有客户端时零开销。行（项）不是元素，客户端访问到才创建节点。模式操作经投递消息执行，执行前重新检查控件仍在树上且已启用。自定义方式见 inheritance-guide.md 的“UI Automation”一节。

验证：无头测试 `tests/unit/AutomationDataPeerTests.cpp`，真实 UIA 客户端自测 `FluentUITests.exe --uia-smoke`；12 处变异（把操作换回不发事件的 setter、行名只取一列、日期按下标作 key、日志行序号不前进等）全部被测试抓到。讲述人实际朗读的效果需要在真机上听。

## 备注

### 性能特征

- **绘制 O(可见行)**：行顶坐标表上二分查找可见区间，只画区间内的行。
- **片段布局只为可见行缓存**：缓存是一个跟着可见区间滑动的窗口，以行号为键。没变化的
  视图重绘（滚动条淡出、拖选）不创建任何布局；尾随时滚动一行，只为新进入视野的那一行
  创建布局。以前每次绘制、每次命中测试、每次算选区高亮都为每个片段新建一个
  `IDWriteTextLayout`。
- **环形裁剪 O(被丢弃的行数)**：行存在 `std::deque` 里，行顶坐标存成绝对值，另记一个
  原点。裁剪时弹出队头并把原点移到新的第一行，幸存的行一个都不改写。以前是从 `vector`
  头部删除再给每个幸存行减去被删高度，缓冲满了以后**每次追加**都是 O(上限)：上限 10 万行、
  每秒 1 万行时约每秒 10⁹ 次元素操作。坐标用 `double`，因为绝对值只增不减，`float` 超过
  约 1670 万 DIP 就丢失整 DIP 精度，每秒 1 万行的日志几分钟就到。
- **idle 零成本**：`WantsAnimationTick()` 只在滚动动画或拖选自动滚动进行中为 true。

**这些优化的证明**（headless 单元测试，修复经过变异检验）：

| 优化 | 测试 |
| --- | --- |
| 没变化的视图重绘不建布局 | `LogViewer.RepaintOfAnUnchangedViewBuildsNoLayouts` |
| 尾随追加只建新行的布局 | `LogViewer.TailFollowAppendBuildsOnlyTheNewLine` |
| 裁剪后缓存行跟着行号重编号（不会贴到邻行上） | `LogViewer.TrimRenumbersTheLayoutWindowWithTheLines` |
| 裁剪后行顶仍从 0 连续 | `LogViewer.TrimKeepsLineTopsContiguousFromZero` |
| 裁剪后 `Clear` 从 0 重新开始 | `LogViewer.ClearAfterTrimRestartsAtZero` |

未验证：每秒上万行时的 F12 HUD 帧成本和滚动平滑度需要在真机上看（demo 的 LogViewer 页，
看 CPU Max）。headless 测试不覆盖像素与流畅度。

### 使用注意事项

- ⚠️ **必须给高度**（或放进定高容器）。它的自然尺寸是整个文档，没有意义；`Measure` 只尊重
  显式的 Width/Height。
- **只读**：没有插入符。Shift+方向键移动的是选区的活动端，锚点留在上次点击的位置。
- **背景默认黑色**：`SetBackground` 可以覆盖。片段颜色是调用者给的字面值，不跟主题走，
  所以浅色主题下也是深底日志。

### 键盘导航

| 按键 | 行为 |
| --- | --- |
| Ctrl+C / Ctrl+Insert | 复制选区 |
| Ctrl+A | 全选 |
| Esc | 有选区时清除选区 |
| Shift+←/→ | 选区活动端移动一个字符（行尾跨到下一行）；加 Ctrl 按词 |
| Shift+↑/↓ | 活动端移到上/下一行同一列 |
| Shift+Home / Shift+End | 到行首 / 行尾；加 Ctrl 到文档首 / 尾 |
| ↑ / ↓ / PageUp / PageDown | 滚动视图，不动选区 |
| Ctrl+Home / Ctrl+End | 滚到顶 / 底 |
| 滚轮 | 平滑滚动 |

### 主题 Token

| Token | 用途 |
| --- | --- |
| `accent` | 选区高亮（@ 0.35 alpha） |

背景回退是硬编码黑色（经 `EffectiveBackground`），滚动条墨水是硬编码白色，文字颜色来自
各个 `TextSpan`。三者都不读主题 token。

### 演示页

demo 导航 → 数据控件 → LogViewer（nav id 408）。

---

## 相关控件

- [TextArea](TextArea.md) — 可编辑、单一样式的虚拟化多行文本；`AppendText` + `SetMaxLines` 也能做日志
- [RichTextBox](RichTextBox.md) — 可编辑的富文本，不虚拟化
- [ScrollViewer](ScrollViewer.md) — LogViewer 内嵌的滚动条模型
