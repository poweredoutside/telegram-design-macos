#include "settings/sections/settings_sidebar.h"

#include "boxes/about_box.h"
#include "boxes/language_box.h"
#include "boxes/star_gift_box.h"
#include "boxes/stickers_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/application.h"
#include "data/components/credits.h"
#include "data/data_credits.h"
#include "data/data_user.h"
#include "info/info_memento.h"
#include "info/profile/info_profile_values.h"
#include "info/stories/info_stories_widget.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "lang/lang_tag.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "mtproto/mtproto_dc_options.h"
#include "settings/sections/settings_active_sessions.h"
#include "settings/sections/settings_advanced.h"
#include "settings/sections/settings_business.h"
#include "settings/sections/settings_chat.h"
#include "settings/sections/settings_credits.h"
#include "settings/sections/settings_folders.h"
#include "settings/sections/settings_information.h"
#include "settings/sections/settings_main.h"
#include "settings/sections/settings_macos_general.h"
#include "settings/sections/settings_notifications.h"
#include "settings/sections/settings_premium.h"
#include "settings/sections/settings_privacy_security.h"
#include "settings/sections/settings_telega.h"
#include "ui/boxes/peer_qr_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/style/style_core_scale.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <rpl/combine.h>
#include <rpl/merge.h>
#include <rpl/then.h>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>

#include "styles/style_dialogs.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

class SidebarAction final : public Ui::AbstractButton {
public:
	SidebarAction(
		QWidget *parent,
		rpl::producer<QString> text,
		const style::icon *icon = nullptr);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	QString _text;
	const style::icon *_icon = nullptr;

};

SidebarAction::SidebarAction(
	QWidget *parent,
	rpl::producer<QString> text,
	const style::icon *icon)
: AbstractButton(parent)
, _icon(icon) {
	resize(0, st::macSettingsRowHeight);
	setFocusPolicy(Qt::StrongFocus);
	std::move(text) | rpl::on_next([=](QString value) {
		_text = std::move(value);
		setAccessibleName(_text);
		update();
	}, lifetime());
	style::PaletteChanged() | rpl::on_next([=] { update(); }, lifetime());
}

void SidebarAction::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto inset = st::macSettingsInset;
	if (_icon && (isOver() || isDown() || hasFocus())) {
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(Qt::NoPen);
		p.setBrush(st::dialogsBgOver);
		p.drawRoundedRect(rect().adjusted(inset, 0, -inset, 0),
			st::macSettingsRadius, st::macSettingsRadius);
	}
	if (_icon) {
		_icon->paint(p, inset,
			(height() - _icon->height()) / 2, width());
	}
	const auto left = _icon
		? inset + style::ConvertScale(35)
		: inset + style::ConvertScale(14);
	const auto font = _icon ? st::macSettingsFont : st::macSettingsVersionFont;
	const auto available = std::max(0, width() - left - inset);
	p.setFont(font);
	p.setPen(_icon ? st::windowBgActive : st::windowSubTextFg);
	p.drawText(QRect(left, 0, available, height()),
		Qt::AlignLeft | Qt::AlignVCenter, font->elided(_text, available));
}

class SidebarProfile final : public Ui::AbstractButton {
public:
	SidebarProfile(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		rpl::producer<bool> selected);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	object_ptr<Ui::UserpicButton> _userpic;
	QString _name;
	QString _phone;
	QString _username;
	bool _selected = false;

};

class SidebarRow final : public Ui::AbstractButton {
public:
	SidebarRow(
		QWidget *parent,
		rpl::producer<QString> title,
		const style::icon &icon,
		QColor color,
		QString asset,
		rpl::producer<bool> selected,
		rpl::producer<QString> label,
		bool labelBadge);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const style::icon &_icon;
	QColor _color;
	QImage _image;
	QImage _activeImage;
	QString _text;
	QString _label;
	bool _premium = false;
	bool _keepIcon = false;
	bool _labelBadge = false;
	bool _selected = false;

};

