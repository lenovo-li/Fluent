# MenuFlyout

弹出式菜单——上下文菜单、命令按钮下拉、级联子菜单的统一实现。

## 何时用

- **右键上下文菜单**：文件列表、编辑器选区、树节点的操作菜单
- **命令按钮下拉**：工具栏按钮下拉展开选项（"新建" → "文件" / "文件夹" / "项目"）
- **MenuBar 下拉**：菜单栏的各个顶级菜单（"文件" / "编辑" / "视图"）
- **级联子菜单**：一个菜单项展开另一层（"导出" → "PNG" / "SVG" / "PDF"）

不要用于：
- 永久显示的选项列表 → 用 ListBox
- 单选/多选工具栏 → 用 ToggleButton / CheckBox
- 对话框操作 → 直接放 Button

## 基础用法

```cpp
// 1. 构建菜单项
std::vector<MenuItem> items;

MenuItem cut;
cut.text = L"剪切";
cut.accelerator = L"Ctrl+X";
cut.AddInvokeHandler([this]() { this->OnCut(); });
items.push_back(std::move(cut));

MenuItem copy;
copy.text = L"复制";
copy.accelerator = L"Ctrl+C";
copy.AddInvokeHandler([this]() { this->OnCopy(); });
items.push_back(std::move(copy));

items.push_back(MenuItem::Sep());  // 分隔线

MenuItem paste;
paste.text = L"粘贴";
paste.accelerator = L"Ctrl+V";
paste.enabled = HasClipboardText();
paste.AddInvokeHandler([this]() { this->OnPaste(); });
items.push_back(std::move(paste));

// 2. 创建 MenuFlyout 并注入服务上下文
auto menu = std::make_unique<MenuFlyout>();
menu->SetContext(Context());  // 继承窗口/D2D/DWrite 服务
menu->SetItems(std::move(items));

// 3. 在鼠标位置显示（右键菜单）
menu->ShowAt(screenX, screenY);

// 4. 订阅关闭事件（可选）
closedSub_ = menu->Closed().Subscribe(this, 
    [](void* owner, MenuFlyout&, RoutedEventArgs&) {
        static_cast<MyControl*>(owner)->OnMenuClosed();
    });

// 5. 持有 MenuFlyout 直到关闭
menu_ = std::move(menu);
```

## API

### 构建菜单

```cpp
void SetItems(std::vector<MenuItem> items);
void SetItemsBorrow(const std::vector<MenuItem>* items);
```

- `SetItems`: 取走所有权（菜单项被移动到 flyout 内部）。每次打开都重建项时用这个
- `SetItemsBorrow`: 借用调用方持有的向量（flyout 不拷贝，只保存指针）。MenuBar 用这个
  来复用同一份菜单多次打开，避免每次都拷贝向量。**调用方必须保证向量的存活期覆盖整个
  借用期，而不只是「菜单打开期间」**——`Close()` 不清除 `borrowedItems_`
  （`MenuFlyout.cpp:627-642`），指针一直留到下次 `SetItems`（`.cpp:361` 是唯一清除点）
  或 flyout 销毁。菜单关闭后让被借用的向量先析构，下次打开就是 use-after-free

正在打开时调用任一方法会先关闭菜单。

### 打开菜单

```cpp
void ShowAt(int screenX, int screenY) override;
void ShowBelow(const RECT& anchorScreenRect);
```

- `ShowAt`: 左上角对齐到给定的**物理屏幕像素**坐标（通常是鼠标位置）。超出工作区时
  翻转/夹紧
- `ShowBelow`: 左对齐到锚定矩形的正下方（**物理像素**）。超出工作区底部时翻转到上方。
  用于命令按钮下拉、MenuBar

两者都是异步的：方法返回后菜单已显示，但关闭发生在之后某个时刻（用户点击/按 Esc）。

### 关闭菜单

```cpp
void Close();
bool IsOpen() const;
```

`Close()` 关闭整个菜单链（包括所有打开的子菜单层级）。关闭后触发 `Closed()` 事件。

### 关闭事件

```cpp
Event<MenuFlyout, RoutedEventArgs>& Closed();
```

菜单完全关闭后触发（无论是用户点击外部、选中菜单项、按 Esc、还是程序调 `Close()`）。

场景：
- 释放 MenuFlyout 的持有权（菜单关闭后不再需要）
- 重新聚焦到触发菜单的控件
- 记录用户是否选了项（vs 取消）

### 透明度

```cpp
void SetCardOpacity(float opacity);
float CardOpacity() const;
void SetContentOpacity(float opacity);
float ContentOpacity() const;
```

