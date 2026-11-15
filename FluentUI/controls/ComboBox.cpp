// ComboBox.cpp

#include "ComboBox.h"
#include "../window/WindowServices.h"
#include "../services/PopupHost.h"
#include "../services/PopupGeometry.h"
#include "../styling/ThemeTokens.h"
#include "../styling/FocusVisual.h"
#include "../core/ScrollMath.h"
#include "../graphics/ResourceCache.h"
#include "../layout/ScrollViewer.h"
#include <algorithm>

namespace fluent {

namespace {
const char* kTag = "ComboBox";
constexpr float kCornerRadius = 4.0f;  // DIP
constexpr float kChevronSize = 8.0f;   // DIP, chevron glyph width
constexpr float kScrollGutter = 16.0f; // DIP, right gutter so items clear the scrollbar

// The header's focus ring: one spec shared by the paint (Render) and the dirty rect
// (CollectDirtyBounds), so the reported region can never drift from what is drawn.
FocusRingSpec HeaderFocusRing() {
    FocusRingSpec spec;              // inset 2 / stroke 1.5 are the defaults
    spec.cornerRadius = kCornerRadius;
    return spec;
}

// Private list view element for the popup content. Renders items with hover
// and selected states, handles pointer and keyboard navigation, and fires a
// callback when an item is clicked or Enter is pressed.
//
// Now uses ScrollViewer for the scrollbar (WinUI-style auto-fade overlay), replacing
// the invisible manual scroll that had no visual affordance.
class ComboListView : public Control {
public:
    ComboListView() { 
        SetClickable(true);
        scroll_.SetKeepVisibleWhenOverflow(true);  // Auto mode: bar visible when needed
    }
    
    // Scroll to the selected item immediately (no animation) and wake the scrollbar.
    // Called when the dropdown opens so the user sees their current selection, not
    // a random position in the list.
    void ScrollToSelection() {
        if (selectedIndex_ < 0 || selectedIndex_ >= Count()) return;
        // Must update scroll content height FIRST, or EnsureVisible is a no-op.
        // PopupHost calls SetContent before Open, so Measure has already run and
        // bounds_ is valid. The list height is items.size() * itemH.
        float itemH = 32.0f;
        scroll_.SetContentHeight(Count() * itemH);
        EnsureVisible(selectedIndex_);
        scroll_.Wake();  // fade the scrollbar in immediately
        // Force immediate repaint so the first frame shows the correct scroll position.
        // Without this, the first render uses offset=0, then a second render corrects it
        // after the mouse moves over the popup (triggering OnPointerMoved -> Invalidate).
        Invalidate();
    }
    // Bind to the ComboBox's shared items vector (not owned) — eliminates the
    // old double storage (a second items copy in the list view). The pointer
    // stays valid for the ComboBox's life
    // (the list view is owned by, and destroyed with, the ComboBox).
    void SetItemsRef(const std::vector<std::wstring>* items) { 
        items_ = items; 
        UpdateScrollExtent();
        Invalidate(); 
    }
    void SetSelectedIndex(int idx) { selectedIndex_ = idx; Invalidate(); }
    void SetOnSelect(std::function<void(int)> cb) { onSelect_ = std::move(cb); }

    // Number of bound items (0 when unbound).
    int Count() const { return items_ ? static_cast<int>(items_->size()) : 0; }

    // Measure to fit all items (capped at a max height for scrolling).
    void Measure(float availW, float availH) override {
        UNREFERENCED_PARAMETER(availH);
        float itemH = 32.0f;
        float maxH = itemH * maxVisibleItems_;
        float w = IsAuto(width_) ? std::min(availW, 300.0f) : width_;
        float h = std::min(itemH * Count(), maxH);
        SetDesired({w, h});
    }
    
    void OnBoundsChanged() override {
        scroll_.SetBounds(bounds_);
        UpdateScrollExtent();
    }
    
    void OnAttachedToTree() override {
        // ScrollViewer needs the theme for scrollbar colors (same as ListBox).
        scroll_.AttachToContext(Context());
    }
    
    void OnDetachedFromTree() override {
        scroll_.DetachFromContext();
    }

    void OnKeyDownRouted(KeyEventArgs& e) override {
        if (Count() == 0) return;
        int idx = hoveredIndex_ >= 0 ? hoveredIndex_ : selectedIndex_;
        if (idx < 0) idx = 0;

        switch (e.vk) {
            case VK_UP:
                hoveredIndex_ = std::max(0, idx - 1);
                EnsureVisible(hoveredIndex_);
                Invalidate();
                e.handled = true; break;
            case VK_DOWN:
                hoveredIndex_ = std::min(Count() - 1, idx + 1);
                EnsureVisible(hoveredIndex_);
                Invalidate();
                e.handled = true; break;
            case VK_HOME:
                hoveredIndex_ = 0;
                EnsureVisible(hoveredIndex_);
                Invalidate();
                e.handled = true; break;
            case VK_END:
                hoveredIndex_ = Count() - 1;
                EnsureVisible(hoveredIndex_);
                Invalidate();
                e.handled = true; break;
            case VK_RETURN:
            case VK_SPACE:
                if (hoveredIndex_ >= 0 && hoveredIndex_ < Count()) {
                    if (onSelect_) onSelect_(hoveredIndex_);
                }
                e.handled = true; break;
            default:
                break;
        }
    }