SidebarProfile::SidebarProfile(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	rpl::producer<bool> selected)
: AbstractButton(parent)
, _userpic(this,
	controller,
	controller->session().user(),
	Ui::UserpicButton::Role::OpenPhoto,
	Ui::UserpicButton::Source::PeerPhoto,
	st::macSettingsProfilePhoto) {
	resize(0, st::macSettingsProfileHeight);
	setFocusPolicy(Qt::StrongFocus);
	_userpic->showSavedMessagesOnSelf(false);
	std::move(selected) | rpl::on_next([=](bool value) {
		_selected = value;
		update();
	}, lifetime());
	const auto user = controller->session().user();
	Info::Profile::NameValue(user) | rpl::on_next([=](QString name) {
		_name = std::move(name);
		setAccessibleName(_name);
		update();
	}, lifetime());
	rpl::combine(
		Info::Profile::PhoneValue(user),
		user->session().settings().phoneNumberHiddenValue()
	) | rpl::on_next([=](const TextWithEntities &phone, bool hidden) {
		_phone = hidden
			? QString(phone.text.size(), QChar(0x2022))
			: phone.text;
		update();
	}, lifetime());
	Info::Profile::UsernameValue(user) | rpl::on_next([=](
			const TextWithEntities &username) {
		_username = username.text;
		update();
	}, lifetime());
	sizeValue() | rpl::on_next([=](QSize size) {
		_userpic->moveToLeft(
			st::macSettingsProfilePhotoLeft,
			(size.height() - _userpic->height()) / 2,
			size.width());
	}, lifetime());
	style::PaletteChanged() | rpl::on_next([=] { update(); }, lifetime());
}

void SidebarProfile::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(_selected
		? st::dialogsBgActive
		: (isOver() || isDown() || hasFocus())
		? st::dialogsBgOver
		: st::windowBg);
	p.drawRoundedRect(rect().adjusted(
		st::macSettingsInset, 0, -st::macSettingsInset, 0),
		st::macSettingsRadius, st::macSettingsRadius);
	const auto left = st::macSettingsProfileTextLeft;
	const auto available = std::max(0,
		width() - left - st::macSettingsInset - style::ConvertScale(35));
	const auto gap = style::ConvertScale(2);
	const auto lines = int(!_phone.isEmpty()) + int(!_username.isEmpty());
	const auto blockHeight = st::dialogsNameFont->height
		+ (lines ? gap + lines * st::dialogsTextFont->height : 0);
	auto top = (height() - blockHeight) / 2;
	p.setFont(st::dialogsNameFont);
	p.setPen(_selected ? st::dialogsNameFgActive : st::windowFg);
	p.drawText(QRect(left, top, available, st::dialogsNameFont->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		st::dialogsNameFont->elided(_name, available));
	top += st::dialogsNameFont->height + gap;
	p.setFont(st::dialogsTextFont);
	p.setPen(_selected ? st::dialogsNameFgActive : st::windowSubTextFg);
	for (const auto &text : { _phone, _username }) {
		if (!text.isEmpty()) {
			p.drawText(QRect(left, top, available, st::dialogsTextFont->height),
				Qt::AlignLeft | Qt::AlignVCenter,
				st::dialogsTextFont->elided(text, available));
			top += st::dialogsTextFont->height;
		}
	}
	if (!_selected) {
		auto color = st::menuIconFg->c;
		color.setAlphaF(.5);
		st::macSettingsNext.paint(
			p,
			QPoint(
				width() - st::macSettingsInset - style::ConvertScale(15)
					- st::macSettingsNext.width(),
				(height() - st::macSettingsNext.height()) / 2),
			width(),
			color);
	}
}

SidebarRow::SidebarRow(
	QWidget *parent,
	rpl::producer<QString> title,
	const style::icon &icon,
	QColor color,
	QString asset,
	rpl::producer<bool> selected,
	rpl::producer<QString> label,
	bool labelBadge)
: AbstractButton(parent)
, _icon(icon)
, _color(color)
, _image(u":/gui/icons/macos/native-"_q + asset + u".png"_q)
, _premium(asset == u"Icon_Premium_Settings"_q)
, _keepIcon(asset == u"Icon_Settings_Stars"_q
	|| asset == u"Icon_Settings_GiftPremium"_q)
