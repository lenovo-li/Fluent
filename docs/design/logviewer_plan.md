# LogViewer 和 RichTextBox 控件实施计划

## 背景

FluentUI 当前缺少两个关键文本显示控件：

1. **LogViewer** - 只读虚拟化日志查看器，支持百万行彩色输出
2. **RichTextBox** - 可编辑富文本编辑器，支持样式、图片、格式（未来）

用户需求明确：LogViewer 优先，支持动态行高 + 行内任意样式（颜色、字号、粗细、下划线）。

## 架构决策

### LogViewer 特性

- **虚拟化渲染**：只绘制可见行（30-50 行），支持百万行日志
- **动态行高**：每行高度 = max(所有 span 的 fontSize × lineSpacing)
- **行内多样式**：一行可包含多个 TextSpan，每个独立设置颜色/字号/粗细/下划线
- **只读**：无编辑能力，无光标，无选择（简化实现）
- **固定宽度字体**：初期仅支持等宽字体（Consolas），避免复杂的文本对齐

### 数据结构

```cpp
struct TextSpan {
    std::wstring text;
    Color color;
    float fontSize;
    DWRITE_FONT_WEIGHT weight;
    bool underline;
};

struct LogLine {
    std::vector<TextSpan> spans;
    float height;  // 缓存的行高，首次渲染时计算
};
```

### 参考实现

- **继承**：直接继承 `Control`，不继承 `Selector` 或 `ItemsControl`
- **滚动**：嵌入 `ScrollViewer scroll_` 成员（类似 ListBox）
- **渲染**：遍历可见行范围，逐 span 用 DirectWrite 绘制
- **输入**：鼠标滚轮 + 拖拽滚动条，无键盘导航

## 实施阶段

### Phase 1：最小可用原型（当前任务）

**目标**：编译通过 + 显示 10 行彩色文本

**交付物**：
- `FluentUI/controls/LogViewer.h` - 基础类声明
- `FluentUI/controls/LogViewer.cpp` - 渲染循环
- 注册到 `FluentUI.vcxproj` 和 `CMakeLists.txt`
- 构建系统验证：`check_source_lists.py` 通过

**API 设计**：
```cpp
class LogViewer : public Control {
public:
    void AppendLine(std::vector<TextSpan> spans);
    void Clear();
    size_t LineCount() const;
    
protected:
    void Render(const DrawingContext& dc) override;
    void OnPointerWheelChanged(PointerEventArgs& e) override;
    void OnBoundsChanged() override;
};
```

**验证标准**：
- MSBuild Debug 干净编译
- FluentUITests.exe 现有测试全绿（不添加新测试）

### Phase 2：虚拟化渲染（下一个任务）

**目标**：支持 100 万行日志，滚动流畅

**技术要点**：
- 二分查找可见行范围（动态行高需要累加计算）
- 缓存每行的 Y 坐标偏移量（避免重复累加）
- 滚动条 extent = 所有行高之和

### Phase 3：Demo 集成

**目标**：FluentUIDemo 中添加 LogViewerDemo 页面

**演示内容**：
- 彩色日志输出（ERROR 红色、WARNING 黄色、INFO 绿色）
- 混合字号（时间戳小字、错误消息大字）
- 性能测试（10 万行秒开）

### Phase 4：RichTextBox（未来）

**延期原因**：
- 编辑器复杂度远高于只读查看器
- 需要光标、选择、撤销栈、IME 输入
- 优先完成 LogViewer 验证架构

## 当前任务记录

### 已完成
- [x] 创建设计文档
- [x] 注册源文件到 FluentUI.vcxproj / CMakeLists.txt
- [x] 验证 check_source_lists.py 同步

### 进行中
- [ ] 修复编译错误（API 签名不匹配）
  - `Invalidate()` 不接受参数（应该用 `InvalidateMeasure()` / `InvalidateArrange()`）
  - `RectDip` 字段是 `x/y/w/h`，不是 `top/Width/Height`
  - `ThemeSnapshot` 不是指针，直接用 `.generation`
  - `MouseButton::Left` 应该是 `PointerButton::Left`
  - 缺少头文件引用（DWriteContext / InputManager）

### 待办
- [ ] 清理不必要的成员变量（isDragging / hoverLineIndex）
- [ ] 简化首个原型（去掉鼠标交互）
- [ ] 编译通过
- [ ] 运行 FluentUITests.exe 验证无回归

## 风险与约束

**性能约束**：
- 每帧绘制时间 <1ms（30 行 × 10 spans = 300 TextLayout 创建）
- 内存占用：100 万行 × 100 字节 = 100 MB（可接受）

**API 稳定性**：
- Phase 1 的 API 是临时的，Phase 2 可能重构
- 不暴露到公开头文件，仅内部使用

**测试策略**：
- Phase 1 无自动化测试（手工验证 Demo）
- Phase 2 添加性能测试（100 万行加载时间）
- Phase 3 添加功能测试（滚动、样式渲染）
