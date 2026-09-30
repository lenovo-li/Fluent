# 待补的控件文档

下列控件已发布，但 `docs/controls/` 下还没有对应文档（README 里标为「文档待写」）。
全部补完后删除本文件。已排除纯基类与辅助类型（`Panel`、`TextEditBase`、`ToggleVisuals`、
`ProgressSweepVisual`、`RichTextDocument`、`primitives/` 下的部件）。

数字为头文件行数，是工作量的粗略指标。

## 第一批：被其他文档引用或最常用

| 控件 | 行数 | 为什么优先 |
|---|---|---|
| `TreeView` | 246 | `ScrollViewer.md` 的「相关控件」提到它 |
| `ScrollPanel` | 268 | `StackPanel.md` 指引读者「外面套 ScrollPanel」，却无 API 记录 |
| `LogViewer` | 302 | 高吞吐日志控件（虚拟化 + 自动尾随） |
| `TabControl` | 268 | 常用导航控件 |
| `MenuBar` | 97 | 与 `MenuFlyout.md` 配套（MenuBar 用 `SetItemsBorrow`） |
| `ToolBar` | 108 | CommandBar 风格，溢出进 flyout |

## 第二批：对话框与常用输入

| 控件 | 行数 |
|---|---|
| `Expander` | 225 |
| `MessageDialog` | 51 |
| `ContentDialog` | 69 |
| `PasswordBox` | 33 |
| `Calendar` | 213 |
| `DatePicker` | 77 |

## 第三批：布局容器

| 控件 | 行数 |
|---|---|
| `GroupBox` | 137 |
| `Viewbox` | 118 |
| `Canvas` | 117 |
| `UniformGrid` | 71 |
| `WrapPanel` | 59 |
| `DockPanel` | 48 |

## 第四批：小控件

| 控件 | 行数 |
|---|---|
| `RepeatButton` | 68 |
| `Separator` | 45 |
| `StatusBar` | 41 |

## 写作要求

- 参照 `MenuFlyout.md` 的风格；最小示例照 `TEMPLATE.md` / `examples/Minimal/main.cpp` 的骨架
- 每个 API 签名从头文件复制，不凭印象写
- token 表必须 grep `.cpp` 确认真的读了该 token
- 脏标记等级查 `SetProperty(...)` 的第三个参数
- 示例代码要实际编译过
- 写完在 `README.md` 对应类别下把「文档待写」改成链接，并更新文档总数
