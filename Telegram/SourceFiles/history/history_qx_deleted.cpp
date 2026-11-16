/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_qx_deleted.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

#include <unordered_set>

namespace QxDeleted {
namespace {

std::unordered_set<HistoryItem*> &Set() {
	static std::unordered_set<HistoryItem*> result;
	return result;
}

} // namespace

bool Enabled() {
	return Core::App().settings().readPref<bool>(
		Core::kQxShowDeletedKey,
		false);
}

std::vector<not_null<HistoryItem*>> FilterToDestroy(
		std::vector<not_null<HistoryItem*>> items) {
	if (!Enabled()) {
		return items;
	}
	auto destroy = std::vector<not_null<HistoryItem*>>();
	for (const auto &item : items) {
		if (Set().emplace(item.get()).second) {
			item->history()->session().data().requestItemResize(item);
		}
	}
	return destroy;
}

bool Kept(const HistoryItem *item) {
	return Set().contains(const_cast<HistoryItem*>(item));
}

void ReleaseAll() {
	if (Set().empty()) {
		return;
	}
	auto items = std::vector<not_null<HistoryItem*>>();
	for (const auto &item : Set()) {
		items.push_back(item);
	}
	Set().clear();
	for (const auto &item : items) {
		item->destroy();
	}
}

void ItemDestroyed(HistoryItem *item) {
	Set().erase(item);
}

} // namespace QxDeleted

namespace QxTtl {

bool Enabled() {
	return Core::App().settings().readPref<bool>(
		Core::kQxShowTtlKey,
		false);
}

} // namespace QxTtl

namespace QxGhost {
namespace {

rpl::variable<bool> &State() {
	static auto result = rpl::variable<bool>(
		Core::App().settings().readPref<bool>(Core::kQxGhostModeKey, false));
	return result;
}

} // namespace

bool Enabled() {
	return State().current();
}

rpl::producer<bool> Value() {
	return State().value();
}

void SetEnabled(bool enabled) {
	if (Enabled() == enabled) {
		return;
	}
	Core::App().settings().writePref<bool>(Core::kQxGhostModeKey, enabled);
	Core::App().saveSettingsDelayed();
	State() = enabled;
}

} // namespace QxGhost
