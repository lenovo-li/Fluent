# RichTextBox

多样式可编辑文本控件——每个 run 有独立的字号、字重、颜色、下划线，且支持段落级
格式（对齐、行间距、缩进）。

## 何时用

- 需要**内联样式混排**（一行里粗体+斜体+不同颜色）
- 需要**段落格式**（居中、行距、悬挂缩进）
- 纯文本编辑器用 TextArea（更快，支持百万字符虚拟化）

## 基础用法

```cpp
auto* rtb = panel->Add(std::make_unique<RichTextBox>());
rtb->SetWidth(400.0f);
rtb->SetHeight(300.0f);

// 监听文本变化
rtb->TextChanged().Subscribe(this, [](void* owner, RichTextBox& sender, std::wstring& text) {
    // text 是完整的纯文本快照（变化后）
    static_cast<MyDialog*>(owner)->OnDocumentChanged(text);
});

// 监听选区变化
rtb->SelectionChanged().Subscribe(this, 
    [](void* owner, RichTextBox& sender, RichTextBox::SelectionChangedArgs& args) {
        // args.start / args.length 是当前选区
        auto* dlg = static_cast<MyDialog*>(owner);
        dlg->UpdateFormatToolbar(args.start, args.length);
    });
```

## API

### 文档操作

```cpp
void SetDocument(RichTextDocument doc);
const RichTextDocument& Document() const;
```

替换整个文档。光标移到末尾，选区清空，撤销栈清空（旧文档的快照不可恢复）。

### 只读模式

```cpp
void SetReadOnly(bool ro);
bool ReadOnly() const;
```

阻止所有编辑操作（键入、粘贴、删除、撤销/重做、样式变更）。选区、复制、滚动仍可用。

### 样式查询与修改

```cpp
TextRun GetCurrentStyle() const;
void SetCurrentStyle(const TextRun& style);
void ApplyStyle(const TextRun& style);
```

- `GetCurrentStyle`: 有选区时返回选区起点的样式；光标在空文档的 0 位置时返回默认
  样式（12pt 黑色普通无衬线）
- `SetCurrentStyle`: 设置**下次插入**的样式。不改已有文本，只影响接下来键入/粘贴的字
- `ApplyStyle`: 重设选区的样式。无选区时等同 `SetCurrentStyle`（成为插入样式）。
  有选区时，选中的所有 run 被替换为这一个样式——**扁平化**，颜色/字号的差异会丢失

### 单属性修改（保留其他属性）

```cpp
void SetSelectionBold(bool bold);
void SetSelectionUnderline(bool underline);
void SetSelectionColor(const D2D1_COLOR_F& color);
void SetSelectionFontSize(float sizeDip);
void SetSelectionFontWeight(DWRITE_FONT_WEIGHT weight);
```

只改一个属性，其他的保持原样——加粗一段混色文字时每种颜色都留下。这和 `ApplyStyle`
的区别：后者是全覆盖，前者是补丁。

无选区时它们设置的是「待插入」样式（`currentStyle_`），所以工具栏按钮在「先选后点」
和「先点后打」两种流程下行为一致。

### 段落格式

```cpp
void SetParagraphAlignment(DWRITE_TEXT_ALIGNMENT alignment);
void SetParagraphLineSpacing(float multiple);   // 1.0 单倍，1.5 1.5倍，2.0 双倍
void SetParagraphLineHeight(float dip);         // 固定行高（覆盖 multiple）
void SetParagraphSpaceBefore(float dip);
void SetParagraphSpaceAfter(float dip);
void SetParagraphFirstLineIndent(float dip);    // 负值 = 悬挂缩进（首行左移）
void SetParagraphLeftIndent(float dip);
void SetParagraphRightIndent(float dip);
void ApplyParagraphStyle(const ParagraphStyle& style);
ParagraphStyle GetCurrentParagraphStyle() const;
```

这些属性是**段落级**的（「居中」不是字符能有的属性）。作用对象是选区触及的所有段落，
或光标所在段落（无选区时）。触及一个段落的一个字符就格式化整段，因为半个段落不能
既居中又左对齐。

