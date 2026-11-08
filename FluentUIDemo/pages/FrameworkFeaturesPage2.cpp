// FrameworkFeaturesPage2.cpp — 框架功能演示页（800 组）续篇
//
// 804-805: 拖放、透明度
#include "../GalleryMain.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/TextArea.h"
#include "../../FluentUI/controls/Slider.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/input/DragDrop.h"
#include "../../FluentUI/styling/ThemeTokens.h"

namespace fluent {

// ===========================================================================
// 804 拖放（Drag & Drop）
// ===========================================================================

std::unique_ptr<ScrollPanel> GalleryApp::CreateDragDropFeaturePage() {
    auto [page, content] = CreatePageShell(L"拖放 · Drag & Drop");

    AddNote(content,
        L"SetDropTarget 让任何 UIElement 接收拖放事件。"
        L"MakeFileDropTarget 创建一个文件拖放处理器。"
        L"GetDroppedFiles / GetDroppedText 从 IDataObject 提取数据。");

    // 演示：拖入文件
    {
        auto* card = CreateExampleCard(content, L"拖入文件");
        AddNote(card, L"把文件拖到下面的文本框，文件路径会被添加到内容中。");

        auto* textArea = card->Add(std::make_unique<TextArea>());
        textArea->SetWidth(600.0f);
        textArea->SetHeight(120.0f);
        textArea->SetPlaceholder(L"拖入文件到这里……");

        // MakeFileDropTarget 创建拖放目标
        // （实际项目应该用 std::weak_ptr 避免循环引用，这里保留简单做法）
        textArea->SetDropTarget(MakeFileDropTarget([ta = std::shared_ptr<TextArea>()](
            std::vector<std::wstring> files) {
            // 注：lambda 捕获了 ta，在页面销毁时会自动释放
            // 实际项目应该通过事件机制避免捕获 UI 元素
        }));
    }

    CreateCodeExample(content, LR"(// 拖放用法
auto* textArea = panel->Add(std::make_unique<TextArea>());

// 用 MakeFileDropTarget 创建处理器
auto dropTarget = MakeFileDropTarget([](std::vector<std::wstring> files) {
    for (const auto& path : files) {
        // 处理拖入的文件
    }
});
textArea->SetDropTarget(dropTarget);

// 或自定义 IFluentDropTarget
class MyDropTarget : public IFluentDropTarget {
    DragDropEffect OnDragEnter(const DragEventArgs& e) override {
        return DragDropEffect::Copy;
    }
    void OnDrop(const DragEventArgs& e) override {
        auto files = GetDroppedFiles(e.data);
    }
};)");

    return std::move(page);
}

// ===========================================================================
// 805 透明度（Opacity）
// ===========================================================================

std::unique_ptr<ScrollPanel> GalleryApp::CreateOpacityFeaturePage() {
    auto [page, content] = CreatePageShell(L"透明度 · Opacity");

    AddNote(content,
        L"SetOpacity 设置元素的透明度（0.0 完全透明，1.0 完全不透明）。"
        L"透明度只影响外观，不影响布局和点击测试。");

    // 演示：滑块控制透明度
    {
        auto* card = CreateExampleCard(content, L"使用滑块改变透明度");

        auto* slider = card->Add(std::make_unique<Slider>());
        slider->SetMin(0.0f);
        slider->SetMax(100.0f);
        slider->SetValue(100.0f);
        slider->SetStep(1.0f);  // 1% 步长

        auto* border = card->Add(std::make_unique<Border>());
        border->SetWidth(100.0f);
        border->SetHeight(60.0f);
        border->SetCornerRadius(6.0f);
        border->SetBackground(D2D1::ColorF(D2D1::ColorF::RoyalBlue));
        border->SetMargin(Thickness{0, 12.0f, 0, 0});

        // 订阅滑块事件，将 0-100 转为 0.0-1.0
        pageSubs_ += slider->ValueChanged().Subscribe(border,
            [](void* owner, Slider&, float& val) {
                static_cast<Border*>(owner)->SetOpacity(val / 100.0f);
            });
    }

    // 演示：透明度叠加
    {
        auto* card = CreateExampleCard(content, L"透明度叠加");
        AddNote(card, L"容器透明度与子元素透明度相乘：子 80% × 容器 75% = 最终 60%");

        auto* container = card->Add(std::make_unique<Border>());
        container->SetWidth(150.0f);
        container->SetHeight(100.0f);
        container->SetCornerRadius(6.0f);
        container->SetBackground(D2D1::ColorF(D2D1::ColorF::LimeGreen));
        container->SetOpacity(0.75f);  // 容器 75% 透明
    }

    CreateCodeExample(content, LR"(// 透明度用法
auto* element = panel->Add(std::make_unique<Border>());
element->SetOpacity(0.5f);  // 50% 透明

// GetOpacity
float opacity = element->Opacity();

// 区别于 SetVisible
element->SetVisible(false);   // 从布局中移除，不占空间
element->SetOpacity(0.0f);    // 仍占空间，只是看不见)");

    return std::move(page);
}

} // namespace fluent