    void OnPointerWheelChanged(PointerEventArgs& e) override {
        if (scroll_.MaxOffset() <= 0.0f) return;  // nothing to scroll
        float lines = e.WheelScrollLines();
        // Smooth scroll now that PopupHost shares the parent window's AnimationRegistry.
        scroll_.AnimateBy(lines * 32.0f);  // 32.0f = itemH
        scroll_.Wake();
        Invalidate();
        e.handled = true;
    }
    
    // Animation tick for smooth scrolling + scrollbar fade/expand (same as ScrollPanel).
    bool WantsAnimationTick() const override { return scroll_.NeedsTick(); }
    void OnAnimationTick(float dtSec) override {
        scroll_.Tick(dtSec);
        
        // Update hover during smooth scrolling: the content moves under the pointer,
        // which didn't move. Without this, the old hoveredIndex_ points to an item that
        // scrolled away, leaving the actual item under the pointer without highlight.
        if (pointerInside_) {
            int newHover = HitItem(lastPointerPos_.y);
            if (newHover != hoveredIndex_) {
                hoveredIndex_ = newHover;
            }
        }
        
        // scroll_ is a member, not a tree node: its Invalidate() has no callback,
        // so the scrollbar's animations never reach the frame scheduler. Report the
        // repaint from here where it does reach the tree (same issue as ScrollPanel).
        Invalidate();
    }

    void Render(const DrawingContext& dc) override {
        if (!Dwrite() || !items_ || items_->empty()) return;
        const auto& items = *items_;
        const ColorTokens& pal = Theme().colors;

        IDWriteTextFormat* fmt = Dwrite()->Format(
            13.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
            DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP);
        if (!fmt) return;

        ClipGuard clip = dc.PushClip(
            D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()));

        float itemH = 32.0f;
        float scrollOffset = scroll_.Offset();
        int first = static_cast<int>(scrollOffset / itemH);
        int visible = static_cast<int>(bounds_.h / itemH) + 2;
        float y = bounds_.y - (scrollOffset - first * itemH);

        for (int i = first; i < static_cast<int>(items.size()) && i < first + visible; ++i) {
            // Reserve kScrollGutter on the right for the scrollbar, so items don't sit
            // directly against the rail (easy mis-click). Left pad 4 + right gutter 16.
            D2D1_RECT_F itemRect = D2D1::RectF(bounds_.x + 4.0f, y, 
                                               bounds_.right() - kScrollGutter, y + itemH);

            if (i == hoveredIndex_) {
                dc.FillRoundedRect(D2D1::RoundedRect(itemRect, Theme().spacing.cornerRadiusSmall, Theme().spacing.cornerRadiusSmall),
                                   D2D1::ColorF(pal.accent.r, pal.accent.g, pal.accent.b, 0.15f));
            } else if (i == selectedIndex_) {
                dc.FillRoundedRect(D2D1::RoundedRect(itemRect, Theme().spacing.cornerRadiusSmall, Theme().spacing.cornerRadiusSmall),
                                   D2D1::ColorF(pal.accent.r, pal.accent.g, pal.accent.b, 0.08f));
            }

            D2D1_RECT_F textRect = D2D1::RectF(itemRect.left + 8.0f, itemRect.top,
                                               itemRect.right - 8.0f, itemRect.bottom);
            D2D1_COLOR_F textColor = EffectiveForeground(pal.textPrimary);
            dc.DrawText(items[i].c_str(), static_cast<UINT32>(items[i].size()), fmt,
                        textRect, textColor, D2D1_DRAW_TEXT_OPTIONS_CLIP);

            if (i == selectedIndex_) {
                dc.FillRoundedRect(
                    D2D1::RoundedRect(D2D1::RectF(itemRect.left + 2.0f, itemRect.top + 8.0f,
                                                  itemRect.left + 4.0f, itemRect.bottom - 8.0f),
                                      1.0f, 1.0f),
                    pal.accent);
            }
            y += itemH;
        }
        
        // Draw the scrollbar overlay (ScrollViewer renders its own rail).
        scroll_.Render(dc);
    }

