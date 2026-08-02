#include "TextInput.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using llp::input::Move;
using llp::input::TextBuffer;

int passed = 0;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void RequireEqual(const std::wstring& expected, const std::wstring& actual, const char* message)
{
    if (expected != actual) throw std::runtime_error(message);
}

void Pass(const char* name)
{
    ++passed;
    std::cout << "PASS " << name << "\n";
}

void TestInsertionSelectionAndUnicodeBoundaries()
{
    TextBuffer buffer;
    buffer.Reset(L"alpha");
    buffer.SetCaret(2, false);
    Require(buffer.InsertText(L"X"), "Middle insertion was rejected.");
    RequireEqual(L"alXpha", buffer.Text(), "Middle insertion produced the wrong text.");
    Require(buffer.Caret() == 3 && !buffer.HasSelection(), "Middle insertion did not collapse at the new caret.");

    buffer.SetSelection(4, 1);
    Require(buffer.InsertText(L"YZ"), "Selection replacement was rejected.");
    RequireEqual(L"aYZha", buffer.Text(), "Selection replacement produced the wrong text.");
    RequireEqual(L"YZ", buffer.Text().substr(1, 2), "Replacement content mismatch.");

    buffer.Reset(L"A\U0001F680B");
    buffer.SetCaret(3, false);
    buffer.MoveCaret(Move::Left, false);
    Require(buffer.Caret() == 1, "Caret split a UTF-16 surrogate pair while moving left.");
    Require(buffer.DeleteForward(false), "Forward delete rejected a surrogate pair.");
    RequireEqual(L"AB", buffer.Text(), "Forward delete removed only half of a surrogate pair.");
    Pass("TextInput_InsertsReplacesAndPreservesUnicodeBoundaries");
}

void TestNavigationAndExtendedSelection()
{
    TextBuffer buffer;
    buffer.Reset(L"one two");
    buffer.MoveCaret(Move::End, false);
    Require(buffer.Caret() == 7, "End did not move to the text end.");
    buffer.MoveCaret(Move::WordLeft, false);
    Require(buffer.Caret() == 4, "Ctrl+Left word navigation mismatch.");
    buffer.MoveCaret(Move::WordLeft, true);
    Require(buffer.SelectionStart() == 0 && buffer.SelectionEnd() == 4, "Shift+Ctrl+Left selection mismatch.");
    RequireEqual(L"one ", buffer.SelectedText(), "Extended selection content mismatch.");
    buffer.MoveCaret(Move::End, false);
    buffer.MoveCaret(Move::Home, true);
    Require(buffer.SelectionStart() == 0 && buffer.SelectionEnd() == 7, "Shift+Home did not select to the start.");
    Pass("TextInput_NavigatesCharactersWordsHomeEndAndSelection");
}

void TestBackspaceDeleteAndWordDeletion()
{
    TextBuffer buffer;
    buffer.Reset(L"alpha beta");
    buffer.MoveCaret(Move::End, false);
    Require(buffer.Backspace(true), "Ctrl+Backspace was rejected.");
    RequireEqual(L"alpha ", buffer.Text(), "Ctrl+Backspace deleted the wrong word range.");
    buffer.MoveCaret(Move::Home, false);
    Require(buffer.DeleteForward(true), "Ctrl+Delete was rejected.");
    RequireEqual(L" ", buffer.Text(), "Ctrl+Delete deleted the wrong word range.");
    buffer.SetSelection(0, 1);
    Require(buffer.Backspace(false), "Backspace did not delete the active selection.");
    Require(buffer.Text().empty(), "Selection deletion left text behind.");
    Pass("TextInput_DeletesSelectionsCharactersAndWords");
}

void TestClipboardPrimitivesAndUndoRedo()
{
    TextBuffer buffer;
    buffer.Reset(L"copy paste");
    buffer.SetSelection(0, 4);
    RequireEqual(L"copy", buffer.SelectedText(), "Selected text did not expose clipboard content.");
    Require(buffer.CutSelection(), "Cut rejected a non-empty selection.");
    RequireEqual(L" paste", buffer.Text(), "Cut produced the wrong text.");
    Require(buffer.InsertText(L"copy"), "Paste primitive rejected text.");
    RequireEqual(L"copy paste", buffer.Text(), "Paste primitive produced the wrong text.");
    Require(buffer.Undo(), "Undo was unavailable after paste.");
    RequireEqual(L" paste", buffer.Text(), "Undo did not restore the pre-paste state.");
    Require(buffer.Redo(), "Redo was unavailable after undo.");
    RequireEqual(L"copy paste", buffer.Text(), "Redo did not restore the paste.");
    buffer.SelectAll();
    Require(buffer.SelectionStart() == 0 && buffer.SelectionEnd() == buffer.Text().size(), "Ctrl+A selection mismatch.");
    Pass("TextInput_ProvidesClipboardPrimitivesSelectAllAndUndoRedo");
}

