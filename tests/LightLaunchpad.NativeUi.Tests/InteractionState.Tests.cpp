#include "InteractionState.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using llp::interaction::Direction;
using llp::interaction::EnterAction;
using llp::interaction::EscapeAction;
using llp::interaction::FocusArea;
using llp::interaction::LaunchpadClickAction;
using llp::interaction::LaunchpadEnterAction;
using llp::interaction::LaunchpadState;
using llp::interaction::SpotlightState;

int passed = 0;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void Pass(const char* name)
{
    ++passed;
    std::cout << "PASS " << name << "\n";
}

std::vector<std::wstring> Results(std::initializer_list<const wchar_t*> ids)
{
    std::vector<std::wstring> values;
    for (const auto* id : ids) values.emplace_back(id);
    return values;
}

void TestOpenLeavesSearchAsTheOnlyFocusAndEnterTargetsFirstResult()
{
    SpotlightState state;
    state.Open();
    state.ResultsChanged(Results({ L"alpha", L"beta" }));
    Require(state.Focus() == FocusArea::Search, "Opening Spotlight did not focus search.");
    Require(state.FocusedResultIndex() == -1, "Opening Spotlight preselected a result.");
    Require(state.Enter() == EnterAction::LaunchFirstResult, "Enter in search did not target the first result.");
    Pass("InteractionState_OpenFocusesOnlySearchAndEnterTargetsFirst");
}

void TestDownEntersResultsAndArrowsNavigateWithoutChangingSearchFocusRules()
{
    SpotlightState state;
    state.Open();
    state.ResultsChanged(Results({ L"a", L"b", L"c", L"d", L"e", L"f" }));
    Require(state.EnterResults(), "Down could not enter non-empty results.");
    Require(state.Focus() == FocusArea::Results && state.FocusedResultIndex() == 0, "Down did not focus the first result.");
    Require(state.MoveResult(Direction::Right, 3), "Right did not move result focus.");
    Require(state.FocusedResultIndex() == 1, "Right selected the wrong result.");
    Require(state.MoveResult(Direction::Down, 3), "Down did not move by a result row.");
    Require(state.FocusedResultIndex() == 4, "Down selected the wrong result row.");
    Require(state.Enter() == EnterAction::LaunchFocusedResult, "Enter in results did not target focused result.");
    state.MoveResult(Direction::Up, 3);
    state.MoveResult(Direction::Up, 3);
    Require(state.Focus() == FocusArea::Search && state.FocusedResultIndex() == -1, "Up from the first result row did not return to search.");
    Pass("InteractionState_DownEntersResultsAndArrowsNavigate");
}

void TestPrintableInputAndTabTraverseSearchResultsAndAuxiliary()
{
    SpotlightState state;
    state.Open();
    state.ResultsChanged(Results({ L"a", L"b" }));
    state.Tab(false, true);
    Require(state.Focus() == FocusArea::Results && state.FocusedResultIndex() == 0, "Tab did not move from search to results.");
    state.Tab(false, true);
    Require(state.Focus() == FocusArea::Auxiliary, "Tab did not move from results to auxiliary controls.");
    Require(state.Enter() == EnterAction::OpenAuxiliary, "Enter did not operate the focused auxiliary control.");
    state.Tab(false, true);
    Require(state.Focus() == FocusArea::Search, "Forward Tab did not wrap to search.");
    state.Tab(true, true);
    Require(state.Focus() == FocusArea::Auxiliary, "Shift+Tab did not traverse in reverse.");
    state.PrintableInput();
    Require(state.Focus() == FocusArea::Search && state.FocusedResultIndex() == -1, "Printable input did not return focus to search.");
    Pass("InteractionState_PrintableAndTabFollowStandardFocusOrder");
}

void TestEscapeReturnsFocusThenClearsQueryThenCloses()
{
    SpotlightState state;
    state.Open();
    state.ResultsChanged(Results({ L"a" }));
    state.EnterResults();
    Require(state.Escape(false) == EscapeAction::FocusSearch, "Esc from results did not return to search first.");
    Require(state.Escape(false) == EscapeAction::ClearQuery, "Esc with a query did not request query clearing.");
    Require(state.Escape(true) == EscapeAction::Close, "Esc with an empty search did not request close.");
    Pass("InteractionState_EscapeProgressesFocusQueryAndClose");
}

