#include "InteractionState.h"

#include <algorithm>

namespace llp::interaction
{
namespace
{
const std::wstring EmptyStableId;
}

void LaunchpadState::Open()
{
    editing_ = false;
}

void LaunchpadState::EnterEdit()
{
    editing_ = true;
}

bool LaunchpadState::ExitEdit()
{
    if (!editing_) return false;
    editing_ = false;
    return true;
}

bool LaunchpadState::IsEditing() const
{
    return editing_;
}

LaunchpadClickAction LaunchpadState::ClickItem(bool additive) const
{
    if (!editing_) return LaunchpadClickAction::LaunchItem;
    return additive ? LaunchpadClickAction::ToggleItemSelection : LaunchpadClickAction::SelectItem;
}

LaunchpadEnterAction LaunchpadState::EnterOnFocusedItem() const
{
    return editing_ ? LaunchpadEnterAction::None : LaunchpadEnterAction::LaunchFocusedItem;
}

bool LaunchpadState::AllowsSelection() const
{
    return editing_;
}

bool LaunchpadState::AllowsMarquee() const
{
    return editing_;
}

bool LaunchpadState::AllowsItemDrag() const
{
    return editing_;
}

bool LaunchpadState::AllowsRegionDrag() const
{
    return editing_;
}

void SpotlightState::Open()
{
    focus_ = FocusArea::Search;
    focusedResultIndex_ = -1;
}

void SpotlightState::ResultsChanged(const std::vector<std::wstring>& stableIds)
{
    std::wstring focusedStableId;
    if (focus_ == FocusArea::Results
        && focusedResultIndex_ >= 0
        && focusedResultIndex_ < static_cast<int>(resultStableIds_.size()))
    {
        focusedStableId = resultStableIds_[focusedResultIndex_];
    }

    resultStableIds_ = stableIds;
    focusedResultIndex_ = -1;
    if (focus_ != FocusArea::Results) return;
    if (resultStableIds_.empty())
    {
        focus_ = FocusArea::Search;
        return;
    }
    if (focusedStableId.empty()) return;
    const auto found = std::find(resultStableIds_.begin(), resultStableIds_.end(), focusedStableId);
    if (found != resultStableIds_.end()) focusedResultIndex_ = static_cast<int>(found - resultStableIds_.begin());
}

bool SpotlightState::EnterResults()
{
    if (resultStableIds_.empty()) return false;
    focus_ = FocusArea::Results;
    if (focusedResultIndex_ < 0 || focusedResultIndex_ >= static_cast<int>(resultStableIds_.size())) focusedResultIndex_ = 0;
    return true;
}

bool SpotlightState::FocusResult(int index)
{
    if (index < 0 || index >= static_cast<int>(resultStableIds_.size())) return false;
    focus_ = FocusArea::Results;
    focusedResultIndex_ = index;
    return true;
}

bool SpotlightState::MoveResult(Direction direction, int columns)
{
    if (focus_ != FocusArea::Results || focusedResultIndex_ < 0 || resultStableIds_.empty()) return false;
    columns = std::max(1, columns);
    int target = focusedResultIndex_;
    switch (direction)
    {
    case Direction::Left:
        if (target <= 0) return false;
        --target;
        break;
    case Direction::Right:
        if (target + 1 >= static_cast<int>(resultStableIds_.size())) return false;
        ++target;
        break;
    case Direction::Up:
        if (target < columns)
        {
            FocusSearch();
            return true;
        }
        target -= columns;
        break;
    case Direction::Down:
        if (target + columns >= static_cast<int>(resultStableIds_.size())) return false;
        target += columns;
        break;
    }
    focusedResultIndex_ = target;
    return true;
}

void SpotlightState::FocusSearch()
{
    focus_ = FocusArea::Search;
    focusedResultIndex_ = -1;
}

void SpotlightState::FocusAuxiliary()
{
    focus_ = FocusArea::Auxiliary;
    focusedResultIndex_ = -1;
}

void SpotlightState::PrintableInput()
{
    FocusSearch();
}

void SpotlightState::Tab(bool reverse, bool hasAuxiliary)
{
    std::vector<FocusArea> order = { FocusArea::Search };
    if (!resultStableIds_.empty()) order.push_back(FocusArea::Results);
    if (hasAuxiliary) order.push_back(FocusArea::Auxiliary);
    auto found = std::find(order.begin(), order.end(), focus_);
    int index = found == order.end() ? 0 : static_cast<int>(found - order.begin());
    index = reverse
        ? (index + static_cast<int>(order.size()) - 1) % static_cast<int>(order.size())
        : (index + 1) % static_cast<int>(order.size());
    focus_ = order[index];
    focusedResultIndex_ = focus_ == FocusArea::Results ? 0 : -1;
}

EscapeAction SpotlightState::Escape(bool queryEmpty)
{
    if (focus_ != FocusArea::Search)
    {
        FocusSearch();
        return EscapeAction::FocusSearch;
    }
    return queryEmpty ? EscapeAction::Close : EscapeAction::ClearQuery;
}

EnterAction SpotlightState::Enter() const
{
    switch (focus_)
    {
    case FocusArea::Search:
        return resultStableIds_.empty() ? EnterAction::None : EnterAction::LaunchFirstResult;
    case FocusArea::Results:
        return focusedResultIndex_ >= 0 && focusedResultIndex_ < static_cast<int>(resultStableIds_.size())
            ? EnterAction::LaunchFocusedResult
            : EnterAction::None;
    case FocusArea::Auxiliary:
        return EnterAction::OpenAuxiliary;
    }
    return EnterAction::None;
}

FocusArea SpotlightState::Focus() const
{
    return focus_;
}

int SpotlightState::FocusedResultIndex() const
{
    return focusedResultIndex_;
}

const std::wstring& SpotlightState::FocusedStableId() const
{
    return focusedResultIndex_ >= 0 && focusedResultIndex_ < static_cast<int>(resultStableIds_.size())
        ? resultStableIds_[focusedResultIndex_]
        : EmptyStableId;
}
}