    void OnPointerMoved(PointerEventArgs& e) override {
        float dipX = e.position.x, dipY = e.position.y;
        lastPointerPos_ = e.position;
        pointerInside_ = true;
        
        // Scrollbar drag: track the thumb.
        if (scroll_.IsDragging()) {
            scroll_.DragTo(dipY);
            Invalidate();
            e.handled = true;
            return;
        }
        
        // Scrollbar hover: wake + update hover state.
        bool overBar = scroll_.HitBarRegion(dipX, dipY);
        scroll_.Wake();
        scroll_.SetBarHover(overBar);
        // Expand/fade animate via OnAnimationTick (parent window's clock).
        
        // Item hover: only when NOT over the scrollbar.
        if (!overBar) {
            int idx = HitItem(dipY);
            if (idx != hoveredIndex_) { hoveredIndex_ = idx; Invalidate(); }
        } else if (hoveredIndex_ >= 0) {
            // Clear item hover when entering the scrollbar region.
            hoveredIndex_ = -1;
            Invalidate();
        }
    }
    
    void OnPointerLeft() override {
        pointerInside_ = false;
        // Clear hover states when pointer leaves the popup entirely.
        scroll_.SetBarHover(false);  // collapse animates via OnAnimationTick
        if (hoveredIndex_ >= 0) {
            hoveredIndex_ = -1;
            Invalidate();
        }
    }
    
    void OnPointerPressed(PointerEventArgs& e) override {
        if (e.button != PointerButton::Left) return;
        float dipX = e.position.x, dipY = e.position.y;
        
        // Scrollbar: click-to-jump + drag (same as ListBox).
        if (scroll_.HitBarRegion(dipX, dipY)) {
            if (!scroll_.HitThumb(dipX, dipY)) {
                const RectDip thumb = scroll_.ThumbRect();
                const float trackRange = std::max(1.0f, bounds_.h - thumb.h);
                const float wantTop = (dipY - thumb.h * 0.5f) - bounds_.y;
                scroll_.SetOffset(wantTop / trackRange * scroll_.MaxOffset());
            }
            scroll_.BeginDrag(dipY);
            if (Context().input)
                Context().input->CapturePointer(this);
            Invalidate();
            e.handled = true;
            return;
        }
        
        // NOT on scrollbar: let the base handle click gesture (capture + set pointerDown_).
        // The base sets e.handled, which is correct — we ARE handling the press.
        Control::OnPointerPressed(e);
    }
    
    void OnPointerReleased(PointerEventArgs& e) override {
        if (e.button != PointerButton::Left) return;
        if (scroll_.IsDragging()) {
            scroll_.EndDrag();
            Invalidate();
        }
        if (Context().input && Context().input->Captured() == this)
            Context().input->ReleaseCapture(this);
        
        // Let the base handle the release->click logic. It checks pointerDown_ (set by
        // the base's OnPointerPressed), tests inside bounds, and calls OnClickRouted.
        Control::OnPointerReleased(e);
    }

    void OnClickRouted(PointerEventArgs& e) override {
        int idx = HitItem(e.position.y);
        if (idx >= 0 && idx < Count() && onSelect_) onSelect_(idx);
    }

private:
    int HitItem(float dipY) const {
        float itemH = 32.0f;
        float y = dipY - bounds_.y + scroll_.Offset();
        int idx = static_cast<int>(y / itemH);
        return (idx >= 0 && idx < Count()) ? idx : -1;
    }

    void EnsureVisible(int idx) {
        if (idx < 0 || idx >= Count()) return;
        float itemH = 32.0f;
        float itemTop = idx * itemH;
        float itemBot = itemTop + itemH;
        float viewTop = scroll_.Offset();
        float viewBot = viewTop + bounds_.h;
        if (itemTop < viewTop)
            scroll_.SetOffset(itemTop);
        else if (itemBot > viewBot)
            scroll_.SetOffset(itemBot - bounds_.h);
    }
    
    void UpdateScrollExtent() {
        float itemH = 32.0f;
        scroll_.SetContentHeight(itemH * Count());
    }

    const std::vector<std::wstring>* items_ = nullptr;  // non-owning; points to Selector::items_
    int selectedIndex_ = -1;
    int hoveredIndex_ = -1;
    ScrollViewer scroll_;
    int maxVisibleItems_ = 8;  // default cap; exposed via SetMaxDropDownItems if needed
    std::function<void(int)> onSelect_;
    // Last known pointer position and whether it's inside the dropdown, for updating
    // hover during smooth scrolling (the content moves under a stationary pointer).
    Point lastPointerPos_;
    bool pointerInside_ = false;
};

} // namespace

// ---------------------------------------------------------------------------
// ComboBox implementation
// ---------------------------------------------------------------------------

ComboBox::ComboBox() {
    SetFocusable(true);
    SetClickable(true);  // header toggles the dropdown via the base click gesture
}

ComboBox::~ComboBox() {
    // Close the dropdown and unregister the window's dismiss hook before the
    // members are torn down, so the window can never invoke a [this] callback
    // into a half-destroyed ComboBox. Idempotent; safe if already closed.
    ClosePopup();
}

