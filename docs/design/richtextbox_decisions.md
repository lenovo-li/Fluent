# RichTextBox 设计决策日志

本文档记录 RichTextBox 实现过程中的设计决策，供用户回来后审查、确认或修改。

## 架构决策

### 决策 1：不继承 TextEditBase
**决定**：RichTextBox 从 Control 直接继承，不复用 TextEditBase。

**理由**：
- TextEditBase 深度耦合 `std::wstring text_`（37 次引用），改成富文本需要重写
  几乎所有逻辑（光标、选区、撤销、索引计算）
- TextArea 2389 行代码，继承负担远大于收益
- 独立实现更清晰，代码量预估 1850 行 vs 重构基类 2500+ 行

**代价**：不能复用 TextEditBase 的 IME 输入、双向文本、撤销栈。需要自己实现，
但可以参考 TextEditBase 的既有逻辑。

---

### 决策 2：Run 自动合并（实施时改了主意）
**最初决定**：相邻样式相同的 TextRun 不自动合并。
**实际实现**：**合并**。`MergeAdjacentRuns()` 在每次 InsertText / DeleteRange 后跑，
`AppendRun` 也会并入末尾同样式 run。

**为什么改**：不合并的方案里，跨 run 删除会留下两段同样式的相邻 run（删掉
"abc**BOLD**def" 的 BOLD 后剩下 "abc" + "def" 两个 run），此后每次渲染都要为它们
各建一个 `IDWriteTextLayout`。文档越编辑越碎，而 run 数直接决定每帧的 layout 数。
合并是 O(run 数) 而不是 O(字符数)，且 run 数本来就小。

打字快路径仍然保留且更重要：InsertText 若落在同样式 run 内部，直接 `text.insert()`，
不新建 run 也不触发合并扫描（测试 `TypingCharacterByCharacterStaysOneRun` 锁定
这个行为——逐字符打 5 个字后仍然只有 1 个 run）。

**代价**：合并会让「用户手动把两段设成同样式」这件事变得不可逆——两个 run 变成
一个，之后无法再区分它们原本是两段。目前没有需要区分的场景（样式相同即视觉相同），
但如果将来要加「段落」概念（每段独立的对齐/缩进），段边界就不能靠 run 边界表达，
需要单独的段落结构。

---

### 决策 3：单行优先，多行延后
**决定**：Phase 1 只支持单行富文本，输入的 `\n` 被忽略或替换为空格。

**理由**：
- 多行需要行切分、行高缓存、滚动、换行符在 run 中的表示（独立 run？文本内嵌？）
- LogViewer 的虚拟化是按逻辑行切的（每行一个 `LogLine` 对象），RichTextBox
  若要支持多行需要类似结构，复杂度翻倍
- 单行已经足够验证 run 模型、样式 API、选区、撤销的正确性

**多行扩展路径**：
- 方案 A：沿用 LogViewer 的 `std::vector<Line>`，每个 Line 持有 `std::vector<TextRun>`
- 方案 B：单一 `std::vector<TextRun>`，用 `\n` 字符切行，渲染时动态计算行边界

Phase 2 前由用户选择方案。

---

### 决策 4：样式继承规则
**场景**：DeleteRange(10, 5) 删除 [10, 15) 区间，该区间跨越三个 run：

```
Run 0: [0,  8)  "Hello "   (黑色 12pt)
Run 1: [8, 13)  "world"    (红色 14pt 粗体)
Run 2: [13, 18) "!"        (黑色 12pt)
```

删除 [10, 15) 后，删除点在 offset=10，那里原本是 Run 1 的 `"rl"`，现在光标在
Run 0 和 Run 2 的拼接处。光标样式应该是黑色还是红色？

**决定**：保留**删除点左侧**的样式。

**理由**：
- Word/Google Docs 行为：删除后光标样式跟随左侧字符
- 实现简单：`GetStyleAt(deleteStart - 1)` 或文档为空时用默认样式

**例外**：若删除点在文档开头（offset=0），用默认样式或文档第一个 run 的样式。

---

### 决策 5：光标初始位置与移动
**Phase 1**：光标固定在文档末尾，不支持方向键/鼠标移动。

**理由**：先验证 run 模型和渲染，移动逻辑留给 Phase 2。

**Phase 2 移动策略**：
- Left/Right：`offset ± 1`（不考虑代理对、组合字符，首版用 `wchar_t` 粒度）
- Home/End：跳到行首/尾（单行模式下就是文档首/尾）
- 鼠标点击：DirectWrite `HitTestPoint` 将 (x, y) 映射到字符索引

**后续优化**：Ctrl+Left/Right 跳单词、代理对边界检测。

---

### 决策 6：基线对齐与行高
**问题**：同一行中有 12pt、18pt、24pt 三种字号的 run，如何对齐？

**决定**：DirectWrite 自动基线对齐，我们只管累加 X 坐标。

**实现**：
```cpp
float x = padding;
float maxHeight = 0.0f;
for (auto& run : doc.Runs()) {
    auto layout = CreateTextLayout(run.text, run.fontSize, ...);
    dc.DrawTextLayout(layout, {x, y}, run.color);
    
    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    x += metrics.width;
    maxHeight = std::max(maxHeight, metrics.height);
}
// 行高 = maxHeight，用于光标绘制和点击检测
```

**验证**：Demo 页面需要一行内混合 12pt、18pt、24pt 文字，目测基线是否对齐。

