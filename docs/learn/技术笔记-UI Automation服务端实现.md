# 技术笔记：UI Automation 服务端实现

> **目标**：让讲述人、Inspect.exe、测试工具和 AI 代理能读取并操作 FluentUI 自绘的控件，
> 同时不拖慢 UI 线程：没有客户端时零开销，有客户端时也不在输入路径上同步做任何事。
>
> 本文记录实测得到、文档里不写或写得不清楚的结论，以及当时否决的方案。代码在
> `FluentUI/automation/`，扩展方式见 `docs/controls/inheritance-guide.md` 的“UI Automation”一节。

---

## 一、结构

```
AutomationHost（每个窗口一个，popup 按需一个）
  └─ WindowRootProvider            WM_GETOBJECT(UiaRootObjectId) 时才创建
       └─ ElementProvider ── AutomationPeer ── UIElement
                                 └─ ItemPeer（列表行、树行、日期格、单元格……不是元素）
```

- **peer 是纯 C++**，不包含 `<UIAutomation.h>`；COM 只在 `automation/Uia*.cpp` 里。
  控件通过重写 `CreateAutomationPeer()` 提供 peer，扩展库不需要碰 COM。
- **peer 惰性创建**：只有客户端访问到某个元素时才创建。没有客户端时每个元素只多一个空指针。
- **虚拟化控件不为每行建元素**：容器 peer 按需为客户端访问到的行创建 ItemPeer，按 key 缓存，
  上限 1024 个。key 的选择决定内容变化时客户端看到什么：下标（ListBox、DataGrid，行数变化时整体释放）、
  稳定 id（TreeView 的行 id、Calendar 的日期、LogViewer 的行序号）。

## 二、实测结论

### 1. 必须设置 `ProviderOptions_UseComThreading`

最初的假设是“不设 UseComThreading 时，UIA core 把所有调用转到拥有 HWND 的线程”。
用 Inspect.exe 实测推翻了它：`get_HostRawElementProvider` 在 UI 线程，但 `get_ProviderOptions` 和
`get_FragmentRoot` 在 UIAutomationCore 的工作线程上被调用（Debug 下的线程断言触发，调用栈为证）。

设置 UseComThreading 后，COM 把调用投递到 provider 所在的 STA，也就是 UI 线程
（`Application` 用 `COINIT_APARTMENTTHREADED` 初始化，消息泵用 `QS_ALLINPUT` 等待，能派发 COM 的投递）。
peer 和 provider 因此都不加锁。

`get_ProviderOptions` 是例外：UIA 要先问它才知道要不要走 COM 路由，所以它可能在任意线程上被调用。
它只返回常量，不做线程检查；其他方法在 Debug 下都检查线程。

### 2. 断开顺序：先 `UiaDisconnectProvider`，再让 provider 失效

先把 provider 置为失效（之后所有方法返回 `UIA_E_ELEMENTNOTAVAILABLE`）再调 `UiaDisconnectProvider`，
后者返回 0x80040201（`UIA_E_ELEMENTNOTAVAILABLE`）：UIA core 在断开过程中会回调 provider。

元素被移除时的做法：provider 立即“冻结”——只继续回答 `ProviderOptions`、`RuntimeId`、`FragmentRoot`
这几个断开过程要用的查询，其余返回 `UIA_E_ELEMENTNOTAVAILABLE`；真正的 `UiaDisconnectProvider`
放到投递的 `WM_FLUENT_UIA_FLUSH` 里做。

### 3. 不能在 SendMessage 里断开

`UiaDisconnectProvider` 在响应 `SendMessage` 时调用会返回 `RPC_E_CANTCALLOUT_ININPUTSYNCCALL`
（Microsoft Learn 有写）。元素移除可能发生在被 Send 过来的消息里，所以断开总是延后到投递的消息；
当前在 `InSendMessageEx` 里时不断开，也不重复投递（否则每次 flush 投递下一次，线程空转）。

窗口销毁时：`WM_DESTROY` 里先停止响应 `WM_GETOBJECT`，断开已冻结的元素 provider，再断开窗口根，
最后 `UiaReturnRawElementProvider(hwnd, 0, 0, nullptr)` 让 UIA 释放为事件保存的引用。

### 4. 模式操作必须投递执行

Invoke、Toggle、SetValue、Select、Expand 等不在 COM 调用里同步执行，而是投递，在下一次 flush 时执行，
执行前重新确认控件仍在树上且已启用。原因：操作会触发应用的事件处理函数，处理函数可能删除控件自身；
同步执行时调用栈上还有 UIA core 的帧和 provider 的 `this`，返回时会读已释放的内存。

同样的理由：**项 peer 只在 flush 里释放**，从不在客户端调用或控件失效回调里同步释放。
（例：TreeView 折叠会让控件失效，而折叠操作正由某个树项 peer 执行。）

### 5. UIA 操作要走用户路径

很多控件的程序 setter 故意不发事件（`SetSelectedIndex`、`SetExpanded`、`SetText` 是“恢复状态”用的）。
UIA 操作是用户通过辅助工具在操作，必须触发和点击、按键相同的事件，否则应用不响应：
讲述人选中导航项后页面不切换，展开 Expander 后懒加载内容不出现，表单填充工具改的值进不了数据模型。
所以控件为 peer 提供了 `Perform...` 系列公开方法（`ButtonBase::PerformActivate`、
`NavRail::PerformSelect`、`Expander::PerformSetExpanded`、`TextEditBase::ReplaceAllAsUserEdit`……）。
测试这类修复时要订阅应用能看到的事件，而不是读回存储的值：读回值在两种路径下都对。

