/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_navigation.h"

#include "lang/lang_keys.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "styles/style_dialogs.h"
#include "styles/style_menu_icons.h"
#include "window/themes/window_theme_glass.h"
#include "ui/effects/liquid_glass.h"

namespace Dialogs {
namespace {

class NavigationButton final : public Ui::AbstractButton {
public:
	NavigationButton(
		QWidget *parent,
		rpl::producer<QString> label,
		const style::icon &icon,
		rpl::producer<bool> primary)
	: AbstractButton(parent)
	, _icon(icon) {
		std::move(primary) | rpl::on_next([=](bool active) {
			_primary = active;
			update();
		}, lifetime());
		setFocusPolicy(Qt::StrongFocus);
		std::move(label) | rpl::on_next([=](const QString &text) {
			_text = text;
			setAccessibleName(text);
			update();
		}, lifetime());
		style::PaletteChanged() | rpl::on_next([=] {
			update();
		}, lifetime());
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		if (isOver() || isDown() || hasFocus()) {
			p.fillRect(rect(), st::dialogsBgOver);
		}
		const auto color = _primary
			? st::windowBgActive->c
			: st::windowSubTextFg->c;
		_icon.paintInCenter(
			p,
			QRect(0, st::dialogsNavigationIconTop, width(), _icon.height()),
			color);
		p.setFont(st::dialogsNavigationFont);
		p.setPen(color);
		p.drawText(
			QRect(0, st::dialogsNavigationTextTop, width(), height()
				- st::dialogsNavigationTextTop),
			Qt::AlignHCenter | Qt::AlignTop,
			st::dialogsNavigationFont->elided(_text, width()
				- 2 * st::dialogsNavigationLabelSkip));
	}

	void focusInEvent(QFocusEvent *e) override {
		AbstractButton::focusInEvent(e);
		update();
	}

	void focusOutEvent(QFocusEvent *e) override {
		AbstractButton::focusOutEvent(e);
		update();
	}

private:
	const style::icon &_icon;
	bool _primary = false;
	QString _text;

};

} // namespace

NavigationBar::NavigationBar(
	QWidget *parent,
	Fn<void(NavigationAction)> activate,
	Fn<QImage()> backdrop,
	Fn<void()> menu)
: RpWidget(parent) {
	resize(0, st::dialogsNavigationHeight);
	const auto add = [&](
			NavigationAction action,
			rpl::producer<QString> label,
			const style::icon &icon) {
		const auto button = Ui::CreateChild<NavigationButton>(
			this,
			std::move(label),
			icon,
			_active.value() | rpl::map([=](NavigationAction current) {
				return current == action;
			}));
		button->setClickedCallback([=] { activate(action); });
		if (action == NavigationAction::Settings && menu) {
			button->setContextMenuPolicy(Qt::CustomContextMenu);
			QObject::connect(button, &QWidget::customContextMenuRequested,
				button, [=] { menu(); });
		}
		sizeValue() | rpl::on_next([=](QSize size) {
			const auto index = int(action);
			const auto left = size.width() * index / 4;
			const auto right = size.width() * (index + 1) / 4;
			button->resize(right - left, size.height());
			button->moveToLeft(left, 0, size.width());
		}, button->lifetime());
	};
	add(NavigationAction::Contacts, tr::lng_menu_contacts(), st::macContactsIcon);
	add(NavigationAction::Calls, tr::lng_menu_calls(), st::macCallsIcon);
	add(NavigationAction::Chats, tr::lng_recent_chats(), st::macChatsIcon);
	add(NavigationAction::Settings, tr::lng_menu_settings(), st::macSettingsIcon);
	paintRequest() | rpl::on_next([=] {
		auto p = QPainter(this);
		const auto glass = Window::Theme::GlassStyle();
		PaintLiquidGlass(
			p,
			this->rect(),
			(glass.enabled && backdrop) ? backdrop() : QImage(),
			st::dialogsBg->c,
			glass,
			12);
		p.fillRect(0, 0, width(), st::lineWidth, st::shadowFg);
	}, lifetime());
	style::PaletteChanged() | rpl::on_next([=] { update(); }, lifetime());
	Window::Theme::GlassStyleValue() | rpl::on_next([=] { update(); }, lifetime());
}

void NavigationBar::setActive(NavigationAction action) {
	_active = action;
}

} // namespace Dialogs
