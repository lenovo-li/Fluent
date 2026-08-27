# TextBlock 控件

## 概述

只读文本显示。默认是**纯静态标签**:不可聚焦、无 I 形光标、不能选中 —— 一个标签出现在
Tab 顺序里只是噪音。需要「只读但可复制」时用 `SetSelectable(true)` 显式开启。

**典型使用场景**:
- 标签、标题、说明文字
- 表单字段的名称
- 设备详情、日志片段(开启 `SetSelectable`)

需要编辑用 `TextBox`(单行)或 `TextArea`(多行)。

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ TextBlock
```

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/TextBlock.h`

---

## 公开 API

### 嵌套类型

```cpp
enum class TypographyRole { Caption, Body, Subtitle, Title, Custom };
```

### 属性

| 成员 | 说明 | 脏标记 |
| --- | --- | --- |
| `SetText(std::wstring)` | 内容 | Measure |
| `SetTypographyRole(TypographyRole)` | 语义字号角色(**推荐**) | Measure |
| `SetFontSize(float)` | 显式字号,副作用见下 | Measure |
| `ClearFontSize()` | 退回 `Body` 并清掉显式值 | Measure |
| `SetWeight(DWRITE_FONT_WEIGHT)` | 字重 | Measure |
| `SetWrap(bool)` | 换行 | Measure |
| `SetLineSpacing(float)` | 行距倍数 | Measure |
| `SetAlignment(DWRITE_TEXT_ALIGNMENT)` | 段落内水平对齐 | **Render** |
| `SetDimmed(bool)` | 次要文字色 | Render |
| `SetSelectable(bool)` | 可选中(同时变为可聚焦) | — |

#### 为什么 `SetAlignment` 是 Render 级

对齐**不改变** DirectWrite 报告的 metrics —— 同一段文字无论左对齐还是右对齐,需要的宽高
一样,只是在自己的框里摆放位置不同。所以它不需要重新测量。

#### `SetTypographyRole` vs `SetFontSize`

优先用角色:字号从主题的 `TypographyTokens` 解析,换主题或换 DPI 时整套文字协调变化。

⚠️ **`SetFontSize` 有副作用**:它把角色置为 `Custom`,此后不再跟随主题。

⚠️ **`ClearFontSize()` 必须同时清两处** —— TextBlock 自己的 `fontSize_` 和 `Control` 基类
的 optional 字段。只清一处会让读 Control 层的通用代码看到「跟随主题」(nullopt),而
TextBlock 实际仍按旧尺寸绘制。这个陷阱写在代码注释里。

### 滚动

```cpp
void SetScrollOffset(float offsetDip);
```

配合 `SetSelectable(true)` 使用:开启后方向键/PageUp/PageDown 与滚轮会滚动这段文本。

---

## 代码示例

### 最小示例

```cpp
#include "FluentUI/controls/TextBlock.h"

auto* text = panel->Add(std::make_unique<TextBlock>());
text->SetText(L"区块标题");
text->SetTypographyRole(TypographyRole::Subtitle);   // 而不是写死 SetFontSize(20)
```

### 换行的段落

```cpp
// 开启换行后，desired 高度取决于可用宽度 —— 所以放它的行必须用 Auto
text->SetWrap(true);
grid->AddRow(GridLength::Auto());
```

### 次要说明文字

```cpp
auto* note = card->Add(std::make_unique<TextBlock>());
note->SetText(L"这一项会影响所有已登录设备。");
note->SetWrap(true);
note->SetDimmed(true);          // 走主题的 textSecondary
```

---

## 备注

### 性能特征

- **文本布局有缓存**(`TextBlockLayoutCache`):文本、宽度、排版参数不变时不重建
  DirectWrite 布局。
- **Render**:一次 DrawText。
- **不请求动画帧**。

### 使用注意事项

- ⚠️ **不换行时超出容器会被裁**,不溢出、不加省略号 —— 框架的 Arrange 契约。需要省略号
  请自己截断字符串。
- ⚠️ **换行文字所在的行/容器不能写死高度**:高度取决于换行结果,写死一个值在窗口变窄时
  会切掉末行。用 `GridLength::Auto()`。
- **字重影响宽度**:SemiBold 比 Normal 宽。本项目修过一个真 bug —— 测量时写死 Normal 而
  绘制走 `EffectiveFontWeight`,导致 Accent 按钮的标题溢出自己申请的框。新增测量路径必须
  从绘制代码用的同一批访问器取排版参数。
- **字形下伸部**:em size 14 对应 line height 约 18.62,差值就是 `g`/`y`/`p` 的下伸部。
  容器按 em size 裁剪会切掉它 —— 修过一次。
- `SetDimmed` 而不是给 `Foreground` 硬编码灰值:深色主题下硬编码的灰会看不见。

### 键盘导航

默认不可聚焦。`SetSelectable(true)` 之后:

| 按键 | 行为 |
| --- | --- |
| Tab | 进入焦点顺序 |
| 方向键 / PageUp / PageDown | 滚动 |
| Ctrl+A / Ctrl+C | 全选 / 复制 |

### 主题 Token

| Token | 用途 |
| --- | --- |
| `textPrimary` | 正常文字 |
| `textSecondary` | `SetDimmed(true)` |
| `typography.captionSize` / `bodySize` / `subtitleSize` / `titleSize` | 四个语义角色 |

### 演示页

demo 导航 → 文本控件 → TextBlock。含四个语义角色对照、三种容器宽度下的换行差异、
不换行被裁的对照、三种对齐、四种字重、Dimmed、行距倍数,以及可选中文本。