---

### 决策 7：撤销栈策略
**Phase 1**：不实现撤销。

**Phase 3 策略**：快照法（整个 `std::vector<TextRun>` 深拷贝）。

**理由**：
- 简单，不需要 diff 算法
- 代价：每次编辑操作（InsertText/DeleteRange/SetStyle）推一个快照，内存翻倍
- 优化：限制栈深度（默认 100 次），或只在「非连续输入」时推栈（停顿 500ms
  或方向键/鼠标移动光标后再打字）

**后续优化**：增量记录（操作类型 + offset + text/style），类似 OT。Phase 3 先
用快照跑通流程。

---

### 决策 8：剪贴板格式
**Phase 3**：Ctrl+C 复制纯文本（无样式），Ctrl+V 粘贴纯文本（用当前光标样式）。

**理由**：样式序列化（HTML/RTF）复杂，Phase 3 先验证文本流程。

**后续扩展**：
- HTML 格式：`<span style="color:red;font-size:14pt;font-weight:bold">text</span>`
- RTF 格式：`{\rtf1 {\colortbl;\red255\green0\blue0;} \f0\fs28\b text}`
- 自定义格式：`CF_RICHTEXTBOX` 注册私有剪贴板格式，序列化 `std::vector<TextRun>`

Phase 4 由用户决定优先级。

---

### 决策 9：选区渲染
**Phase 2**：选区用半透明蓝色矩形（`#3B6DFF @ 30% alpha`），先画选区、再画文本。

**跨 run 选区处理**：
```cpp
// 选区 [startOffset, endOffset)
for (auto& run : visibleRuns) {
    int runStart = run.offset;
    int runEnd = runStart + run.text.length();
    
    if (runEnd <= startOffset || runStart >= endOffset) {
        // run 完全在选区外，正常画
        DrawRun(run, normalColor);
    } else {
        // run 与选区有交集，分三段：前缀、选中部分、后缀
        int selStart = std::max(startOffset, runStart) - runStart;
        int selEnd = std::min(endOffset, runEnd) - runStart;
        
        DrawText(run.text.substr(0, selStart), normalColor);
        DrawRect(selStart, selEnd);  // 选区底色
        DrawText(run.text.substr(selStart, selEnd - selStart), normalColor);
        DrawText(run.text.substr(selEnd), normalColor);
    }
}
```

**简化方案**（Phase 2 首版）：选区矩形横跨整行，不按字符精确裁剪。后续优化。

---

### 决策 10：Demo 页面工具栏
**Phase 1**：三个按钮 — 加粗、红色、大字号。

**行为**：点击按钮修改 `currentStyle_`，后续打字应用新样式。按钮状态（按下/
抬起）反映当前样式。

**Phase 3 扩展**：`SetSelectionBold()` — 选中已有文字，点按钮修改样式。

**UI 布局**：
```
[B] [红] [A↑]   ← 工具栏按钮（StackPanel Horizontal）
┌─────────────┐
│ 光标闪烁    │  ← RichTextBox
└─────────────┘
```

---

## 开放问题（等用户回来讨论）

1. **多行支持优先级**：Phase 2 要不要立刻做多行？还是先完成单行的选区、撤销、
   剪贴板，多行留给 Phase 4？

2. **Run 合并时机**：永远不合并？停顿 500ms 后异步合并？撤销推栈时合并？

3. **IME 输入**：Phase 几加？TextEditBase 有完整实现可参考，但需要处理 run
   切分（组合字符可能跨 run）。

4. **样式工具栏**：是否需要字号下拉框、颜色选择器？还是只要按钮演示即可？

5. **性能目标**：10 万字符、1000 个 run 的文档，渲染帧率目标是多少？

---

## 实施检查清单

### Phase 1（基础渲染 + 固定光标打字）
- [ ] RichTextDocument 数据结构
- [ ] InsertText/DeleteRange/GetRunAt/GetStyleAt
- [ ] RichTextBox::Render（遍历 run，累加 X 坐标）
- [ ] 光标闪烁（定时器 500ms）
- [ ] OnChar 追加字符（末尾）
- [ ] Backspace 删除末尾字符
- [ ] 单元测试（run 切分、边界情况）
- [ ] Demo 页面（工具栏 + RichTextBox）
- [ ] Debug 编译 + 测试通过
- [ ] Release 编译验证

### Phase 2（交互：光标移动 + 选区）
- [ ] 鼠标点击定位光标（HitTestPoint）
- [ ] 方向键移动光标（Left/Right/Home/End）
- [ ] 鼠标拖拽选区（anchor + current）
- [ ] 选区高亮渲染（跨 run 矩形）
- [ ] Shift+方向键扩展选区
- [ ] Ctrl+A 全选
- [ ] Backspace/Delete 删除选区
- [ ] 测试：选区边界、跨 run 删除

### Phase 3（编辑功能：剪贴板 + 撤销 + 样式 API）
- [ ] Ctrl+C 复制选区（纯文本）
- [ ] Ctrl+V 粘贴（用当前样式）
- [ ] 撤销栈（快照法）
- [ ] Ctrl+Z/Y 撤销/重做
- [ ] SetSelectionBold/Color/FontSize
- [ ] 工具栏按钮应用样式到选区
- [ ] 测试：撤销栈深度、样式跨 run 应用

---

**最后更新**：2026-09-12，开始 Phase 1 实施
