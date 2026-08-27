# TextArea 控件

## 概述

多行文本编辑器,带行索引、换行模式、行数上限与「跟随尾部」。既是编辑器,也是本框架的
**日志查看器** —— `AppendText` + `SetMaxLines` + `SetAutoScrollToTail` 就是一个 `tail -f`。

单行输入请用 `TextBox`。

**典型使用场景**:
- 多行文本编辑(备注、描述)
- 日志/输出面板(追加 + 尾随 + 环形上限)
- 只读的长文本展示

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ TextEditBase
                  └─ TextArea
```

`TextEditBase` 提供文本缓冲、插入符、选区、撤销栈、IME、输入策略;`TextArea` 加行索引、
换行模式、滚动与尾随。`TextBox` 是同门兄弟。

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/TextArea.h`

---

## 公开 API

### 继承自 `TextEditBase`

| 成员 | 说明 |
| --- | --- |
| `SetText(const std::wstring&)` / `Text()` | 全量内容 |
| `SetPlaceholder(std::wstring)` | 空且未聚焦时的提示 |
| `SetReadOnly(bool)` | 只读(仍可选择复制) |
| `SetMaxLength(size_t)` | 最大字符数 |
| `SetInputFilter(std::function<bool(wchar_t)>)` | 逐字符准入 |
| `TextChanged()` | `Event<TextEditBase, std::wstring>` |
| `SelectionChanged()` | `Event<TextEditBase, SelectionChangedArgs>` |

⚠️ 两个事件的 **Sender 类型是 `TextEditBase`**,不是 `TextArea` —— 事件定义在基类上。

### 换行模式

```cpp
enum class TextWrapMode { Wrap, NoWrap };
void SetWrapMode(TextWrapMode mode);
TextWrapMode WrapMode() const;
```

切换模式会**丢弃缓存布局并重建行索引** —— 两条路径不共享缓存状态,留着上一模式的布局会按
错误的断行绘制。走 `SetProperty`,所以冗余设置零成本、不排帧。**Measure 级**:换行宽度不再
约束 desired 尺寸。

### 追加与日志模式

```cpp
void AppendText(std::wstring_view s);
void SetMaxLines(size_t n);        size_t MaxLines() const;
void SetAutoScrollToTail(bool);    bool AutoScrollToTail() const;
bool FollowingTail() const;
void ScrollToTail();
```

**`AppendText` 是为日志优化的追加路径**:只扫描新增字符来扩展行索引,不做全量
`LineIndex::Rebuild`,所以追加成本与**新增长度**成正比,而不是与缓冲区总长成正比。

**三个尾随相关的 API 区别要分清**:

| 成员 | 含义 |
| --- | --- |
| `SetAutoScrollToTail(bool)` | 功能是否**武装** |
| `FollowingTail()` | 当前是否**正在**跟随(向上滚会清除,滚回底部会恢复) |
| `ScrollToTail()` | **立即**跳到底部并标记为跟随 |

用户点「跳到末尾」按钮时应当调 `ScrollToTail()`,而不是 `SetAutoScrollToTail(true)` ——
后者只是武装功能,不会移动视图。

#### ⚠️ MaxLines 在两种换行模式下的行为不同

- **NoWrap**:偏移被**精确补偿**(丢掉 k 行 == 少了 k × lineHeight 的内容),所以正在翻看
  历史的用户看到文字**纹丝不动**。
- **Wrap**:不存在精确补偿 —— 一个逻辑行对应多少视觉行是未知的 —— 偏移只能重新夹逼,
  视图**会跳**。

这是 Wrap 本身的形状,不是 MaxLines 引入的。**日志模式请用 NoWrap。**

---

## 代码示例

### 多行编辑

```cpp
#include "FluentUI/controls/TextArea.h"

auto* area = panel->Add(std::make_unique<TextArea>());
area->SetWrapMode(TextWrapMode::Wrap);
area->SetHeight(240.0f);          // 必须给视口高度
```

### 日志面板(tail -f)

```cpp
auto* log = panel->Add(std::make_unique<TextArea>());
log->SetReadOnly(true);
log->SetWrapMode(TextWrapMode::NoWrap);   // 日志模式：偏移可精确补偿
log->SetMaxLines(5000);                   // 环形上限，防止无限增长
log->SetAutoScrollToTail(true);           // 武装尾随
log->SetHeight(320.0f);

// 每来一条：
log->AppendText(L"[12:00:01] 连接建立\n");   // 只扫描新增字符
```

---

## 备注

### 性能特征

- **布局缓存 + 行索引**:`AppendText` 只扫新增部分。
- ⚠️ **宽度就绪前刻意不构建布局**:布局尚未给出有效宽度时,换行会发生在 `ContentWidth()`
  的 1 DIP 下限上 —— 那是**每个字符一个视觉行**,存在的最贵的换行方式,而且下一帧拿到真实
  宽度后立刻被丢弃。所以有一个「宽度是否就绪」的门控。
- **Render 只画可见行**。

### 使用注意事项

- ⚠️ **必须给高度**:多行编辑器没有自然高度,视口尺寸由调用者决定。
- **日志用 NoWrap**,理由见上。
- 本文件 2110 行,是仓库里最大的控件实现 —— 大文件不等于坏文件,这里的复杂度来自
  IME、行索引、滚动补偿三者的真实交互。

### 键盘导航

| 按键 | 行为 |
| --- | --- |
| 方向键 / Home / End / PageUp / PageDown | 移动插入符;配合 Shift 选择 |
| Ctrl+A / C / X / V / Z / Y | 全选 / 复制 / 剪切 / 粘贴 / 撤销 / 重做 |
| Enter | 换行(只读时无效) |

### 主题 Token

| Token | 用途 |
| --- | --- |
| `controlFillDefault` | 编辑区底色 |
| `controlStrokeDefault` | 边框 |
| `accent` | 聚焦边框、选区背景 |
| `textPrimary` | 文字与插入符 |
| `textSecondary` | 占位符 |

### 演示页

demo 导航 → 文本控件 → TextArea;日志模式见「性能演示 → 日志追尾」。
