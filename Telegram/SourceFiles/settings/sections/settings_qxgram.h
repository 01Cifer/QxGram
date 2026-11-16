/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"
#include "ui/rp_widget.h"

namespace Settings {

[[nodiscard]] Type QxGramId();
[[nodiscard]] rpl::producer<QString> QxPhrase(QString ru, QString en);

} // namespace Settings
