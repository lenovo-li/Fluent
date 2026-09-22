// ImageEventsTests.cpp — Image 的 ImageOpened / ImageFailed 事件测试
//
// 关键约束：Image::LoadImage() 需要 D2D 设备（Context().window->D2D().DC()），
// 未 attach 时直接 return，不会触发任何事件。headless 测试没有真实设备，
// 所以这里只能测事件的订阅/退订机制，触发路径需要真机验证。
//
// 真机验证要看的地方：demo 的 Image 页面（加载成功触发 ImageOpened，
// 把路径改成不存在的文件触发 ImageFailed）。

#include "../framework/Test.h"
#include "../../FluentUI/controls/Image.h"

using namespace fluent;

namespace {
struct EventRec {
    int openedCount = 0;
    int failedCount = 0;
    std::wstring lastPath;
    SizeDip lastSize{0.0f, 0.0f};

    static void OnOpened(void* owner, Image&, Image::ImageOpenedArgs& args) {
        auto* r = static_cast<EventRec*>(owner);
        r->openedCount++;
        r->lastPath = args.path;
        r->lastSize = args.size;
    }
    static void OnFailed(void* owner, Image&, Image::ImageFailedArgs& args) {
        auto* r = static_cast<EventRec*>(owner);
        r->failedCount++;
        r->lastPath = args.path;
    }
};
}  // namespace

// 两个事件都能订阅，返回的 Subscription 可以正常持有。
TEST(ImageEvents, BothEventsAreSubscribable) {
    Image img;
    EventRec rec;
    auto s1 = img.ImageOpened().Subscribe(&rec, &EventRec::OnOpened);
    auto s2 = img.ImageFailed().Subscribe(&rec, &EventRec::OnFailed);

    EXPECT_TRUE(img.ImageOpened().HasSubscribers());
    EXPECT_TRUE(img.ImageFailed().HasSubscribers());
}

// 多订阅者可以同时注册（Event<> 的多播能力）。
TEST(ImageEvents, MultipleSubscribersRegister) {
    Image img;
    EventRec a, b;
    auto s1 = img.ImageFailed().Subscribe(&a, &EventRec::OnFailed);
    auto s2 = img.ImageFailed().Subscribe(&b, &EventRec::OnFailed);

    EXPECT_TRUE(img.ImageFailed().HasSubscribers());
}

// Subscription 是 RAII：离开作用域即退订。
TEST(ImageEvents, SubscriptionUnregistersOnDestruction) {
    Image img;
    EventRec rec;
    {
        auto sub = img.ImageFailed().Subscribe(&rec, &EventRec::OnFailed);
        EXPECT_TRUE(img.ImageFailed().HasSubscribers());
    }
    EXPECT_FALSE(img.ImageFailed().HasSubscribers());
}

// 零订阅者时 HasSubscribers 为假，Image 可以据此跳过构造 args。
TEST(ImageEvents, NoSubscribersReportsFalse) {
    Image img;
    EXPECT_FALSE(img.ImageOpened().HasSubscribers());
    EXPECT_FALSE(img.ImageFailed().HasSubscribers());
}

// 未 attach 时设置源不崩溃，也不触发事件 —— LoadImage 在没有设备时提前返回。
// 这条锁定的是「SetSource 只记录路径，真正加载发生在 attach」这个契约。
TEST(ImageEvents, SetSourceWhileDetachedRaisesNothing) {
    Image img;
    EventRec rec;
    auto s1 = img.ImageOpened().Subscribe(&rec, &EventRec::OnOpened);
    auto s2 = img.ImageFailed().Subscribe(&rec, &EventRec::OnFailed);

    img.SetSource(L"C:\\nonexistent\\path\\image.png");

    EXPECT_EQ(rec.openedCount, 0);
    EXPECT_EQ(rec.failedCount, 0);
    EXPECT_FALSE(img.LoadAttempted());   // 还没试过加载
}

// 两个事件是独立的槽位，订阅一个不影响另一个。
TEST(ImageEvents, EventsAreIndependent) {
    Image img;
    EventRec rec;
    auto sub = img.ImageFailed().Subscribe(&rec, &EventRec::OnFailed);

    EXPECT_TRUE(img.ImageFailed().HasSubscribers());
    EXPECT_FALSE(img.ImageOpened().HasSubscribers());
}