- `CardOpacity`: 弹出窗口的背景卡片透明度 [0..1]，默认 1.0（不透明）。值 < 1 时父窗口
  内容透过显示（因为弹出窗口无材质）
- `ContentOpacity`: 菜单项文本/图标的透明度 [0..1]，默认 1.0

这两个值应用于所有层级（包括之后打开的子菜单）。

### 解散守卫

```cpp
void SetDismissGuard(std::function<bool(int screenX, int screenY)> cb);
```

点击外部时的保护：回调返回 `true` 时菜单**不关闭**（调用方自己处理这次点击）。MenuBar
用这个来实现「点击另一个标题时切换菜单而不是关闭」。

点击菜单自己的弹出窗口内部时总是保持打开（不走守卫）。

### 服务上下文

```cpp
void SetContext(const UIContext& ctx);
void SetOwnerContext(const UIContext& ctx) override;
```

MenuFlyout 不是树节点（它不继承 `UIElement`），所以需要手动注入服务上下文（窗口/D2D/
DWrite）。两个方法等价，后者是 `IContextMenu` 接口要求的（`UIElement::SetContextMenu`
会自动转发）。

不注入上下文的后果：菜单项无法测量文本宽度（`Dwrite()` 返回空），弹出窗口无法注册
键盘/解散钩子。

## MenuItem

菜单项——文本 + 可选的快捷键提示 + 可选的子菜单 + 点击回调。

### 属性

```cpp
std::wstring text;
std::wstring accelerator;        // 右对齐提示，如 L"Ctrl+C"
bool separator = false;          // 分隔线（不可选中，无交互）
bool enabled = true;
bool checked = false;            // 前导勾选标记
std::vector<MenuItem> submenu;   // 非空 = 父项（绘制右箭头）
```

- `text`: 显示文本。空字符串时画空行（配合 `separator = true` 得到分隔线）
- `accelerator`: 快捷键提示（纯显示，不绑定实际键盘快捷键）。MenuFlyout 不处理快捷键
  触发——应用的键盘处理器自己调 `MenuItem::RaiseInvoked()`
- `separator`: 分隔线（细灰线，不可交互）。便捷构造：`MenuItem::Sep()`
- `checked`: 前导勾选图标。不自动切换——回调里自己翻转 `item.checked` 再重建菜单
- `submenu`: 子菜单项向量。非空时这一项成为**父项**（绘制右箭头，点击/悬停时打开子菜单，
  不触发 `Invoked`）

### 订阅点击事件

```cpp
Event<MenuItem, RoutedEventArgs>& Invoked();
void AddInvokeHandler(std::function<void()> cb);
```

- `Invoked()`: 标准 Event 接口。订阅者必须存储返回的 `Subscription`（丢弃即取消订阅）
- `AddInvokeHandler`: 便捷包装，接受捕获 lambda，菜单项自己存储闭包和订阅。可多次调用
  （每个回调都执行）

示例：

```cpp
// 方式 1：标准 Event 订阅（需要存储 Subscription）
Subscription sub_;
sub_ = item.Invoked().Subscribe(this, [](void* owner, MenuItem&, RoutedEventArgs&) {
    static_cast<MyDialog*>(owner)->OnCut();
});

// 方式 2：便捷包装（无需存储 Subscription）
item.AddInvokeHandler([this]() { this->OnCut(); });
```

区别：
- `Invoked()` 零分配（回调是函数指针，状态通过 `owner` 指针传递）
- `AddInvokeHandler` 每次调用分配一个 `std::function<void()>`（但菜单构建是一次性的，
  开销可忽略）

### 编程式触发

```cpp
void RaiseInvoked();
bool HasInvokeHandler() const;
```

- `RaiseInvoked()`: 手动触发 `Invoked` 事件（就像用户点击了这一项）。无订阅者时无操作
- `HasInvokeHandler()`: 是否有人订阅了 `Invoked`

场景：键盘快捷键绑定——窗口的 `OnKeyDownRouted` 里判断 `Ctrl+C`，找到对应的菜单项，
调 `item.RaiseInvoked()`，复用同一份逻辑。

### 可移动、不可拷贝

```cpp
MenuItem(MenuItem&&) noexcept;            // 可移动
MenuItem(const MenuItem&) = delete;       // 不可拷贝
```

菜单项是**值类型**，通过 `std::move` 移入向量：

```cpp
std::vector<MenuItem> items;
MenuItem cut;
cut.text = L"剪切";
items.push_back(std::move(cut));  // ✅ 移动
```