void TestRefreshPreservesStableFocusAndCannotLaunchReplacement()
{
    SpotlightState state;
    state.Open();
    state.ResultsChanged(Results({ L"a", L"b", L"c" }));
    state.EnterResults();
    state.MoveResult(Direction::Right, 3);
    Require(state.FocusedStableId() == L"b", "Fixture did not focus stable result b.");
    state.ResultsChanged(Results({ L"c", L"b", L"a" }));
    Require(state.FocusedResultIndex() == 1 && state.FocusedStableId() == L"b", "Result refresh did not preserve focus by stable ID.");
    state.ResultsChanged(Results({ L"c", L"a" }));
    Require(state.Focus() == FocusArea::Results && state.FocusedResultIndex() == -1, "Missing focused result silently substituted another item.");
    Require(state.Enter() == EnterAction::None, "Enter could launch an unintended replacement after refresh.");
    Require(state.EnterResults() && state.FocusedResultIndex() == 0, "Explicit Down did not re-establish a safe result focus.");
    Pass("InteractionState_RefreshPreservesStableIdAndNeverSubstitutesLaunch");
}

void TestLaunchpadBrowseModeLaunchesAndRejectsEditingGestures()
{
    LaunchpadState state;
    state.Open();
    Require(!state.IsEditing(), "Launchpad did not open in browse mode.");
    Require(state.ClickItem(false) == LaunchpadClickAction::LaunchItem, "Browse click did not launch the item.");
    Require(state.ClickItem(true) == LaunchpadClickAction::LaunchItem, "Ctrl+click bypassed browse launch semantics.");
    Require(state.EnterOnFocusedItem() == LaunchpadEnterAction::LaunchFocusedItem, "Browse Enter did not launch the focused item.");
    Require(!state.AllowsSelection() && !state.AllowsMarquee() && !state.AllowsItemDrag() && !state.AllowsRegionDrag(), "Browse mode exposed an editing gesture.");
    Pass("InteractionState_LaunchpadBrowseLaunchesAndRejectsEditingGestures");
}

void TestLaunchpadEditModeSelectsWithoutAccidentalLaunch()
{
    LaunchpadState state;
    state.Open();
    state.EnterEdit();
    Require(state.IsEditing(), "Explicit edit command did not enter edit mode.");
    Require(state.ClickItem(false) == LaunchpadClickAction::SelectItem, "Edit click did not select the item.");
    Require(state.ClickItem(true) == LaunchpadClickAction::ToggleItemSelection, "Edit Ctrl+click did not toggle multiselection.");
    Require(state.EnterOnFocusedItem() == LaunchpadEnterAction::None, "Edit Enter could accidentally launch from selection focus.");
    Require(state.AllowsSelection() && state.AllowsMarquee() && state.AllowsItemDrag() && state.AllowsRegionDrag(), "Edit mode did not enable all organization gestures.");
    Require(state.ExitEdit(), "Done/Esc did not report leaving edit mode.");
    Require(!state.IsEditing(), "Done/Esc left Launchpad in edit mode.");
    Require(!state.ExitEdit(), "Exiting browse mode reported a false edit transition.");
    Pass("InteractionState_LaunchpadEditSelectsWithoutAccidentalLaunch");
}
}

int main()
{
    try
    {
        TestOpenLeavesSearchAsTheOnlyFocusAndEnterTargetsFirstResult();
        TestDownEntersResultsAndArrowsNavigateWithoutChangingSearchFocusRules();
        TestPrintableInputAndTabTraverseSearchResultsAndAuxiliary();
        TestEscapeReturnsFocusThenClearsQueryThenCloses();
        TestRefreshPreservesStableFocusAndCannotLaunchReplacement();
        TestLaunchpadBrowseModeLaunchesAndRejectsEditingGestures();
        TestLaunchpadEditModeSelectsWithoutAccidentalLaunch();
        std::cout << passed << "/7 tests passed\n";
        return passed == 7 ? 0 : 1;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL " << exception.what() << "\n";
        return 1;
    }
}