void ComboBox::OnAttachedToTree() {
    // The framework injects the host on tree attach, replacing the
    // old manual Attach(WindowServices*). Cache the window for the attach period
    // (popup positioning + dismiss registration read it); it is cleared on detach.
    window_ = Context().window;
    if (!window_) return;

    // Re-attach to the window the popup was built for (a TabControl page switched
    // back, a panel re-parented within the window): the kept popup and list are
    // still valid — same owner HWND, same D2D/DWrite, same theme and caches — so
    // there is nothing to rebuild. See OnDetachedFromTree for why they are kept.
    if (popup_ && popupWindow_ == window_) return;

    // First attach, or a move to a DIFFERENT window: build a popup owned by this
    // one. A kept list is still attached to the old popup's context, whose
    // InputManager lives inside that popup — detach it while the old popup still
    // exists (DetachFromContext notifies that InputManager), then drop the popup.
    // This path is not reachable from inside the old popup's dispatch in practice:
    // that would take a handler moving the control into another window synchronously.
    if (listView_ && listView_->IsAttached()) listView_->DetachFromContext();
    popup_.reset();
    popupWindow_ = nullptr;

    // Create the popup host (hidden, reused on each open).
    popup_ = std::make_unique<PopupHost>();
    if (FAILED(popup_->Create(window_->Instance(), window_->Hwnd(),
                              &window_->D2D(), &window_->DWrite()))) {
        TraceMsg(kTag, "OnAttachedToTree: PopupHost::Create failed");
        popup_.reset();
        return;
    }
    popupWindow_ = window_;
    // Hook the dropdown content onto the parent window's animation clock (smooth
    // wheel scroll, scrollbar fade/expand). Must precede SetContent below, which
    // injects the registry into the list view's context.
    popup_->SetParentAnimationClock(window_, Context().animations);
    // Share the tree's resource caches with the dropdown content.
    popup_->SetResourceCache(Context().resourceCache);
    // Apply any opacity set before attach (see SetPopupOpacity).
    popup_->SetCardOpacity(popupOpacity_);
    // Share the tree's stable theme snapshot so the dropdown + its rows theme
    // identically to the parent.
    popup_->SetTheme(Context().theme);

    // Create the list view content (once; a move to another window reuses it). It
    // reads DWrite through the context PopupHost injects on SetContent. The list
    // binds to the shared base items_ vector (no copy — the list view does not
    // mirror the items).
    if (!listView_) {
        auto* list = new ComboListView();
        list->SetItemsRef(&items_);
        list->SetOnSelect([this](int idx) { OnItemSelected(idx); });
        listView_.reset(list);
    }
    if (auto* list = dynamic_cast<ComboListView*>(listView_.get()))
        list->SetSelectedIndex(selectedIndex_);
    popup_->SetContent(listView_.get());
    // Every close path, including the popup's own light dismiss, funnels here.
    popup_->SetOnClose([this]() { OnPopupClosed(); });
}

void ComboBox::OnDetachedFromTree() {
    // Close the dropdown and release its window registrations while the host is
    // still valid, then drop the cached window pointer (the context is about to
    // clear).
    ClosePopup();
    // popup_ and listView_ are deliberately KEPT, not reset. This detach can run
    // synchronously from inside the popup's own dispatch: a click on a list row ->
    // ComboListView's onSelect -> SetSelectedIndex -> SelectionChanged -> an app
    // handler that switches the TabControl page (or removes this control) ->
    // DetachFromContext -> here, all before the popup's WndProc and the list's
    // pointer handler have returned. Resetting them here freed the PopupHost (and
    // the InputManager mid-PointerPressed) and the list under their own frames.
    // Kept objects
    // cost one hidden HWND; OnAttachedToTree reuses them on a re-attach to this
    // window, and ~ComboBox destroys them.
    window_ = nullptr;
}

void ComboBox::OnItemsChanged() {
    // ItemsControl hook: items_ was replaced. Re-clamp the selection to the new
    // range and refresh the (already items_-bound) list view's selected index.
    selectedIndex_ = ClampIndex(selectedIndex_);
    if (auto* list = dynamic_cast<ComboListView*>(listView_.get()))
        list->SetSelectedIndex(selectedIndex_);
    Invalidate();
}

void ComboBox::OnBoundsChanged() {
    // If the popup is open and our bounds changed (e.g. parent ScrollPanel scrolled,
    // moving our header), close the popup. The alternative — repositioning it every
    // frame — is complex and feels janky. WinUI/WPF close on scroll; we do the same.
    // Arrange returns early on an unchanged rect, so this only fires on a real move.
    if (popupOpen_) {
        ClosePopup();
    }
}

