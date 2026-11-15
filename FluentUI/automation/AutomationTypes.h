// AutomationTypes.h — the framework's own names for UI Automation concepts.
//
// WHY NOT <UIAutomation.h>. Peers live in public headers that extension libraries
// include; pulling the UIA SDK headers into every control header would add compile
// cost and macro noise to the whole tree. These enums carry the documented UIA
// numeric values (uiautomationclient.h, UIA_*ControlTypeId / ToggleState), so the
// provider passes them through unchanged. The values are part of the UIA contract
// and never change; AutomationProviderTests checks a few against the SDK constants.
#pragma once

#include <cstdint>

namespace fluent::automation {

// UIA_*ControlTypeId values.
enum class ControlType : int32_t {
    Button = 50000,
    CheckBox = 50002,
    ComboBox = 50003,
    Edit = 50004,
    Hyperlink = 50005,
    Image = 50006,
    ListItem = 50007,
    List = 50008,
    Menu = 50009,
    MenuBar = 50010,
    MenuItem = 50011,
    ProgressBar = 50012,
    RadioButton = 50013,
    ScrollBar = 50014,
    Slider = 50015,
    Spinner = 50016,
    StatusBar = 50017,
    Tab = 50018,
    TabItem = 50019,
    Text = 50020,
    ToolBar = 50021,
    ToolTip = 50022,
    Tree = 50023,
    TreeItem = 50024,
    Custom = 50025,
    Group = 50026,
    Thumb = 50027,
    DataGrid = 50028,
    DataItem = 50029,
    Document = 50030,
    SplitButton = 50031,
    Window = 50032,
    Pane = 50033,
    Header = 50034,
    HeaderItem = 50035,
    Table = 50036,
    TitleBar = 50037,
    Separator = 50038,
};

// UIA ToggleState values.
enum class ToggleState : int32_t { Off = 0, On = 1, Indeterminate = 2 };

// UIA LiveSetting values: how a screen reader treats a LiveRegionChanged on this
// element. Polite waits for the current speech; Assertive interrupts it.
enum class LiveSetting : int32_t { Off = 0, Polite = 1, Assertive = 2 };

}  // namespace fluent::automation
