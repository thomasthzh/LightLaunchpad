#include "TextInput.h"

#include <algorithm>
#include <cwctype>

namespace llp::input
{
namespace
{
constexpr size_t MaximumUndoDepth = 100;
}

const std::wstring& TextBuffer::Text() const
{
    return text_;
}

std::wstring TextBuffer::DisplayText() const
{
    if (!composing_) return text_;
    return text_.substr(0, compositionStart_) + compositionText_ + text_.substr(compositionEnd_);
}

size_t TextBuffer::Caret() const
{
    return caret_;
}

size_t TextBuffer::DisplayCaret() const
{
    return composing_ ? compositionStart_ + compositionCaret_ : caret_;
}

size_t TextBuffer::Anchor() const
{
    return anchor_;
}

size_t TextBuffer::SelectionStart() const
{
    return std::min(anchor_, caret_);
}

size_t TextBuffer::SelectionEnd() const
{
    return std::max(anchor_, caret_);
}

size_t TextBuffer::CompositionStart() const
{
    return composing_ ? compositionStart_ : caret_;
}

size_t TextBuffer::CompositionEnd() const
{
    return composing_ ? compositionStart_ + compositionText_.size() : caret_;
}

bool TextBuffer::HasSelection() const
{
    return anchor_ != caret_;
}

bool TextBuffer::IsComposing() const
{
    return composing_;
}

const std::wstring& TextBuffer::CompositionText() const
{
    return compositionText_;
}

std::wstring TextBuffer::SelectedText() const
{
    return HasSelection() ? text_.substr(SelectionStart(), SelectionEnd() - SelectionStart()) : std::wstring{};
}

void TextBuffer::Reset(const std::wstring& text)
{
    text_ = text.substr(0, MaximumLength);
    caret_ = text_.size();
    anchor_ = caret_;
    composing_ = false;
    compositionStart_ = caret_;
    compositionEnd_ = caret_;
    compositionCaret_ = 0;
    compositionText_.clear();
    suppressedLegacyImeText_.clear();
    undo_.clear();
    redo_.clear();
}

bool TextBuffer::SetText(const std::wstring& text)
{
    if (text.size() > MaximumLength || text == text_) return false;
    CancelComposition();
    RememberUndo();
    text_ = text;
    caret_ = text_.size();
    anchor_ = caret_;
    return true;
}

void TextBuffer::SetCaret(size_t position, bool extendSelection)
{
    if (composing_) CancelComposition();
    const size_t normalized = NormalizeIndex(text_, position);
    caret_ = normalized;
    if (!extendSelection) anchor_ = normalized;
}

void TextBuffer::SetSelection(size_t anchor, size_t caret)
{
    if (composing_) CancelComposition();
    anchor_ = NormalizeIndex(text_, anchor);
    caret_ = NormalizeIndex(text_, caret);
}

void TextBuffer::SelectAll()
{
    if (composing_) CancelComposition();
    anchor_ = 0;
    caret_ = text_.size();
}

void TextBuffer::MoveCaret(Move movement, bool extendSelection)
{
    if (composing_) return;
    size_t target = caret_;
    if (!extendSelection && HasSelection() && (movement == Move::Left || movement == Move::Right))
    {
        target = movement == Move::Left ? SelectionStart() : SelectionEnd();
    }
    else
    {
        switch (movement)
        {
        case Move::Left: target = PreviousBoundary(text_, caret_); break;
        case Move::Right: target = NextBoundary(text_, caret_); break;
        case Move::WordLeft: target = WordLeft(caret_); break;
        case Move::WordRight: target = WordRight(caret_); break;
        case Move::Home: target = 0; break;
        case Move::End: target = text_.size(); break;
        }
    }
    caret_ = target;
    if (!extendSelection) anchor_ = caret_;
}

bool TextBuffer::InsertText(const std::wstring& text)
{
    if (composing_ || text.empty()) return false;
    return ReplaceRange(SelectionStart(), SelectionEnd(), text, true);
}

bool TextBuffer::CutSelection()
{
    return DeleteSelection();
}

bool TextBuffer::Backspace(bool byWord)
{
    if (composing_) return false;
    if (HasSelection()) return DeleteSelection();
    const size_t start = byWord ? WordLeft(caret_) : PreviousBoundary(text_, caret_);
    return start < caret_ && ReplaceRange(start, caret_, L"", true);
}

bool TextBuffer::DeleteForward(bool byWord)
{
    if (composing_) return false;
    if (HasSelection()) return DeleteSelection();
    const size_t end = byWord ? DeleteWordRight(caret_) : NextBoundary(text_, caret_);
    return end > caret_ && ReplaceRange(caret_, end, L"", true);
}

bool TextBuffer::CanUndo() const
{
    return !undo_.empty();
}

bool TextBuffer::CanRedo() const
{
    return !redo_.empty();
}

bool TextBuffer::Undo()
{
    if (undo_.empty()) return false;
    CancelComposition();
    redo_.push_back(Capture());
    Restore(undo_.back());
    undo_.pop_back();
    return true;
}

bool TextBuffer::Redo()
{
    if (redo_.empty()) return false;
    CancelComposition();
    undo_.push_back(Capture());
    Restore(redo_.back());
    redo_.pop_back();
    return true;
}

void TextBuffer::BeginComposition()
{
    if (composing_) return;
    suppressedLegacyImeText_.clear();
    composing_ = true;
    compositionStart_ = SelectionStart();
    compositionEnd_ = SelectionEnd();
    compositionCaret_ = 0;
    compositionText_.clear();
}

void TextBuffer::UpdateComposition(const std::wstring& text, size_t caret)
{
    if (!composing_) BeginComposition();
    compositionText_ = text;
    compositionCaret_ = NormalizeIndex(compositionText_, std::min(caret, compositionText_.size()));
}

bool TextBuffer::CommitComposition(const std::wstring& result)
{
    if (!composing_) return false;
    const size_t start = compositionStart_;
    const size_t end = compositionEnd_;
    composing_ = false;
    compositionText_.clear();
    compositionCaret_ = 0;
    const bool changed = ReplaceRange(start, end, result, true);
    suppressedLegacyImeText_ = result;
    return changed || start == end;
}

void TextBuffer::CancelComposition()
{
    if (!composing_) return;
    composing_ = false;
    compositionText_.clear();
    compositionCaret_ = 0;
    compositionStart_ = caret_;
    compositionEnd_ = caret_;
}

bool TextBuffer::HandleLegacyImeCharacter(wchar_t character)
{
    if (!suppressedLegacyImeText_.empty())
    {
        if (suppressedLegacyImeText_.front() == character)
        {
            suppressedLegacyImeText_.erase(suppressedLegacyImeText_.begin());
            return false;
        }
        suppressedLegacyImeText_.clear();
    }
    if (composing_) CancelComposition();
    return InsertText(std::wstring(1, character));
}

bool TextBuffer::IsHighSurrogate(wchar_t value)
{
    const unsigned int code = static_cast<unsigned short>(value);
    return code >= 0xD800 && code <= 0xDBFF;
}

bool TextBuffer::IsLowSurrogate(wchar_t value)
{
    const unsigned int code = static_cast<unsigned short>(value);
    return code >= 0xDC00 && code <= 0xDFFF;
}

size_t TextBuffer::NormalizeIndex(const std::wstring& text, size_t position)
{
    position = std::min(position, text.size());
    if (position > 0 && position < text.size() && IsHighSurrogate(text[position - 1]) && IsLowSurrogate(text[position])) --position;
    return position;
}

size_t TextBuffer::PreviousBoundary(const std::wstring& text, size_t position)
{
    position = NormalizeIndex(text, position);
    if (position == 0) return 0;
    if (position >= 2 && IsHighSurrogate(text[position - 2]) && IsLowSurrogate(text[position - 1])) return position - 2;
    return position - 1;
}

size_t TextBuffer::NextBoundary(const std::wstring& text, size_t position)
{
    position = NormalizeIndex(text, position);
    if (position >= text.size()) return text.size();
    if (position + 1 < text.size() && IsHighSurrogate(text[position]) && IsLowSurrogate(text[position + 1])) return position + 2;
    return position + 1;
}

bool TextBuffer::IsWordAt(const std::wstring& text, size_t position)
{
    if (position >= text.size()) return false;
    const wchar_t value = text[position];
    if (IsHighSurrogate(value) && position + 1 < text.size() && IsLowSurrogate(text[position + 1])) return false;
    const unsigned int code = static_cast<unsigned short>(value);
    return std::iswalnum(value) || code == L'_' || (code >= 0x3400 && code <= 0x9FFF);
}

size_t TextBuffer::WordLeft(size_t position) const
{
    size_t cursor = NormalizeIndex(text_, position);
    while (cursor > 0)
    {
        const size_t previous = PreviousBoundary(text_, cursor);
        if (IsWordAt(text_, previous)) break;
        cursor = previous;
    }
    while (cursor > 0)
    {
        const size_t previous = PreviousBoundary(text_, cursor);
        if (!IsWordAt(text_, previous)) break;
        cursor = previous;
    }
    return cursor;
}

size_t TextBuffer::WordRight(size_t position) const
{
    size_t cursor = NormalizeIndex(text_, position);
    while (cursor < text_.size() && IsWordAt(text_, cursor)) cursor = NextBoundary(text_, cursor);
    while (cursor < text_.size() && !IsWordAt(text_, cursor)) cursor = NextBoundary(text_, cursor);
    return cursor;
}

size_t TextBuffer::DeleteWordRight(size_t position) const
{
    size_t cursor = NormalizeIndex(text_, position);
    if (cursor < text_.size() && IsWordAt(text_, cursor))
    {
        while (cursor < text_.size() && IsWordAt(text_, cursor)) cursor = NextBoundary(text_, cursor);
        return cursor;
    }
    while (cursor < text_.size() && !IsWordAt(text_, cursor)) cursor = NextBoundary(text_, cursor);
    while (cursor < text_.size() && IsWordAt(text_, cursor)) cursor = NextBoundary(text_, cursor);
    return cursor;
}

TextBuffer::Snapshot TextBuffer::Capture() const
{
    return { text_, caret_, anchor_ };
}

void TextBuffer::Restore(const Snapshot& snapshot)
{
    text_ = snapshot.text;
    caret_ = NormalizeIndex(text_, snapshot.caret);
    anchor_ = NormalizeIndex(text_, snapshot.anchor);
    composing_ = false;
    compositionText_.clear();
    suppressedLegacyImeText_.clear();
}

void TextBuffer::RememberUndo()
{
    undo_.push_back(Capture());
    if (undo_.size() > MaximumUndoDepth) undo_.erase(undo_.begin());
    redo_.clear();
}

bool TextBuffer::ReplaceRange(size_t start, size_t end, const std::wstring& replacement, bool rememberUndo)
{
    const size_t lower = std::min(start, end);
    const size_t upper = std::max(start, end);
    start = NormalizeIndex(text_, lower);
    end = NormalizeIndex(text_, upper);
    if (text_.size() - (end - start) + replacement.size() > MaximumLength) return false;
    if (start == end && replacement.empty()) return false;
    if (rememberUndo) RememberUndo();
    text_.replace(start, end - start, replacement);
    caret_ = start + replacement.size();
    anchor_ = caret_;
    return true;
}

bool TextBuffer::DeleteSelection()
{
    return HasSelection() && ReplaceRange(SelectionStart(), SelectionEnd(), L"", true);
}
}