不能拷贝的原因：`Invoked()` 是 `Event<>`，订阅者持有它的地址（`Subscription` 内部保存
指针）。拷贝会产生两个地址，已有的订阅会指向旧地址，而新菜单项无法收到通知——结果是
隐式取消订阅。这种行为令人惊讶且难以调试，所以禁止拷贝。

为什么能移动？`Event<>` 存储在 `std::unique_ptr` 里，向量移动指针（Event 本身的地址
不变），所以订阅保持有效。

## 级联子菜单

子菜单项写在父项的 `submenu` 向量里：

```cpp
MenuItem exportMenu;
exportMenu.text = L"导出";

MenuItem png;
png.text = L"PNG 图像";
png.AddInvokeHandler([this]() { this->ExportPNG(); });
exportMenu.submenu.push_back(std::move(png));

MenuItem svg;
svg.text = L"SVG 矢量";
svg.AddInvokeHandler([this]() { this->ExportSVG(); });
exportMenu.submenu.push_back(std::move(svg));

items.push_back(std::move(exportMenu));
```

行为：
- **悬停**父项立即打开子菜单（同时关闭旧的子菜单层级）
- **点击**父项打开子菜单
- **右箭头**键从父项进入子菜单
- **左箭头**键从子菜单返回父层
- 点击子菜单的叶项后，**整个菜单链**（根菜单 + 所有子菜单）一起关闭

父项不触发 `Invoked`——它的职责是打开子菜单。

## 自动解散

整个菜单链关闭的情况（`HandleDismiss`）：

1. **点击菜单外部** —— 除非 `dismissGuard_` 返回 true（见上）
2. **失去激活** —— 除非激活的是自己的另一个弹出窗口（子菜单获得激活时不能解散父菜单）
3. **父窗口移动或缩放** —— 无条件关闭（菜单已经不在锚定位置了）
4. **选中叶项** —— 触发 `Invoked` 后自动关闭

点击菜单**内部**不关闭（无论是父项、子菜单、还是禁用项）。

Esc 不在这个列表里：它只关闭最深一层，只有根层时才关闭整个菜单（见键盘导航）。

## 键盘导航

| 键 | 行为 |
| --- | --- |
| ↑ / ↓ | 选中上一个/下一个可选项（跳过分隔线和禁用项，首尾循环） |
| Home / End | 跳到第一个/最后一个可选项 |
| → | 打开当前选中父项的子菜单（无子菜单时无操作） |
| ← | 从子菜单返回父层（根层时无操作） |
| Enter / Space | 触发当前选中项（父项时打开子菜单，叶项时触发 `Invoked` 并关闭） |
| Esc | **只关闭最深一层**；只剩根层时关闭整个菜单 |

Esc 的逐层退出是刻意的：三层深的菜单里按一次 Esc 应该退回上一层，而不是整个消失。

按键**不**由弹出窗口自己接收——父窗口保持焦点（标题栏不变灰），按键经
`WindowServices::RegisterActivePopupKeyHandler` 路由进来，再转发给最深层的列表视图。

键盘输入路由：MenuFlyout 通过 `WindowServices::RegisterActivePopupKeyHandler` 注册回调，
父窗口的 `OnKeyDownRouted` 收到按键后转发给它——这样菜单弹出时父窗口保持焦点（标题栏
不变灰），但按键由菜单处理。

## 右键菜单（IContextMenu）

`UIElement` 有两条右键菜单入口，区别在于**谁拥有菜单**（`UIElement.h:326-338`）：

```cpp
// 1. 独占：本元素拥有这个菜单
auto menu = std::make_unique<MenuFlyout>();
menu->SetItems(BuildMyMenuItems());
myControl->SetContextMenu(std::move(menu));  // 自动注入 Context，右键时调 ShowAt
```

```cpp
// 2. 共享：菜单由别处持有，多个元素引用同一个实例
MenuFlyout sharedMenu;                       // 生命周期必须长于所有引用它的元素
sharedMenu.SetItems(BuildMyMenuItems());
controlA->SetContextMenuRef(&sharedMenu);
controlB->SetContextMenuRef(&sharedMenu);
```

`SetContextMenu` 收 `std::unique_ptr<IContextMenu>`（**不是** `shared_ptr`），元素独占所有权。
要让多个控件共享同一个菜单，用 `SetContextMenuRef(IContextMenu*)` 的非拥有引用——此时
菜单的存活由调用方负责，元素不参与。

## 架构细节

### 多层级栈

