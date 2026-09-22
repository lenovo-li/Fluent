// MenuFlyoutEventsTests.cpp — MenuFlyout.Closed 事件（替代旧的 SetOnClosed 回调）
//
// 关键约束：MenuFlyout 的打开路径（ShowAt / ShowBelow）需要真实窗口 ——
// PopupHost::Create 要 HINSTANCE + 父 HWND。headless 下 levels_ 永远是空的，
// 而 Close() 的第一行就是 `if (levels_.empty()) return;`，所以事件的实际
// 触发无法在此验证，只能测订阅/退订机制和「未打开时 Close 不触发」这条契约。
//
// 真机验证要看的地方：demo 的 MenuFlyout 页面 —— 打开菜单后点击外部/按 Esc/
// 选中菜单项，三条关闭路径都应触发一次 Closed。

#include "../framework/Test.h"
#include "../../FluentUI/controls/MenuFlyout.h"
#include "../../FluentUI/input/RoutedEvent.h"

using namespace fluent;

namespace {
struct ClosedRec {
    int count = 0;
    static void On(void* owner, MenuFlyout&, RoutedEventArgs&) {
        static_cast<ClosedRec*>(owner)->count++;
    }
};
}  // namespace

// 未打开时 Close() 是 no-op：levels_ 为空就提前返回，事件不触发。
// 这条锁定的是「Closed 只在真的关掉一条已打开的菜单链时响」，没有它，
// 一个每帧调 Close() 的调用方会收到无穷多次通知。
TEST(MenuFlyoutEvents, CloseWhenNotOpenDoesNotRaise) {
    MenuFlyout flyout;
    ClosedRec rec;
    auto sub = flyout.Closed().Subscribe(&rec, &ClosedRec::On);

    flyout.Close();
    EXPECT_EQ(rec.count, 0);
}

// 多订阅者可以同时注册 —— 这正是从单槽位 SetOnClosed 迁移过来的动机：
// 框架和应用能同时监听，后注册的不会覆盖前一个。
TEST(MenuFlyoutEvents, MultipleSubscribersRegister) {
    MenuFlyout flyout;
    ClosedRec a, b;
    auto s1 = flyout.Closed().Subscribe(&a, &ClosedRec::On);
    auto s2 = flyout.Closed().Subscribe(&b, &ClosedRec::On);

    EXPECT_TRUE(flyout.Closed().HasSubscribers());
}

// Subscription 是 RAII：离开作用域即退订，不需要手动 SetOnClosed(nullptr)。
// 旧 API 忘记清空就会留下悬垂的 [this]，这是迁移的另一个动机。
TEST(MenuFlyoutEvents, SubscriptionUnregistersOnDestruction) {
    MenuFlyout flyout;
    ClosedRec rec;
    {
        auto sub = flyout.Closed().Subscribe(&rec, &ClosedRec::On);
        EXPECT_TRUE(flyout.Closed().HasSubscribers());
    }
    EXPECT_FALSE(flyout.Closed().HasSubscribers());
}

// 零订阅者时 HasSubscribers 为假，Close() 可以据此跳过构造 args。
TEST(MenuFlyoutEvents, NoSubscribersReportsFalse) {
    MenuFlyout flyout;
    EXPECT_FALSE(flyout.Closed().HasSubscribers());
}

// 未打开时 Close() 不崩溃（无论有无订阅者）。
TEST(MenuFlyoutEvents, CloseWithNoSubscribersDoesNotCrash) {
    MenuFlyout flyout;
    flyout.Close();
    EXPECT_TRUE(true);
}

// 旧的 SetOnClosed 仍可调用（标记了 deprecated 但保留实现），新旧可共存。
// Close() 的实现是「先 Raise 事件，再调旧回调」，两条路径都保留。
TEST(MenuFlyoutEvents, DeprecatedCallbackCoexistsWithEvent) {
    MenuFlyout flyout;
    ClosedRec rec;
    auto sub = flyout.Closed().Subscribe(&rec, &ClosedRec::On);

    bool legacyRegistered = false;
#pragma warning(push)
#pragma warning(disable : 4996)  // 故意调用 deprecated API，这就是被测内容
    flyout.SetOnClosed([&legacyRegistered] { legacyRegistered = true; });
#pragma warning(pop)

    // 两条路径都挂上了，互不影响（未打开所以都没触发）
    EXPECT_TRUE(flyout.Closed().HasSubscribers());
    EXPECT_FALSE(legacyRegistered);
}
