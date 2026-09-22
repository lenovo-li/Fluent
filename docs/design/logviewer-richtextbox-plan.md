# LogViewer 和 RichTextBox 设计方案

## 需求

### LogViewer（日志查看器）
- **只读**，高吞吐量日志输出
- **虚拟化渲染**：支持百万行，O(visible) 绘制
- **行内多样式**：一行内任意分段，每段独立颜色、字号、粗细、下划线
- **动态行高**：大字撑高行，不固定单一行高
- 目标性能：100 万行日志秒开，滚动流畅

### RichTextBox（富文本编辑器）
- **可编辑**，标准富文本编辑
- **不虚拟化**：用 DirectWrite 全文档排版
- 支持字体、大小、颜色、粗细、下划线等任意字符级样式
- 支持选择、光标、撤销/重做、复制/粘贴
- 合理文档规模（几千段、几百页）

---

## 一、LogViewer 技术方案

### 1.1 数据结构

```cpp
namespace FluentUI {

// 一个样式段
struct TextSpan {
    std::wstring text;
    Color color = Colors::White;
    float fontSize = 12.0f;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    bool underline = false;
};

// 一行日志（由多个 span 组成）
struct LogLine {
    std::vector<TextSpan> spans;
    float height = 0.0f;  // 缓存的行高（首次绘制时计算）
    bool heightMeasured = false;
};

} // namespace FluentUI
```

### 1.2 虚拟化策略

**动态行高 + 二分查找可见范围**：

1. **首次渲染时测量行高**：遍历所有 span，计算 `max(span.fontSize) × lineSpacing`
2. **缓存行高**：存入 `LogLine::height`，后续滚动复用
3. **Y 坐标累加表**：维护 `std::vector<float> lineYPositions_`，其中 `lineYPositions_[i]` 
   是第 i 行的顶部 Y 坐标（DIP）
4. **二分查找可见行**：给定 `scrollTop` 和 `viewportHeight`，用 `std::lower_bound` 
   找到第一条和最后一条可见行
5. **只渲染可见行**：循环 `[firstVisible, lastVisible]`，逐行绘制

### 1.3 渲染逻辑

```cpp
void LogViewer::Render(DrawingContext& dc) {
    // 1. 计算可见范围
    float scrollTop = scroll_.Offset();
    float viewportHeight = ContentHeight();
    
    auto it1 = std::lower_bound(lineYPositions_.begin(), lineYPositions_.end(), scrollTop);
    auto it2 = std::upper_bound(lineYPositions_.begin(), lineYPositions_.end(), scrollTop + viewportHeight);
    
    int firstVisible = std::distance(lineYPositions_.begin(), it1);
    int lastVisible = std::min(int(lines_.size()) - 1, int(std::distance(lineYPositions_.begin(), it2)));
    
    // 2. 渲染可见行
    for (int i = firstVisible; i <= lastVisible; ++i) {
        float y = lineYPositions_[i] - scrollTop;
        RenderLine(dc, lines_[i], y);
    }
}

void LogViewer::RenderLine(DrawingContext& dc, LogLine& line, float y) {
    float x = padding_;
    float maxHeight = 0.0f;
    
    for (auto& span : line.spans) {
        // 为每个 span 创建临时 TextLayout
        ComPtr<IDWriteTextLayout> layout;
        Dwrite()->CreateTextLayout(
            span.text.c_str(), span.text.length(),
            GetTextFormat(span.fontSize, span.weight),
            9999.0f, 9999.0f, &layout
        );
        
        if (span.underline) {
            layout->SetUnderline(TRUE, {0, UINT32(span.text.length())});
        }
        
        // 绘制
        dc.DrawTextLayout(layout.Get(), PointDip{x, y}, span.color);
        
        // 推进 x 坐标
        DWRITE_TEXT_METRICS metrics;
        layout->GetMetrics(&metrics);
        x += metrics.width;
        maxHeight = std::max(maxHeight, metrics.height);
    }
    
    // 首次绘制时缓存行高
    if (!line.heightMeasured) {
        line.height = maxHeight;
        line.heightMeasured = true;
        RebuildYPositions(从当前行开始);  // 更新后续行的 Y 坐标
    }
}
```

### 1.4 滚动范围

```cpp
float LogViewer::GetContentExtent() const {
    if (lines_.empty()) return 0.0f;
    return lineYPositions_.back() + lines_.back().height;
}
```

### 1.5 API 设计

