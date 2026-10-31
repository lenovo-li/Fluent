# StatusBar 控件

## 概述

窗口底部的状态条：左侧一行状态文字，右侧可放一个任意控件（进度条、按钮、Spinner）。高度固定
32 DIP，通常放在 DockPanel 的底部或 Grid 的最后一行。

**典型使用场景**:
- 显示"就绪" / "正在加载…" / 错误摘要
- 后台任务进度（右侧放 ProgressBar）
- 当前文档的行列号、编码等附加信息

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Panel
              └─ StatusBar
```

**命名空间**: `fluent`  
**头文件**: `FluentUI/controls/StatusBar.h`

---

## 公开 API

```cpp
StatusBar();                                                     // 32 DIP 高，VAlign::Bottom
void SetText(std::wstring text);                                 // 左侧文字
const std::wstring& Text() const;
void SetRightContent(std::unique_ptr<FrameworkElement> content); // nullptr 清除
```

`SetText` 改的是内部 TextBlock，脏标记为 Measure；`SetRightContent` 销毁旧内容并请求 Measure。

---

## 代码示例

```cpp
auto* bar = dock->Add(std::make_unique<StatusBar>());
DockPanel::SetDock(bar, Dock::Bottom);
bar->SetText(L"就绪");

auto progress = std::make_unique<ProgressBar>();
progress->SetWidth(150.0f);
progress->SetValue(0.35f);
bar->SetRightContent(std::move(progress));
```

---

## 备注

### 外观

- **自身不画底色**，只在顶部画一条 1px 分隔线（`controlStrokeDefault`），显示的是它所在表面
  （窗口 Mica、卡片）的颜色——Win11 状态条的做法。以前画的是一块不透明的
  `controlFillDefault` 圆角色块，看起来像一个按钮；放进圆角容器时，它直角的下沿还会冒出
  容器的圆角。需要独立色带时，把 StatusBar 放进一个设了背景的 Border。
- **文字是 caption 字号、次要颜色**（`SetTypographyRole(Caption)` + `SetDimmed(true)`）：状态
  信息是背景信息，正文字号加主色会和上面的页面内容抢注意力。
- 左右各内缩 12 DIP。

### 布局

StatusBar 自己排列两个子元素，**不读子元素的 Margin**。文字的左内缩以前写成了 TextBlock 的
Margin，因而从未生效，"Ready" 紧贴着左边。现在内缩在 `ArrangeOverride` 里直接计算。
验证：`StatusBar.TextIsInsetFromTheLeftEdge`；把内缩去掉后该测试失败（x = 10 vs 22）。

右侧内容按自身 Desired 宽度右对齐、垂直居中；左侧文字占剩余宽度，按自身行高垂直居中。

### 性能特征

- 两个子元素，Measure / Arrange O(1)。
- 不请求动画帧（右侧的 ProgressBar 自己负责它的动画）。

未验证：外观只能在真机上看——Gallery「窗口 & 对话框 → StatusBar」页，亮色和暗色主题各看
一次分隔线和文字颜色。

### 主题 Token

| Token | 用途 |
| --- | --- |
| `controlStrokeDefault` | 顶部分隔线 |
| `textSecondary` | 状态文字（经 TextBlock 的 `SetDimmed`） |
| `typography.captionSize` | 状态文字字号 |

### 演示页

Gallery：窗口 & 对话框 → StatusBar（`FluentUIDemo/pages/MenuPages.cpp`，`CreateStatusBarPage`）。