和 `SetSelectionXxx` 不同，这些方法**没有「待插入」回退**：光标处始终有一个真实的
段落可以格式化，所以调用总是修改文档。因此即使无选区它们也受 `readOnly_` 阻挡，而
`SetSelectionBold` 无选区时只改 `currentStyle_`，不碰文档，所以不受阻。

跨段落选区调用 `GetCurrentParagraphStyle()` 时，返回第一个段落的样式（工具栏需要
显示一个状态而不是什么都不显示）。

### 选区操作

```cpp
UINT32 SelectionStart() const;
UINT32 SelectionLength() const;
std::wstring SelectedText() const;
bool HasSelection() const;
void Select(UINT32 start, UINT32 length);
void SelectAll();
```

索引是 UTF-16 code unit 偏移量（和 `std::wstring::operator[]` 一致）。光标位于
`[0..length]` 之间，即「字符之前」而非「字符上」。

`SelectedText()` 返回纯文本（不带样式），即 Copy 会放到剪贴板上的内容（但这个方法
本身不碰剪贴板）。

### 撤销/重做

```cpp
bool CanUndo() const;
bool CanRedo() const;
```

撤销栈是快照制：每个条目是整个段落向量（含 run）+ 光标/锚点。上限 100 条（`kMaxUndo`），
超出后最老的被丢弃。

为什么不用操作日志？因为富文本编辑既改文字**又**改样式，逆操作需要携带每个被碰 run
的旧样式——这就是快照的内容，而逆操作还要写复杂的反向逻辑。快照简单且完备。

Ctrl+Z / Ctrl+Y 在 `OnKeyDownRouted` 里已绑定，外部不需要手动调。

### 内容尺寸与滚动

```cpp
float ContentHeight(float outerWidth = -1.0f) const;
float ContentWidth(float outerWidth = -1.0f) const;
bool HorizontallyScrollable() const;
bool VerticallyScrollable() const;
float ScrollOffset() const;
void ScrollTo(float offsetDip);
```

- `ContentHeight/Width`: 带 padding 的文档排版尺寸，即滚动条以它为依据。`outerWidth < 0`
  时用 `bounds_.w`；Measure 会传入约束宽度，因为那时 `bounds_` 还未更新
- `Horizontally/VerticallyScrollable`: 内容是否超出视口（即滚动条是否激活）。Wrap 模式
  下前者永远 false
- `ScrollOffset/ScrollTo`: 当前滚动位置（从文档顶部起算的 DIP），及编程式滚动。值
  被夹到合法范围。用于恢复保存的滚动位置、或滚动到搜索命中点

### 换行模式

```cpp
void SetWordWrap(bool wrap);
bool WordWrap() const;
```

- **Wrap**（默认）：行在控件宽度处回折，无横向滚动，resize 时每行都重排
- **NoWrap**：每个逻辑行（`\n` 分隔）是一条视觉行，右侧跑出去。代码编辑器/表格文本
  需要这个，因为折行会破坏列对齐

切换模式会重建布局（文档高度变化，NoWrap 通常矮得多），滚动偏移被重新夹紧。

### 光标矩形（IME 候选框定位）

```cpp
bool CaretRectDip(RectDip& out) const override;
```

返回光标的 DIP 矩形（窗口坐标，已计入滚动）。IME 候选窗依赖它定位，所以必须实现。
未聚焦时返回 false。

## 事件

### TextChanged

```cpp
Event<RichTextBox, std::wstring>& TextChanged();
```

文档内容变化后触发（键入、粘贴、删除、撤销、重做、`SetDocument`）。载荷是变化**后**
的完整纯文本快照（不带样式）。

编辑同时会移动光标，所以这个事件之后**总是**跟着 `SelectionChanged`——订阅者会收到
两个通知。

### SelectionChanged

```cpp
struct SelectionChangedArgs { UINT32 start; UINT32 length; };
Event<RichTextBox, SelectionChangedArgs>& SelectionChanged();
```

选区或光标移动时触发（不改文本）。也在 `TextChanged` 之后触发（编辑会移光标）。

