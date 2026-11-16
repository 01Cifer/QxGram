/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <vector>

class HistoryItem;

namespace QxDeleted {

[[nodiscard]] bool Enabled();

// Captures the items that should stay visible with a deleted badge
// and returns the items that should be destroyed now.
[[nodiscard]] std::vector<not_null<HistoryItem*>> FilterToDestroy(
	std::vector<not_null<HistoryItem*>> items);

[[nodiscard]] bool Kept(const HistoryItem *item);

// Destroys all captured items, called when the option is turned off.
void ReleaseAll();

void ItemDestroyed(HistoryItem *item);

} // namespace QxDeleted

namespace QxTtl {

[[nodiscard]] bool Enabled();

} // namespace QxTtl

namespace QxGhost {

[[nodiscard]] bool Enabled();
[[nodiscard]] rpl::producer<bool> Value();
void SetEnabled(bool enabled);

} // namespace QxGhost
