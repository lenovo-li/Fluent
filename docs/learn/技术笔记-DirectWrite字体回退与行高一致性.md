# 技术笔记：DirectWrite 字体回退与行高一致性

**日期**：2026-09-26  
**作者**：AI Assistant  
**关联代码**：`FluentUI/controls/TextArea.cpp`，测试 `tests/unit/TextAreaLineHeightProbe.cpp`

---

## 问题现象

TextArea 控件在输入时出现视觉跳动：
- 纯中文行："支持多行输入。"
- 输入首个拉丁字符后："支持多行输入。s"
- **该行向下跳变约 0.76 DIP**（在 12pt 字号下）

第三行（本来就有拉丁字符）保持稳定。

---

## 根本原因：DirectWrite Font Fallback

### 什么是 Font Fallback？

当一个 `IDWriteTextFormat` 包含多种语言的字符时，DirectWrite 会自动为不同字符选择不同的物理字体：

```cpp
// 创建一个 IDWriteTextFormat，指定主字体为 "Segoe UI Variable"
IDWriteTextFormat* fmt = factory->CreateTextFormat(
    L"Segoe UI Variable",  // 主字体（拉丁字符）
    nullptr,               // 字体集合（nullptr = 系统默认）
    DWRITE_FONT_WEIGHT_NORMAL,
    DWRITE_FONT_STYLE_NORMAL,
    DWRITE_FONT_STRETCH_NORMAL,
    12.0f,  // 字号
    L"",
    &fmt
);

// 用这个 format 创建 layout
IDWriteTextLayout* layout1, *layout2;
factory->CreateTextLayout(L"支持多行输入。", 7, fmt, 1e5f, 1e5f, &layout1);
factory->CreateTextLayout(L"支持多行输入。s", 8, fmt, 1e5f, 1e5f, &layout2);
```

**实际使用的字体**：
- `layout1`（纯中文）：DirectWrite 检测到中文字符，fallback 到 **Microsoft YaHei** 或 **SimSun**
- `layout2`（中文+拉丁）：检测到拉丁字符 "s"，使用主字体 **Segoe UI Variable**

### 为什么行高不同？

每种字体有自己的行高（`DWRITE_FONT_METRICS::ascent + descent + lineGap`）：

```cpp
DWRITE_LINE_METRICS lm[4];
UINT32 n = 0;

layout1->GetLineMetrics(lm, 4, &n);
// lm[0].height = 15.2402 DIP (YaHei 的行高)

layout2->GetLineMetrics(lm, 4, &n);
// lm[0].height = 16.0020 DIP (Segoe UI Variable 的行高)
```

**差异**：16.0020 - 15.2402 = **0.7618 DIP**，这就是用户看到的跳动。

---

## 测试验证

使用 `tests/unit/TextAreaLineHeightProbe.cpp` 直接测量 DWrite 返回的行高：

```cpp
TEST(ZProbe, TextAreaLineMetrics) {
    DWriteContext dw;
    dw.Initialize();
    
    IDWriteTextFormat* fmt = dw.Format(12.0f, ...);
    
    auto measure = [&](const wchar_t* text) {
        ComPtr<IDWriteTextLayout> layout;
        dw.Factory()->CreateTextLayout(text, wcslen(text), fmt, 1e5f, 1e5f, &layout);
        DWRITE_LINE_METRICS lm[4]{};
        UINT32 n = 0;
        layout->GetLineMetrics(lm, 4, &n);
        return lm[0].height;
    };
    
    float h1 = measure(L"支持多行输入。");      // 15.2402
    float h2 = measure(L"支持多行输入。s");     // 16.0020
    float h3 = measure(L"Ctrl+Z 撤销，Ctrl+Y 重做。"); // 16.0020
    
    EXPECT_NE(h1, h2);  // 纯中文 vs 混合，行高不同
    EXPECT_EQ(h2, h3);  // 两个混合文本，行高相同
}
```