场景：
- 工具栏按钮的启用/禁用（有选区时「加粗」可用）
- 格式工具栏的状态更新（显示当前位置的字号/颜色/对齐）
- 标尺上的缩进手柄位置

## 输入处理

### 键盘快捷键（内置）

- **Ctrl+C / Ctrl+Insert**: 复制
- **Ctrl+X / Shift+Delete**: 剪切（只读模式下无效）
- **Ctrl+V / Shift+Insert**: 粘贴（只读模式下无效）
- **Ctrl+Z**: 撤销（只读模式下无效）
- **Ctrl+Y**: 重做（只读模式下无效）
- **Ctrl+A**: 全选
- **Backspace / Delete**: 删除（只读模式下无效）
- **方向键**: 移动光标；Shift 扩展选区；Ctrl+左右 按词跳转
- **Home / End**: 行首/行尾；Ctrl+Home/End 文档首/尾
- **PageUp / PageDown**: 滚动一屏

### IME 支持

中文/日文/韩文输入法的组合字符串（composition）在提交前绘制在光标处，但**不**进入
文档——组合是临时的，可以取消，不能进撤销栈也不能被复制。

组合期间，布局里的偏移量比文档偏移量多 `composition_.size()`（`LayoutOffset()` 负责
这个转换）。`LayoutText()` 返回 `doc_.GetPlainText() + composition_`，这是
`IDWriteTextLayout` 看到的文本。

### 多击选择

- **双击**: 选中词（`WordBoundary` 定义）
- **三击**: 选中逻辑行（`\n` 到 `\n`）

拖动时保持粒度：双击后拖动时按词扩展，初始双击的那个词始终保留在选区里（即使指针
往回拖）。

## 架构细节

### 文档模型：分段布局

一个段落对应一个 `IDWriteTextLayout`（`paragraphLayouts_` 是向量）。段落在垂直方向
堆叠，`spaceBefore/spaceAfter` 是段落间隙。

为什么不是全文一个 layout？因为段落级格式（对齐、行间距、缩进）在 DirectWrite 里
是 `IDWriteTextFormat` 或 `IDWriteTextLayout` 的属性，不能在一个 layout 内给不同
段落设不同的对齐方式。

缩进的实现：DirectWrite 没有原生缩进 API，所以用两步模拟——
1. 布局盒宽度减去 `leftIndent + rightIndent`（挤窄）
2. 绘制时把原点右移 `leftIndent`（视觉偏移）

`firstLineIndent` 是额外的首行偏移，负值即悬挂缩进（首行左移，项目符号列表就是这个）。

### 画刷生命期

`SetDrawingEffect` 需要画刷在 layout 生命期内保持有效，所以 `runBrushes` 和 layout
在同一个 `ParagraphLayoutInfo` 里，一起创建、一起销毁。

### 光标闪烁

`WantsBlink()` 返回 `IsFocused()`，窗口的闪烁定时器驱动 `OnBlink()` 翻转
`caretVisible_` 并 `Invalidate()`。失焦时自动停止闪烁（不需要手动取消注册）。

### 滚动条行为

`ScrollViewer` 配置为 `SetKeepVisibleWhenOverflow(false)`——内容适配时滚动条完全不画，
滚动或悬停时淡入，空闲一段时间后淡出。编辑器大部分时间在阅读而非滚动，固定轨道是噪音。

### Changed() vs SelectionMoved() vs Refresh()

三个内部方法，职责不同：

- **`Changed()`**: 文档内容变化（文本或样式）→ 刷新布局 → 触发 `TextChanged` + 
  `SelectionChanged`（编辑总是移光标）
- **`SelectionMoved()`**: 光标/选区移动但文档未变 → 刷新布局 → 只触发 `SelectionChanged`。
  用于 `Select` / `SelectAll` / `MoveCaret` / 拖选。用 `Changed()` 会错误地告诉订阅者
  文档被编辑了，破坏「标记未保存」和自动保存
- **`Refresh()`**: 两者共享的布局/范围/光标可见性刷新工作，不触发任何事件

