#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace llp::input
{
enum class Move
{
    Left,
    Right,
    WordLeft,
    WordRight,
    Home,
    End
};

class TextBuffer
{
public:
    static constexpr size_t MaximumLength = 4096;

    const std::wstring& Text() const;
    std::wstring DisplayText() const;
    size_t Caret() const;
    size_t DisplayCaret() const;
    size_t Anchor() const;
    size_t SelectionStart() const;
    size_t SelectionEnd() const;
    size_t CompositionStart() const;
    size_t CompositionEnd() const;
    bool HasSelection() const;
    bool IsComposing() const;
    const std::wstring& CompositionText() const;
    std::wstring SelectedText() const;

    void Reset(const std::wstring& text);
    bool SetText(const std::wstring& text);
    void SetCaret(size_t position, bool extendSelection);
    void SetSelection(size_t anchor, size_t caret);
    void SelectAll();
    void MoveCaret(Move movement, bool extendSelection);

    bool InsertText(const std::wstring& text);
    bool CutSelection();
    bool Backspace(bool byWord);
    bool DeleteForward(bool byWord);

    bool CanUndo() const;
    bool CanRedo() const;
    bool Undo();
    bool Redo();

    void BeginComposition();
    void UpdateComposition(const std::wstring& text, size_t caret);
    bool CommitComposition(const std::wstring& result);
    void CancelComposition();
    bool HandleLegacyImeCharacter(wchar_t character);

private:
    struct Snapshot
    {
        std::wstring text;
        size_t caret = 0;
        size_t anchor = 0;
    };

    static bool IsHighSurrogate(wchar_t value);
    static bool IsLowSurrogate(wchar_t value);
    static size_t NormalizeIndex(const std::wstring& text, size_t position);
    static size_t PreviousBoundary(const std::wstring& text, size_t position);
    static size_t NextBoundary(const std::wstring& text, size_t position);
    static bool IsWordAt(const std::wstring& text, size_t position);
    size_t WordLeft(size_t position) const;
    size_t WordRight(size_t position) const;
    size_t DeleteWordRight(size_t position) const;
    Snapshot Capture() const;
    void Restore(const Snapshot& snapshot);
    void RememberUndo();
    bool ReplaceRange(size_t start, size_t end, const std::wstring& replacement, bool rememberUndo);
    bool DeleteSelection();

    std::wstring text_;
    size_t caret_ = 0;
    size_t anchor_ = 0;
    bool composing_ = false;
    size_t compositionStart_ = 0;
    size_t compositionEnd_ = 0;
    size_t compositionCaret_ = 0;
    std::wstring compositionText_;
    std::wstring suppressedLegacyImeText_;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
};
}
