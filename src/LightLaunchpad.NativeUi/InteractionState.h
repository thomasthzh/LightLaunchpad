#pragma once

#include <string>
#include <vector>

namespace llp::interaction
{
enum class FocusArea
{
    Search,
    Results,
    Auxiliary
};

enum class Direction
{
    Left,
    Right,
    Up,
    Down
};

enum class EnterAction
{
    None,
    LaunchFirstResult,
    LaunchFocusedResult,
    OpenAuxiliary
};

enum class EscapeAction
{
    FocusSearch,
    ClearQuery,
    Close
};

class SpotlightState
{
public:
    void Open();
    void ResultsChanged(const std::vector<std::wstring>& stableIds);
    bool EnterResults();
    bool FocusResult(int index);
    bool MoveResult(Direction direction, int columns);
    void FocusSearch();
    void FocusAuxiliary();
    void PrintableInput();
    void Tab(bool reverse, bool hasAuxiliary);
    EscapeAction Escape(bool queryEmpty);
    EnterAction Enter() const;

    FocusArea Focus() const;
    int FocusedResultIndex() const;
    const std::wstring& FocusedStableId() const;

private:
    FocusArea focus_ = FocusArea::Search;
    int focusedResultIndex_ = -1;
    std::vector<std::wstring> resultStableIds_;
};
}