`levels_` 是向量，`levels_[0]` 是根菜单，`levels_[1]` 是第一层子菜单，以此类推。每个
层级包含：
- 一个 `PopupHost`（独立的 WS_POPUP 窗口）
- 一个 `MenuListView`（内容控件，渲染菜单项）
- 父层索引（根层是 -1）

### 延迟销毁（retired_ 向量）

关闭层级时不能立即 `delete PopupHost`——因为关闭可能发生在 PopupHost 的 `WndProc` 内部
（菜单项的 `onInvoke` 回调调了 `Close()`，而那次调用是从 `OnPointerReleased` 发起的）。
立即销毁会导致 use-after-free（WndProc 返回时栈已被释放）。

所以 `CloseFrom` 只**隐藏**层级并移入 `retired_` 向量，真正销毁发生在安全时刻：
- 下次 `OpenRoot`（打开新菜单前清理旧的）
- 或 `~MenuFlyout`（析构时一起清理）

这和 `ComboBox` 保持隐藏的弹出窗口活着是同一个模式。

### 借用 vs 持有

- **持有**（`SetItems`）：向量移入 `items_`，flyout 负责生命期
- **借用**（`SetItemsBorrow`）：`borrowedItems_` 指向调用方的向量，`items_` 为空

MenuBar 用借用来复用同一份菜单（每次打开都是同一个向量），避免每次都拷贝数百个菜单项。

### 窗口钩子生命期

菜单打开时注册两个钩子：
- **键盘路由**（`WindowServices::RegisterActivePopupKeyHandler`）
- **解散监测**（`WindowServices::RegisterDismissHandler`）

钩子持有 `[this]` 回调。关闭时 `Subscription::~Subscription()` 自动注销，所以窗口不会
持有悬垂指针——即使 MenuFlyout 在打开状态下被销毁。

### MenuItem 的 Event 堆分配

`Event<>` 是**不可拷贝**且**不可移动**的（订阅者持有它的地址）。MenuItem 是值类型，
在向量里移来移去。两者冲突。

解决：`invoked_` 和 `handlers_` 都是 `std::unique_ptr`——向量移动指针，Event 留在原
地址，订阅保持有效。

代价：每个有回调的菜单项多一次堆分配。但菜单构建是一次性的（不是每帧），可接受。

### 测量与定位

菜单宽度 = 最长项的文本宽度 + 快捷键宽度 + padding + 图标/箭头预留空间。

菜单高度 = `Σ(每项高度)` + padding。

定位：
- `ShowAt`: 左上角对齐到给定点，超出工作区时向左/上翻转
- `ShowBelow`: 左对齐锚定矩形，优先放下方，超出底部时翻转到上方

子菜单定位：右对齐父项的右边缘，垂直对齐父项的顶部。超出屏幕右边缘时翻转到左侧。

## 性能特征

- **菜单项数量**：无虚拟化，所有项一次绘制。适合 5–50 项，不适合数百项（用 ListBox）
- **子菜单层级**：理论无限制，实际 2–3 层（更深会让用户迷失）
- **子菜单打开**：悬停即开，无延迟。划过一排父项会依次开关每个子菜单

## 限制与未来

- **无图标**：当前不支持菜单项前导图标（只有勾选标记）
- **无单选组**：勾选是独立的布尔值，不支持「一组中只有一个勾选」的单选逻辑（应用自己维护）
- **无自定义内容**：菜单项是 `MenuItem` 结构体，不支持嵌入任意控件（如 Slider / ColorPicker）
- **固定样式**：菜单卡片样式（圆角、颜色）来自主题，不可单独定制
- **无阴影**：`PopupHost` 是 `WS_EX_NOREDIRECTIONBITMAP` 的 DComp 窗口，DWM 不认
  `CS_DROPSHADOW`（历史上试过，是个 no-op 并已移除）。需要阴影得自己画进 DComp surface

这些可以加，但当前版本专注于「文本菜单 + 级联子菜单」这个核心能力。

## 与其他控件的对比

| 控件 | 用途 | 打开方式 | 选择模式 |
|------|------|----------|----------|
| MenuFlyout | 临时操作菜单 | 右键/点击触发 | 单次点击执行 |
| ListBox | 永久选项列表 | 始终显示 | 单选/多选 |
| ComboBox | 单选下拉框 | 点击展开 | 单选 + 回填 |
| ToolBar | 常用命令栏 | 始终显示 | 每个按钮独立 |

MenuFlyout 是**临时**的——打开、操作、关闭，不占用永久屏幕空间。