**运行结果**（12pt 字号）：
```
=== font size 12 DIP ===
  empty                  height=15.9609 baseline=12.9492
  probe Ag               height=15.9609 baseline=12.9492
  line1 (mixed)          height=16.0020 baseline=12.9492
  line2 (CJK only)       height=15.2402 baseline=12.1875  ← 纯中文，YaHei
  line2 + s              height=16.0020 baseline=12.9492  ← 加拉丁字符，Segoe
  line3 (mixed)          height=16.0020 baseline=12.9492
```

---

## 解决方案：强制统一行高

### 方法：`SetLineSpacing(UNIFORM, ...)`

在创建 `IDWriteTextLayout` 后，调用 `SetLineSpacing` 强制所有行使用统一的行高：

```cpp
IDWriteTextLayout* layout = lineLayouts_.Get(...);

if (layout) {
    const float uniformHeight = LineHeight();  // 全局探测的行高（16.00 DIP）
    const float baseline = uniformHeight * 0.8f;  // DWrite 默认 baseline 比例
    layout->SetLineSpacing(
        DWRITE_LINE_SPACING_METHOD_UNIFORM,  // 统一行高模式
        uniformHeight,                        // 行高
        baseline                              // 基线位置
    );
}
```

### 关键点：探测字符串必须混合

`LineHeight()` 用于全局行高探测，它的测试字符串必须**包含拉丁+中文**，强制 DWrite 选择主字体：

```cpp
// ❌ 错误：空字符串或纯中文会触发 fallback
CreateTextLayout(L"", 0, fmt, ...);          // 15.96 DIP
CreateTextLayout(L"中文", 2, fmt, ...);      // 15.24 DIP (YaHei)

// ✅ 正确：混合字符强制主字体
CreateTextLayout(L"Ag中", 3, fmt, ...);     // 16.00 DIP (Segoe)
```

### 完整修复代码

**TextArea.cpp - NoWrap 模式**：
```cpp
IDWriteTextLayout* TextArea::LineLayout(size_t lineNumber, size_t minCover) const {
    // ... 创建 layout ...
    IDWriteTextLayout* layout = lineLayouts_.Get(lineNumber, ...);
    
    // 强制统一行高
    if (layout) {
        const float uniformHeight = LineHeight();
        const float baseline = uniformHeight * 0.8f;
        layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, 
                               uniformHeight, baseline);
    }
    
    return layout;
}
```

**TextArea.cpp - Wrap 模式**：
```cpp
IDWriteTextLayout* TextArea::WrapParagraphLayout(size_t p) const {
    // ... 创建 layout ...
    IDWriteTextLayout* layout = lineLayouts_.Get(p, ...);
    
    // 强制统一行高：防止 font fallback 导致纯中文行和混合行行高不一致
    if (layout) {
        const float uniformHeight = LineHeight();
        const float baseline = uniformHeight * 0.8f;
        layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                               uniformHeight, baseline);
    }
    
    return layout;
}
```

**TextArea.cpp - LineHeight() 探测**：
```cpp
float TextArea::LineHeight() const {
    // ... 缓存检查 ...
    
    if (Dwrite()) {
        IDWriteTextFormat* fmt = Dwrite()->Format(...);
        ComPtr<IDWriteTextLayout> probe;
        
        // 用混合字符强制主字体测量
        Dwrite()->Factory()->CreateTextLayout(
            L"Ag中", 3,  // ← 关键：混合拉丁+中文
            fmt, 1e5f, 1e5f, 
            probe.GetAddressOf()
        );
        
        DWRITE_TEXT_METRICS tm{};
        if (SUCCEEDED(probe->GetMetrics(&tm)) && tm.height > 0) {
            result = tm.height;  // 16.00 DIP at 12pt
        }
    }
    
    // 缓存结果
    lineHeightCache_ = result;
    return result;
}
```

---

## 其他控件是否受影响？