## 性能特征

- **文档规模**: 全内容在内存，全文一次布局（分段但非虚拟化）。适合 1–10 页文档，
  不适合百页长文或日志（用 TextArea）
- **布局成本**: O(段落数 × 段内字符数)。换行模式切换、resize、样式变更都会重建布局
- **撤销栈**: 每个快照是整个段落向量的深拷贝。100 条上限（`kMaxUndo`），超出后最老的丢弃
- **滚动**: 通过 `ScrollViewer`，整个文档始终绘制（无视口裁剪）。大文档滚动会重绘全文

## 限制与未来

- **无表格**: 当前不支持单元格、合并、边框
- **无列表计数器**: 项目符号/编号需要手动文本（`"1. "`）或字形（`"• "`）
- **无图片内联**: 不支持 `<img>` 语义，只能文本
- **段落向量重建**: 任何样式变更都重建整个段落向量（未做增量修改）

这些可以加，但当前版本专注于「内联样式混排 + 段落格式」这个核心能力。

## 备注

### 已修复的设计坑

**撤销快照必须含段落向量**：早期版本快照的是扁平 run 列表，撤销后段落格式（对齐、
行距、缩进）全部丢失——因为 run 里没有这些字段。现在快照是 `std::vector<Paragraph>`，
格式完整保留。

### 设计决策

**不继承 TextEditBase。** TextEditBase 以 `std::wstring text_` 为唯一数据源，而
RichTextBox 的数据源是 `RichTextDocument`（段落 → run）。继承会产生两份真相，要么同步、
要么重写基类几乎全部逻辑（光标、选区、撤销、索引计算）。所以 RichTextBox 直接派生自
`Control`，自己实现光标、选区、输入、剪贴板和 IME，必要处参照 TextEditBase 的写法。
也因此只读用同名的 `SetReadOnly` / `ReadOnly()`，而不是继承来的。

**相邻同样式 run 自动合并。** `MergeAdjacentRuns()` 在每次 `InsertText` / `DeleteRange`
后运行，`AppendRun` 也会并入末尾同样式 run。不合并的话，跨 run 删除会留下两段同样式的
相邻 run（删掉 `abc**BOLD**def` 的 BOLD 后剩 `abc` + `def`），文档越编辑越碎，而 run 数
直接决定 `SetDrawingEffect` 等范围格式调用的次数。合并是 O(run 数)，run 数本来就小。
打字快路径不受影响：插入点落在同样式 run 内部时直接 `text.insert()`，不新建 run、不扫描
（测试 `TypingCharacterByCharacterStaysOneRun` 锁定：逐字符打 5 个字仍只有 1 个 run）。
代价是「两段手动设成同样式的文字」合并后无法再区分——样式相同即视觉相同，目前没有需要
区分的场景；段落边界由独立的 `Paragraph` 结构表达，不依赖 run 边界。

**光标处样式的查询规则。** `GetCurrentStyle()` 有选区时查选区起点，否则查光标偏移处
（即光标**右侧**）的字符；光标在文末时退回最后一个字符；文档为空时返回待输入样式
`currentStyle_`（`RichTextBox.cpp` 的 `GetCurrentStyle`）。注意这与 Word「跟随左侧字符」
的习惯不同。

**剪贴板只走纯文本。** 复制写 `CF_UNICODETEXT`，粘贴读纯文本并套用当前光标样式。样式
序列化（HTML / RTF / 私有格式）尚未实现。

### 与 TextArea 的选择

| 维度 | RichTextBox | TextArea |
|------|-------------|----------|
| 样式 | 每个 run 独立字号/颜色/字重 | 全文单一样式 |
| 段落格式 | 对齐/行距/缩进 | 无（只有全局 lineHeight） |
| 容量 | 1–10 页（全内容布局） | 百万行（虚拟化） |
| 滚动 | 重绘全文 | 只绘可见行 |
| 用途 | 富文本编辑器、邮件撰写 | 代码编辑器、日志查看 |

两者不能互换：TextArea 有虚拟化但无样式；RichTextBox 有样式但无虚拟化。