### 6. 零开销的判定点在每次失效，而不是只在创建时

peer 在客户端离开后仍然存在（Inspect 遍历一次就关了）。如果只在创建 peer 时检查
`UiaClientsAreListening`，之后每次控件失效仍会排队、投递、读文本。所以每个排队函数都检查监听状态；
客户端离开后，项缓存在下一次变化时整体丢弃，之后行数再变化也不投递任何消息。
基准 `FluentUIBench --gate` 的 UiaNoClient / UiaListening 场景验证：没有客户端时 0 次投递、0 次调用。

### 7. 虚拟化列表只暴露视口附近的行

第一版把 5 万行的 DataGrid 的每一行都作为兄弟节点暴露。实测（真实 `IUIAutomation` 客户端，Release）：
沿 NextSibling 走一行约 0.45 ms，5 万行约 22 秒。Inspect 收到焦点事件后会列出焦点元素的全部兄弟节点，
所以点一下表格它就卡死了。peer 本身是按需创建的，这里慢的是跨线程的客户端调用次数。

改为只暴露可见行加上下各一页（最多 256 行）后，同样的遍历是 6 行、1 ms。窗口外的行仍可通过选中、焦点、
ScrollItem 到达；滚动使窗口移动时发 ChildrenInvalidated，客户端重新读取。WPF 和 WinUI 的虚拟化列表在
UIA 树里也是这样（只有已实现的项是子节点）。窗口外的行用 ItemContainer 模式（`FindItemByProperty`）
按名字或选中状态找到，再用该行的 VirtualizedItem 模式 `Realize()` 滚动进窗口：查找按下标逐个比对名字，
不为每行建节点；Realize 与 ScrollItem 一样投递执行。

### 8. popup 窗口

`PopupHost` 是独立的 `WS_POPUP` + `WS_EX_NOACTIVATE` 顶层窗口，不在所属窗口的 UIA 树里。
菜单（MenuFlyout）的每一级 popup 自己响应 `WM_GETOBJECT`，内容 peer 是它的子节点。
ComboBox 的下拉列表不这样做：它的项属于 ComboBox 的 peer，再在 popup 里列一次会出现两份。

键盘焦点留在所属窗口，所以 popup 的 host 把“宿主有 OS 焦点”代理到所属窗口，悬停/方向键选中的菜单项
才能报告 HasKeyboardFocus，并在变化时发 AutomationFocusChanged。

实测：MenuOpened 事件发出时 `UiaRaiseAutomationEvent` 返回 S_OK，Inspect 能看到菜单和它的变化；
但用 `AddAutomationEventHandler(MenuOpened, 桌面, Subtree)` 订阅的客户端（本进程和另一个进程的
PowerShell UIA 客户端都试过）没有收到。元素级订阅能收到同一个 popup 发出的 Invoked 事件。
原因未查明；Learn 提到 WS_POPUP 窗口应实现 Window 模式，popup 根目前没有实现，可能与此有关。

## 三、否决的方案

- **不用 UseComThreading、自己把调用转到 UI 线程**：见二.1，前提不成立。
- **在后台线程应答查询**：需要锁住整棵元素树，违背单线程模型。UI 线程卡住时客户端超时，
  这是所有 Win32 UIA provider 的固有行为。
- **复用 `HitTestDeep` 做 `ElementProviderFromPoint`**：`HitTestDeep` 是输入策略，禁用元素不命中、
  Panel 不命中。Inspect 需要指向禁用按钮也能看到它，所以 UIA 的命中测试在 peer 层单独实现。
- **把 popup 内容的 provider 嫁接为所属控件的子节点（跨 HWND 导航）**：popup 仍会被系统作为桌面
  顶层窗口枚举一次，同一元素出现两次，FragmentRoot 不一致。
- **让 popup 内容成为所属控件的 Visual 子节点**：会把 popup 内容的失效路由到主窗口，破坏 popup 自己的
  渲染合并。

## 四、已知限制

- TextPattern 的 `GetBoundingRectangles` 返回整个控件的矩形，不按行给出（放大镜不会跟着光标行移动）；
  不报告字体、颜色等格式属性。
- `IsOffscreen` 不和窗口客户区求交：窗口缩小后被挤到右边界外的元素仍报 `false`。
- InfoBar 只播报客户端访问过之后的文本变化，新出现的 InfoBar 不自动播报。
- DataGrid 暴露行和单元格（单元格 Name = 文本，HelpText = 列标题），但没有 Grid/Table 模式，
  客户端不能按 (行, 列) 直接取单元格，也没有列标题节点。
- TreeView 的行 id 必须唯一：UIA 节点按 id 作 key。
- `FindItemByProperty` 只支持 Name、SelectionItem.IsSelected 和“下一项”，其他属性返回 E_INVALIDARG；按名字查找是线性扫描（5 万行一次约几十毫秒，只在客户端主动查找时发生）。

## 五、自测方法

- 无头测试 `FluentUITests.exe Automation`：用记录调用的假 UIA core（`tests/unit/AutomationTestHelpers.h`），
  不需要窗口和 COM 运行时，覆盖生命周期、事件门控、合并、操作路径。
- 真实客户端冒烟 `FluentUITests.exe --uia-smoke`：同进程 MTA 线程上的 `IUIAutomation` 客户端操作真实窗口
  （需要 GPU）。客户端必须在独立线程：同一线程对自己的 HWND 做 UIA 客户端调用会死锁。
- 变异测试：故意改坏一处（例如把 `PerformSelect` 换回 `SetSelectedIndex`），确认有测试失败。
  多个缺陷之所以漏过，就是测试走的路径恰好被假实现绕开了。