void ComboBox::SetSelectedIndex(int index) {
    // Reuse the base clamp/dedup + OnSelectionChanged hook, then sync the list view.
    Selector<std::wstring>::SetSelectedIndex(index);
    if (auto* list = dynamic_cast<ComboListView*>(listView_.get()))
        list->SetSelectedIndex(selectedIndex_);
}

void ComboBox::OnSelectionChanged(int /*oldIdx*/, int newIdx) {
    // In editable mode the header draws text_, not SelectedItem(), so a selection change
    // has to write the chosen item into the field or the control looks inert: picking
    // from the dropdown moved selectedIndex_ while the box kept showing the typed text.
    //
    // Only on a REAL item. Clearing the selection (-1) must not wipe a partially typed
    // entry -- the user is mid-edit and has selected nothing, which is not the same as
    // having selected an empty string.
    //
    // The caret goes to the end so typing continues naturally after a pick, matching what
    // SetEditable(true) does when it seeds the field from an existing selection.
    if (editable_) {
        if (const std::wstring* sel = SelectedItem()) {
            text_ = *sel;
            caret_ = static_cast<UINT32>(text_.size());
        }
    }
    // Selector hook: fires on any real selection change (programmatic or user).
    selectionChanged_.Raise(*this, newIdx);
}

void ComboBox::SetPopupOpacity(float opacity) {
    // Store first, then push if the popup already exists. Storing unconditionally is
    // what makes a pre-attach call work; without it the setting was dropped on the
    // floor whenever a page configured the control before adding it to the tree.
    popupOpacity_ = std::clamp(opacity, 0.0f, 1.0f);
    if (popup_) popup_->SetCardOpacity(popupOpacity_);
}

void ComboBox::Measure(float availW, float availH) {
    UNREFERENCED_PARAMETER(availH);
    // Measure like a button: explicit size or a reasonable default.
    SetDesired({IsAuto(width_) ? std::min(availW, 200.0f) : width_,
                     IsAuto(height_) ? 32.0f : height_});
}

// ---------------------------------------------------------------------------
// Editable mode
// ---------------------------------------------------------------------------

const std::wstring ComboBox::emptyString_;

void ComboBox::SetEditable(bool editable) {
    if (editable_ == editable) return;
    editable_ = editable;
    if (editable_) {
        // Seed the field from the current selection so turning editing on does not
        // blank a combo that already had a value.
        if (const std::wstring* sel = SelectedItem()) text_ = *sel;
        caret_ = static_cast<UINT32>(text_.size());
    } else {
        // Leaving editable mode drops the typed text: the value is once again
        // whatever the selection says, and keeping a stale field would make Text()
        // disagree with what is drawn.
        text_.clear();
        caret_ = 0;
    }
    caretVisible_ = false;
    InvalidateDirty(DirtyFlags::Render);
}

std::wstring ComboBox::Text() const { return HeaderText(); }

const std::wstring& ComboBox::HeaderText() const {
    if (editable_) return text_;
    if (const std::wstring* sel = SelectedItem()) return *sel;
    return emptyString_;
}

void ComboBox::SetText(std::wstring text) {
    if (!editable_) return;
    if (text_ == text) return;
    text_ = std::move(text);
    caret_ = static_cast<UINT32>(text_.size());
    // Programmatic, so no TextChanged — that event means "the user typed".
    InvalidateDirty(DirtyFlags::Render);
}

void ComboBox::ReplaceTextAsUserEdit(const std::wstring& text) {
    if (!editable_ || text_ == text) return;
    text_ = text;
    caret_ = static_cast<UINT32>(text_.size());
    caretVisible_ = true;
    InvalidateDirty(DirtyFlags::Render);
    std::wstring snapshot = text_;
    textChanged_.Raise(*this, snapshot);
}

void ComboBox::ClampCaret() {
    caret_ = std::min<UINT32>(caret_, static_cast<UINT32>(text_.size()));
}

void ComboBox::InsertEditText(const std::wstring& s) {
    if (s.empty()) return;
    ClampCaret();
    text_.insert(caret_, s);
    caret_ += static_cast<UINT32>(s.size());
    caretVisible_ = true;
    InvalidateDirty(DirtyFlags::Render);
    std::wstring snapshot = text_;
    textChanged_.Raise(*this, snapshot);
}

void ComboBox::OnTextInput(wchar_t ch) {
    if (!editable_) return;
    // Reject control characters; Backspace/Delete/Enter arrive as key-downs.
    if (ch < 0x20 || ch == 0x7F) return;
    InsertEditText(std::wstring(1, ch));
}