| 控件 | 受影响？ | 原因 |
|------|---------|------|
| **TextArea** | ✅ 是 | 多行文本，每行可能有不同字符组合 → **已修复** |
| **TextBox** | ❌ 否 | 单行文本，无"行与行之间"的跳变 |
| **TextBlock** | ⚠️ 部分 | 当 `lineSpacing_ = 0` 时可能受影响，但有 `SetLineSpacing()` API 可手动设置 |
| **LogViewer** | ❌ 否 | 每行独立测量 `maxHeight`，不依赖全局固定行高 |
| **RichTextBox** | ❌ 否 | 已精细处理 `SetLineSpacing`，行高差异是设计特性 |

---

## 推荐实践

### 1. 多行文本控件：强制统一行高

任何需要**稳定行间距**的多行文本控件，都应该在创建 layout 后调用 `SetLineSpacing`：

```cpp
layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, 
                       globalHeight, globalBaseline);
```

### 2. 行高探测：使用混合字符

行高探测字符串应包含**主字体和 fallback 字体都会用到的字符**，确保测量的是主字体：

```cpp
// 英文界面
CreateTextLayout(L"Ag中", 3, ...);  // 拉丁+中文

// 中文界面
CreateTextLayout(L"中Ag", 3, ...);  // 中文+拉丁

// 阿拉伯界面
CreateTextLayout(L"مرحبا Ag", 8, ...);  // 阿拉伯+拉丁
```

### 补充：单行文本也会跳

问题不只出现在多行控件里。一个单行 TextBlock 只要内容在"纯中文"和"含拉丁字母"之间切换，
它的高度就会变，下面的布局整体移动：Gallery 的 ContentDialog 页里，结果文本从
"结果：非模态已打开（主窗口仍可交互）"换成"结果：非模态关闭 Cancel（取消）"，14 DIP 下
高度从 16.51 变成 17.34。所以 TextBlock 也按主字体统一行高：`DWriteContext::PrimaryLineMetrics`
用 "Ag中" 探测并按（字号，字重）缓存行高和基线，TextBlock 在没有显式 `SetLineSpacing` 时
用它做 `SetLineSpacing(UNIFORM)`。测试 `TextBlockLayoutCache.PureCjkAndMixedTextMeasureTheSameHeight`。

### 3. 富文本控件：允许行高差异

如果控件**设计上允许**不同行有不同样式（如 RichTextBox），则不需要强制统一行高：

```cpp
// RichTextBox：每段可以有不同行高
if (para.style.lineSpacing != 1.0f) {
    layout->SetLineSpacing(...);  // 只在用户明确设置时统一
}
// else: 保持 DWrite 默认行为，每行根据实际字体调整
```

---

## 性能考虑

`SetLineSpacing` 是 **layout-级操作**，不会污染共享的 `IDWriteTextFormat`：

```cpp
// ✅ 安全：每个 layout 独立设置
layout1->SetLineSpacing(...);
layout2->SetLineSpacing(...);

// ❌ 错误：修改共享 format，影响所有使用者
format->SetLineSpacing(...);  // IDWriteTextFormat 没有这个方法
```

性能开销：
- `SetLineSpacing` 本身开销极小（设置几个浮点数）
- **不会触发重新 layout**（如果在 `CreateTextLayout` 后立即调用）
- 缓存的 layout 保留了这个设置

---

## 参考资料

- [MSDN: IDWriteTextLayout::SetLineSpacing](https://docs.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritetextlayout-setlinespacing)
- [MSDN: DWRITE_LINE_SPACING_METHOD](https://docs.microsoft.com/en-us/windows/win32/api/dwrite/ne-dwrite-dwrite_line_spacing_method)
- Test: `tests/unit/TextAreaLineHeightProbe.cpp`

---

## 历史

- **2026-09-26**：初始版本，记录 TextArea font fallback 行高跳变问题及修复方案
- **2026-10-31**：补充单行 TextBlock 的同类问题，TextBlock 改为按主字体统一行高
