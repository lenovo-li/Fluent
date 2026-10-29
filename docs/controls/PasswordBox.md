# PasswordBox 控件

## 概述

密码输入框:处于密码模式的 `TextBox`,外加 WinUI 的显示按钮(眼睛)。文字画成圆点,复制 /
剪切被拒绝,获得焦点期间输入法关闭。按住眼睛按钮显示明文,松开即恢复遮蔽。

**典型使用场景**:
- 登录、Wi-Fi / EAP 密码
- 私钥口令等不应进剪贴板、不应经过输入法组字的秘密

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Control
              └─ TextEditBase
                  └─ TextBox
                      └─ PasswordBox
```

**命名空间**: `fluent`
**头文件**: `FluentUI/controls/PasswordBox.h`

---

## 公开 API

### 构造函数

```cpp
PasswordBox();   // 已开启 SetPassword(true),显示模式默认 Peek
```

### 属性

#### PasswordRevealMode

```cpp
enum class PasswordRevealMode { Hidden, Peek, Visible };
void SetPasswordRevealMode(PasswordRevealMode mode);
PasswordRevealMode GetPasswordRevealMode() const;
```

| 值 | 行为 |
| --- | --- |
| `Hidden` | 没有按钮,始终遮蔽 |
| `Peek`(默认) | 框内右侧有眼睛按钮,按住左键显示明文 |
| `Visible` | 始终明文,没有按钮 |

三种模式下复制 / 剪切和输入法**都**被拒绝:它们看的是密码模式,不是当前是否遮蔽。

#### 状态查询

```cpp
bool IsRevealButtonVisible() const;  // 按钮当前是否绘制、可点
bool IsPeeking() const;              // 按钮是否正被按住
```

其余属性(`SetText` / `SetPlaceholder` / `SetMaxLength` / `SetReadOnly`、`TextChanged`
事件等)继承自 `TextBox`,见 [TextBox](TextBox.md)。

---

## 代码示例

### 最小示例

```cpp
#include "FluentUI/controls/PasswordBox.h"

auto* pwd = panel->Add(std::make_unique<fluent::PasswordBox>());
pwd->SetPlaceholder(L"Wi-Fi 密码");
pwd->SetMaxLength(63);
pwd->SetWidth(300.0f);
// 读取: pwd->Text()
```

### 不要显示按钮

```cpp
pwd->SetPasswordRevealMode(fluent::PasswordRevealMode::Hidden);
```

### 普通 TextBox 切换为密码模式

```cpp
box->SetPassword(true);   // 遮蔽 + 拒绝复制 + 关闭输入法,没有按钮
```

---

## 备注

### 显示按钮的行为

- **显示条件**:`Peek` 模式、框内有文字、控件启用。**与焦点无关**,这一点与 WinUI 不同
  (WinUI 只在聚焦时显示)。原因:`InputManager` 在把按下事件路由给元素之前就已经给了它
  焦点,若按钮只在聚焦时出现,点击未聚焦的框时按钮会在指针下"冒出来",这次本想放插入符的
  点击就会落到眼睛上。始终显示也避免了文字宽度随焦点变化。
- **按住显示,以下任一情况恢复遮蔽**:松开左键;捕获丢失(窗口失活、其他窗口抢走捕获、
  控件脱离树,`InputManager` 都会补发一个位置为 (-1,-1) 的取消释放);失去焦点。
- 按住期间指针移出按钮仍保持显示,只有松开才结束(与 WinUI 一致)。
- 点按钮**不移动插入符、不开始选择**,双击也不会选词;焦点留在(或移到)这个框。
- 悬停 / 按下底板用 `controlFillHover` / `controlFillPressed`,悬停时光标是箭头。
- 文字不会跑到按钮下面:`ReservedRightDip()` 预留了按钮宽度,滚动、插入符可见性和文字裁剪
  都扣掉它。

### 输入法

密码模式且获得焦点时,`TextEditBase::SyncImeAssociation` 用
`ImmAssociateContextEx(hwnd, nullptr, 0)` 解除窗口的输入上下文(先取消进行中的组字),
失去焦点、`SetPassword(false)`、脱离树时用 `IACE_DEFAULT` 恢复。关联是按 HWND 的,所以
每个控件只撤销自己施加的关闭;焦点从一个密码框移到另一个时先恢复再关闭,移到其他控件时
输入法回到默认。万一仍有组字消息到达,`OnImeComposition` 直接丢弃。

无头测试里没有 HWND,只能验证决策(`ImeSuppressed()`);语言栏是否真的变灰需要在真实窗口
上确认。

### 主题 Token

| Token | 用途 |
| --- | --- |
| `controlFillHover` / `controlFillPressed` | 按钮悬停 / 按下底板 |
| `textPrimary` | 眼睛图标(常态) |
| `textSecondary` | 眼睛图标(按住时) |
| 其余 | 同 [TextBox](TextBox.md) |

### 演示页

demo 导航 → 输入控件 → PasswordBox(nav 208)。

---

## 相关控件

- [TextBox](TextBox.md) — 父类,`SetPassword(bool)` 提供密码模式本身