bool ComboBox::HandleEditKey(UINT vk) {
    if (!editable_) return false;
    ClampCaret();
    switch (vk) {
    case VK_BACK:
        if (caret_ == 0) return true;  // consumed, nothing to erase
        text_.erase(caret_ - 1, 1);
        --caret_;
        caretVisible_ = true;
        InvalidateDirty(DirtyFlags::Render);
        {
            std::wstring snapshot = text_;
            textChanged_.Raise(*this, snapshot);
        }
        return true;
    case VK_DELETE:
        if (caret_ >= text_.size()) return true;
        text_.erase(caret_, 1);
        caretVisible_ = true;
        InvalidateDirty(DirtyFlags::Render);
        {
            std::wstring snapshot = text_;
            textChanged_.Raise(*this, snapshot);
        }
        return true;
    case VK_LEFT:
        if (caret_ > 0) --caret_;
        caretVisible_ = true;
        InvalidateDirty(DirtyFlags::Render);
        return true;
    case VK_RIGHT:
        // Right at the end of the text is NOT consumed in a way that reaches the
        // dropdown: swallow it so a caret move never doubles as "open the list".
        if (caret_ < text_.size()) ++caret_;
        caretVisible_ = true;
        InvalidateDirty(DirtyFlags::Render);
        return true;
    case VK_HOME:
        caret_ = 0;
        caretVisible_ = true;
        InvalidateDirty(DirtyFlags::Render);
        return true;
    case VK_END:
        caret_ = static_cast<UINT32>(text_.size());
        caretVisible_ = true;
        InvalidateDirty(DirtyFlags::Render);
        return true;
    default:
        return false;
    }
}

void ComboBox::OnBlink() {
    if (!editable_ || !IsFocused()) {
        if (caretVisible_) { caretVisible_ = false; InvalidateDirty(DirtyFlags::Render); }
        return;
    }
    caretVisible_ = !caretVisible_;
    InvalidateDirty(DirtyFlags::Render);
}

HCURSOR ComboBox::Cursor() const {
    if (!editable_) return nullptr;  // window default (arrow)
    static HCURSOR ibeam = LoadCursor(nullptr, IDC_IBEAM);
    return ibeam;
}

RECT ComboBox::HeaderScreenRect() const {
    // bounds_ is in CLIENT DIPs, so the origin must be the client area's screen
    // position — not GetWindowRect. NativeWindowHost's WM_NCCALCSIZE keeps a resize
    // band on the left/right/bottom (ResizeGripOuterPx, 8 DIP), so the window rect's
    // left edge sits one band to the left of the client origin; using it shifted the
    // dropdown 8 DIP left of the header. ClientToScreen reads the real client origin
    // and stays correct if the NC layout changes (or is 0 when maximized).
    POINT origin = {0, 0};
    ClientToScreen(window_->Hwnd(), &origin);
    return AnchorScreenRect(origin.x, origin.y,
                            bounds_.x, bounds_.y, bounds_.w, bounds_.h,
                            window_->DpiScale());
}

bool ComboBox::HeaderContainsScreenPoint(int screenX, int screenY) const {
    if (!window_) return false;
    RECT r = HeaderScreenRect();
    POINT pt = {screenX, screenY};
    return PtInRect(&r, pt) != FALSE;
}

void ComboBox::OpenPopup() {
    if (!popup_ || !window_ || popupOpen_) return;

    // Compute the anchor rect in screen coordinates (header bounds).
    RECT anchor = HeaderScreenRect();

    // Popup width matches the header; height fits the list (capped by Measure).
    float popupW = bounds_.w;
    float itemH = 32.0f;
    float maxH = itemH * 8.0f;
    float popupH = std::min(itemH * items_.size(), maxH) + 16.0f;  // +padding

    if (SUCCEEDED(popup_->Open(anchor, popupW, popupH))) {
        popupOpen_ = true;
        
        // Scroll to the selected item and wake the scrollbar so the user sees
        // their current selection, not a random position in the list.
        if (auto* list = dynamic_cast<ComboListView*>(listView_.get()))
            list->ScrollToSelection();
        
        if (window_) {
            dismissSub_ = window_->RegisterActivePopupDismiss([this](PopupDismissReason reason, HWND otherHwnd, int screenX, int screenY) {
                // For deactivate, check if the other HWND is our popup. If so, ignore.
                if (reason == PopupDismissReason::Deactivate) {
                    if (popup_ && otherHwnd == popup_->Hwnd())
                        return false;  // keep popup open
                }
                // For click dismiss, keep the popup open when the click lands
                // inside the popup window (the list handles it) OR on our own
                // header (so the header's OnClick can toggle the popup closed
                // instead of this callback closing it and OnClick reopening it
                // on the same click).
                if (reason == PopupDismissReason::Click) {
                    if (popup_ && popup_->ContainsScreenPoint(screenX, screenY))
                        return false;  // keep popup open
                    if (window_ && HeaderContainsScreenPoint(screenX, screenY))
                        return false;  // let the header toggle handle it
                }
                // For Move/Resize: close the popup (anchor moved, popup would detach).
                // For other clicks outside: dismiss normally.
                ClosePopup();
                return true;  // dismissed
            });
            
            // Register wheel forwarding: check if the wheel landed inside the popup,
            // and if so route it to the popup's content. This is what makes the
            // dropdown scrollable via mouse wheel despite WS_EX_NOACTIVATE.
            wheelSub_ = window_->RegisterActivePopupWheelHandler([this](int screenX, int screenY, int delta) {
                if (!popup_ || !popupOpen_) return false;
                if (!popup_->ContainsScreenPoint(screenX, screenY)) return false;
                // Convert screen coords to client coords inside the popup, then route.
                POINT pt = {screenX, screenY};
                ScreenToClient(popup_->Hwnd(), &pt);
                popup_->RouteMouseWheel(pt.x, pt.y, delta);
                return true;  // consumed
            });
        }
        // Raised only on the success path: a failed Open left no dropdown, and
        // reporting one would have a deferred-loading handler fill a list nobody
        // can see.
        RoutedEventArgs args;
        args.source = this;
        args.originalSource = this;
        dropDownOpened_.Raise(*this, args);
    } else {
        TraceMsg(kTag, "OpenPopup: PopupHost::Open failed");
    }
}