, _labelBadge(labelBadge) {
	resize(0, st::macSettingsRowHeight);
	setFocusPolicy(Qt::StrongFocus);
	_activeImage = _image;
	if (!_activeImage.isNull()) {
		auto p = QPainter(&_activeImage);
		p.setCompositionMode(QPainter::CompositionMode_SourceIn);
		p.fillRect(_activeImage.rect(), Qt::white);
	}
	std::move(title) | rpl::on_next([=](QString text) {
		_text = text;
		setAccessibleName(text);
		update();
	}, lifetime());
	std::move(selected) | rpl::on_next([=](bool value) {
		_selected = value;
		update();
	}, lifetime());
	std::move(label) | rpl::on_next([=](QString text) {
		_label = std::move(text);
		setAccessibleDescription(_label);
		update();
	}, lifetime());
	style::PaletteChanged() | rpl::on_next([=] { update(); }, lifetime());
}

void SidebarRow::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto s = [](int value) { return style::ConvertScale(value); };
	const auto iconRect = QRect(
		st::macSettingsIconLeft,
		(height() - st::macSettingsIconSize) / 2,
		st::macSettingsIconSize,
		st::macSettingsIconSize);
	const auto area = rect().adjusted(
		st::macSettingsInset, 0, -st::macSettingsInset, 0);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	if (_selected || isOver() || isDown() || hasFocus()) {
		p.setBrush(_selected ? st::dialogsBgActive : st::dialogsBgOver);
		p.drawRoundedRect(area, st::macSettingsRadius, st::macSettingsRadius);
	}
	if (_image.isNull()) {
		p.setBrush(_color);
		p.drawRoundedRect(iconRect, s(7), s(7));
		_icon.paintInCenter(p, iconRect, Qt::white);
	} else if (_premium) {
		auto gradient = QLinearGradient(iconRect.topLeft(), iconRect.topRight());
		gradient.setColorAt(0., QColor(0x6b, 0x93, 0xff));
		gradient.setColorAt(0.5, QColor(0x97, 0x6f, 0xff));
		gradient.setColorAt(1., QColor(0xe4, 0x6a, 0xce));
		p.setBrush(gradient);
		p.drawRoundedRect(iconRect, s(5), s(5));
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.drawImage(iconRect.adjusted(s(4), s(4), -s(4), -s(4)), _activeImage);
	} else {
		p.fillRect(iconRect.adjusted(s(3), s(3), -s(3), -s(3)),
			(_selected && !_keepIcon)
				? st::dialogsBgActive->c : QColor(Qt::white));
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.drawImage(iconRect, (_selected && !_keepIcon) ? _activeImage : _image);
	}
	const auto chevronWidth = _selected ? 0 : st::macSettingsNext.width();
	if (!_selected) {
		auto color = st::windowSubTextFg->c;
		color.setAlphaF(.5);
		st::macSettingsNext.paint(
			p,
			QPoint(
				width() - st::macSettingsInset - s(14) - chevronWidth,
				(height() - st::macSettingsNext.height()) / 2),
			width(),
			color);
	}
	p.setFont(st::macSettingsFont);
	const auto right = width() - st::macSettingsInset - s(14)
		- (chevronWidth ? chevronWidth + s(6) : 0);
	const auto available = std::max(0, right - st::macSettingsTextLeft);
	if (_labelBadge && !_label.isEmpty()) {
		const auto badgeFont = st::macSettingsVersionFont;
		const auto badgeWidth = badgeFont->width(_label) + s(12);
		const auto badgeHeight = badgeFont->height + s(4);
		const auto gap = s(6);
		const auto hasRoom = available > badgeWidth + gap + s(12);
		const auto textWidth = hasRoom ? available - badgeWidth - gap : available;
		const auto title = st::macSettingsFont->elided(_text, textWidth);
		p.setPen(st::windowFg);
		p.drawText(QRect(st::macSettingsTextLeft, 0, textWidth, height()),
			Qt::AlignVCenter | Qt::AlignLeft, title);
		if (hasRoom) {
			const auto badge = QRect(
				st::macSettingsTextLeft + st::macSettingsFont->width(title) + gap,
				(height() - badgeHeight) / 2,
				badgeWidth,
				badgeHeight);
			auto color = st::windowActiveTextFg->c;
			color.setAlphaF(.15);
			p.setPen(Qt::NoPen);
			p.setBrush(color);
			p.drawRoundedRect(badge, s(5), s(5));
			p.setFont(badgeFont);
			p.setPen(st::windowActiveTextFg);
			p.drawText(badge, Qt::AlignCenter, _label);
		}
		return;
	}
	const auto label = st::macSettingsFont->elided(_label, available / 2);
	const auto labelWidth = st::macSettingsFont->width(label);
	if (!label.isEmpty()) {
		p.setPen(_selected ? st::dialogsNameFgActive : st::windowSubTextFg);
		p.drawText(QRect(right - labelWidth, 0, labelWidth, height()),
			Qt::AlignVCenter | Qt::AlignLeft, label);
	}
	p.setPen(_selected ? st::dialogsNameFgActive : st::windowFg);
	const auto textWidth = std::max(0, available
		- (labelWidth ? labelWidth + s(10) : 0));
	p.drawText(QRect(st::macSettingsTextLeft, 0, textWidth, height()),
		Qt::AlignVCenter | Qt::AlignLeft,
		st::macSettingsFont->elided(_text, textWidth));
}
} // namespace

