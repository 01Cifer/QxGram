/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_qxgram.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "history/history_qx_deleted.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {
namespace {

using namespace Builder;

struct ToggleArgs {
	std::string_view key;
	rpl::producer<QString> title;
	QString id;
	QStringList keywords;
	Fn<void(bool)> onToggleChange;
};

void AddToggle(SectionBuilder &builder, ToggleArgs &&args) {
	const auto value = Core::App().settings().readPref<bool>(args.key, false);
	const auto key = args.key;
	const auto extra = args.onToggleChange;
	const auto button = builder.addButton({
		.id = args.id,
		.title = std::move(args.title),
		.toggled = rpl::single(value),
		.keywords = std::move(args.keywords),
	});
	button->toggledChanges(
	) | rpl::on_next([=](bool toggled) {
		Core::App().settings().writePref<bool>(key, toggled);
		Core::App().saveSettingsDelayed();
		if (extra) {
			extra(toggled);
		}
	}, button->lifetime());
}

void BuildFeatures(SectionBuilder &builder) {
	builder.addSkip();
	builder.addSubsectionTitle(QxPhrase(
		u"Функции"_q,
		u"Features"_q));

	AddToggle(builder, {
		.key = Core::kQxShowDeletedKey,
		.title = QxPhrase(
			u"🗑️ Удалено"_q,
			u"🗑️ Deleted"_q),
		.id = u"qxgram/show-deleted"_q,
		.keywords = { u"deleted"_q, u"удалённые"_q, u"сообщения"_q },
		.onToggleChange = [](bool on) {
			if (!on) {
				QxDeleted::ReleaseAll();
			}
		},
	});

	AddToggle(builder, {
		.key = Core::kQxShowTtlKey,
		.title = QxPhrase(
			u"🔥 TTL"_q,
			u"🔥 TTL"_q),
		.id = u"qxgram/show-ttl"_q,
		.keywords = { u"ttl"_q, u"таймер"_q, u"self-destruct"_q },
	});

	AddToggle(builder, {
		.key = Core::kQxAllowScreenshotKey,
		.title = QxPhrase(
			u"📷 Разрешить скриншот"_q,
			u"📷 Allow screenshots"_q),
		.id = u"qxgram/allow-screenshot"_q,
		.keywords = { u"screenshot"_q, u"скриншот"_q, u"защита"_q },
	});

	AddToggle(builder, {
		.key = Core::kQxGhostModeKey,
		.title = QxPhrase(
			u"👻 Ghost"_q,
			u"👻 Ghost"_q),
		.id = u"qxgram/ghost-mode"_q,
		.keywords = { u"ghost"_q, u"призрак"_q, u"онлайн"_q },
	});

	builder.addSkip();
	builder.addDivider();
}

class QxGram : public Section<QxGram> {
public:
	QxGram(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

const auto kMeta = BuildHelper({
	.id = QxGram::Id(),
	.parentId = MainId(),
	.title = &tr::lng_settings_qxgram,
	.icon = &st::menuIconSettings,
}, [](SectionBuilder &builder) {
	BuildFeatures(builder);
});

const SectionBuildMethod kQxGramSection = kMeta.build;

QxGram::QxGram(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> QxGram::title() {
	return tr::lng_settings_qxgram();
}

void QxGram::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	build(content, kQxGramSection);

	Ui::ResizeFitChild(this, content);
}

} // namespace

rpl::producer<QString> QxPhrase(QString ru, QString en) {
	if (Lang::GetInstance().id().startsWith(u"ru")) {
		return rpl::single(std::move(ru));
	}
	return rpl::single(std::move(en));
}

Type QxGramId() {
	return QxGram::Id();
}

} // namespace Settings
