# [ControlName] 控件

> **模板说明**: 复制此文件为新控件的文档，替换 `[ControlName]` 和所有占位符内容。

## 概述

[一段描述控件用途与典型使用场景的文字，50-100 字]

**典型使用场景**:
- [场景 1]
- [场景 2]
- [场景 3]

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ [Control/ContentControl/Panel/...]
              └─ ControlName
```

**命名空间**: `fluent`（全库唯一命名空间；没有 `FluentUI::Controls` 这种子命名空间）  
**头文件**: `FluentUI/controls/ControlName.h`（布局容器在 `FluentUI/layout/`）

---

## 公开 API

### 构造函数

```cpp
ControlName();
```

[描述默认构造行为]

---

### 属性

#### PropertyName

```cpp
ReturnType GetPropertyName() const;
void SetPropertyName(const ParamType& value);
```

**说明**: [属性用途]  
**默认值**: [默认值]  
**脏标记**: [Render / Arrange / Measure]

---

### 方法

#### MethodName

```cpp
ReturnType MethodName(ParamType1 param1, ParamType2 param2);
```

**参数**:
- `param1` — [参数说明]
- `param2` — [参数说明]

**返回值**: [返回值说明]

**说明**: [方法用途与行为]

---

### 事件

#### EventName

```cpp
Event<ControlName, EventArgsType>& EventName();
```

事件一律是**私有成员 + 公开访问器方法**（全库约定，见 `Button.h:55`）。不要把 `Event`
写成公开裸成员——`Event` 不可移动，暴露成员会把这个约束泄漏到 ABI。

**参数**: `EventArgsType`
- `Field1` — [字段说明]
- `Field2` — [字段说明]

**说明**: [事件触发时机]

**订阅示例**:
```cpp
// Handler 签名是 void(void* owner, Sender& sender, Args& args)（Event.h:37）：
// owner 是首参，sender 是引用不是指针，args 非 const。
sub_ = control->EventName().Subscribe<&MyWindow::OnEvent>(this);
// ...
void MyWindow::OnEvent(ControlName& sender, EventArgsType& args) { }
Subscription sub_;   // 必须作为成员持有
```

`Subscribe` 标了 `[[nodiscard]]`，返回的 `Subscription` 析构即退订
。**丢弃返回值 = 回调永不触发**，这是本框架最容易踩的坑。

---

## 代码示例

### 最小示例

示例代码必须能真正编译。**照抄 `examples/Minimal/main.cpp` 的骨架**，不要自创写法——
下面这个形状里的每一处都是被编译器强制的，改动任何一处都编不过：

```cpp
#include <FluentUI/FluentUI.h>
#include "FluentUI/controls/ControlName.h"   // 伞头文件不含全部控件，按需补

using namespace fluent;

class MainWindow final : public Window {
public:
    MainWindow() {
        SetTitle(L"ControlName Example");
        SetClientSize(800.0f, 600.0f);       // 不是 SetSize
    }

protected:
    // SetRoot 是 protected，只能在 Window 子类内部调用，
    // 所以构建控件树必须在这里，不能在 main 里对 Window 实例调。
    void OnInitialize() override {
        auto control = std::make_unique<ControlName>();
        control->SetPropertyName(value);
        sub_ = control->EventName().Subscribe<&MainWindow::OnEvent>(this);
        SetRoot(std::move(control));          // 不是 SetContent
    }

private:
    void OnEvent(ControlName&, EventArgsType&) { }
    Subscription sub_;                       // 丢了它回调就不触发
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    Application app(instance);                // 只有 explicit Application(HINSTANCE)
    MainWindow window;
    if (FAILED(window.Show(app))) return 1;   // Show 的首参 Application& 无默认值
    return app.Run();
}
```

四个易错点，都有编译器或运行期后果：

| 写法 | 为什么不行 |
|---|---|
| `Application app;` | `Application` 只有 `explicit Application(HINSTANCE)`；因此入口必须是 `wWinMain` 才拿得到 `instance` |
| `window->SetSize({800, 600})` | 无此方法，是 `SetClientSize(float, float)`；另有 `SetAutoSize(bool)` |
| `window->SetContent(...)` | Window 上无此方法，是 `SetRoot`——且它 protected，必须派生后在 `OnInitialize()` 里调 |
| `window->Show();` | 是 `Show(Application&, const WindowState* = nullptr)`，首参无默认值 |

### 实际应用示例

```cpp
// [展示实际应用场景的完整示例]
```

---

## 备注

### 性能特征

- [描述控件的性能特征]
- [是否虚拟化？]
- [idle 成本？]
- [动画是否在 compositor 线程？]

### 使用注意事项

- [注意事项 1]
- [注意事项 2]
- [已知限制]

### 键盘导航

| 按键 | 行为 |
|------|------|
| Tab | [行为] |
| Enter | [行为] |
| Space | [行为] |
| Arrow keys | [行为] |

### 主题 Token

控件使用以下主题 token（见 `styling/ThemeTokens.h`）：

- `tokenName` — [用途]
- `tokenName` — [用途]

---

## 相关控件

- `RelatedControl1` — [关系说明]
- `RelatedControl2` — [关系说明]

> 只在目标 `.md` **确实存在**时才写成 `[名字](名字.md)` 链接，否则用反引号写纯文本。
> `docs/controls/` 目前只有 26 份控件文档，绝大多数控件还没有文档页，凭印象加链接就是死链。

---

## 写作要求

### 与头文件注释的分工

**头文件写**（面向维护者、未来的你）：
- 为什么选这个算法/数据结构
- 历史：曾经怎么做、为何改
- 实现约束（必须这样做，否则会 X）
- 私有成员存在的理由

**控件文档写**（面向用户、API 消费者）：
- 公开 API 参考（签名、参数、默认值）
- 可编译示例
- 使用陷阱（"必须给高度"、"订阅必须持有"）
- 性能结论（"O(visible)"、"idle 零成本"），不写推导

**重复判定**：如果一段话删掉后，头文件读者会问"为什么"，它属于头文件；如果控件用户会问"怎么用"，它属于文档。

### API 准确性

- **API 签名从头文件复制**，不要凭印象写；标注了 `private`/`protected` 的成员不要列进公开 API
- **脏标记等级查 `SetProperty(...)` 的第三个参数**；若走的是裸 `Invalidate()` / `InvalidateMeasure()`，
  照实说明路径（等级仍成立，但来源不同）
- **主题 token 必须 grep `.cpp` 确认真的读了**——有些控件用的是硬编码明暗字面值而非 token，
  文档若照「应该如此」写就会骗人
- **枚举要写清作用域**：`Orientation` 一类枚举全是**嵌套**在各控件内（`StackPanel::Orientation`），
  没有全局 `Orientation`；而 `HAlign` / `VAlign` / `Thickness` 是 `namespace fluent` 级（`core/Layout.h`）。
  别把命名空间级枚举写进「嵌套类型」小节
- **行为描述以 `.cpp` 为准**，头注释也可能过期。两者矛盾时报告出来，而不是选一个抄