void TestImeCompositionAndDuplicateSuppression()
{
    TextBuffer buffer;
    buffer.Reset(L"ac");
    buffer.SetCaret(1, false);
    buffer.BeginComposition();
    buffer.UpdateComposition(L"中", 1);
    RequireEqual(L"ac", buffer.Text(), "IME composition mutated committed text.");
    RequireEqual(L"a中c", buffer.DisplayText(), "IME composition was not inserted at the caret.");
    Require(buffer.DisplayCaret() == 2, "IME composition caret mismatch.");
    Require(buffer.CommitComposition(L"中"), "IME result was not committed.");
    RequireEqual(L"a中c", buffer.Text(), "IME result committed at the wrong position.");
    Require(!buffer.CommitComposition(L"中"), "A duplicate IME result was committed twice.");
    Require(!buffer.HandleLegacyImeCharacter(L'中'), "A duplicate WM_IME_CHAR was not suppressed.");
    RequireEqual(L"a中c", buffer.Text(), "Duplicate WM_IME_CHAR changed the text.");

    buffer.BeginComposition();
    Require(buffer.HandleLegacyImeCharacter(L'文'), "A legacy-only IME character was rejected.");
    RequireEqual(L"a中文c", buffer.Text(), "Legacy-only IME character committed incorrectly.");
    Pass("TextInput_ComposesAtCaretAndSuppressesDuplicateImeCommit");
}

void TestCompositionCancelRestoresSelectionAndResetIsClean()
{
    TextBuffer buffer;
    buffer.Reset(L"hello");
    buffer.SetSelection(1, 4);
    buffer.BeginComposition();
    buffer.UpdateComposition(L"你", 1);
    RequireEqual(L"h你o", buffer.DisplayText(), "Composition did not visually replace the selection.");
    buffer.CancelComposition();
    RequireEqual(L"hello", buffer.Text(), "Cancelling composition changed committed text.");
    Require(buffer.SelectionStart() == 1 && buffer.SelectionEnd() == 4, "Cancelling composition lost the prior selection.");
    buffer.Reset(L"");
    Require(buffer.Text().empty() && buffer.Caret() == 0 && !buffer.CanUndo() && !buffer.IsComposing(), "Reset retained stale text-input state.");
    Pass("TextInput_CancelsCompositionAndResetsWithoutStaleState");
}

void TestEditLatencyStaysWithinInputBudget()
{
    TextBuffer buffer;
    buffer.Reset(std::wstring(300, L'a'));
    std::vector<long long> samples;
    samples.reserve(2000);
    for (size_t index = 0; index < 2000; ++index)
    {
        buffer.SetCaret(index % 301, false);
        const auto started = std::chrono::steady_clock::now();
        Require(buffer.InsertText(L"x"), "Timed insertion was rejected.");
        Require(buffer.Undo(), "Timed insertion was not undoable.");
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count();
        samples.push_back(elapsed);
    }
    std::sort(samples.begin(), samples.end());
    const long long p95 = samples[(samples.size() * 95) / 100];
    RequireEqual(std::wstring(300, L'a'), buffer.Text(), "Timed edits did not restore the original buffer.");
    Require(p95 <= 5000000, "Production text-buffer edit P95 exceeded five milliseconds.");
    std::cout << "METRIC TextInputEditP95Ns=" << p95 << " samples=" << samples.size() << "\n";
    Pass("TextInput_EditAndUndoP95StayWithinBudget");
}
}

int main()
{
    try
    {
        TestInsertionSelectionAndUnicodeBoundaries();
        TestNavigationAndExtendedSelection();
        TestBackspaceDeleteAndWordDeletion();
        TestClipboardPrimitivesAndUndoRedo();
        TestImeCompositionAndDuplicateSuppression();
        TestCompositionCancelRestoresSelectionAndResetIsClean();
        TestEditLatencyStaysWithinInputBudget();
        std::cout << passed << "/7 tests passed\n";
        return passed == 7 ? 0 : 1;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL " << exception.what() << "\n";
        return 1;
    }
}
