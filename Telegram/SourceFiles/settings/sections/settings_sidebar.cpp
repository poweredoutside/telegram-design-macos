#include "settings/sections/settings_sidebar.h"

#include "settings/sections/settings_advanced.h"
#include "settings/sections/settings_chat.h"
#include "settings/sections/settings_folders.h"
#include "settings/sections/settings_information.h"
#include "settings/sections/settings_main.h"
#include "settings/sections/settings_notifications.h"
#include "settings/sections/settings_privacy_security.h"
#include "lang/lang_keys.h"
#include "lang/lang_instance.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/style/style_core_scale.h"
#include "window/window_session_controller.h"
#include "styles/style_dialogs.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <rpl/then.h>
#include <QtGui/QImage>

namespace Settings {
namespace {

class SidebarRow final : public Ui::AbstractButton {
public:
	SidebarRow(
		QWidget *parent,
		rpl::producer<QString> title,
		const style::icon &icon,
		QColor color,
		QString asset,
		rpl::producer<bool> selected)
	: AbstractButton(parent)
	, _icon(icon)
	, _color(color)
	, _image(u":/gui/icons/macos/native-"_q + asset + u".png"_q) {
		resize(0, style::ConvertScale(48));
		setFocusPolicy(Qt::StrongFocus);
		std::move(title) | rpl::on_next([=](QString text) {
			_text = text;
			setAccessibleName(text);
			update();
		}, lifetime());
		std::move(selected) | rpl::on_next([=](bool value) {
			_selected = value;
			update();
		}, lifetime());
		style::PaletteChanged() | rpl::on_next([=] { update(); }, lifetime());
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		const auto s = [](int value) { return style::ConvertScale(value); };
		const auto iconRect = QRect(s(20), s(10), s(28), s(28));
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(Qt::NoPen);
		if (_selected || isOver() || isDown() || hasFocus()) {
			p.setBrush(_selected ? st::dialogsBgActive : st::dialogsBgOver);
			p.drawRoundedRect(rect().adjusted(s(8), s(2), -s(8), -s(2)), s(9), s(9));
		}
		if (_image.isNull()) {
			p.setBrush(_color);
			p.drawRoundedRect(iconRect, s(7), s(7));
			_icon.paintInCenter(p, iconRect, Qt::white);
		} else {
			p.setBrush(Qt::white);
			p.drawRoundedRect(iconRect, s(7), s(7));
			p.setRenderHint(QPainter::SmoothPixmapTransform);
			p.drawImage(iconRect, _image);
		}
		p.setFont(st::normalFont);
		p.setPen(_selected ? st::dialogsNameFgActive : st::windowFg);
		p.drawText(QRect(s(60), 0, width() - s(78), height()),
			Qt::AlignVCenter | Qt::AlignLeft,
			st::normalFont->elided(_text, width() - s(78)));
	}

private:
	const style::icon &_icon;
	QColor _color;
	QImage _image;
	QString _text;
	bool _selected = false;

};

} // namespace

Sidebar::Sidebar(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	Fn<void()> menu)
: RpWidget(parent)
, _selected(InformationId()) {
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(this, st::defaultScrollArea);
	const auto content = scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(scroll));
	Ui::AddSkip(content, style::ConvertScale(12));
	const auto add = [&](Type type, rpl::producer<QString> title,
			const style::icon &icon, QColor color, QString asset) {
		const auto row = content->add(object_ptr<SidebarRow>(content,
			std::move(title), icon, color, std::move(asset),
			_selected.value() | rpl::map([=](Type selected) {
				return type == selected;
			})));
		row->setClickedCallback([=] {
			_selected = type;
			auto params = Window::SectionShow(Window::SectionShow::Way::ClearStack);
			params.forbidLayer = true;
			controller->showSettings(type, params);
		});
	};
	add(InformationId(), tr::lng_settings_information(), st::menuIconUserShow,
		QColor(0, 122, 255), u"Icon_SettingsProfile"_q);
	Ui::AddSkip(content, style::ConvertScale(16));
	add(NotificationsId(), tr::lng_settings_section_notify(), st::menuIconNotifications,
		QColor(255, 69, 58), u"Icon_SettingsNotifications"_q);
	add(PrivacySecurityId(), tr::lng_settings_section_privacy(), st::menuIconLock,
		QColor(142, 142, 147), u"Icon_SettingsSecurity"_q);
	auto appearance = rpl::single(rpl::empty) | rpl::then(
		Lang::GetInstance().updated()
	) | rpl::map([] {
		return Lang::GetInstance().id().startsWith(u"ru"_q)
			? u"Оформление"_q
			: u"Appearance"_q;
	});
	add(ChatId(), std::move(appearance), st::menuIconChatBubble,
		QColor(48, 176, 199), u"Icon_AppearanceSettings"_q);
	add(FoldersId(), tr::lng_settings_section_filters(), st::macChatsIcon,
		QColor(0, 122, 255), u"Icon_SettingsFilters"_q);
	add(AdvancedId(), tr::lng_settings_advanced(), st::menuIconStorage,
		QColor(175, 82, 222), u"Icon_SettingsStorage"_q);
	Ui::AddSkip(content, style::ConvertScale(16));
	add(MainId(), tr::lng_menu_settings(), st::macSettingsIcon,
		QColor(142, 142, 147), u"Icon_SettingsGeneral"_q);
	const auto more = Ui::CreateChild<Ui::IconButton>(this, st::macMore);
	more->setAccessibleName(tr::lng_main_menu(tr::now));
	more->setClickedCallback(std::move(menu));
	sizeValue() | rpl::on_next([=](QSize size) {
		const auto top = style::ConvertScale(52);
		more->moveToRight(style::ConvertScale(10), style::ConvertScale(6));
		scroll->setGeometry(0, top, size.width(), std::max(0, size.height() - top));
		content->resizeToWidth(size.width());
	}, lifetime());
	paintRequest() | rpl::on_next([=] {
		auto p = QPainter(this);
		const auto s = [](int value) { return style::ConvertScale(value); };
		p.fillRect(rect(), st::dialogsBg);
		p.setFont(st::semiboldFont);
		p.setPen(st::windowFg);
		p.drawText(QRect(s(20), 0, width() - s(80), s(52)),
			Qt::AlignVCenter, tr::lng_menu_settings(tr::now));
		p.fillRect(width() - 1, 0, 1, height(), st::shadowFg);
	}, lifetime());
	style::PaletteChanged() | rpl::on_next([=] { update(); }, lifetime());
}

} // namespace Settings