```cpp
class LogViewer : public Control {
public:
    // 追加一行（多样式）
    void AppendLine(std::vector<TextSpan> spans);
    
    // 清空
    void Clear();
    
    // 限制最大行数（环形缓冲）
    void SetMaxLines(int maxLines);
    
    // 自动滚动到底部（tail -f 模式）
    void SetAutoScrollToTail(bool enable);
    
private:
    std::vector<LogLine> lines_;
    std::vector<float> lineYPositions_;  // 累加 Y 坐标表
    ScrollViewer scroll_;
    int maxLines_ = 0;  // 0 = 无限制
    bool autoScrollToTail_ = true;
};
```

---

## 二、RichTextBox 技术方案

### 2.1 架构

**继承自 TextEditBase**，复用 TextEditBase 的：
- 光标管理
- 选择逻辑
- 撤销/重做栈
- 输入路由（键盘、鼠标、IME）

**扩展点**：
- `text_` 从 `std::wstring` 改为支持样式的文档模型
- `RenderText()` 从单一 TextLayout 改为分段渲染

### 2.2 文档模型

```cpp
struct RichTextRun {
    std::wstring text;
    Color color = Colors::Black;
    float fontSize = 12.0f;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    bool underline = false;
};

struct RichTextDocument {
    std::vector<RichTextRun> runs;
    
    // 辅助方法
    size_t Length() const;
    std::wstring GetPlainText() const;
    void InsertText(size_t offset, const std::wstring& text, const RichTextRun& style);
    void DeleteRange(size_t start, size_t length); 
    RichTextRun GetStyleAt(size_t offset) const;
}; 
```

### 2.3 排版策略

**方案 A（推荐）**：每个 Run 一个 IDWriteTextLayout

- 简单直接，每次绘制时遍历可见 run，逐个创建 layout 并绘制
- Run 边界处理自然（不同字体/大小的字符基线对齐）
- 性能：几百个 run 无压力（TextLayout 创建 <0.1ms）

**方案 B（备选）**：单个 IDWriteTextLayout + SetFontSize/SetFontWeight

- 用一个 layout 容纳全文档，通过 `SetFontSize(range)`、`SetFontWeight(range)` 
  设置每个 run 的样式
- 优点：DWrite 内部优化行间距和基线，自动处理 bidi
- 缺点：API 繁琐，且颜色需要在绘制时通过 `IDWriteTextRenderer` 分段设置

**推荐方案 A**：先做简单的，性能足够后不切换。

### 2.4 编辑操作

```cpp
class RichTextBox : public TextEditBase {
public:
    // 设置选区样式
    void SetSelectionBold(bool bold);
    void SetSelectionColor(Color color);
    void SetSelectionFontSize(float size);
    void SetSelectionUnderline(bool underline);
    
    // 插入文本（使用当前样式）
    void InsertTextWithStyle(const std::wstring& text, const RichTextRun& style);
    
protected:
    void RenderText(DrawingContext& dc, RectDip contentRect) override;
    
private:
    RichTextDocument doc_;
    RichTextRun currentStyle_;  // 当前输入样式（光标处）
};
```

---

## 三、实施计划

### Phase 1：LogViewer 核心（优先）

1. **定义数据结构**：`TextSpan`, `LogLine`（`controls/LogViewer.h`）
2. **基础控件框架**：继承 `Control`，持有 `ScrollViewer`
3. **虚拟化渲染**：实现 `RenderLine()` + 可见行计算
4. **行高缓存 + Y 坐标表**：`lineYPositions_` 的增量更新
5. **AppendLine API**：追加行 + 更新滚动范围
6. **滚动交互**：复用 `ScrollViewer` 的滚轮 + 拖拽
7. **测试场景**：100 行 → 1 万行 → 100 万行，验证虚拟化生效

**验收标准**：
- 100 万行日志 `AppendLine` 总耗时 <3 秒（批量追加）
- 滚动帧率 >60 FPS（P99 <16ms）
- 内存占用 <200 MB（100 万行）

### Phase 2：LogViewer 增强功能

8. **SetMaxLines**：环形缓冲，trim 时重建 Y 坐标表
9. **SetAutoScrollToTail**：追加时自动滚动到底部
10. **Clear**：清空日志
11. **Demo 页面**：`FluentUIDemo` 添加 LogViewer 示例，模拟高速日志输出

### Phase 3：RichTextBox 核心

