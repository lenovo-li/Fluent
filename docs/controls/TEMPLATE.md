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
Event<ControlName, EventArgsType> EventName;
```

**参数**: `EventArgsType`
- `Field1` — [字段说明]
- `Field2` — [字段说明]

**说明**: [事件触发时机]

**订阅示例**:
```cpp
control->EventName.Subscribe([](ControlName* sender, const EventArgsType& args) {
    // 处理事件
});
```

---

## 代码示例

### 最小示例

```cpp
#include "FluentUI/controls/ControlName.h"

using namespace fluent;

int main() {
    Application app;
    
    auto window = std::make_unique<Window>();
    window->SetTitle(L"ControlName Example");
    window->SetSize({800, 600});
    
    auto control = std::make_unique<ControlName>();
    // 配置控件
    control->SetPropertyName(value);
    
    window->SetContent(std::move(control));
    window->Show();
    
    return app.Run();
}
```

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

- [RelatedControl1](RelatedControl1.md) — [关系说明]
- [RelatedControl2](RelatedControl2.md) — [关系说明]

---

## 另见

- [布局指南](../LAYOUT_GUIDE.md)
- [主题定制指南](../THEMING_GUIDE.md)
- [性能最佳实践](../PERFORMANCE_BEST_PRACTICES.md)