Sidebar::Sidebar(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	Fn<void()> menu,
	Fn<void()> repaintBackdrop)
: RpWidget(parent)
, _selected(InformationId()) {
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(this, st::defaultScrollArea);
	const auto content = scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(scroll));
	scroll->scrolls() | rpl::on_next(repaintBackdrop, lifetime());
	content->heightValue() | rpl::skip(1) | rpl::on_next(
		repaintBackdrop, lifetime());
	const auto search = Ui::CreateChild<Ui::InputField>(
		this, st::macSettingsSearch, tr::lng_dlg_filter());
	const auto searchIcon = Ui::CreateChild<Ui::IconButton>(
		search, st::macSettingsSearchIcon);
	searchIcon->setAccessibleName(tr::lng_dlg_filter(tr::now));
	searchIcon->setClickedCallback([=] { search->setFocusFast(); });
	const auto query = lifetime().make_state<rpl::variable<QString>>();
	search->changes() | rpl::on_next([=] {
		*query = search->getLastText().trimmed();
		scroll->scrollToY(0);
	}, lifetime());
	const auto show = [=](Type type) {
		_selected = type;
		auto params = Window::SectionShow(Window::SectionShow::Way::ClearStack);
		params.forbidLayer = true;
		controller->showSettings(type, params);
	};
	const auto profile = content->add(object_ptr<Ui::SlideWrap<SidebarProfile>>(
		content, object_ptr<SidebarProfile>(content, controller,
			_selected.value() | rpl::map([](Type type) {
				return type == InformationId();
			}))));
	profile->entity()->setClickedCallback([=] { show(InformationId()); });
	profile->toggleOn(query->value() | rpl::map([](const QString &text) {
		return text.isEmpty();
	}), anim::type::instant);
	const auto skip = [&](int height) {
		const auto wrap = content->add(Ui::CreateSlideSkipWidget(
			content, style::ConvertScale(height)));
		wrap->toggleOn(query->value() | rpl::map([](const QString &text) {
			return text.isEmpty();
		}), anim::type::instant);
	};
	const auto addAccount = content->add(
		object_ptr<Ui::SlideWrap<SidebarAction>>(
			content, object_ptr<SidebarAction>(content,
				tr::lng_menu_add_account(), &st::macSettingsAddAccount)));
	auto accountChanges = rpl::single(rpl::empty) | rpl::then(
		rpl::merge(controller->session().domain().accountsChanges(),
			controller->session().domain().maxAccountsChanges()
				| rpl::map([] { return rpl::empty; })));
	auto canAddAccount = std::move(accountChanges) | rpl::map([=] {
		const auto &domain = controller->session().domain();
		return !controller->session().supportMode()
			&& domain.accounts().size() < domain.maxAccounts();
	});
	addAccount->toggleOn(rpl::combine(query->value(), std::move(canAddAccount))
		| rpl::map([](const QString &text, bool allowed) {
			return text.isEmpty() && allowed;
		}), anim::type::instant);
	addAccount->entity()->setClickedCallback([=] {
		controller->window().preventOrInvoke([=] {
			Core::App().setActivePrimaryWindow(&controller->window());
			controller->session().domain().addActivated(MTP::Environment{});
		});
	});
	const auto add = [&](Type type, rpl::producer<QString> title,
			const style::icon &icon, QColor color, QString asset,
			Fn<void()> action = nullptr,
			rpl::producer<QString> label = rpl::single(QString()),
			bool labelBadge = false) {
		const auto wrap = content->add(object_ptr<Ui::SlideWrap<SidebarRow>>(
			content, object_ptr<SidebarRow>(content,
				rpl::duplicate(title), icon, color, std::move(asset),
				_selected.value() | rpl::map([=](Type selected) {
					return type && type == selected;
				}), std::move(label), labelBadge)));
		wrap->toggleOn(rpl::combine(std::move(title), query->value())
			| rpl::map([](const QString &title, const QString &query) {
				if (query.isEmpty()) {
					return true;
				}
				const auto words = query.split(QChar(' '), Qt::SkipEmptyParts);
				return ranges::all_of(words, [&](const QString &word) {
					return title.contains(word, Qt::CaseInsensitive);
				});
			}), anim::type::instant);
		wrap->entity()->setClickedCallback([=] {
			if (action) {
				action();
			} else {
				show(type);
			}
		});
	};
	skip(10);
	add(nullptr, tr::lng_menu_my_profile(), st::menuIconProfile,
		QColor(0, 122, 255), u"Icon_SettingsStories"_q, [=] {
			auto params = Window::SectionShow();
			params.forbidLayer = true;
			controller->showSection(
				Info::Stories::MakeMyProfile(controller->session().user()),
				params);
		});
	skip(20);
	const auto languageUpdates = [] {
		return rpl::single(rpl::empty) | rpl::then(
			Lang::GetInstance().updated());
	};
	const auto localized = [=](QString english, QString russian) {
		return languageUpdates() | rpl::map([=] {
			return Lang::GetInstance().id().startsWith(u"ru"_q)
				? russian : english;
		});
	};
	add(TelegaId(), languageUpdates() | rpl::map(TelegaSettingsLabel),
		st::macSettingsIcon, QColor(0, 122, 255), u"Icon_SettingsGeneral"_q);
	add(nullptr, languageUpdates() | rpl::map(TelegaAiChatLabel),
		st::menuIconChatBubble, QColor(175, 82, 222), QString(), [=] {
			ShowTelegaAiChat(controller);
		});
	skip(20);
	add(MacGeneralId(), localized(u"General"_q, u"Основные"_q), st::macSettingsIcon,
		QColor(142, 142, 147), u"Icon_SettingsGeneral"_q);
	add(NotificationsId(), tr::lng_settings_section_notify(), st::menuIconNotifications,
		QColor(255, 69, 58), u"Icon_SettingsNotifications"_q);
	add(PrivacySecurityId(), tr::lng_settings_section_privacy(), st::menuIconLock,
		QColor(142, 142, 147), u"Icon_SettingsSecurity"_q);
	add(DataStorageId(), tr::lng_settings_data_storage(), st::menuIconStorage,
		QColor(175, 82, 222), u"Icon_SettingsStorage"_q);
	add(SessionsId(), tr::lng_settings_sessions_title(), st::menuIconDevices,
		QColor(0, 122, 255), u"Icon_PrivacySettings_ActiveSessions"_q);
	add(ChatId(), localized(u"Appearance"_q, u"Оформление"_q), st::menuIconChatBubble,
		QColor(48, 176, 199), u"Icon_AppearanceSettings"_q);
	add(nullptr, tr::lng_settings_language(), st::menuIconChatBubble,
		QColor(175, 82, 222), u"Icon_SettingsLanguage"_q, [=] {
			static auto Guard = base::binary_guard();
			Guard = LanguageBox::Show(controller);
		}, rpl::single(rpl::empty) | rpl::then(
			Lang::GetInstance().updated()
		) | rpl::map([] { return Lang::GetInstance().nativeName(); }));
	add(nullptr, tr::lng_settings_stickers_emoji(), st::menuIconStickers,
		QColor(255, 149, 0), u"Icon_SettingsStickers"_q, [=] {
			controller->show(Box<StickersBox>(
				controller->uiShow(), StickersBox::Section::Installed));
		});
	add(FoldersId(), tr::lng_settings_section_filters(), st::macChatsIcon,
		QColor(0, 122, 255), u"Icon_SettingsFilters"_q);
	if (controller->session().premiumPossible()) {
		skip(20);
		add(PremiumId(), tr::lng_premium_summary_title(), st::menuIconPremium,
			QColor(142, 92, 246), u"Icon_Premium_Settings"_q, [=] {
				controller->setPremiumRef("settings");
				show(PremiumId());
			});
		controller->session().credits().load();
		add(CreditsId(), tr::lng_settings_credits(), st::menuIconPremium,
			QColor(255, 179, 0), u"Icon_Settings_Stars"_q, [=] {
				controller->setPremiumRef("settings");
				show(CreditsId());
			}, controller->session().credits().balanceValue()
				| rpl::map([](CreditsAmount amount) {
					return amount
						? Lang::FormatCreditsAmountToShort(amount).string
						: QString();
				}));
		add(BusinessId(), tr::lng_business_title(), st::menuIconShop,
			QColor(142, 92, 246), u"Icon_SettingsBusiness"_q);
		if (controller->session().premiumCanBuy()) {
			add(nullptr, tr::lng_settings_gift_premium(), st::menuIconGiftPremium,
				QColor(142, 92, 246), u"Icon_Settings_GiftPremium"_q, [=] {
					Ui::ChooseStarGiftRecipient(controller);
				});
		}
	}
	skip(20);
	add(nullptr, tr::lng_settings_faq(), st::menuIconFaq,
		QColor(0, 122, 255), u"Icon_SettingsFaq"_q, [=] {
			OpenFaq(controller);
		});
	add(nullptr, tr::lng_settings_ask_question(), st::menuIconDiscussion,
		QColor(0, 122, 255), u"Icon_SettingsAskQuestion"_q, [=] {
			OpenAskQuestionConfirm(controller);
		});
	skip(20);
	const auto version = currentVersionText();
	const auto versionRow = content->add(
		object_ptr<Ui::SlideWrap<SidebarAction>>(
			content, object_ptr<SidebarAction>(content, rpl::single(version))));
	versionRow->toggleOn(query->value() | rpl::map([](const QString &text) {
		return text.isEmpty();
	}), anim::type::instant);
	versionRow->entity()->setClickedCallback([=] {
		QGuiApplication::clipboard()->setText(version);
	});
	Ui::AddSkip(content, st::dialogsNavigationHeight);
	const auto edit = Ui::CreateChild<Ui::LinkButton>(
		this, tr::lng_theme_edit(tr::now), st::macSettingsEdit);
	edit->setClickedCallback([=] { show(InformationId()); });
	const auto qr = Ui::CreateChild<Ui::IconButton>(this, st::macSettingsQr);
	qr->setAccessibleName(tr::lng_group_invite_context_qr(tr::now));
	qr->setClickedCallback([=] {
		Ui::DefaultShowFillPeerQrBoxCallback(
			controller->uiShow(), controller->session().user());
	});
	setContextMenuPolicy(Qt::CustomContextMenu);
	QObject::connect(this, &QWidget::customContextMenuRequested, this,
		[menu = std::move(menu)] { menu(); });
	const auto position = [=] {
		const auto header = st::macSettingsHeaderHeight;
		edit->moveToRight(style::ConvertScale(10),
			(st::macSettingsStatusHeight - edit->height()) / 2);
		qr->moveToLeft(style::ConvertScale(10),
			(st::macSettingsStatusHeight - qr->height()) / 2);
		search->setGeometryToLeft(style::ConvertScale(10),
			st::macSettingsStatusHeight + style::ConvertScale(10),
			std::max(1, width() - style::ConvertScale(20)),
			st::macSettingsSearch.heightMin);
		scroll->setGeometry(0, header, width(), std::max(0, height() - header));
		content->resizeToWidth(width());
	};
	sizeValue() | rpl::on_next(position, lifetime());
	Lang::GetInstance().updated() | rpl::on_next([=] {
		edit->setText(tr::lng_theme_edit(tr::now));
		position();
		update();
	}, lifetime());
	paintRequest() | rpl::on_next([=] {
		auto p = QPainter(this);
		const auto inset = style::ConvertScale(50);
		p.fillRect(rect(), st::boxSearchBg);
		p.setFont(st::dialogsNameFont);
		p.setPen(st::windowFg);
		p.drawText(QRect(inset, 0, std::max(0, width() - 2 * inset),
			st::macSettingsStatusHeight), Qt::AlignCenter,
			tr::lng_menu_settings(tr::now));
		p.fillRect(width() - st::lineWidth, 0, st::lineWidth, height(), st::shadowFg);
	}, lifetime());
	style::PaletteChanged() | rpl::on_next([=] { update(); }, lifetime());
}

} // namespace Settings