12. **文档模型**：`RichTextRun`, `RichTextDocument`
13. **继承 TextEditBase**：替换 `text_` 为 `doc_`
14. **分段渲染**：override `RenderText()`，遍历 run 绘制
15. **插入/删除**：适配 `InsertText` / `DeleteSelection` 到文档模型
16. **样式 API**：`SetSelectionBold` 等

### Phase 4：RichTextBox 编辑功能

17. **光标样式跟踪**：`currentStyle_` 从光标位置推断
18. **撤销/重做**：适配 `UndoStack` 到富文本模型
19. **复制/粘贴**：纯文本格式（样式信息暂不序列化）
20. **Demo 页面**：简易富文本编辑器，带工具栏（加粗、颜色选择器）

---

## 四、风险与权衡

### 4.1 LogViewer

**风险**：动态行高的 Y 坐标表在追加时需要 O(n) 更新后续行位置

**缓解**：
- 只在首次绘制时测量行高，追加时**预估行高**（用默认字号）
- 滚动到未测量区域时才实测，逐步修正 Y 坐标表
- 类似 TextArea 的 `WrapExtentMap` 策略

**权衡**：
- 未测量区域的滚动条 thumb 可能轻微抖动（用户滚动到新区域时修正）
- 这是 VS Code / Sublime / TextArea Wrap 模式的既有行为，可接受

### 4.2 RichTextBox

**风险**：每个 run 一个 TextLayout 可能导致 layout 创建开销

**缓解**：
- 缓存每个 run 的 layout（类似 `LineLayoutCache`）
- 只在 run 内容/样式变化时重建
- 如果仍慢，切换到方案 B（单 layout + 分段样式）

**权衡**：
- 首版不支持嵌入图片、表格（只支持文本样式）
- 样式持久化（保存/加载富文本文件）留待后续（可用 JSON/RTF/Markdown）

---

## 五、文件清单

### 新增文件

- `FluentUI/controls/LogViewer.h` - LogViewer 头文件
- `FluentUI/controls/LogViewer.cpp` - LogViewer 实现
- `FluentUI/controls/RichTextBox.h` - RichTextBox 头文件
- `FluentUI/controls/RichTextBox.cpp` - RichTextBox 实现
- `tests/controls/LogViewerTests.cpp` - LogViewer 单元测试
- `tests/controls/RichTextBoxTests.cpp` - RichTextBox 单元测试

### 需要注册到项目文件

- `FluentUI/FluentUI.vcxproj` - 添加 4 个新源文件
- `FluentUI/FluentUI.vcxproj.filters` - 添加 Filter `controls`
- `tests/FluentUITests.vcxproj` - 添加 2 个测试文件
- `tests/FluentUITests.vcxproj.filters` - 添加 Filter `controls`
- `CMakeLists.txt` - 在 `FLUENTUI_SOURCES` 和 `TEST_SOURCES` 添加新文件

---

## 六、验证方法

### LogViewer

1. **虚拟化**：追加 100 万行，打开 Task Manager 验证内存 <200 MB
2. **性能**：滚动时按 F12 看 HUD，CPU P99 <16ms
3. **行内样式**：同一行混合红/黄/绿、大字/小字、粗体/下划线，视觉正确
4. **动态行高**：一行中有 24px 大字时，该行高度应自动撑高
5. **滚动条**：thumb 位置正确，拖拽流畅

### RichTextBox

1. **编辑**：输入文本，选中一段，点击"加粗"，样式应用正确
2. **光标**：光标在加粗文本后，新输入的字符应继承加粗样式
3. **撤销/重做**：Ctrl+Z 撤销样式修改，文本恢复原样式
4. **复制/粘贴**：选中带样式文本，复制到另一个位置（样式首版丢失可接受）
5. **混合样式**：同一段落中混合多种字号、颜色、粗细，渲染正确

---

## 七、后续扩展方向

### LogViewer

- **搜索高亮**：全文搜索，匹配行高亮显示
- **行号**：左侧显示行号列
- **颜色主题**：预设 ANSI 颜色方案（solarized, dracula）
- **导出**：导出为 HTML（保留样式）

### RichTextBox

- **嵌入对象**：图片、表格、超链接
- **段落样式**：对齐方式、缩进、行间距
- **列表**：有序/无序列表
- **持久化**：保存/加载为 JSON、RTF、Markdown
- **协作**：多光标、实时协作编辑（WebSocket + OT）