void ComboBox::ClosePopup() {
    if (!popup_ || !popupOpen_) return;
    // A popup in the steady Open state closes through PopupHost::Close, whose
    // SetOnClose callback (OnPopupClosed) does the bookkeeping and raises
    // DropDownClosed as the last thing Close() does, with the popup already
    // Closed. Return straight after it and touch nothing: the handler may have
    // reopened the dropdown (popupOpen_ is true again and must stay so). It must not
    // destroy this ComboBox synchronously (see PopupState.h), but returning at once
    // keeps this frame from adding to that hazard.
    if (popup_->IsOpen()) {
        popup_->Close();
        return;
    }
    // Close() would be a no-op (the popup is not in the steady Open state, so no
    // callback would run): the control must still end up closed rather than stuck
    // with popupOpen_ set.
    OnPopupClosed();
}

void ComboBox::OnPopupClosed() {
    if (!popupOpen_) return;  // already done (ClosePopup's fallback call)
    popupOpen_ = false;
    dismissSub_.reset();  // unregister the window's dismiss hook
    wheelSub_.reset();    // unregister the window's wheel-forwarding hook
    Invalidate();

    RoutedEventArgs args;
    args.source = this;
    args.originalSource = this;
    dropDownClosed_.Raise(*this, args);
}

void ComboBox::OnItemSelected(int index) {
    // SetSelectedIndex raises SelectionChanged via the Selector hook when the
    // index actually changes; do not raise again here.
    SetSelectedIndex(index);
    ClosePopup();
}

void ComboBox::OnKeyDownRouted(KeyEventArgs& e) {
    UINT vk = e.vk;

    // Editable-mode editing keys take precedence over dropdown navigation so
    // Left/Right/Home/End never open the popup when a caret is moving.
    if (editable_ && HandleEditKey(vk)) {
        e.handled = true;
        return;
    }

    // In editable mode Space is a character being typed, never a dropdown command.
    // Handling the key-down does not stop the WM_CHAR (the host delivers TextInput
    // unconditionally), so treating Space as "open" popped the list on every word
    // break of "new york", and with the list open, forwarding it made ComboListView
    // pick the hovered row and overwrite the typed text with it. Left unhandled so
    // the space is simply inserted by OnTextInput.
    if (editable_ && vk == VK_SPACE) return;

    // Forward navigation keys to the popup if it's open.
    if (popupOpen_ && popup_) {
        if (vk == VK_ESCAPE) {
            ClosePopup();
            e.handled = true;
            return;
        }
        if (popup_->ForwardKey(vk)) {
            e.handled = true;
            return;
        }
    }

    // Open the popup with Space/Enter/Down/Alt+Down. Editable mode opens on Down
    // (and Alt+Down) only: Space is text (above), and Enter in a text field means
    // "done with this value", so it is left unhandled for the surrounding UI (a
    // dialog's default button) instead of popping the list over what was typed.
    // With the list OPEN, Enter still reaches the forward above and picks the
    // highlighted row, which is the classic combo behavior.
    if (!popupOpen_) {
        bool altDown = (GetKeyState(VK_MENU) & 0x8000) != 0;
        const bool opensList =
            editable_ ? (vk == VK_DOWN)
                      : (vk == VK_SPACE || vk == VK_RETURN || vk == VK_DOWN);
        if (opensList || (vk == VK_DOWN && altDown)) {
            OpenPopup();
            e.handled = true;
            return;
        }
    }
}

void ComboBox::OnClickRouted(PointerEventArgs&) {
    if (popupOpen_)
        ClosePopup();
    else
        OpenPopup();
}

void ComboBox::OnStateChanged() {
    Invalidate();
}

