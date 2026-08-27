# Hyperlink 控件

## 概述

带下划线的可点击文字。可以只是一个「看起来像链接的按钮」(触发 `Click` 事件),也可以
携带一个 URI 交给系统 shell 打开。

> **这个控件早就实现了。** demo 页曾长期写着「计划中,暂未实现」并推荐用
> `Button::Kind::Subtle` 代替 —— 那是过期文档,已经修正。过期文档比没有文档更糟,
> 因为它会让人去写不必要的替代方案。

**典型使用场景**:
- 「服务条款」「隐私政策」这类句子中嵌的链接
- 「了解更多」「查看文档」
- 应用内导航(不设 URI,只用 `Click`)

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ Hyperlink
```

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/Hyperlink.h`

---

## 公开 API

### 构造函数

```cpp
Hyperlink();
```

默认可聚焦、可点击、无 URI(此时只触发 `Click`,永不碰 shell)。

### 嵌套类型

```cpp
enum class HyperlinkActivation {
    Default,     // 跟随 UIContext.behavior->hyperlinkRequireCtrl（库默认 CtrlClick）
    Click,       // 普通点击直接交给 shell（WinUI 行为）
    CtrlClick,   // 只有 Ctrl+点击交给 shell；普通点击触发 Click 事件
};
```

**为什么库默认是 `CtrlClick` 而不是 WinUI 的 `Click`**:在应用内容里嵌一个点一下就跳走
浏览器的链接容易误触。想要 WinUI 行为请显式选 `Click`。这是个安全取舍,所以做成可选
策略而不是硬编码。

`Default` 让实例跟随树级的 `BehaviorSettings`,便于整个应用统一设置。

### 属性

| 成员 | 说明 |
| --- | --- |
| `SetText(std::wstring)` / `Text()` | 链接文字 |
| `SetUri(std::wstring)` / `Uri()` | 激活时打开的 URI。空(默认)= 只触发 `Click` |
| `SetActivation(HyperlinkActivation)` / `Activation()` | 本实例的激活策略 |

字号走 `Control::SetFontSize` / `EffectiveFontSize`(共享属性层)。此控件曾有一份自己的
`fontSize_`,会和继承来的那个静默不一致,已在共享层落地时删掉。

### 事件

#### Click

```cpp
Event<Hyperlink, RoutedEventArgs>& Click();
```

点击 / Space / Enter 时触发。

**URI 被 shell 打开时不会触发 `Click`** —— 两者是互斥的分支,所以一个「负责导航」的处理器
不会在 shell 已经处理过的激活上再跑一次。

⚠️ 返回的 `Subscription` 必须持有(RAII)。

#### LastActivationOpenedUri

```cpp
bool LastActivationOpenedUri() const;
```

上一次激活是否走了 shell。公开出来的唯一理由是**可测性**:`ShellExecute` 在测试里跑不
起来,这是断言「Ctrl 分支被走到了」的唯一手段。

---

## 代码示例

### 最小示例

```cpp
#include "FluentUI/controls/Hyperlink.h"

auto* link = panel->Add(std::make_unique<Hyperlink>());
link->SetText(L"了解更多");
link->SetHAlign(HAlign::Left);        // 默认 Stretch 会占满整行

sub_ = link->Click().Subscribe(this,
    [](void* owner, Hyperlink&, RoutedEventArgs&) {
        static_cast<MyPage*>(owner)->ShowDetails();
    });
```

### 带 URI

```cpp
link->SetUri(L"https://example.com/docs");
link->SetActivation(HyperlinkActivation::CtrlClick);   // 库默认，写出来更清楚
```

### 句子中嵌链接

```cpp
// 本框架没有富文本，所以「一句话中间嵌链接」要用水平 StackPanel 拼，
// 而不是在一段文字里插标记
auto* row = card->Add(std::make_unique<StackPanel>());
row->SetOrientation(StackPanel::Orientation::Horizontal);
row->SetSpacing(4.0f);

auto* pre = row->Add(std::make_unique<TextBlock>());
pre->SetText(L"继续即表示你同意");
pre->SetVAlign(VAlign::Center);

auto* terms = row->Add(std::make_unique<Hyperlink>());
terms->SetText(L"服务条款");
terms->SetVAlign(VAlign::Center);
```

---

## 备注

### 性能特征

- **Render**:一次 DrawText + 一条下划线。
- 悬停时下划线变化是 Render 级,无布局影响。

### 使用注意事项

- **绘制超出 `bounds_` 两处**:下划线在文字基线之下,焦点环在四周。两者都计入
  `VisualOverflowDip()` —— 这个控件此前两个都没声明,导致下划线与焦点环被父容器裁掉。
- **没有富文本**:句子中嵌链接要用水平 `StackPanel` 拼。
- **没有「访问过」状态**:框架不跟踪历史。需要的话自己记录并改 `Foreground`。
- 默认 `HAlign::Stretch`,通常要显式设 `Left`。

### 键盘导航

| 按键 | 行为 |
| --- | --- |
| Tab | 进入焦点顺序 |
| Space / Enter | 按当前策略激活(触发 `Click` 或交给 shell) |

### 主题 Token

| Token | 用途 |
| --- | --- |
| `accent` | 链接文字与下划线 |
| `accentHover` / `accentPressed` | 悬停 / 按下 |
| `focusStroke` | 焦点环 |
| `textSecondary` | 禁用态 |

### 演示页

demo 导航 → 基础控件 → Hyperlink。含基础链接(Click 计数实时显示)、三档激活策略并列
对照、句子中嵌链接的混合用法、禁用态。
