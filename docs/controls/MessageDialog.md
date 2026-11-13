# MessageDialog 对话框

## 概述

简化的消息框控件，包装 ContentDialog 提供单行消息显示。自动处理按钮配置（Ok/OkCancel/YesNo），自动调整尺寸，固定按钮到结果的映射（肯定按钮 → `DialogResult::Primary`，否定按钮 → `DialogResult::Cancel`）。是 `MessageBox` 的现代 UI 替代。

**典型使用场景**:
- 确认操作对话框（"确定删除？"）
- 错误提示（"操作失败，请重试"）
- 信息通知（"保存成功"）

**命名空间**: `fluent`  
**头文件**: `FluentUI/controls/MessageDialog.h`

---

## 继承层次

```
Visual
  └─ UIElement
      └─ FrameworkElement
          └─ Window
              └─ DialogWindow
                  └─ ContentDialog
                      └─ MessageDialog
```

---

## 公开 API

### 嵌套类型

```cpp
enum class DialogButtons { Ok, OkCancel, YesNo };
```

**Ok**: 单个"确定"按钮  
**OkCancel**: "确定" + "取消"  
**YesNo**: "是" + "否"

### 静态方法

#### Show

```cpp
static DialogResult Show(NativeWindowHost& owner, std::wstring title,
                        std::wstring message, DialogButtons buttons = DialogButtons::Ok);
```

**参数**:
- `owner` — 父窗口，对话框居中显示在其上并模态阻塞
- `title` — 对话框标题
- `message` — 消息文本（单行或多行，自动换行）
- `buttons` — 按钮组合，默认 `Ok`

**返回值**: `DialogResult`
- `DialogResult::Primary` — 用户点击肯定按钮（确定/是）
- `DialogResult::Cancel` — 用户点击否定按钮（取消/否）或关闭窗口

**说明**: 静态便捷方法，创建对话框、显示并运行模态循环，返回结果。调用方阻塞直到对话框关闭。

### 构造函数

```cpp
MessageDialog(std::wstring title, std::wstring message, DialogButtons buttons = DialogButtons::Ok);
```

**说明**: 手动构造 MessageDialog（高级用法）。通常直接用静态 `Show` 方法。

---

## 代码示例

### 最小示例

```cpp
#include <FluentUI/FluentUI.h>
#include "FluentUI/controls/MessageDialog.h"

using namespace fluent;

class MainWindow final : public Window {
public:
    MainWindow() {
        SetTitle(L"MessageDialog Example");
        SetClientSize(640.0f, 480.0f);
    }

protected:
    void OnInitialize() override {
        auto btn = std::make_unique<Button>();
        btn->SetText(L"显示对话框");
        clickSub_ = btn->Click().Subscribe<&MainWindow::OnClick>(this);
        SetRoot(std::move(btn));
    }

private:
    void OnClick(Button&, RoutedEventArgs&) {
        DialogResult result = MessageDialog::Show(
            GetHost(),
            L"确认",
            L"确定要删除此文件吗？",
            MessageDialog::DialogButtons::OkCancel
        );
        if (result == DialogResult::Primary) {
            // 用户点击"确定"
        }
    }
    Subscription clickSub_;
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    Application app(instance);
    MainWindow window;
    if (FAILED(window.Show(app))) return 1;
    return app.Run();
}
```

### 三种按钮组合

```cpp
// 单按钮：仅通知
DialogResult r1 = MessageDialog::Show(
    GetHost(), L"提示", L"保存成功！",
    MessageDialog::DialogButtons::Ok
);
// 返回必是 DialogResult::Primary（只有确定按钮）

// 确定/取消：需要确认的操作
DialogResult r2 = MessageDialog::Show(
    GetHost(), L"警告", L"操作无法撤销，是否继续？",
    MessageDialog::DialogButtons::OkCancel
);
if (r2 == DialogResult::Primary) {
    // 用户点击"确定"
} else {
    // 用户点击"取消"或关闭窗口
}

// 是/否：二选一问题
DialogResult r3 = MessageDialog::Show(
    GetHost(), L"选择", L"是否启用自动备份？",
    MessageDialog::DialogButtons::YesNo
);
if (r3 == DialogResult::Primary) {
    // 用户点击"是"
} else {
    // 用户点击"否"
}
```

---

## 备注

### 性能特征

- **创建**: 构造 ContentDialog + ScrollPanel + TextBlock + 按钮，O(1)
- **显示**: 运行模态消息循环，阻塞调用方直到对话框关闭
- **关闭**: 销毁对话框及所有子控件
- **模态**: 父窗口在对话框显示期间禁用输入

### 按钮到结果的映射

固定映射，不可自定义：

| DialogButtons | 按钮文本 | 点击结果 |
|---|---|---|
| Ok | "确定" | `DialogResult::Primary` |
| OkCancel | "确定" / "取消" | Primary / Cancel |
| YesNo | "是" / "否" | Primary / Cancel |

肯定按钮（确定/是）始终映射到 `Primary`，否定按钮和窗口关闭映射到 `Cancel`。

### 与 ContentDialog 的关系

MessageDialog 是 ContentDialog 的薄包装：

- **BuildContent**: 创建 `ScrollPanel` 包含 `TextBlock(message)`，`SetWrap(true)` 自动换行
- **AddButton**: 按 `DialogButtons` 枚举添加对应按钮
- **TitleBarHeight**: 设为 40.0f（比默认 48.0f 更紧凑）
- **自动尺寸**: 不设 Width/Height，由内容和按钮自然撑开

需要自定义内容（非纯文本）或自定义按钮时，直接用 ContentDialog。

### 文本自动换行

MessageDialog 的 TextBlock 设置了 `SetWrap(true)`，长消息自动多行显示。对话框宽度由 ContentDialog 的自动尺寸逻辑决定（有最大宽度限制）。

### 模态行为

`Show` 内部调用 `DialogWindow::ShowDialog(owner)`，运行模态消息循环：

- 父窗口输入被禁用（灰显）
- 对话框关闭前 `Show` 不返回
- 支持 Esc 键关闭（返回 `Cancel`）

### 使用注意事项

1. **阻塞调用**: `Show` 是同步的，后续代码要等对话框关闭才执行
2. **owner 必须是有效窗口**: 无效窗口句柄会导致对话框无父窗口（可能显示在桌面任意位置）
3. **按钮文本固定**: 不支持自定义按钮文本（"确定"/"取消"/"是"/"否"），需要自定义文本请用 ContentDialog
4. **单次使用**: `Show` 创建对话框后销毁，不可复用
5. **线程安全**: 必须在 UI 线程调用

---

## 相关控件

- `ContentDialog`（文档待写，`controls/ContentDialog.h`） — MessageDialog 的基类，支持自定义内容和按钮
- [Button](Button.md) — 对话框按钮
- `DialogWindow`（文档待写）— 对话框窗口基类，提供模态支持