void ComboBox::Render(const DrawingContext& dc) {
    const ColorTokens& pal = Theme().colors;

    const float corner = EffectiveCornerRadius(kCornerRadius);
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
        D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()),
        corner, corner);

    // Fill: state-driven for hover/press feedback.
    D2D1_COLOR_F fill;
    switch (State()) {
        case VisualState::Hover:   fill = pal.controlFillHover; break;
        case VisualState::Pressed: fill = pal.controlFillPressed; break;
        default:                   fill = pal.controlFillDefault; break;
    }

    dc.FillRoundedRect(rr, EffectiveBackground(fill));

    // Border.
    dc.DrawRoundedRect(rr, EffectiveBorderBrush(pal.controlStrokeDefault),
                       EffectiveBorderThickness(1.0f));

    // Header text: either typed text (editable) or selected item (readonly).
    // The same format and layout is used in both modes, only the source differs.
    if (Dwrite()) {
        const std::wstring& displayText = HeaderText();
        if (!displayText.empty()) {
            if (IDWriteTextFormat* fmt = Dwrite()->Format(
                    13.0f, DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_TEXT_ALIGNMENT_LEADING,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                    DWRITE_WORD_WRAPPING_NO_WRAP)) {
                D2D1_RECT_F textRect = D2D1::RectF(bounds_.x + 10.0f, bounds_.y,
                                                   bounds_.right() - 24.0f, bounds_.bottom());
                D2D1_COLOR_F textColor = EffectiveForeground(pal.textPrimary);
                dc.DrawText(displayText.c_str(), static_cast<UINT32>(displayText.size()),
                            fmt, textRect, textColor, D2D1_DRAW_TEXT_OPTIONS_CLIP);

                // Caret (editable mode only, visible phase only).
                if (editable_ && IsFocused() && caretVisible_) {
                    ClampCaret();
                    ComPtr<IDWriteTextLayout> layout;
                    if (SUCCEEDED(Dwrite()->Factory()->CreateTextLayout(
                            displayText.c_str(), static_cast<UINT32>(displayText.size()),
                            fmt, textRect.right - textRect.left,
                            textRect.bottom - textRect.top, &layout))) {
                        DWRITE_HIT_TEST_METRICS htm;
                        float caretX, caretY;
                        if (SUCCEEDED(layout->HitTestTextPosition(caret_, false,
                                                                   &caretX, &caretY, &htm))) {
                            float x = textRect.left + caretX;
                            float y1 = textRect.top + caretY;
                            float y2 = y1 + htm.height;
                            dc.DrawLine(D2D1::Point2F(x, y1), D2D1::Point2F(x, y2),
                                        textColor, 1.0f);
                        }
                    }
                }
            }
        }
    }

    // Chevron (down arrow) on the right. The glyph is a static "v" shape, so it
    // is built once as a unit-space path geometry in the shared cache and drawn
    // here positioned by a transform — instead of re-emitting line primitives
    // every frame. Falls back to primitives if no cache.
    float cx = bounds_.right() - 16.0f;
    float cy = bounds_.y + bounds_.h * 0.5f;
    ResourceCache* cache = Context().resourceCache;
    ComPtr<ID2D1PathGeometry> chevron;
    if (cache) {
        const float hw = kChevronSize * 0.5f;
        chevron = cache->GetGeometry(GlyphId::ChevronDown, kChevronSize,
            [hw](ID2D1GeometrySink* sink) {
                // Unit space centered on the glyph origin; positioned by transform.
                sink->BeginFigure(D2D1::Point2F(-hw, -2.0f), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddLine(D2D1::Point2F(0.0f, 2.0f));
                sink->AddLine(D2D1::Point2F(hw, -2.0f));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
            });
    }
    if (chevron) {
        TransformGuard tg = dc.PushTransform(D2D1::Matrix3x2F::Translation(cx, cy));
        dc.DrawGeometry(chevron.Get(), pal.textPrimary, 1.5f);
    } else {
        dc.DrawLine(D2D1::Point2F(cx - kChevronSize * 0.5f, cy - 2.0f),
                    D2D1::Point2F(cx, cy + 2.0f), pal.textPrimary, 1.5f);
        dc.DrawLine(D2D1::Point2F(cx, cy + 2.0f),
                    D2D1::Point2F(cx + kChevronSize * 0.5f, cy - 2.0f), pal.textPrimary, 1.5f);
    }

    // Focus ring. Routed through the shared helper (same geometry as the hand-rolled
    // version it replaces: inset 2, corner kCornerRadius+2, stroke 1.5) so
    // CollectDirtyBounds can derive its pad from the same spec.
    if (IsFocused()) {
        dc.DrawRoundedRect(FocusRingRect(bounds_, HeaderFocusRing()), pal.accent,
                           HeaderFocusRing().strokeWidth);
    }
}

float ComboBox::VisualOverflowDip() const {
    return FocusRingPadDip(HeaderFocusRing());
}

} // namespace fluent
