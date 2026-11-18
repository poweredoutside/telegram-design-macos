#include "settings/sections/settings_macos_general.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "settings/sections/settings_advanced.h"
#include "settings/sections/settings_calls.h"
#include "settings/sections/settings_shortcuts.h"
#include "settings/settings_common_session.h"
#include "settings/settings_power_saving.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/style/style_core_scale.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <rpl/then.h>
#include <QtGui/QPainterPath>

#include "styles/style_settings.h"

namespace Settings {
namespace {

class GeneralButton final : public Ui::SettingsButton {
public:
	GeneralButton(
		QWidget *parent,
		rpl::producer<QString> title,
		bool first,
		bool last,
		bool next);
	void setSelected(rpl::producer<bool> selected);
	[[nodiscard]] QAccessible::Role accessibilityRole() override;
	[[nodiscard]] Ui::AccessibilityState accessibilityState() const override;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const bool _first;
	const bool _last;
	const bool _next;
	bool _selectable = false;
	bool _selected = false;

};

class MacGeneral final : public Section<MacGeneral> {
public:
	MacGeneral(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	[[nodiscard]] rpl::producer<QString> title() override;
	bool paintOuter(
		not_null<QWidget*> outer,
		int maxVisibleHeight,
		QRect clip) override;

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void setupContent();
	void addHeader(rpl::producer<QString> title);
	[[nodiscard]] not_null<GeneralButton*> addButton(
		rpl::producer<QString> title,
		bool first = true,
		bool last = true,
		bool next = false);
	void addToggle(
		rpl::producer<QString> title,
		rpl::producer<bool> value,
		Fn<void(bool)> save,
		bool first = true,
		bool last = true);

	const not_null<Ui::VerticalLayout*> _content;

};

rpl::producer<QString> Localized(QString english, QString russian) {
	return rpl::single(rpl::empty) | rpl::then(
		Lang::GetInstance().updated()
	) | rpl::map([=] {
		return Lang::GetInstance().id().startsWith(u"ru"_q)
			? russian : english;
	});
}

GeneralButton::GeneralButton(
	QWidget *parent,
	rpl::producer<QString> title,
	bool first,
	bool last,
	bool next)
: SettingsButton(parent, std::move(title), st::macSettingsPageButtonNoIcon)
, _first(first)
, _last(last)
, _next(next) {
	if (_next) {
		auto padding = st::macSettingsPageButtonNoIcon.padding;
		padding.setRight(style::ConvertScale(35));
		setPaddingOverride(padding);
	}
}

void GeneralButton::setSelected(rpl::producer<bool> selected) {
	_selectable = true;
	auto padding = st::macSettingsPageButtonNoIcon.padding;
	padding.setRight(style::ConvertScale(35));
	setPaddingOverride(padding);
	std::move(selected) | rpl::on_next([=](bool value) {
		_selected = value;
		accessibilityStateChanged({ .checked = true });
		update();
	}, lifetime());
}

QAccessible::Role GeneralButton::accessibilityRole() {
	return _selectable
		? QAccessible::RadioButton
		: SettingsButton::accessibilityRole();
}

Ui::AccessibilityState GeneralButton::accessibilityState() const {
	return _selectable
		? Ui::AccessibilityState{ .checkable = true, .checked = _selected }
		: SettingsButton::accessibilityState();
}

void GeneralButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	p.setRenderHint(QPainter::Antialiasing);
	auto path = QPainterPath();
	path.setFillRule(Qt::WindingFill);
	const auto radius = st::macSettingsRadius;
	path.addRoundedRect(rect(), radius, radius);
	if (!_first) {
		path.addRect(0, 0, width(), radius);
	}
	if (!_last) {
		path.addRect(0, height() - radius, width(), radius);
	}
	p.setClipPath(path.simplified());
	const auto over = (isOver() || isDown()) && !isDisabled();
	paintBg(p, e->rect(), over);
	paintRipple(p, 0, 0);
	paintText(p, over, width());
	paintToggle(p, width());
	const auto inset = style::ConvertScale(14);
	if (_next) {
		auto color = st::windowSubTextFg->c;
		color.setAlphaF(.5);
		st::macSettingsNext.paint(
			p,
			QPoint(
				width() - inset - st::macSettingsNext.width(),
				(height() - st::macSettingsNext.height()) / 2),
			width(),
			color);
	} else if (_selected) {
		const auto x = width() - inset - style::ConvertScale(13);
		const auto y = (height() - style::ConvertScale(10)) / 2;
		auto check = QPainterPath();
		check.moveTo(x, y + style::ConvertScale(5));
		check.lineTo(x + style::ConvertScale(4), y + style::ConvertScale(9));
		check.lineTo(x + style::ConvertScale(12), y);
		p.setPen(QPen(st::windowBgActive, style::ConvertScale(2),
			Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.drawPath(check);
	}
	if (!_last) {
		p.fillRect(inset, height() - st::lineWidth,
			width() - 2 * inset, st::lineWidth, st::shadowFg);
	}
}

MacGeneral::MacGeneral(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _content(Ui::CreateChild<Ui::VerticalLayout>(this)) {
	setupContent();
	_content->heightValue() | rpl::on_next([=](int height) {
		resize(width(), height);
	}, lifetime());
}

rpl::producer<QString> MacGeneral::title() {
	return Localized(u"General"_q, u"Основные"_q);
}

bool MacGeneral::paintOuter(
	not_null<QWidget*> outer,
	int maxVisibleHeight,
	QRect clip) {
	auto p = QPainter(outer);
	p.fillRect(clip, st::boxSearchBg);
	return true;
}

int MacGeneral::resizeGetHeight(int newWidth) {
	const auto innerWidth = std::clamp(
		newWidth - 2 * st::macSettingsPageInset,
		0,
		style::ConvertScale(600));
	_content->resizeToWidth(innerWidth);
	_content->move((newWidth - innerWidth) / 2, 0);
	return _content->height();
}

void MacGeneral::addHeader(rpl::producer<QString> title) {
	Ui::AddSkip(_content, style::ConvertScale(20));
	_content->add(object_ptr<Ui::FlatLabel>(
		_content, std::move(title), st::macSettingsPageHeader),
		style::margins(style::ConvertScale(14), 0,
			style::ConvertScale(14), style::ConvertScale(5)));
}

not_null<GeneralButton*> MacGeneral::addButton(
	rpl::producer<QString> title,
	bool first,
	bool last,
	bool next) {
	return _content->add(object_ptr<GeneralButton>(
		_content, std::move(title), first, last, next));
}

void MacGeneral::addToggle(
	rpl::producer<QString> title,
	rpl::producer<bool> value,
	Fn<void(bool)> save,
	bool first,
	bool last) {
	const auto button = addButton(std::move(title), first, last);
	button->toggleOn(std::move(value));
	button->toggledChanges() | rpl::on_next([=](bool enabled) {
		save(enabled);
		Core::App().saveSettingsDelayed();
	}, button->lifetime());
}

void MacGeneral::setupContent() {
	const auto settings = &Core::App().settings();
	addHeader(Localized(u"POWER SAVING"_q, u"ЭНЕРГОСБЕРЕЖЕНИЕ"_q));
	addButton(Localized(u"Power Saving Mode"_q, u"Режим энергосбережения"_q),
		true, true, true
	)->setClickedCallback([=] {
		controller()->show(Box(PowerSavingBox, PowerSaving::Flags()));
	});

#ifndef TDESKTOP_DISABLE_SPELLCHECK
	addHeader(Localized(u"SPELLING AND GRAMMAR"_q, u"ПРАВОПИСАНИЕ"_q));
	addToggle(Localized(u"Check Spelling While Typing"_q,
		u"Проверять правописание при вводе"_q),
		settings->spellcheckerEnabledValue(), [=](bool enabled) {
			settings->setSpellcheckerEnabled(enabled);
		});
#endif

	addHeader(Localized(u"EMOJI"_q, u"ЭМОДЗИ"_q));
	addToggle(Localized(u"Replace Emoji Automatically"_q,
		u"Автоматически заменять эмодзи"_q),
		settings->replaceEmojiValue(), [=](bool enabled) {
			settings->setReplaceEmoji(enabled);
		}, true, false);
	addToggle(Localized(u"Suggest Emoji"_q, u"Подсказывать эмодзи"_q),
		rpl::single(settings->suggestEmoji()), [=](bool enabled) {
			settings->setSuggestEmoji(enabled);
		}, false, false);
	addToggle(tr::lng_settings_large_emoji(),
		settings->largeEmojiValue(), [=](bool enabled) {
			settings->setLargeEmoji(enabled);
		}, false, true);

	addHeader(Localized(u"SHORTCUTS"_q, u"СОЧЕТАНИЯ КЛАВИШ"_q));
	addButton(tr::lng_settings_shortcuts(), true, true, true
	)->setClickedCallback([=] { showOther(ShortcutsId()); });

	addHeader(Localized(u"INPUT SETTINGS"_q, u"НАСТРОЙКИ ВВОДА"_q));
	using Submit = Ui::InputSubmitSettings;
	const auto addSubmit = [&](Submit value, rpl::producer<QString> title,
			bool first, bool last) {
		const auto button = addButton(std::move(title), first, last);
		button->setSelected(settings->sendSubmitWayValue()
			| rpl::map([=](Submit current) { return current == value; }));
		button->setClickedCallback([=] {
			settings->setSendSubmitWay(value);
			Core::App().saveSettingsDelayed();
		});
	};
	addSubmit(Submit::Enter, tr::lng_settings_send_enter(), true, false);
	addSubmit(Submit::CtrlEnter, tr::lng_settings_send_ctrlenter(), false, true);

	addHeader(Localized(u"CALL SETTINGS"_q, u"НАСТРОЙКИ ЗВОНКОВ"_q));
	addButton(Localized(u"Call Settings"_q, u"Настройки звонков"_q),
		true, true, true
	)->setClickedCallback([=] { showOther(CallsId()); });

	addHeader(Localized(u"ADVANCED"_q, u"ДОПОЛНИТЕЛЬНО"_q));
	addButton(tr::lng_settings_advanced(), true, true, true
	)->setClickedCallback([=] { showOther(AdvancedId()); });
	Ui::AddSkip(_content, style::ConvertScale(20));
}

}

Type MacGeneralId() {
	return MacGeneral::Id();
}

}
