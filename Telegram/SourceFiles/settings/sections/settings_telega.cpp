#include "settings/sections/settings_telega.h"

#include "api/api_gemini_chat.h"
#include "base/options.h"
#include "base/platform/base_platform_info.h"
#include "boxes/download_path_box.h"
#include "boxes/translate_box.h"
#include "chat_helpers/stickers_list_widget.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/core_telega_material.h"
#include "core/core_telega_preferences.h"
#include "core/file_utilities.h"
#include "core/update_checker.h"
#include "history/view/history_view_quick_action.h"
#include "history/view/history_view_context_menu.h"
#include "data/data_message_archive.h"
#include "data/data_session.h"
#include "history/view/history_view_message.h"
#include "info/profile/info_profile_actions.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "platform/platform_specific.h"
#include "settings.h"
#include "settings/sections/settings_advanced.h"
#include "settings/sections/settings_chat.h"
#include "settings/sections/settings_folders.h"
#include "settings/sections/settings_privacy_security.h"
#include "settings/settings_common_session.h"
#include "spellcheck/platform/platform_language.h"
#include "telega_version.h"
#include "ui/boxes/choose_language_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/chat_style_radius.h"
#include "ui/chat/chat_theme.h"
#include "ui/controls/compose_ai_button_factory.h"
#include "ui/controls/send_button.h"
#include "ui/widgets/buttons.h"
#include "ui/painter.h"
#include "ui/layers/generic_box.h"
#include "ui/new_badges.h"
#include "ui/search_field_controller.h"
#include "ui/style/style_core_font.h"
#include "ui/style/style_material.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/chat_filters_tabs_mode.h"
#include "ui/widgets/color_editor.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/masked_input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/vertical_layout_reorder.h"
#include "window/main_window.h"
#include "window/window_application_icon.h"
#include "window/notifications_manager.h"
#include "window/section_memento.h"
#include "window/section_widget.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_theme_live_wallpaper.h"
#include "window/themes/window_theme_local.h"
#include "window/themes/window_theme_preview.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"

#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#ifdef Q_OS_WIN
#include "platform/win/bundled_tg_ws_proxy.h"
#include "platform/win/windows_title_controls.h"
#endif

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QTime>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QDesktopServices>
#include <QtGui/QIntValidator>
#include <QtGui/QKeyEvent>
#include <QtGui/QTextLayout>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QVBoxLayout>

namespace Settings {
namespace {

namespace Theme = Window::Theme;
namespace Gemini = Api::GeminiChat;

QString Text(const QString &russian, const QString &english) {
	return Lang::GetInstance().id().startsWith(u"ru"_q) ? russian : english;
}

void ThemeQtWidget(not_null<Ui::RpWidget*> widget) {
	const auto apply = [=] {
		auto palette = widget->palette();
		palette.setColor(QPalette::Window, st::windowBg->c);
		palette.setColor(QPalette::Base, st::windowBgOver->c);
		palette.setColor(QPalette::Button, st::windowBgOver->c);
		palette.setColor(QPalette::WindowText, st::windowFg->c);
		palette.setColor(QPalette::Text, st::windowFg->c);
		palette.setColor(QPalette::PlaceholderText, st::windowSubTextFg->c);
		palette.setColor(QPalette::ButtonText, st::windowFg->c);
		palette.setColor(QPalette::Highlight, st::windowActiveTextFg->c);
		palette.setColor(QPalette::HighlightedText, st::windowBg->c);
		widget->setPalette(palette);
		widget->update();
	};
	apply();
	style::PaletteChanged() | rpl::on_next(apply, widget->lifetime());
}

void AddAction(
		not_null<Ui::VerticalLayout*> list,
		QString text,
		Fn<void()> click) {
	const auto button = list->add(object_ptr<Button>(
		list,
		rpl::single(text),
		IsGroupedSettingsContent(list)
			? st::macSettingsPageButtonNoIcon
			: ResolveButtonStyle(list, st::settingsButtonNoIcon)
	));
	button->setAccessibleName(text);
	button->setClickedCallback(std::move(click));
}

void AddToggle(
		not_null<Ui::VerticalLayout*> list,
		QString text,
		rpl::producer<bool> value,
		Fn<void(bool)> change) {
	const auto button = list->add(object_ptr<Button>(
		list,
		rpl::single(text),
		IsGroupedSettingsContent(list)
			? st::macSettingsPageButtonNoIcon
			: ResolveButtonStyle(list, st::settingsButtonNoIcon)
	))->toggleOn(std::move(value));
	button->setAccessibleName(text);
	button->toggledChanges() | rpl::on_next(std::move(change), button->lifetime());
}

not_null<Button*> AddValueAction(
		not_null<Ui::VerticalLayout*> list,
		QString title,
		rpl::producer<QString> value,
		Fn<void()> click) {
	const auto button = AddButtonWithLabel(list,
		rpl::single(title),
		std::move(value),
		st::macSettingsPageButtonNoIcon);
	button->setAccessibleName(title);
	button->setClickedCallback(std::move(click));
	return button;
}

void ShowColorPicker(
		not_null<Window::SessionController*> controller,
		QString title,
		QColor initial,
		Fn<void(QColor)> apply) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(title));
		const auto editor = box->addRow(object_ptr<ColorEditor>(
			box, ColorEditor::Mode::HSL, initial));
		box->setWidth(editor->width());
		const auto done = [=] {
			auto color = editor->color();
			color.setAlpha(255);
			apply(color);
			box->closeBox();
		};
		editor->submitRequests() | rpl::on_next(done, editor->lifetime());
		box->addButton(tr::lng_box_ok(), done);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void ShowRadioChoice(
		not_null<Window::SessionController*> controller,
		QString title,
		QStringList options,
		int current,
		Fn<void(int)> apply) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(title));
		const auto group = std::make_shared<Ui::RadiobuttonGroup>(current);
		for (auto index = 0; index != options.size(); ++index) {
			box->addRow(object_ptr<Ui::Radiobutton>(
				box, group, index, options[index], st::settingsSendType),
				st::settingsSendTypePadding);
		}
		box->addButton(tr::lng_box_ok(), [=] {
			const auto selected = group->current();
			const auto callback = apply;
			box->closeBox();
			callback(selected);
		});
	}));
}

void ShowUpdateInstallChoice(
		not_null<Window::SessionController*> controller,
		QString title,
		QStringList options,
		int current,
		Fn<void(int)> apply) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(title));
		box->setWidth(style::ConvertScale(384));
		const auto content = AddGroupedSettingsContent(box->verticalLayout());
		const auto rows = AddGroupedSettingsRows(content);
		for (auto index = 0; index != options.size(); ++index) {
			const auto row = AddButtonWithLabel(rows,
				rpl::single(options[index]),
				rpl::single(index == current ? u"✓"_q : QString()),
				st::macSettingsPageButtonNoIcon);
			row->setAllowTextLines(2);
			row->setAccessibleDescription(index == current
				? Text(u"Выбрано"_q, u"Selected"_q)
				: QString());
			row->setClickedCallback([=] {
				const auto selected = index;
				const auto callback = apply;
				box->closeBox();
				callback(selected);
			});
		}
		FinishGroupedSettingsRows(rows, &st::boxBg);
		AddGroupedSettingsFooter(content, rpl::single(Text(
			u"Во втором режиме обновление установится при следующем запуске Telega Desktop."_q,
			u"The second option installs the update the next time Telega Desktop opens."_q)));
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

not_null<Ui::MaskedInputField*> AddSettingsInput(
		not_null<Ui::GenericBox*> box,
		QString title,
		QString initial,
		int maxLength) {
	const auto row = box->addRow(object_ptr<Ui::RpWidget>(box));
	const auto field = new Ui::MaskedInputField(row, st::defaultInputField,
		rpl::single(title), initial);
	field->setAccessibleName(title);
	field->setMaxLength(maxLength);
	const auto height = field->sizeHint().height();
	row->resize(row->width(), height);
	row->widthValue() | rpl::on_next([=](int width) {
		field->resize(width, height);
	}, row->lifetime());
	return field;
}

not_null<Ui::VerticalLayout*> AddDisclosure(
		not_null<Ui::VerticalLayout*> list,
		QString title,
		rpl::producer<QString> value) {
	auto panel = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		list, object_ptr<Ui::VerticalLayout>(list));
	const auto raw = panel.data();
	AddValueAction(list, std::move(title), std::move(value), [=] {
		raw->toggle(!raw->toggled(), anim::type::instant);
	});
	list->add(std::move(panel))->hide(anim::type::instant);
	return raw->entity();
}

rpl::producer<QString> OptionCount(
		std::vector<Core::Telega::DisplayOption> options) {
	auto changes = std::vector<rpl::producer<bool>>();
	changes.reserve(options.size());
	for (const auto option : options) {
		changes.push_back(Core::Telega::DisplayValue(option));
	}
	return rpl::combine(std::move(changes)) | rpl::map([](const std::vector<bool> &values) {
		return u"%1/%2"_q.arg(std::count(values.begin(), values.end(), true)).arg(values.size());
	});
}

void AddSteppedSlider(
		not_null<Ui::VerticalLayout*> list,
		QString title,
		QStringList stops,
		QStringList values,
		int current,
		Fn<void(int)> save,
		Fn<void(int)> preview = nullptr) {
	const auto content = list->add(object_ptr<Ui::VerticalLayout>(list));
	content->paintRequest() | rpl::on_next([=](const QRect &clip) {
		auto painter = QPainter(content);
		painter.fillRect(clip, st::windowBg);
	}, content->lifetime());
	const auto inset = style::ConvertScale(14);
	const auto max = int(values.size()) - 1;
	auto labelWidth = 0;
	for (const auto &value : values) {
		labelWidth = std::max(labelWidth, st::normalFont->width(value));
	}
	auto row = MakeSliderWithLabel(content, st::settingsScale,
		st::settingsScaleLabel, inset, labelWidth, true);
	const auto slider = row.slider;
	const auto label = row.label;
	const auto stepFor = [=](float64 fraction) {
		return std::clamp(int(std::round(fraction * max)), 0, max);
	};
	slider->setAccessibleName(title);
	slider->setFocusPolicy(Qt::StrongFocus);
	slider->setAdjustCallback([=](float64 fraction) {
		return float64(stepFor(fraction)) / max;
	});
	slider->setValue(float64(current) / max);
	label->setText(values[current]);
	slider->setChangeProgressCallback([=](float64 fraction) {
		const auto step = stepFor(fraction);
		label->setText(values[step]);
		if (preview) {
			preview(step);
		}
	});
	slider->setChangeFinishedCallback([=](float64 fraction) {
		save(stepFor(fraction));
	});
	content->add(std::move(row.widget), { inset, inset, inset, 0 });
	const auto ticks = content->add(object_ptr<Ui::RpWidget>(content),
		{ inset, 0, 2 * inset + labelWidth, style::ConvertScale(8) });
	ticks->resize(0, st::normalFont->height + style::ConvertScale(4));
	ticks->paintRequest() | rpl::on_next([=] {
		auto painter = QPainter(ticks);
		painter.setFont(st::normalFont->f);
		painter.setPen(st::windowSubTextFg);
		for (auto index = 0; index != stops.size(); ++index) {
			const auto align = !index ? Qt::AlignLeft
				: (index == stops.size() - 1) ? Qt::AlignRight : Qt::AlignHCenter;
			painter.drawText(ticks->rect(), align | Qt::AlignVCenter, stops[index]);
		}
	}, ticks->lifetime());
	style::PaletteChanged() | rpl::on_next([=] {
		content->update();
		ticks->update();
	}, content->lifetime());
}

void OfferSettingsRestart(not_null<Window::SessionController*> controller) {
	controller->show(Ui::MakeConfirmBox({
		.text = tr::lng_settings_need_restart(),
		.confirmed = [] {
			Core::App().saveSettings();
			Core::Restart();
		},
		.confirmText = tr::lng_settings_restart_now(),
		.cancelText = tr::lng_settings_restart_later(),
	}));
}

void AddClientOption(
		not_null<Ui::VerticalLayout*> list,
		not_null<Window::SessionController*> controller,
		QString title,
		const char *id) {
	const auto option = &base::options::lookup<bool>(id);
	if (!option->relevant()) {
		return;
	}
	AddToggle(list, std::move(title), rpl::single(option->value())
		| rpl::then(option->changes() | rpl::map([=] {
			return option->value();
		})), [=](bool enabled) {
		if (enabled == option->value()) {
			return;
		}
		option->set(enabled);
		if (option->restartRequired()) {
			controller->show(Ui::MakeConfirmBox({
				.text = tr::lng_settings_need_restart(),
				.confirmed = [] { Core::Restart(); },
				.confirmText = tr::lng_settings_restart_now(),
				.cancelText = tr::lng_settings_restart_later(),
			}));
		}
	});
}

void AddDisplayOption(
		not_null<Ui::VerticalLayout*> list,
		QString title,
		Core::Telega::DisplayOption option) {
	AddToggle(list, std::move(title), Core::Telega::DisplayValue(option),
		[=](bool enabled) { Core::Telega::SetDisplayEnabled(option, enabled); });
}

not_null<Button*> AddIndexChoice(
		not_null<Ui::VerticalLayout*> list,
		not_null<Window::SessionController*> controller,
		QString title,
		QStringList choices,
		Fn<int()> current,
		Fn<void(int)> save,
		decltype(&ShowRadioChoice) show = ShowRadioChoice) {
	const auto index = [=] { return std::clamp(current(), 0, int(choices.size()) - 1); };
	const auto value = list->lifetime().make_state<rpl::variable<QString>>(choices[index()]);
	const auto weak = QPointer<Ui::VerticalLayout>(list.get());
	return AddValueAction(list, title, value->value(), [=] {
		show(controller, title, choices, index(), [=](int selected) {
			if (weak && selected >= 0 && selected < choices.size()) {
				save(selected);
				*value = choices[index()];
			}
		});
	});
}

template <typename Enum>
void AddChoice(
		not_null<Ui::VerticalLayout*> list,
		not_null<Window::SessionController*> controller,
		QString title,
		QStringList choices,
		Enum (*current)(),
		void (*save)(Enum)) {
	AddIndexChoice(list, controller, std::move(title), std::move(choices),
		[=] { return int(current()); },
		[=](int index) { save(Enum(index)); });
}

void ShowOrderedSettings(
		not_null<Window::SessionController*> controller,
		QString title,
		QStringList selected,
		QStringList available,
		Fn<QString(const QString&)> itemTitle,
		Fn<void(const QStringList&)> save,
		int limit = 0) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(title));
		box->addRow(object_ptr<Ui::FlatLabel>(box, rpl::single(Text(
			u"Перетаскивайте строки для изменения порядка."_q,
			u"Drag rows to change their order."_q)), st::boxDividerLabel));
		const auto list = box->addRow(object_ptr<Ui::VerticalLayout>(box));
		auto order = selected;
		for (const auto &id : available) {
			if (!order.contains(id)) {
				order.push_back(id);
			}
		}
		const auto dragging = list->lifetime().make_state<bool>(false);
		for (const auto &id : order) {
			const auto row = list->add(object_ptr<Button>(list,
				rpl::single(itemTitle(id)), st::macSettingsPageButtonNoIcon));
			row->setProperty("telega_order_id", id);
			const auto active = row->lifetime().make_state<rpl::variable<bool>>(
				selected.contains(id));
			row->toggleOn(active->value(), true);
			row->setClickedCallback([=] {
				if (!*dragging) {
					*active = !active->current();
				}
			});
		}
		const auto reorder = list->lifetime().make_state<Ui::VerticalLayoutReorder>(list);
		reorder->updates() | rpl::on_next([=](Ui::VerticalLayoutReorder::Single update) {
			if (update.state == Ui::VerticalLayoutReorder::State::Started) {
				*dragging = true;
			} else {
				crl::on_main(list, [=] { *dragging = false; });
			}
		}, list->lifetime());
		reorder->start();
		box->addButton(tr::lng_box_done(), [=] {
			auto result = QStringList();
			for (auto index = 0; index != list->count(); ++index) {
				const auto row = static_cast<Button*>(list->widgetAt(index).get());
				if (row->toggled()) {
					result.push_back(row->property("telega_order_id").toString());
				}
			}
			if (limit && result.size() > limit) {
				controller->showToast(Text(u"Выберите не больше %1 действий."_q,
					u"Choose up to %1 actions."_q).arg(limit));
				return;
			}
			save(result);
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

QDir ThemeLibrary() {
	return QDir(cWorkingDir() + u"tdata/telega-themes"_q);
}

QString NewThemePath(QDir folder, QString name) {
	name = name.trimmed().left(64);
	const auto forbidden = u"<>:\"/\\|?*"_q;
	for (auto &character : name) {
		if (character.unicode() < 32 || forbidden.contains(character)) {
			character = QChar('_');
		}
	}
	while (name.endsWith(u'.') || name.endsWith(u' ')) {
		name.chop(1);
	}
	if (name.isEmpty()) {
		name = u"Theme"_q;
	}
	const auto extension = u".tdesktop-theme"_q;
	auto path = folder.filePath(name + extension);
	for (auto suffix = 2; QFileInfo::exists(path) || QFileInfo(path).isSymLink(); ++suffix) {
		path = folder.filePath(name + u" (%1)"_q.arg(suffix) + extension);
	}
	return path;
}

QByteArray ReadTheme(const QString &path) {
	auto file = QFile(path);
	return file.open(QIODevice::ReadOnly)
		&& file.size() <= Theme::kThemeFileSizeLimit
		? file.read(Theme::kThemeFileSizeLimit + 1)
		: QByteArray();
}

bool WriteFile(const QString &path, const QByteArray &bytes) {
	auto file = QSaveFile(path);
	return !bytes.isEmpty()
		&& file.open(QIODevice::WriteOnly)
		&& file.write(bytes) == bytes.size()
		&& file.commit();
}

QString SaveThemeToLibrary(const QString &name, const QByteArray &bytes) {
	const auto folder = ThemeLibrary();
	if (bytes.isEmpty() || bytes.size() > Theme::kThemeFileSizeLimit
		|| QFileInfo(folder.absolutePath()).isSymLink()
		|| !QDir().mkpath(folder.absolutePath())) {
		return {};
	}
	const auto path = NewThemePath(folder, name);
	return Theme::PreviewFromFile(bytes, path, {}) && WriteFile(path, bytes)
		? path
		: QString();
}

void ChooseLocalTheme(
		not_null<Window::SessionController*> controller,
		QString title,
		QString hint,
		Fn<void(QString)> chosen) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(title));
		const auto folder = ThemeLibrary();
		const auto files = QFileInfo(folder.absolutePath()).isSymLink()
			? QFileInfoList()
			: folder.entryInfoList(
				{ u"*.tdesktop-theme"_q },
				QDir::Files | QDir::NoSymLinks,
				QDir::Name | QDir::IgnoreCase);
		const auto content = AddGroupedSettingsContent(box->verticalLayout());
		if (files.isEmpty()) {
			AddGroupedSettingsFooter(content, rpl::single(Text(
				u"Здесь пока пусто. Создайте тему или импортируйте её из файла."_q,
				u"No saved themes yet. Create one or import a theme file."_q)));
		} else {
			AddGroupedSettingsHeader(content, rpl::single(hint))->setAccessibleName(hint);
			const auto rows = AddGroupedSettingsRows(content);
			for (const auto &info : files) {
				AddAction(rows, info.completeBaseName(), [=, path = info.absoluteFilePath()] {
					const auto callback = chosen;
					const auto selected = path;
					box->closeBox();
					callback(selected);
				});
			}
			FinishGroupedSettingsRows(rows, &st::boxBg);
		}
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

bool CanApplyLocalTheme(not_null<Window::SessionController*> controller) {
	if (!Theme::Background()->editingTheme() && !Theme::HasThemePreview()) {
		return true;
	}
	controller->showToast(Text(
		u"Сначала закройте редактор или предпросмотр темы."_q,
		u"Close the theme editor or preview first."_q));
	return false;
}

void AddLocalThemePreview(
		not_null<Ui::FixedHeightWidget*> sample,
		not_null<Theme::LocalThemeColors*> colors) {
	sample->setAccessibleName(Text(u"Пример чата"_q, u"Chat preview"_q));
	const auto title = Text(u"Пример чата"_q, u"Chat preview"_q);
	const auto incoming = Text(u"Привет!"_q, u"Hello!"_q);
	const auto outgoing = Text(u"Привет, как дела?"_q, u"Hi, how are you?"_q);
	sample->paintRequest() | rpl::on_next([=] {
		auto painter = QPainter(sample);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setFont(st::normalFont->f);
		const auto inset = style::ConvertScale(10);
		const auto height = style::ConvertScale(26);
		const auto radius = style::ConvertScale(10);
		const auto paint = [&](QRect rect, const QColor &color, const QString &label) {
			painter.setPen(Qt::NoPen);
			painter.setBrush(color);
			painter.drawRoundedRect(rect, radius, radius);
			painter.setPen(Theme::LocalThemeTextColor(color));
			painter.drawText(rect, Qt::AlignCenter, label);
		};
		painter.setPen(Qt::NoPen);
		painter.setBrush(colors->background);
		painter.drawRoundedRect(sample->rect(), radius, radius);
		const auto sidebar = style::ConvertScale(24);
		const auto chats = style::ConvertScale(44);
		const auto left = 3 * inset + sidebar + chats;
		const auto full = std::max(0, sample->width() - left - inset);
		if (colors->wallpaper.isValid()) {
			paint(QRect(left, inset, full, sample->height() - 2 * inset),
				colors->wallpaper, QString());
		}
		paint(QRect(inset, inset, sidebar, sample->height() - 2 * inset),
			colors->sidebar, QString());
		for (auto row = 0; row != 3; ++row) {
			paint(QRect(2 * inset + sidebar, inset + row * (height + 4),
				chats, height), colors->chats, QString());
		}
		paint(QRect(left, inset, full, height), colors->accent, title);
		const auto bubble = std::min(full, style::ConvertScale(124));
		paint(QRect(left, inset + height + 4, bubble, height), colors->incoming, incoming);
		paint(QRect(sample->width() - inset - bubble, inset + 2 * height + 8,
			bubble, height), colors->outgoing, outgoing);
		paint(QRect(inset, sample->height() - inset - height, sidebar, height),
			colors->settings, QString());
	}, sample->lifetime());
}

void ShowSimpleThemeEditor(not_null<Window::SessionController*> controller) {
	if (!CanApplyLocalTheme(controller)) {
		return;
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Text(u"Создать тему"_q, u"Create a theme"_q)));
		box->setWidth(style::ConvertScale(456));
		box->setMaxHeight(style::ConvertScale(560));
		const auto colors = box->lifetime().make_state<Theme::LocalThemeColors>(
			Theme::CurrentLocalThemeColors());
		const auto nameLabel = Text(u"Название темы"_q, u"Theme name"_q);
		const auto name = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(nameLabel)));
		name->setAccessibleName(nameLabel);
		name->setMaxLength(64);
		const auto sample = box->addRow(object_ptr<Ui::FixedHeightWidget>(
			box,
			style::ConvertScale(110)));
		AddLocalThemePreview(sample, colors);
		const auto content = AddGroupedSettingsContent(box->verticalLayout());
		const auto rows = AddGroupedSettingsRows(content);
		const auto addColor = [&](QString title, QColor *value) {
			const auto label = rows->lifetime().make_state<rpl::variable<QString>>(
				value->isValid()
					? value->name().toUpper()
					: Text(u"Текущие обои"_q, u"Current wallpaper"_q));
			AddValueAction(rows, title, label->value(), [=] {
				const auto weak = QPointer<Ui::GenericBox>(box.get());
				const auto initial = value->isValid() ? *value : colors->background;
				ShowColorPicker(controller, title, initial, [=](QColor color) {
					if (weak) {
						*value = color;
						*label = color.name().toUpper();
						sample->update();
					}
				});
			});
		};
		addColor(Text(u"Акцент: кнопки и активный чат"_q, u"Accent: buttons and active chat"_q), &colors->accent);
		addColor(Text(u"Сообщения собеседника"_q, u"Incoming messages"_q), &colors->incoming);
		addColor(Text(u"Ваши сообщения"_q, u"Your messages"_q), &colors->outgoing);
		addColor(Text(u"Фон приложения"_q, u"App background"_q), &colors->background);
		addColor(Text(u"Фон переписки"_q, u"Conversation background"_q), &colors->wallpaper);
		addColor(Text(u"Фон настроек"_q, u"Settings background"_q), &colors->settings);
		addColor(Text(u"Список чатов"_q, u"Chat list"_q), &colors->chats);
		addColor(Text(u"Боковая панель папок"_q, u"Folders sidebar"_q), &colors->sidebar);
		FinishGroupedSettingsRows(rows, &st::boxBg);
		AddGroupedSettingsFooter(content, rpl::single(Text(
			u"Фон переписки заменяет обои на сплошной цвет. Если его не менять, обои сохранятся."_q,
			u"Conversation background replaces the wallpaper with a solid color. Leave it unchanged to keep your wallpaper."_q)));
		box->addButton(rpl::single(Text(u"Сохранить и применить"_q, u"Save and apply"_q)), [=] {
			const auto title = name->getLastText().trimmed();
			if (title.isEmpty()) {
				name->showError();
				return;
			} else if (!CanApplyLocalTheme(controller)) {
				return;
			}
			const auto path = SaveThemeToLibrary(title, Theme::CollectLocalTheme(*colors));
			if (path.isEmpty()) {
				box->showToast(Text(u"Не удалось сохранить тему."_q, u"Could not save the theme."_q));
				return;
			}
			box->closeBox();
			if (Theme::Apply(path)) {
				Theme::KeepApplied();
				controller->showToast(Text(u"Тема сохранена и применена."_q, u"Theme saved and applied."_q));
			} else {
				controller->showToast(Text(
					u"Тема сохранена в «Моих темах», но не применилась."_q,
					u"The theme was saved in My Themes but could not be applied."_q));
			}
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void SetupLocalThemes(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> list) {
	const auto warn = [=] {
		controller->showToast(Text(
			u"Не удалось открыть или сохранить тему."_q,
			u"Could not open or save the theme."_q));
	};
	AddAction(list, Text(u"Создать тему"_q, u"Create a theme"_q), [=] {
		ShowSimpleThemeEditor(controller);
	});
	AddAction(list, Text(u"Мои темы"_q, u"My Themes"_q), [=] {
		if (!CanApplyLocalTheme(controller)) {
			return;
		}
		ChooseLocalTheme(controller, Text(u"Мои темы"_q, u"My Themes"_q), Text(
			u"Выберите тему, чтобы применить её"_q,
			u"Choose a theme to apply"_q), [=](QString path) {
			if (!CanApplyLocalTheme(controller)) {
				return;
			} else if (Theme::Apply(path)) {
				Theme::KeepApplied();
				controller->showToast(Text(u"Тема применена."_q, u"Theme applied."_q));
			} else {
				warn();
			}
		});
	});
	AddAction(list, Text(u"Импорт темы из файла"_q, u"Import theme from file"_q), [=] {
		const auto path = QFileDialog::getOpenFileName(
			controller->widget(),
			Text(u"Импортировать тему"_q, u"Import theme"_q),
			{},
			Text(u"Файлы тем"_q, u"Theme files"_q)
				+ u" (*.tdesktop-theme *.tdesktop-palette)"_q);
		if (!path.isEmpty()) {
			const auto name = QFileInfo(path).completeBaseName();
			if (SaveThemeToLibrary(name, ReadTheme(path)).isEmpty()) {
				warn();
			} else {
				controller->showToast(Text(u"Тема добавлена в «Мои темы»."_q, u"Theme added to My Themes."_q));
			}
		}
	});
	AddAction(list, Text(u"Экспорт темы в файл"_q, u"Export theme to file"_q), [=] {
		ChooseLocalTheme(controller, Text(u"Экспорт темы"_q, u"Export a theme"_q), Text(
			u"Выберите сохранённую тему для экспорта"_q,
			u"Choose a saved theme to export"_q), [=](QString source) {
			auto path = QFileDialog::getSaveFileName(
				controller->widget(),
				Text(u"Экспортировать тему"_q, u"Export theme"_q),
				QFileInfo(source).fileName(),
				Text(u"Файлы тем"_q, u"Theme files"_q) + u" (*.tdesktop-theme)"_q);
			if (path.isEmpty()) {
				return;
			}
			if (!path.endsWith(u".tdesktop-theme"_q, Qt::CaseInsensitive)) {
				path += u".tdesktop-theme"_q;
				if (QFileInfo::exists(path)) {
					warn();
					return;
				}
			}
			const auto bytes = ReadTheme(source);
			if (bytes.size() > Theme::kThemeFileSizeLimit || !WriteFile(path, bytes)) {
				warn();
			} else {
				controller->showToast(Text(u"Файл темы сохранён."_q, u"Theme file exported."_q));
			}
		});
	});
	AddAction(list, Text(u"Открыть папку тем"_q, u"Open themes folder"_q), [=] {
		const auto path = ThemeLibrary().absolutePath();
		if (QFileInfo(path).isSymLink() || !QDir().mkpath(path)
			|| !QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
			warn();
		}
	});
}

void SetupPreferencesBackup(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> list) {
	struct Preference {
		const char *key;
		int initial;
		int max;
	};
	struct Switch {
		const char *key;
		bool (*get)();
		void (*set)(bool);
	};
	using Option = Core::Telega::DisplayOption;
	struct DisplaySwitch {
		const char *key;
		Option option;
	};
	constexpr auto displaySwitches = std::array{
		DisplaySwitch{ "profilePhotoDc", Option::ProfilePhotoDc },
		DisplaySwitch{ "relativeLastSeen", Option::RelativeLastSeen },
		DisplaySwitch{ "timeWithSeconds", Option::TimeWithSeconds },
		DisplaySwitch{ "hideDefaultGreetingSticker", Option::HideDefaultGreetingSticker },
		DisplaySwitch{ "editedIcon", Option::EditedIcon },
		DisplaySwitch{ "forwardCount", Option::ForwardCount },
		DisplaySwitch{ "exactViewCounts", Option::ExactViewCounts },
		DisplaySwitch{ "roundedContactAvatars", Option::RoundedContactAvatars },
		DisplaySwitch{ "hideSidebarStatus", Option::HideSidebarStatus },
		DisplaySwitch{ "hideSenderMiniAvatars", Option::HideSenderMiniAvatars },
		DisplaySwitch{ "accentSenderNames", Option::AccentSenderNames },
		DisplaySwitch{ "hidePrivateReactions", Option::HidePrivateReactions },
		DisplaySwitch{ "hideGroupReactions", Option::HideGroupReactions },
		DisplaySwitch{ "hideChannelReactions", Option::HideChannelReactions },
		DisplaySwitch{ "hideSendAs", Option::HideSendAs },
		DisplaySwitch{ "quoteReplies", Option::QuoteReplies },
		DisplaySwitch{ "countCharacters", Option::CountCharacters },
		DisplaySwitch{ "selectServiceMessages", Option::SelectServiceMessages },
		DisplaySwitch{ "hideAllChatsFolder", Option::HideAllChatsFolder },
		DisplaySwitch{ "pauseVoiceInBackground", Option::PauseVoiceInBackground },
		DisplaySwitch{ "pauseRoundVideoInBackground", Option::PauseRoundVideoInBackground },
		DisplaySwitch{ "profileMusicCover", Option::ProfileMusicCover },
		DisplaySwitch{ "uploadBoost", Option::UploadBoost },
		DisplaySwitch{ "ignorePeerThemes", Option::IgnorePeerThemes },
		DisplaySwitch{ "hideTypingIndicator", Option::HideTypingIndicator },
		DisplaySwitch{ "onlineSenderBadge", Option::OnlineSenderBadge },
		DisplaySwitch{ "hideCopyPhotoAction", Option::HideCopyPhotoAction },
		DisplaySwitch{ "hideSaveMediaAction", Option::HideSaveMediaAction },
		DisplaySwitch{ "repeatMessageAction", Option::RepeatMessageAction },
		DisplaySwitch{ "messageDetailsAction", Option::MessageDetailsAction },
		DisplaySwitch{ "pauseAutomaticChatReads", Option::PauseAutomaticChatReads },
		DisplaySwitch{ "hideStoryViewReceipts", Option::HideStoryViewReceipts },
		DisplaySwitch{ "hideOnlineAnnouncements", Option::HideOnlineAnnouncements },
		DisplaySwitch{ "inlineCalculator", Option::InlineCalculator },
		DisplaySwitch{ "quickMessageActions", Option::QuickMessageActions },
		DisplaySwitch{ "previewPollResults", Option::PreviewPollResults },
		DisplaySwitch{ "systemEmoji", Option::SystemEmoji },
		DisplaySwitch{ "keepUnreadWhenSending", Option::KeepUnreadWhenSending },
		DisplaySwitch{ "filterVisualNoise", Option::FilterVisualNoise },
		DisplaySwitch{ "uniformAvatarRadius", Option::UniformAvatarRadius },
		DisplaySwitch{ "visibleGroupAuthors", Option::VisibleGroupAuthors },
		DisplaySwitch{ "setOfflineAfterSending", Option::SetOfflineAfterSending },
		DisplaySwitch{ "hideSponsoredMessages", Option::HideSponsoredMessages },
	};
	constexpr auto preferences = std::array{
		Preference{ "model", 0, 2 },
		Preference{ "role", 0, 3 },
		Preference{ "context", 1, 3 },
		Preference{ "temperature", 10, 20 },
		Preference{ "streaming", 1, 1 },
		Preference{ "transcribeVoice", 0, 1 },
		Preference{ "transcribeRoundVideo", 0, 1 },
	};
	constexpr auto switches = std::array{
		Switch{ "hideStories", Core::Telega::HideStories, Core::Telega::SetHideStories },
		Switch{ "commaAfterMention", Core::Telega::CommaAfterMention, Core::Telega::SetCommaAfterMention },
		Switch{ "hideCopyToast", Core::Telega::HideCopyToast, Core::Telega::SetHideCopyToast },
		Switch{ "hideBubbleTails", Core::Telega::HideBubbleTails, Core::Telega::SetHideBubbleTails },
		Switch{ "hideFastShare", Core::Telega::HideFastShare, Core::Telega::SetHideFastShare },
		Switch{ "plainReplyPreviews", Core::Telega::PlainReplyPreviews, Core::Telega::SetPlainReplyPreviews },
		Switch{ "pauseVideoInBackground", Core::Telega::PauseVideoInBackground, Core::Telega::SetPauseVideoInBackground },
		Switch{ "preferOriginalVideo", Core::Telega::PreferOriginalVideo, Core::Telega::SetPreferOriginalVideo },
		Switch{ "defaultPhotoHd", Core::Telega::DefaultPhotoHd, Core::Telega::SetDefaultPhotoHd },
	};
	const auto warn = [=] {
		controller->showToast(Text(
			u"Не удалось прочитать или сохранить настройки Telega."_q,
			u"Could not read or save Telega preferences."_q));
	};
	const auto filter = u"Telega preferences (*.json)"_q;
	AddAction(list, Text(u"Экспорт настроек Telega"_q, u"Export Telega preferences"_q), [=] {
		const auto path = QFileDialog::getSaveFileName(
			controller->widget(),
			Text(u"Экспорт настроек Telega"_q, u"Export Telega preferences"_q),
			u"telega-preferences.json"_q,
			filter);
		if (path.isEmpty()) {
			return;
		}
		auto gemini = QJsonObject();
		for (const auto &pref : preferences) {
			gemini.insert(QString::fromLatin1(pref.key), std::clamp(
				Gemini::Preference(pref.key, pref.initial),
				0,
				pref.max));
		}
		auto options = QJsonObject();
		for (const auto &option : switches) {
			options.insert(QString::fromLatin1(option.key), option.get());
		}
		for (const auto &option : displaySwitches) {
			options.insert(QString::fromLatin1(option.key),
				Core::Telega::DisplayEnabled(option.option));
		}
		const auto data = QJsonDocument(QJsonObject{
			{ u"format"_q, u"telega-preferences-v1"_q },
			{ u"wallpaperBrightness"_q, Theme::ChatBackgroundBrightness() },
			{ u"videoSeekStep"_q, Core::Telega::VideoSeekStep() },
			{ u"stickerScale"_q, Core::Telega::StickerScale() },
			{ u"stickerTime"_q, int(Core::Telega::StickerTime()) },
			{ u"channelBottomAction"_q, int(Core::Telega::BottomAction()) },
			{ u"downloadMode"_q, int(Core::Telega::TransferDownloadMode()) },
			{ u"appIcon"_q, int(Core::Telega::ApplicationIconAppearance().style) },
			{ u"appIconSquare"_q, Core::Telega::ApplicationIconAppearance().roundedSquare },
			{ u"avatarCornerPercent"_q, Core::Telega::AvatarCornerPercent() },
			{ u"quickActions"_q, QJsonArray::fromStringList(HistoryView::TelegaQuickActionOrder()) },
			{ u"navigation"_q, QJsonArray::fromStringList(Core::Telega::Navigation()) },
			{ u"gemini"_q, gemini },
			{ u"options"_q, options },
		}).toJson(QJsonDocument::Indented);
		if (!WriteFile(path, data)) {
			warn();
		} else {
			controller->showToast(Text(u"Настройки экспортированы."_q, u"Preferences exported."_q));
		}
	});
	AddAction(list, Text(u"Импорт настроек Telega"_q, u"Import Telega preferences"_q), [=] {
		const auto path = QFileDialog::getOpenFileName(
			controller->widget(),
			Text(u"Импорт настроек Telega"_q, u"Import Telega preferences"_q),
			{},
			filter);
		if (path.isEmpty()) {
			return;
		}
		auto file = QFile(path);
		constexpr auto limit = 16 * 1024;
		if (!file.open(QIODevice::ReadOnly) || file.size() > limit) {
			warn();
			return;
		}
		const auto data = file.read(limit + 1);
		const auto object = QJsonDocument::fromJson(data).object();
		const auto brightness = object.value(u"wallpaperBrightness"_q).toInt(-1);
		const auto seekStep = object.value(u"videoSeekStep"_q);
		const auto seconds = seekStep.toInt(-1);
		const auto gemini = object.value(u"gemini"_q).toObject();
		const auto options = object.value(u"options"_q);
		const auto stickerScale = object.value(u"stickerScale"_q);
		const auto stickerTime = object.value(u"stickerTime"_q);
		const auto channelBottom = object.value(u"channelBottomAction"_q);
		const auto downloadMode = object.value(u"downloadMode"_q);
		const auto navigation = object.value(u"navigation"_q);
		const auto appIcon = object.value(u"appIcon"_q);
		const auto appIconSquare = object.value(u"appIconSquare"_q);
		const auto avatarCorner = object.value(u"avatarCornerPercent"_q);
		const auto quickActions = object.value(u"quickActions"_q);
		const auto valid = [](QJsonValue value, int minimum, int maximum) {
			const auto integer = value.toInt(minimum - 1);
			return value.isUndefined() || (integer >= minimum && integer <= maximum);
		};
		if (data.size() > limit
			|| object.value(u"format"_q).toString() != u"telega-preferences-v1"_q
			|| brightness < 25 || brightness > 150
			|| (!seekStep.isUndefined() && seconds != 5 && seconds != 10
				&& seconds != 15 && seconds != 30)
			|| (!options.isUndefined() && !options.isObject())
			|| !valid(stickerScale, 60, 140)
			|| !valid(stickerTime, 0, 2)
			|| !valid(channelBottom, 0, 2)
			|| !valid(downloadMode, 0, 2)
			|| !valid(appIcon, 0, int(Core::Telega::AppIconStyle::Blue))
			|| (!appIconSquare.isUndefined() && !appIconSquare.isBool())
			|| !valid(avatarCorner, 0, 50)
			|| (!quickActions.isUndefined() && !quickActions.isArray())
			|| (!navigation.isUndefined() && !navigation.isArray())) {
			warn();
			return;
		}
		auto navigationItems = QStringList();
		const auto knownItems = Core::Telega::NavigationItems();
		for (const auto &value : navigation.toArray()) {
			const auto item = value.toString();
			if (!knownItems.contains(item) || navigationItems.contains(item)) {
				warn();
				return;
			}
			navigationItems.push_back(item);
		}
		if (!navigation.isUndefined() && quickActions.isUndefined()) {
			navigationItems.push_front(u"divider"_q);
			navigationItems.push_front(u"profile"_q);
			navigationItems.push_back(u"wallet"_q);
			navigationItems.removeDuplicates();
		}
		auto quickActionItems = QStringList();
		const auto availableActions = HistoryView::TelegaQuickActionIds();
		for (const auto &value : quickActions.toArray()) {
			const auto id = value.toString();
			if (!availableActions.contains(id) || quickActionItems.contains(id)
				|| quickActionItems.size() == HistoryView::kTelegaQuickActionLimit) {
				warn();
				return;
			}
			quickActionItems.push_back(id);
		}
		for (const auto &pref : preferences) {
			const auto value = gemini.value(QString::fromLatin1(pref.key)).toInt(-1);
			if (value < 0 || value > pref.max) {
				warn();
				return;
			}
		}
		const auto optionValues = options.toObject();
		for (const auto &option : switches) {
			const auto value = optionValues.value(QString::fromLatin1(option.key));
			if (!value.isUndefined() && !value.isBool()) {
				warn();
				return;
			}
		}
		for (const auto &option : displaySwitches) {
			const auto value = optionValues.value(QString::fromLatin1(option.key));
			if (!value.isUndefined() && !value.isBool()) {
				warn();
				return;
			}
		}
		Theme::SetChatBackgroundBrightness(brightness);
		if (!seekStep.isUndefined()) {
			Core::Telega::SetVideoSeekStep(seconds);
		}
		if (!stickerScale.isUndefined()) {
			Core::Telega::SetStickerScale(stickerScale.toInt());
		}
		if (!stickerTime.isUndefined()) {
			Core::Telega::SetStickerTime(Core::Telega::StickerTimestamp(stickerTime.toInt()));
		}
		if (!channelBottom.isUndefined()) {
			Core::Telega::SetBottomAction(Core::Telega::ChannelBottomAction(channelBottom.toInt()));
		}
		if (!downloadMode.isUndefined()) {
			Core::Telega::SetTransferDownloadMode(Core::Telega::DownloadMode(downloadMode.toInt()));
		}
		if (!navigation.isUndefined()) {
			Core::Telega::SetNavigation(navigationItems);
		}
		if (!appIcon.isUndefined() || !appIconSquare.isUndefined()) {
			const auto previous = Core::Telega::ApplicationIconAppearance();
			Core::Telega::SetApplicationIconAppearance({
				.style = Core::Telega::AppIconStyle(appIcon.toInt(int(previous.style))),
				.roundedSquare = appIconSquare.toBool(previous.roundedSquare),
			});
		}
		if (!avatarCorner.isUndefined()) {
			Core::Telega::SetAvatarCornerPercent(avatarCorner.toInt());
		}
		if (!quickActions.isUndefined()) {
			HistoryView::SetTelegaQuickActionOrder(quickActionItems);
		}
		for (const auto &pref : preferences) {
			Gemini::SetPreference(pref.key, gemini.value(QString::fromLatin1(pref.key)).toInt());
		}
		for (const auto &option : switches) {
			const auto value = optionValues.value(QString::fromLatin1(option.key));
			if (value.isBool()) {
				option.set(value.toBool());
			}
		}
		for (const auto &option : displaySwitches) {
			const auto value = optionValues.value(QString::fromLatin1(option.key));
			if (value.isBool()) {
				Core::Telega::SetDisplayEnabled(option.option, value.toBool());
			}
		}
		Ui::ForceFullRepaint(controller->widget());
		controller->showToast(Text(u"Настройки импортированы."_q, u"Preferences imported."_q));
	});
	const auto resetTitle = Text(u"Сброс настроек Telega"_q, u"Reset Telega preferences"_q);
	AddAction(list, resetTitle, [=] {
		controller->show(Ui::MakeConfirmBox({
			.text = Text(u"Сбросить настройки до значений по умолчанию? Темы и история останутся."_q,
				u"Reset preferences to defaults? Themes and history will be kept."_q),
			.confirmed = [=] {
				Theme::SetChatBackgroundBrightness(100);
				Core::Telega::SetVideoSeekStep(5);
				Core::Telega::SetStickerScale(100);
				Core::Telega::SetStickerTime(Core::Telega::StickerTimestamp::Automatic);
				Core::Telega::SetBottomAction(Core::Telega::ChannelBottomAction::Mute);
				Core::Telega::SetTransferDownloadMode(Core::Telega::DownloadMode::Standard);
				Core::Telega::SetNavigation(Core::Telega::DefaultNavigation());
				Core::Telega::SetApplicationIconAppearance({});
				Core::Telega::SetAvatarCornerPercent(50);
				HistoryView::SetTelegaQuickActionOrder(HistoryView::DefaultTelegaQuickActions());
				Gemini::SetModelName({});
				for (const auto &pref : preferences) {
					Gemini::SetPreference(pref.key, pref.initial);
				}
				for (const auto &option : switches) {
					option.set(false);
				}
				for (const auto &option : displaySwitches) {
					Core::Telega::SetDisplayEnabled(option.option, false);
				}
				Ui::ForceFullRepaint(controller->widget());
				controller->showToast(Text(u"Настройки сброшены."_q, u"Preferences reset."_q));
			},
			.confirmText = Text(u"Сбросить"_q, u"Reset"_q),
			.confirmStyle = &st::attentionBoxButton,
			.title = resetTitle,
		}));
	});
}

void SetupBackgroundBrightness(
		not_null<Ui::VerticalLayout*> list,
		not_null<Window::SessionController*> controller) {
	const auto title = Text(u"Яркость фона"_q, u"Background brightness"_q);
	Ui::AddDividerText(list, rpl::single(title));
	auto row = MakeSliderWithLabel(
		list,
		st::settingsScale,
		st::settingsScaleLabel,
		style::ConvertScale(16),
		st::normalFont->width(u"150%"_q),
		true);
	const auto slider = row.slider;
	const auto label = row.label;
	const auto initial = Theme::ChatBackgroundBrightness();
	const auto preview = new Ui::FixedHeightWidget(
		list, style::ConvertScale(134));
	preview->setAccessibleName(Text(u"Предпросмотр фона чата"_q,
		u"Chat wallpaper preview"_q));
	const auto theme = std::shared_ptr<Ui::ChatTheme>(
		Theme::DefaultChatThemeOn(preview->lifetime()));
	theme->repaintBackgroundRequests() | rpl::on_next([=] {
		preview->update();
	}, preview->lifetime());
	style::PaletteChanged() | rpl::on_next([=] {
		preview->update();
	}, preview->lifetime());
	preview->paintRequest() | rpl::on_next([=] {
		if (preview->width() <= 0) {
			return;
		}
		Theme::ObserveLiveWallpaper(preview);
		auto painter = QPainter(preview);
		painter.setRenderHint(QPainter::Antialiasing);
		const auto radius = style::ConvertScale(12);
		auto border = QPainterPath();
		border.addRoundedRect(preview->rect(), radius, radius);
		painter.setClipPath(border);
		painter.fillRect(preview->rect(), st::windowBg);
		Window::SectionWidget::PaintBackground(
			painter, theme.get(), preview->size(), preview->rect(), true);
		const auto inset = style::ConvertScale(12);
		const auto bubbleHeight = style::ConvertScale(30);
		const auto bubble = [&](bool outgoing, const QString &text) {
			const auto font = st::normalFont;
			const auto width = std::clamp(
				font->width(text) + inset * 2,
				0,
				std::max(preview->width() - inset * 2, 0));
			const auto rect = QRect(
				outgoing ? preview->width() - inset - width : inset,
				(preview->height() / 2) + (outgoing ? 4 : -bubbleHeight - 4),
				width,
				bubbleHeight);
			painter.setPen(Qt::NoPen);
			painter.setBrush(outgoing ? st::msgOutBg : st::msgInBg);
			painter.drawRoundedRect(rect, radius, radius);
			painter.setPen(outgoing ? st::historyTextOutFg : st::historyTextInFg);
			painter.setFont(font->f);
			painter.drawText(rect, Qt::AlignCenter,
				font->elided(text, std::max(width - inset, 0)));
		};
		bubble(false, Text(u"Привет!"_q, u"Hello!"_q));
		bubble(true, Text(u"Пример сообщения"_q, u"Sample message"_q));
	}, preview->lifetime());
	slider->setAccessibleName(title);
	slider->setFocusPolicy(Qt::StrongFocus);
	slider->setValue((initial - 25) / 125.);
	label->setText(u"%1%"_q.arg(initial));
	const auto repaint = new QTimer(row.widget.data());
	repaint->setSingleShot(true);
	repaint->setInterval(32);
	QObject::connect(repaint, &QTimer::timeout, slider.get(), [=] {
		Ui::ForceFullRepaint(controller->widget());
	});
	slider->setChangeProgressCallback([=](float64 fraction) {
		const auto value = 25 + int(std::round(fraction * 125.));
		label->setText(u"%1%"_q.arg(value));
		if (value != Theme::ChatBackgroundBrightness()) {
			Theme::SetChatBackgroundBrightness(value);
			preview->update();
			if (!repaint->isActive()) {
				repaint->start();
			}
		}
	});
	list->add(std::move(row.widget), st::settingsBigScalePadding);
	const auto inset = style::ConvertScale(14);
	list->add(object_ptr<Ui::FixedHeightWidget>::fromRaw(preview),
		style::margins(inset, 0, inset, inset));
}

void ShowLiveWallpaperBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		using Video = Theme::LiveWallpaperVideo;
		using Status = Video::Status;
		struct Preview {
			QString path;
			std::unique_ptr<Video> video;
			rpl::lifetime lifetime;
			rpl::variable<QString> message;
			bool applying = false;
		};
		box->setTitle(rpl::single(Text(u"Живые обои"_q, u"Live wallpaper"_q)));
		const auto state = box->lifetime().make_state<Preview>();
		const auto preview = box->addRow(object_ptr<Ui::FixedHeightWidget>(
			box, style::ConvertScale(210)));
		preview->setAccessibleName(Text(u"Предпросмотр обоев"_q, u"Wallpaper preview"_q));
		const auto still = Theme::Background()->createCurrentImage();
		preview->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(preview);
			const auto radius = style::ConvertScale(12);
			auto clip = QPainterPath();
			clip.addRoundedRect(preview->rect(), radius, radius);
			p.setRenderHint(QPainter::Antialiasing);
			p.setClipPath(clip);
			p.fillRect(preview->rect(), st::boxBg);
			const auto &frame = state->video ? state->video->frame() : still;
			Theme::PaintWallpaperFrame(
				p, frame.isNull() ? still : frame, preview->size(), preview->rect());
			const auto brightness = Theme::ChatBackgroundBrightness();
			if (brightness != 100) {
				const auto alpha = std::abs(brightness - 100) * 255 / 100;
				const auto shade = (brightness < 100) ? 0 : 255;
				p.fillRect(preview->rect(), QColor(shade, shade, shade, alpha));
			}
		}, preview->lifetime());
		state->message = Text(
			u"Выберите видео до 60 секунд. Звук не воспроизводится."_q,
			u"Choose a video up to 60 seconds. Playback is muted."_q);
		Ui::AddDividerText(box->verticalLayout(), state->message.value());
		const auto apply = box->addButton(
			rpl::single(Text(u"Применить"_q, u"Apply"_q)));
		const auto useStatic = box->addLeftButton(rpl::single(Text(
			u"Статичный фон"_q, u"Still wallpaper"_q)), [=] {
			Theme::UseStaticWallpaper();
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		apply->setDisabled(true);
		apply->setClickedCallback([=] {
			if (state->applying || !state->video
				|| state->video->status() != Status::Ready) {
				return;
			}
			state->applying = true;
			apply->setDisabled(true);
			useStatic->setDisabled(true);
			state->message = Text(u"Сохранение обоев…"_q, u"Saving wallpaper…"_q);
			Theme::ApplyLiveWallpaper(state->path, crl::guard(box, [=](bool ok) {
				if (ok) {
					box->closeBox();
				} else {
					state->applying = false;
					state->message = Text(
						u"Не удалось сохранить видео."_q,
						u"Could not save the video."_q);
					apply->setDisabled(state->video->status() != Status::Ready);
					useStatic->setDisabled(false);
				}
			}));
		});
		const auto load = [=](const QString &path) {
			state->lifetime.destroy();
			state->path = path;
			state->video = std::make_unique<Video>(path);
			state->video->setPaused(
				QGuiApplication::applicationState() != Qt::ApplicationActive);
			const auto filename = (path == Theme::SavedLiveWallpaperPath())
				? Text(u"Сохранённое видео"_q, u"Saved video"_q)
				: QFileInfo(path).fileName();
			state->video->frameUpdates() | rpl::on_next([=] {
				preview->update();
			}, state->lifetime);
			state->video->statusValue() | rpl::on_next([=](Status status) {
				apply->setDisabled(status != Status::Ready || state->applying);
				if (state->applying) {
					return;
				} else if (status == Status::Ready) {
					state->message = filename;
				} else if (status == Status::TooLong) {
					state->message = Text(
						u"Видео должно длиться не больше 60 секунд."_q,
						u"The video must be 60 seconds or shorter."_q);
				} else if (status == Status::Invalid) {
					state->message = Text(
						u"Не удалось открыть видео или определить его длительность."_q,
						u"Could not read the video or determine its duration."_q);
				} else {
					state->message = Text(
						u"Загрузка предпросмотра…"_q, u"Loading preview…"_q);
				}
				preview->update();
			}, state->lifetime);
		};
		AddAction(box->verticalLayout(), Text(u"Выбрать видео…"_q, u"Choose video…"_q), [=] {
			if (!state->applying) {
				FileDialog::GetOpenPath(
					box.get(),
					Text(u"Выбор видео для обоев"_q, u"Choose wallpaper video"_q),
					Text(u"Видео"_q, u"Video"_q) + u" (*.mp4 *.webm *.mov *.mkv *.m4v)"_q,
					crl::guard(box, [=](FileDialog::OpenResult &&result) {
						if (!result.paths.isEmpty()) {
							load(result.paths.front());
						}
					}));
			}
		});
		QObject::connect(
			qApp,
			&QGuiApplication::applicationStateChanged,
			box.get(),
			[=](Qt::ApplicationState applicationState) {
				if (state->video) {
					state->video->setPaused(applicationState != Qt::ApplicationActive);
				}
			});
		if (const auto saved = Theme::SavedLiveWallpaperPath(); !saved.isEmpty()) {
			load(saved);
		}
	}));
}

void SetupVideoSeekStep(
		not_null<Ui::VerticalLayout*> list,
		not_null<Window::SessionController*> controller) {
	const auto steps = std::array{ 5, 10, 15, 30 };
	auto labels = QStringList();
	for (const auto seconds : steps) {
		labels.push_back(Text(u"%1 сек."_q, u"%1 sec"_q).arg(seconds));
	}
	AddIndexChoice(list, controller, Text(u"Шаг перемотки видео"_q, u"Video seek step"_q),
		std::move(labels), [=] {
			return int(std::find(steps.begin(), steps.end(), Core::Telega::VideoSeekStep()) - steps.begin());
		}, [=](int index) {
			Core::Telega::SetVideoSeekStep(steps[index]);
	});
}

template <typename Derived>
class TelegaPage : public Section<Derived> {
public:
	TelegaPage(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section<Derived>(parent, controller)
	, _list(Ui::CreateChild<Ui::VerticalLayout>(this)) {
		Ui::ResizeFitChild(this, _list);
	}

protected:
	bool paintOuter(not_null<QWidget*> outer, int, QRect clip) override {
		auto painter = QPainter(outer);
		painter.fillRect(clip, st::boxSearchBg);
		return true;
	}

	void beginGroups() {
		_search.emplace(CreateSectionSearchRow(this));
		_content = AddGroupedSettingsContent(_list).get();
		_content->add(object_ptr<Ui::RpWidget>::fromRaw(_search->row),
			{ 0, style::ConvertScale(8), 0, 0 });
	}

	not_null<Ui::VerticalLayout*> addGroup(QString title, QString keywords = {}) {
		const auto panel = _content->add(object_ptr<GroupPanel>(
			_content, object_ptr<Ui::VerticalLayout>(_content)));
		if (!title.isEmpty()) {
			AddGroupedSettingsHeader(panel->entity(), rpl::single(title)
			)->setAccessibleName(title);
		} else {
			panel->entity()->add(object_ptr<Ui::FixedHeightWidget>(
				panel->entity(), style::ConvertScale(12)));
		}
		const auto rows = AddGroupedSettingsRows(panel->entity());
		_groups.push_back({ panel, rows, SearchWords(title + u' ' + keywords) });
		return rows;
	}

	void addGroupFooter(QString text) {
		AddGroupedSettingsFooter(_groups.back().panel->entity(), rpl::single(std::move(text)));
	}

	void finishGroups() {
		for (auto &group : _groups) {
			FinishGroupedSettingsRows(group.rows);
			const auto rows = group.rows->findChildren<QWidget*>();
			for (const auto row : rows) {
				if (!row->accessibleName().isEmpty()) {
					group.terms.append(SearchWords(row->accessibleName()));
				}
			}
		}
		const auto inset = style::ConvertScale(12);
		const auto empty = _content->add(object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			_content, object_ptr<Ui::FlatLabel>(_content,
				rpl::single(Text(u"Ничего не найдено"_q, u"No results"_q)),
				st::macSettingsPageHeader), style::margins(inset, inset, inset, inset)));
		empty->hide(anim::type::instant);
		_content->add(object_ptr<Ui::FixedHeightWidget>(_content, inset));
		_search->controller->queryChanges() | rpl::on_next([=, this](QString query) {
			const auto words = SearchWords(query);
			auto matched = false;
			for (const auto &group : _groups) {
				const auto visible = MatchesWords(group.terms, words);
				group.panel->toggle(visible, anim::type::instant);
				matched = matched || visible;
			}
			empty->toggle(!matched, anim::type::instant);
		}, this->lifetime());
	}

	const not_null<Ui::VerticalLayout*> _list;

private:
	using GroupPanel = Ui::SlideWrap<Ui::VerticalLayout>;
	struct Group {
		not_null<GroupPanel*> panel;
		not_null<Ui::VerticalLayout*> rows;
		QStringList terms;
	};
	std::optional<SectionSearchRow> _search;
	Ui::VerticalLayout *_content = nullptr;
	std::vector<Group> _groups;

};

Fn<void(int)> AddAvatarPreview(
		not_null<Ui::VerticalLayout*> list,
		QString label,
		int initial) {
	using Option = Core::Telega::DisplayOption;
	const auto sample = list->add(object_ptr<Ui::FixedHeightWidget>(
		list, style::ConvertScale(56)));
	sample->setAccessibleName(label);
	const auto value = sample->lifetime().make_state<int>(initial);
	rpl::merge(
		Core::Telega::DisplayValue(Option::UniformAvatarRadius),
		Core::Telega::DisplayValue(Option::RoundedContactAvatars)
	) | rpl::on_next([=] {
		sample->update();
	}, sample->lifetime());
	sample->paintRequest() | rpl::on_next([=](const QRect &clip) {
		auto painter = QPainter(sample);
		painter.fillRect(clip, st::windowBg);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setPen(Qt::NoPen);
		const auto inset = style::ConvertScale(14);
		const auto size = style::ConvertScale(32);
		const auto top = (sample->height() - size) / 2;
		painter.setBrush(st::windowBgOver);
		const auto textStart = 2 * inset + size;
		const auto textWidth = std::clamp(
			sample->width() - textStart - inset, 0, style::ConvertScale(116));
		const auto line = style::ConvertScale(4);
		painter.drawRoundedRect(textStart, sample->height() / 2 - 2 * line,
			textWidth, line, line / 2., line / 2.);
		painter.drawRoundedRect(textStart, sample->height() / 2 + line,
			textWidth * 2 / 3, line, line / 2., line / 2.);
		const auto rounded = Core::Telega::DisplayEnabled(Option::RoundedContactAvatars);
		const auto uniform = Core::Telega::DisplayEnabled(Option::UniformAvatarRadius);
		const auto radius = rounded ? std::min(*value, 30) : *value;
		const auto contactRadius = size * radius / 100.;
		const auto forumRadius = size * (uniform ? radius : std::min(radius, 30)) / 100.;
		painter.setBrush(st::windowBgActive);
		painter.drawRoundedRect(inset, top, size, size, contactRadius, contactRadius);
		const auto forumLeft = sample->width() - inset - size;
		if (forumLeft > textStart + textWidth + inset) {
			painter.setBrush(st::windowSubTextFg);
			painter.drawRoundedRect(forumLeft, top, size, size, forumRadius, forumRadius);
		}
	}, sample->lifetime());
	style::PaletteChanged() | rpl::on_next([=] { sample->update(); }, sample->lifetime());
	return [=](int next) {
		if (*value != next) {
			*value = next;
			sample->update();
		}
	};
}

not_null<Ui::FixedHeightWidget*> AddStickerPreview(
		not_null<Ui::VerticalLayout*> list,
		const QString &label,
		not_null<rpl::variable<int>*> scale) {
	using Timestamp = Core::Telega::StickerTimestamp;
	const auto sample = list->add(object_ptr<Ui::FixedHeightWidget>(
		list, style::ConvertScale(136)));
	sample->setAccessibleName(label);
	const auto icon = Lottie::MakeIcon({
		.name = u"greeting"_q,
		.sizeOverride = QSize(256, 256),
	});
	const auto image = icon ? icon->frame() : QImage();
	const auto time = QTime::currentTime();
	sample->paintRequest() | rpl::on_next([=](const QRect &clip) {
		auto painter = QPainter(sample);
		painter.fillRect(clip, st::windowBg);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setRenderHint(QPainter::SmoothPixmapTransform);
		painter.setPen(Qt::NoPen);
		const auto inset = style::ConvertScale(14);
		const auto available = std::max(0, sample->width() - 2 * inset);
		const auto size = std::min(
			style::ConvertScale(80 * scale->current() / 100),
			available);
		const auto sticker = QRect(
			sample->width() - inset - size,
			(sample->height() - size) / 2,
			size,
			size);
		const auto line = style::ConvertScale(4);
		const auto textWidth = std::clamp(
			sticker.x() - 2 * inset, 0, style::ConvertScale(116));
		painter.setBrush(st::windowBgOver);
		painter.drawRoundedRect(inset, sample->height() / 2 - 2 * line,
			textWidth, line, line / 2., line / 2.);
		painter.drawRoundedRect(inset, sample->height() / 2 + line,
			textWidth * 2 / 3, line, line / 2., line / 2.);
		painter.drawImage(sticker, image);
		const auto mode = Core::Telega::StickerTime();
		if (mode == Timestamp::Hidden
			|| (mode == Timestamp::Automatic && !sample->underMouse())) {
			return;
		}
		const auto text = Core::Telega::FormatTime(time);
		const auto font = st::msgDateFont;
		const auto width = font->width(text) + 2 * st::msgDateImgPadding.x();
		const auto height = font->height + 2 * st::msgDateImgPadding.y();
		const auto side = sticker.x() - width - inset / 2;
		const auto left = (mode == Timestamp::Always && side >= inset)
			? side
			: std::max(inset, sticker.x() + size - width);
		const auto badge = QRect(left, sticker.y() + size - height, width, height);
		painter.setBrush(st::msgServiceBg);
		painter.drawRoundedRect(badge, height / 2., height / 2.);
		painter.setPen(st::msgServiceFg);
		painter.setFont(font);
		painter.drawText(badge, Qt::AlignCenter, text);
	}, sample->lifetime());
	scale->changes() | rpl::on_next([=] { sample->update(); }, sample->lifetime());
	Core::Telega::DisplayValue(Core::Telega::DisplayOption::TimeWithSeconds)
	| rpl::on_next([=] { sample->update(); }, sample->lifetime());
	style::PaletteChanged() | rpl::on_next([=] { sample->update(); }, sample->lifetime());
	sample->events() | rpl::filter([](not_null<QEvent*> event) {
		return event->type() == QEvent::Enter || event->type() == QEvent::Leave;
	}) | rpl::on_next([=] { sample->update(); }, sample->lifetime());
	return sample;
}

void SetupFolderAppearance(
		not_null<Ui::VerticalLayout*> list,
		not_null<Window::SessionController*> controller) {
	const auto settings = &Core::App().settings();
	AddIndexChoice(list, controller, Text(u"Заголовок папки"_q, u"Folder title"_q), {
		tr::lng_filters_tabs_default(tr::now), tr::lng_filters_tabs_text_icons(tr::now),
		tr::lng_filters_tabs_icons(tr::now), tr::lng_filters_tabs_text(tr::now),
	}, [=] { return int(settings->chatFiltersTabsMode()); }, [=](int index) {
		settings->setChatFiltersTabsMode(Ui::ChatsFiltersTabsMode(index));
		Core::App().saveSettingsDelayed();
	});
	const auto sample = list->add(object_ptr<Ui::FixedHeightWidget>(list, style::ConvertScale(40)));
	const auto labels = std::array{
		Text(u"Все чаты"_q, u"All chats"_q),
		Text(u"Группы"_q, u"Groups"_q),
		Text(u"Каналы"_q, u"Channels"_q),
	};
	const auto icons = std::array{ &st::menuIconChatBubble, &st::menuIconGroups, &st::menuIconChannel };
	using Mode = Ui::ChatsFiltersTabsMode;
	sample->paintRequest() | rpl::on_next([=](const QRect &clip) {
		auto painter = QPainter(sample);
		painter.fillRect(clip, st::windowBg);
		const auto mode = Ui::HorizontalChatsFiltersTabsMode(settings->chatFiltersTabsMode());
		const auto showText = (mode != Mode::IconsOnly);
		const auto showIcons = (mode != Mode::TextOnly);
		const auto width = sample->width() / int(labels.size());
		const auto font = st::normalFont;
		const auto inset = style::ConvertScale(6);
		painter.setFont(font->f);
		for (auto index = 0; index != int(labels.size()); ++index) {
			const auto icon = icons[index];
			const auto iconWidth = showIcons ? icon->width() : 0;
			const auto gap = (showText && showIcons) ? inset : 0;
			const auto label = showText ? QFontMetrics(font->f).elidedText(labels[index], Qt::ElideRight,
				std::max(0, width - 2 * inset - iconWidth - gap)) : QString();
			const auto textWidth = font->width(label);
			const auto start = index * width + (width - iconWidth - gap - textWidth) / 2;
			if (showIcons) {
				icon->paint(painter, start, (sample->height() - icon->height()) / 2, sample->width());
			}
			if (!label.isEmpty()) {
				painter.setPen(index ? st::windowSubTextFg : st::windowActiveTextFg);
				painter.drawText(QRect(start + iconWidth + gap, 0, textWidth, sample->height()),
					Qt::AlignVCenter, label);
			}
		}
		painter.fillRect(inset, sample->height() - st::lineWidth,
			std::max(0, width - 2 * inset), st::lineWidth, st::windowBgActive);
	}, sample->lifetime());
	settings->chatFiltersTabsModeValue() | rpl::on_next([=] { sample->update(); }, sample->lifetime());
	style::PaletteChanged() | rpl::on_next([=] { sample->update(); }, sample->lifetime());
	AddIndexChoice(list, controller, Text(u"Счётчик уведомлений"_q, u"Notification count"_q), {
		Text(u"Все чаты"_q, u"All chats"_q), Text(u"Только со звуком"_q, u"Unmuted chats only"_q),
	}, [=] { return settings->includeMutedCounterFolders() ? 0 : 1; }, [=](int index) {
		settings->setIncludeMutedCounterFolders(index == 0);
		Core::App().saveSettingsDelayed();
		Core::App().notifications().notifySettingsChanged(
			Window::Notifications::ChangeType::IncludeMuted);
	});
}

class AppIconOption final : public QAbstractButton {
public:
	AppIconOption(
		QWidget *parent,
		QString label,
		Core::Telega::AppIconAppearance appearance)
	: QAbstractButton(parent)
	, _style(appearance.style) {
		setText(label);
		setAccessibleName(label);
		setCheckable(true);
		setFocusPolicy(Qt::StrongFocus);
		setCursor(Qt::PointingHandCursor);
		setFixedSize(style::ConvertScale(112), style::ConvertScale(108));
		setRoundedSquare(appearance.roundedSquare);
	}

	void setRoundedSquare(bool roundedSquare) {
		_roundedSquare = roundedSquare;
		_icon = Window::CreateApplicationIconPreview({ _style, roundedSquare });
		update();
	}

	void setCustomImage(const QImage &image) {
		_custom = true;
		_roundedSquare = true;
		_icon = image.isNull() ? QIcon() : QIcon(QPixmap::fromImage(image));
		setEnabled(!image.isNull());
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override {
		auto painter = QPainter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		const auto iconSize = style::ConvertScale(64);
		const auto inset = style::ConvertScale(4);
		const auto icon = QRect((width() - iconSize) / 2, inset + 2, iconSize, iconSize);
		if (isChecked() || hasFocus() || underMouse()) {
			painter.setPen(QPen(isChecked()
				? st::windowActiveTextFg->c
				: st::windowSubTextFg->c, isChecked() ? 2 * st::lineWidth : st::lineWidth));
			painter.setBrush(Qt::NoBrush);
			const auto ring = QRectF(icon).adjusted(-inset, -inset, inset, inset);
			if (_roundedSquare) {
				const auto radius = ring.width() * .23;
				painter.drawRoundedRect(ring, radius, radius);
			} else {
				painter.drawEllipse(ring);
			}
		}
		_icon.paint(&painter, icon);
		if (_custom && _icon.isNull()) {
			painter.setPen(st::windowSubTextFg);
			painter.setFont(st::semiboldFont->f);
			painter.drawText(icon, Qt::AlignCenter, u"+"_q);
		}
		const auto font = st::normalFont;
		painter.setFont(font->f);
		painter.setPen(isChecked() ? st::windowActiveTextFg : st::windowFg);
		const auto label = text();
		painter.drawText(
			QRect(0, icon.bottom() + 2 * inset, width(), height() - icon.bottom() - 2 * inset),
			Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
			label);
	}

private:
	const Core::Telega::AppIconStyle _style;
	QIcon _icon;
	bool _roundedSquare = false;
	bool _custom = false;

};

void SetupTelegaAppIcon(
		not_null<Ui::VerticalLayout*> list,
		not_null<Window::SessionController*> controller) {
	using IconStyle = Core::Telega::AppIconStyle;
	const auto title = Text(u"Логотип приложения"_q, u"App logo"_q);
	const auto button = AddButtonWithIcon(
		list,
		rpl::single(title),
		st::macSettingsPageButton,
		{ .icon = &st::menuIconPalette, .type = IconType::Simple });
	button->setAccessibleName(title);
	button->setClickedCallback([=] {
		controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(rpl::single(title));
			box->setWidth(style::ConvertScale(420));
			box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });
			const auto preferred = Core::Telega::ApplicationIconAppearance();
			const auto customId = int(IconStyle::Blue) + 1;
			const auto customImage = box->lifetime().make_state<QImage>(
				Window::SavedCustomApplicationIcon());
			const auto customSelected = Window::CustomApplicationIconSelected()
				&& !customImage->isNull();
			const auto choices = std::array{
				std::pair(IconStyle::Default, Text(u"Стандартная"_q, u"Default"_q)),
				std::pair(IconStyle::Blue, u"Telega"_q),
				std::pair(IconStyle::Accent, Platform::IsWindows()
					? Text(u"Акцент Windows"_q, u"Windows Accent"_q)
					: Text(u"Системный акцент"_q, u"System Accent"_q)),
				std::pair(IconStyle::Dark, Text(u"Тёмная"_q, u"Dark"_q)),
				std::pair(IconStyle::Light, Text(u"Светлая"_q, u"Light"_q)),
			};
			const auto row = box->addRow(object_ptr<Ui::FixedHeightWidget>(
				box,
				style::ConvertScale(224)));
			const auto grid = new QGridLayout(row);
			grid->setContentsMargins(0, 0, 0, 0);
			grid->setSpacing(0);
			grid->setAlignment(Qt::AlignCenter);
			const auto options = new QButtonGroup(row);
			for (auto index = 0; index != int(choices.size()); ++index) {
				const auto &[style, label] = choices[index];
				const auto option = new AppIconOption(row, label, { style, preferred.roundedSquare });
				options->addButton(option, int(style));
				option->setChecked(!customSelected && style == preferred.style);
				grid->addWidget(option, index / 3, index % 3);
			}
			const auto custom = new AppIconOption(row,
				Text(u"Своя иконка"_q, u"Custom icon"_q), { IconStyle::Default, true });
			custom->setCustomImage(*customImage);
			options->addButton(custom, customId);
			custom->setChecked(customSelected);
			grid->addWidget(custom, 1, 2);
			AddAction(box->verticalLayout(),
				Text(u"Загрузить свою иконку…"_q, u"Import a custom icon…"_q), [=] {
					FileDialog::GetOpenPath(
						box.get(),
						Text(u"Выбор иконки приложения"_q, u"Choose an app icon"_q),
						Text(u"Изображения"_q, u"Images"_q)
							+ u" (*.png *.ico *.jpg *.jpeg *.bmp *.webp)"_q,
						crl::guard(box, [=](FileDialog::OpenResult &&result) {
							if (result.paths.isEmpty()) {
								return;
							}
							auto image = Window::ReadCustomApplicationIcon(result.paths.front());
							if (image.isNull()) {
								controller->showToast(Text(
									u"Не удалось открыть изображение для иконки."_q,
									u"Could not open the icon image."_q));
								return;
							}
							*customImage = std::move(image);
							custom->setCustomImage(*customImage);
							custom->setChecked(true);
						}));
				});
			const auto square = box->addRow(object_ptr<Ui::Checkbox>(
				box,
				Text(u"Форма скруглённого квадрата"_q, u"Rounded square shape"_q),
				preferred.roundedSquare,
				st::defaultBoxCheckbox));
			QObject::connect(custom, &QAbstractButton::toggled, square, [=](bool checked) {
				square->setDisabled(checked);
			});
			square->setDisabled(customSelected);
			square->checkedChanges() | rpl::on_next([=](bool roundedSquare) {
				for (const auto option : options->buttons()) {
					if (option != custom) {
						static_cast<AppIconOption*>(option)->setRoundedSquare(roundedSquare);
					}
				}
			}, box->lifetime());
			box->addRow(object_ptr<Ui::FlatLabel>(box, rpl::single(Text(
				u"Логотип меняется после перезапуска Telega."_q,
				u"The app logo changes after restarting Telega."_q)), st::boxDividerLabel));
			box->addButton(rpl::single(Text(u"Выбрать"_q, u"Select"_q)), [=] {
				const auto selected = options->checkedId();
				const auto useCustom = (selected == customId);
				if (selected < 0) {
					return;
				}
				const auto active = Core::Telega::ActiveApplicationIconAppearance();
				const auto &activeCustom = Window::ActiveCustomApplicationIcon();
				auto restart = false;
				if (useCustom) {
					if (!Window::SaveCustomApplicationIcon(*customImage)) {
						controller->showToast(Text(
							u"Не удалось сохранить иконку."_q,
							u"Could not save the icon."_q));
						return;
					}
					restart = activeCustom != *customImage;
				} else {
					const auto choice = Core::Telega::AppIconAppearance{
						.style = IconStyle(selected),
						.roundedSquare = square->checked(),
					};
					Core::Telega::SetApplicationIconAppearance(choice);
					restart = !activeCustom.isNull()
						|| choice.style != active.style
						|| choice.roundedSquare != active.roundedSquare;
				}
				if (Platform::IsWindows()) {
					Core::App().settings().setTrayIconMonochrome(false);
				}
				Window::SelectCustomApplicationIcon(useCustom);
				box->closeBox();
				if (restart) {
					OfferSettingsRestart(controller);
				}
			});
			box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		}));
	});
}

void AddAiKeyAction(
		not_null<Ui::VerticalLayout*> list,
		not_null<Window::SessionController*> controller,
		QString title,
		Gemini::Provider provider = Gemini::Provider::Gemini) {
	static auto changed = rpl::event_stream<>();
	AddValueAction(list, title, (rpl::single(rpl::empty) | rpl::then(changed.events()))
		| rpl::map([=] {
			return Gemini::HasKey(provider) ? u"••••••••"_q : Text(u"Не задан"_q, u"Not set"_q);
		}), [=] {
		controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(rpl::single(title));
			const auto row = box->addRow(object_ptr<Ui::RpWidget>(box));
			const auto field = new Ui::PasswordInput(row, st::defaultInputField,
				rpl::single(Text(u"Ключ %1"_q, u"%1 API key"_q)
					.arg(Gemini::ProviderName(provider))));
			const auto height = field->sizeHint().height();
			row->resize(row->width(), height);
			row->widthValue() | rpl::on_next([=](int width) {
				field->resize(width, height);
			}, row->lifetime());
			field->setMaxLength(256);
			field->setAccessibleName(title);
			box->addRow(object_ptr<Ui::FlatLabel>(box, rpl::single(Text(
				u"Ключ хранится только до перезапуска приложения."_q,
				u"The key is only kept until the app restarts."_q)), st::boxDividerLabel));
			box->setFocusCallback([=] { field->setFocusFast(); });
			const auto save = [=] {
				const auto key = field->text().trimmed();
				if (key.isEmpty()) {
					field->showError();
					return;
				}
				Gemini::SetKey(key.toUtf8(), provider);
				changed.fire({});
				box->closeBox();
			};
			QObject::connect(field, &Ui::PasswordInput::submitted, box.get(), save);
			box->addButton(tr::lng_settings_save(), save);
			box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
			if (Gemini::HasKey(provider)) {
				box->addLeftButton(rpl::single(Text(u"Удалить ключ"_q, u"Remove key"_q)), [=] {
					Gemini::SetKey({}, provider);
					changed.fire({});
					box->closeBox();
				});
			}
		}));
	});
}

void SetupTelegaTranscription(
		not_null<Ui::VerticalLayout*> list,
		not_null<Window::SessionController*> controller) {
	const auto voice = list->lifetime().make_state<rpl::variable<bool>>(
		Gemini::Preference("transcribeVoice", 0) != 0);
	const auto round = list->lifetime().make_state<rpl::variable<bool>>(
		Gemini::Preference("transcribeRoundVideo", 0) != 0);
	auto providers = rpl::combine(voice->value(), round->value(), [](bool audio, bool video) {
		return audio && video ? u"Gemini"_q : !audio && !video ? u"Telega"_q
			: Text(u"По типу записи"_q, u"By recording type"_q);
	});
	const auto rows = AddDisclosure(list, Text(u"Расшифровка голосовых и видеосообщений"_q,
		u"Voice and video message transcription"_q), std::move(providers));
	const auto add = [&](QString title, const char *key, not_null<rpl::variable<bool>*> state) {
		AddIndexChoice(rows, controller, std::move(title), { u"Telega"_q, u"Gemini"_q },
			[=] { return state->current() ? 1 : 0; }, [=](int index) {
				Gemini::SetPreference(key, index);
				*state = (index != 0);
			});
	};
	add(Text(u"Голосовые сообщения"_q, u"Voice messages"_q), "transcribeVoice", voice);
	add(Text(u"Видеосообщения"_q, u"Video messages"_q), "transcribeRoundVideo", round);
	AddAiKeyAction(list, controller, Text(u"Ключ Gemini"_q, u"Gemini key"_q));
}

class Themes final : public TelegaPage<Themes> {
public:
	Themes(QWidget *parent, not_null<Window::SessionController*> controller)
	: TelegaPage(parent, controller) {
		beginGroups();
		const auto library = addGroup(Text(u"Мои темы"_q, u"My Themes"_q),
			u"настроить цвет цветовая тема создать импорт экспорт сохранить custom colors library"_q);
		SetupLocalThemes(controller, library);
		addGroupFooter(Text(
			u"Созданные и импортированные темы хранятся на этом компьютере. Чтобы перенести тему, экспортируйте её в файл."_q,
			u"Created and imported themes are stored on this computer. Export a theme to a file to move it elsewhere."_q));
		const auto wallpaper = addGroup(Text(u"Фон чатов"_q, u"Chat wallpaper"_q),
			u"яркость обои фон wallpaper background brightness"_q);
		SetupBackgroundBrightness(wallpaper, controller);
		SetupChatBackground(controller, wallpaper);
		addGroupFooter(Text(
			u"Яркость меняет только обои, а не текст и сообщения. 100% — исходная яркость."_q,
			u"Brightness affects only the wallpaper, not text or messages. 100% is the original brightness."_q));
		const auto other = addGroup(QString(), u"чат тема chat theme"_q);
		AddAction(other, Text(u"Настройки оформления чатов"_q, u"Chat appearance settings"_q), [=] {
			showOther(ChatId());
		});
		finishGroups();
	}

	rpl::producer<QString> title() override {
		return rpl::single(Text(u"Темы и фон"_q, u"Themes and wallpaper"_q));
	}

};

void ShowMaterialDesignSettings(
		not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		using Option = Core::Telega::MaterialOption;
		const auto read = Core::Telega::PreferredMaterialOption;
		box->setTitle(rpl::single(u"Material Design 3"_q));
		box->setWidth(style::ConvertScale(456));
		box->setMaxHeight(style::ConvertScale(720));
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		const auto content = AddGroupedSettingsContent(box->verticalLayout());
		const auto preview = content->add(object_ptr<Ui::FixedHeightWidget>(
			content, style::ConvertScale(180)));
		preview->setAccessibleName(Text(u"Предпросмотр стиля"_q, u"Style preview"_q));
		preview->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(preview);
			p.setRenderHint(QPainter::Antialiasing);
			p.setPen(Qt::NoPen);
			const auto s = [](int size) { return style::ConvertScale(size); };
			const auto bg = st::boxBg->c;
			const auto accent = st::windowActiveTextFg->c;
			const auto card = style::MaterialTone(bg, accent, .09);
			const auto selected = style::MaterialTone(bg, accent, .34);
			const auto line = [&](QRectF area, int radius, QColor color) {
				p.setBrush(color);
				p.drawRoundedRect(area, radius, radius);
			};
			const auto w = preview->width();
			if (w < s(240)) {
				return;
			}
			const auto left = w * .4;
			line(preview->rect(), s(20), card);
			line({ qreal(s(10)), qreal(s(10)), left - s(15), qreal(s(160)) },
				read(Option::RoundedMenu) ? s(14) : 0, bg);
			line({ left + s(5), qreal(s(10)), w - left - s(15), qreal(s(160)) },
				s(14), bg);
			const auto roundedSelection = read(Option::RoundedSelection);
			const auto colors = std::array{
				QColor(239, 144, 175),
				QColor(129, 198, 129),
				QColor(105, 178, 241),
				QColor(255, 190, 96),
			};
			for (auto i = 0; i != 4; ++i) {
				const auto y = s(19 + 38 * i);
				if (i == 1) {
					line({ qreal(s(13)), qreal(y - s(3)), left - s(21), qreal(s(34)) },
						roundedSelection ? s(12) : 0, selected);
				}
				p.setBrush(colors[i]);
				p.drawEllipse(QRect(s(17), y, s(26), s(26)));
				line({ qreal(s(50)), qreal(y + s(5)), (left - s(64)) * .72, qreal(s(5)) },
					s(3), st::windowFg->c);
				line({ qreal(s(50)), qreal(y + s(15)), left - s(64), qreal(s(4)) },
					s(2), st::windowSubTextFg->c);
			}
			const auto right = w - left - s(34);
			const auto grouped = read(Option::Grouped);
			const auto switches = read(Option::Switches);
			const auto mutedSwitches = read(Option::MutedSwitches);
			for (auto i = 0; i != 3; ++i) {
				const auto y = s(20 + 26 * i);
				line({ left + s(14), qreal(y), right, qreal(s(23)) },
					grouped ? s(6) : 0, card);
				line({ left + s(24), qreal(y + s(9)), right * .42, qreal(s(5)) },
					s(3), st::windowFg->c);
				const auto muted = i == 2 && mutedSwitches;
				line({ w - qreal(s(55)), qreal(y + s(5)), qreal(s(23)), qreal(s(13)) },
					s(7), muted ? st::windowSubTextFg->c : accent);
				if (switches) {
					line({ w - qreal(s(muted ? 53 : 44)), qreal(y + s(7)),
						qreal(s(9)), qreal(s(9)) }, s(5), bg);
				}
			}
			const auto floating = read(Option::FloatingField);
			line({ left + s(14), qreal(s(133)), right - s(33), qreal(s(28)) },
				floating ? s(14) : 0, floating ? card : bg);
			line({ left + s(25), qreal(s(145)), right * .28, qreal(s(5)) },
				s(3), st::windowSubTextFg->c);
			line({ w - qreal(s(48)), qreal(s(133)), qreal(s(28)), qreal(s(28)) },
				floating ? s(14) : s(5), accent);
		}, preview->lifetime());
		style::PaletteChanged() | rpl::on_next([=] {
			preview->update();
		}, preview->lifetime());
		const auto changed = box->lifetime().make_state<bool>(false);
		const auto save = [=](Option option, bool value) {
			if (value != read(option)) {
				Core::Telega::SetPreferredMaterialOption(option, value);
				*changed = true;
				preview->update();
			}
		};
		const auto add = [=](
				not_null<Ui::VerticalLayout*> rows,
				Option option,
				QString title) {
			AddToggle(rows, std::move(title), rpl::single(read(option)),
				[=](bool value) { save(option, value); });
		};
		[[maybe_unused]] const auto appearanceHeading = AddGroupedSettingsHeader(
			content, rpl::single(Text(u"Вид"_q, u"Appearance"_q)));
		const auto appearance = AddGroupedSettingsRows(content);
		AddIndexChoice(appearance, controller, Text(
			u"Шрифт по умолчанию"_q, u"Default font"_q), {
			u"Google Sans"_q,
			Text(u"Стандартный"_q, u"Default"_q),
		}, [=] { return read(Option::GoogleSans) ? 0 : 1; },
		[=](int index) { save(Option::GoogleSans, index == 0); });
		add(appearance, Option::Grouped,
			Text(u"Сгруппированные пункты списков"_q, u"Grouped list items"_q));
		add(appearance, Option::RoundedSelection,
			Text(u"Скруглённое выделение чатов"_q, u"Rounded chat selection"_q));
		add(appearance, Option::FloatingField,
			Text(u"Плавающее поле ввода"_q, u"Floating message field"_q));
		add(appearance, Option::RoundedMenu,
			Text(u"Скруглённое боковое меню"_q, u"Rounded side menu"_q));
		add(appearance, Option::Expressive,
			Text(u"Анимации Material Expressive"_q, u"Material Expressive animations"_q));
		FinishGroupedSettingsRows(appearance, &st::boxBg);
		AddGroupedSettingsFooter(content, rpl::single(Text(
			u"Изменения применяются после перезапуска. Шрифт, выбранный в «Оформлении», имеет приоритет."_q,
			u"Changes apply after restarting. A font chosen in Appearance takes priority."_q)));
		[[maybe_unused]] const auto controlsHeading = AddGroupedSettingsHeader(
			content, rpl::single(Text(u"Элементы управления"_q, u"Controls"_q)));
		const auto controls = AddGroupedSettingsRows(content);
		add(controls, Option::Switches,
			Text(u"Стиль переключателей"_q, u"Switch style"_q));
		add(controls, Option::MutedSwitches,
			Text(u"Приглушённые выключенные переключатели"_q, u"Muted off switches"_q));
		add(controls, Option::Sliders,
			Text(u"Стиль слайдеров"_q, u"Slider style"_q));
		FinishGroupedSettingsRows(controls, &st::boxBg);
		Ui::AddSkip(content, style::ConvertScale(12));
		box->boxClosing() | rpl::on_next([=] {
			if (*changed && Core::Telega::UseMaterial3Style()) {
				crl::on_main(controller->widget(), [=] {
					OfferSettingsRestart(controller);
				});
			}
		}, box->lifetime());
	}));
}

class Appearance final : public TelegaPage<Appearance> {
public:
	Appearance(QWidget *parent, not_null<Window::SessionController*> controller)
	: TelegaPage(parent, controller) {
		using Option = Core::Telega::DisplayOption;
		beginGroups();
		using Style = Core::Telega::InterfaceStyle;
		const auto styles = std::array{
			Style::MacOS,
			Style::Windows,
			Style::Material3,
			Style::Windows11,
		};
		const auto interfaceRows = addGroup(QString(),
			u"style design macos windows material стиль дизайн"_q);
		const auto selectedStyle = interfaceRows->lifetime().make_state<rpl::variable<Style>>(
			Core::Telega::PreferredInterfaceStyle());
		const auto styleNames = QStringList{
			u"macOS"_q, u"Telegram"_q, u"Material Design 3"_q, u"Unigram"_q,
		};
		const auto styleChoice = AddIndexChoice(
			interfaceRows,
			controller,
			Text(u"Выбор стиля"_q, u"Interface style"_q),
			styleNames,
			[=] {
				const auto preferred = selectedStyle->current();
				const auto i = std::find(styles.begin(), styles.end(), preferred);
				return int(i - styles.begin());
			},
			[=](int index) {
				const auto selected = styles[index];
				if (selected != Core::Telega::PreferredInterfaceStyle()) {
					Core::Telega::SetPreferredInterfaceStyle(selected);
					*selectedStyle = selected;
					OfferSettingsRestart(controller);
				}
			});
		const auto beta = Text(u"бета"_q, u"beta"_q);
		styleChoice->setAccessibleDescription(beta);
		const auto badge = Ui::NewBadge::CreateNewBadge(styleChoice, rpl::single(beta));
		const auto &buttonStyle = styleChoice->st();
		auto longestValue = 0;
		for (const auto &name : styleNames) {
			longestValue = std::max(longestValue, buttonStyle.rightLabel.style.font->width(name));
		}
		const auto gap = buttonStyle.style.font->spacew;
		const auto reserved = buttonStyle.padding.right()
			+ std::max(buttonStyle.padding.right(), st::settingsButtonRightSkip)
			+ longestValue;
		styleChoice->widthValue() | rpl::on_next([=, &buttonStyle](int width) {
			const auto left = buttonStyle.padding.left() + styleChoice->fullTextWidth() + gap;
			badge->setVisible(left + badge->width() + gap + reserved <= width);
			const auto top = buttonStyle.padding.top()
				+ buttonStyle.style.font->ascent
				- st::settingsPremiumNewBadge.style.font->ascent
				- st::settingsPremiumNewBadgePadding.top();
			badge->moveToLeft(left, top, width);
		}, badge->lifetime());
		if (Platform::IsWindows()) {
			const auto title = Text(u"Настройки Material Design 3"_q,
				u"Material Design 3 settings"_q);
			const auto row = interfaceRows->add(object_ptr<Ui::SlideWrap<Button>>(
				interfaceRows,
				object_ptr<Button>(interfaceRows, rpl::single(title),
					st::macSettingsPageButtonNoIcon)));
			row->entity()->setAccessibleName(title);
			row->entity()->setClickedCallback([=] {
				ShowMaterialDesignSettings(controller);
			});
			row->toggleOn(selectedStyle->value() | rpl::map([](Style style) {
				return style == Style::Material3;
			}), anim::type::instant);
		}
		const auto font = addGroup(Text(u"Шрифт"_q, u"Font"_q),
			u"google sans системный шрифт system font"_q);
		SetupFontSettings(controller, font);
		const auto wallpaper = addGroup(QString(), u"живые обои live wallpaper"_q);
		AddAction(wallpaper, Text(u"Живые обои"_q, u"Live wallpaper"_q), [=] {
			ShowLiveWallpaperBox(controller);
		});
#ifdef Q_OS_WIN
		if (Core::Telega::UseMacOSStyle()) {
			AddIndexChoice(
				addGroup(Text(u"Окно"_q, u"Window"_q)),
				controller,
				Text(u"Расположение кнопок окна"_q, u"Window button position"_q),
				{
					Text(u"Слева — macOS"_q, u"Left — macOS"_q),
					Text(u"Справа — Windows"_q, u"Right — Windows"_q),
				},
				[] { return Platform::TitleControlsOnRight() ? 1 : 0; },
				[](int index) { Platform::SetTitleControlsOnRight(index == 1); });
		}
#endif
		const auto radiusTitle = Text(u"Скругление аватаров"_q, u"Avatar rounding"_q);
		const auto avatar = addGroup(radiusTitle, u"аватар квадрат круг avatar square round"_q);
		const auto radius = Core::Telega::AvatarCornerPercent();
		const auto avatarPreview = avatar->lifetime().make_state<Fn<void(int)>>();
		auto radii = QStringList();
		for (auto percent = 0; percent <= 50; ++percent) {
			radii.push_back(u"%1%"_q.arg(percent));
		}
		AddSteppedSlider(avatar, radiusTitle, { Text(u"Квадрат"_q, u"Square"_q),
			Text(u"Круг"_q, u"Circle"_q) }, radii, radius,
			[=](int value) {
				if (value != Core::Telega::AvatarCornerPercent()) {
					Core::Telega::SetAvatarCornerPercent(value);
					OfferSettingsRestart(controller);
				}
			}, [=](int value) { (*avatarPreview)(value); });
		*avatarPreview = AddAvatarPreview(avatar, radiusTitle, radius);
		const auto addRoundingToggle = [=](QString label, Option option) {
			AddToggle(avatar, std::move(label), Core::Telega::DisplayValue(option),
				[=](bool enabled) {
					if (enabled != Core::Telega::DisplayEnabled(option)) {
						Core::Telega::SetDisplayEnabled(option, enabled);
						OfferSettingsRestart(controller);
					}
				});
		};
		addRoundingToggle(Text(u"Единое скругление"_q, u"Same rounding for forums"_q),
			Option::UniformAvatarRadius);
		addRoundingToggle(Text(u"Скруглённые квадраты для аватаров"_q,
			u"Rounded-square avatars"_q), Option::RoundedContactAvatars);
		addGroupFooter(Text(
			u"Слева аватар чата, справа аватар форума. Единое скругление снимает ограничение 30% для форумов. Режим скруглённых квадратов ограничивает радиус всех аватаров до 30%. Изменения применяются после перезапуска."_q,
			u"Chat avatar on the left, forum avatar on the right. Uniform rounding removes the 30% cap for forums. Rounded-square mode caps all avatar radii at 30%. Changes apply after restarting."_q));
		const auto chats = addGroup(Text(u"Список чатов"_q, u"Chat list"_q));
		AddDisplayOption(chats, Text(u"Скрывать статус"_q, u"Hide sidebar status"_q),
			Option::HideSidebarStatus);
		AddToggle(chats, Text(u"Скрывать истории"_q, u"Hide stories"_q),
			Core::Telega::HideStoriesValue(), Core::Telega::SetHideStories);
#ifdef Q_OS_WIN
		AddToggle(chats, Text(u"Скрывать мини-аватары отправителей"_q,
			u"Hide sender mini avatars"_q),
			Core::Telega::DisplayValue(Option::HideSenderMiniAvatars), [=](bool enabled) {
				Core::Telega::SetDisplayEnabled(Option::HideSenderMiniAvatars, enabled);
				Ui::ForceFullRepaint(controller->widget());
			});
#endif // Q_OS_WIN
		const auto folders = addGroup(Text(u"Папки с чатами"_q, u"Chat folders"_q));
		SetupFolderAppearance(folders, controller);
		AddDisplayOption(folders, Text(u"Скрыть вкладку «Все чаты»"_q,
			u"Hide the All Chats tab"_q), Option::HideAllChatsFolder);
		AddAction(folders, Text(u"Управление папками"_q, u"Manage folders"_q), [=] {
			showOther(FoldersId());
		});
		SetupTelegaAppIcon(
			addGroup(QString(), u"app logo icon логотип значок иконка"_q),
			controller);
		const auto appearance = addGroup(Text(u"Оформление"_q, u"Appearance"_q));
		AddDisplayOption(appearance, Text(u"Системные эмодзи"_q, u"System emoji"_q),
			Option::SystemEmoji);
		AddToggle(appearance, Text(u"Собственные темы чатов"_q, u"Individual chat themes"_q),
			Core::Telega::DisplayValue(Option::IgnorePeerThemes)
				| rpl::map([](bool ignore) { return !ignore; }),
			[](bool enabled) { Core::Telega::SetDisplayEnabled(Option::IgnorePeerThemes, !enabled); });
		addGroupFooter(Text(u"В каждом чате будет тема, выбранная для него. Системные эмодзи применяются после перезапуска."_q,
			u"Each chat uses its selected theme. System emoji take effect after restart."_q));
		const auto general = addGroup(Text(u"Основные"_q, u"General"_q));
		AddDisplayOption(general, Text(u"Не округлять счётчики просмотров"_q, u"Show exact view counts"_q),
			Option::ExactViewCounts);
		AddDisplayOption(general, Text(u"Время с секундами"_q, u"Show seconds on messages"_q),
			Option::TimeWithSeconds);
		AddDisplayOption(general, Text(u"Фильтр Zalgo"_q, u"Zalgo filter"_q), Option::FilterVisualNoise);
		AddClientOption(general, controller, Text(u"Меньше скруглять сообщения"_q,
			u"Use less-rounded message bubbles"_q), Ui::kOptionUseSmallMsgBubbleRadius);
		const auto stickerTitle = Text(u"Размер стикеров"_q, u"Sticker size"_q);
		const auto stickers = addGroup(stickerTitle, u"время на стикерах sticker timestamps"_q);
		const auto scale = Core::Telega::StickerScale();
		const auto previewScale = stickers->lifetime().make_state<rpl::variable<int>>(scale);
		auto scales = QStringList();
		for (auto percent = 60; percent <= 140; percent += 5) {
			scales.push_back(u"%1%"_q.arg(percent));
		}
		AddSteppedSlider(stickers, stickerTitle, { Text(u"Маленький"_q, u"Small"_q),
			Text(u"Большой"_q, u"Large"_q) }, scales, (scale - 60) / 5,
			[](int index) { Core::Telega::SetStickerScale(60 + index * 5); },
			[=](int index) { *previewScale = 60 + index * 5; });
		const auto preview = AddStickerPreview(stickers, stickerTitle, previewScale);
		AddIndexChoice(stickers, controller, Text(u"Время на стикерах"_q, u"Sticker timestamps"_q), {
			Text(u"По умолчанию"_q, u"Automatic"_q), Text(u"Всегда сбоку"_q, u"Always show"_q),
			Text(u"Скрыть"_q, u"Hide"_q),
		}, [] { return int(Core::Telega::StickerTime()); }, [=](int index) {
			Core::Telega::SetStickerTime(Core::Telega::StickerTimestamp(index));
			preview->update();
		});
		addGroupFooter(Text(u"Размер стикеров обновится после перезапуска."_q,
			u"Sticker size updates after restart."_q));
		const auto features = addGroup(u"Telegram Premium"_q,
			u"Telegram Gemini Premium AI голосовые видеосообщения"_q);
		AddDisplayOption(features, Text(u"Скрытие рекламных сообщений (после перезапуска)"_q,
			u"Hide sponsored messages (after restart)"_q), Option::HideSponsoredMessages);
		const auto settings = &Core::App().settings();
		AddToggle(features, Text(u"Перевод всей переписки"_q, u"Translate entire chats"_q),
			settings->translateChatEnabledValue(), [=](bool enabled) {
				settings->setTranslateChatEnabled(enabled);
				Core::App().saveSettingsDelayed();
			});
		SetupTelegaTranscription(features, controller);
		addGroupFooter(Text(u"Gemini получает запись только при выборе команды расшифровки через Gemini."_q,
			u"Gemini receives the recording only when you choose its transcription action."_q));
		finishGroups();
	}

	rpl::producer<QString> title() override {
		return rpl::single(Text(u"Оформление"_q, u"Appearance"_q));
	}

};

void SetupQuickMessageActions(
		not_null<Ui::VerticalLayout*> layout,
		not_null<Window::SessionController*> controller) {
	using Option = Core::Telega::DisplayOption;
	AddDisplayOption(layout,
		Text(u"Панель быстрых действий"_q, u"Quick action row"_q),
		Option::QuickMessageActions);
	const auto title = Text(u"Кнопки панели действий"_q, u"Quick action buttons"_q);
	AddAction(layout, title, [=] {
		ShowOrderedSettings(controller, title,
			HistoryView::TelegaQuickActionOrder(),
			HistoryView::TelegaQuickActionIds(),
			HistoryView::TelegaQuickActionTitle,
			HistoryView::SetTelegaQuickActionOrder,
			HistoryView::kTelegaQuickActionLimit);
	});
}

class Messages final : public TelegaPage<Messages> {
public:
	Messages(QWidget *parent, not_null<Window::SessionController*> controller)
	: TelegaPage(parent, controller) {
		using Option = Core::Telega::DisplayOption;
		beginGroups();
		const auto chats = addGroup(Text(u"Чаты"_q, u"Chats"_q));
		AddChoice(chats, controller, Text(u"Нижняя кнопка в каналах"_q, u"Bottom button in channels"_q), {
			Text(u"Уведомления"_q, u"Notifications"_q),
			Text(u"Обсуждение"_q, u"Discussion"_q), Text(u"Скрыть"_q, u"Hide"_q),
		}, Core::Telega::BottomAction, Core::Telega::SetBottomAction);
		AddClientOption(chats, controller, Text(u"Широкие текстовые сообщения"_q, u"Wider text messages"_q),
			HistoryView::kOptionUnlimitedMessageWidth);
		AddDisplayOption(chats, Text(u"Широкие посты в каналах"_q, u"Wide channel posts"_q),
			Option::WideChannelPosts);
		AddClientOption(chats, controller, Text(u"Скрыть кнопку ИИ Telega при вводе"_q, u"Hide Telega’s compose AI button"_q),
			Ui::kOptionHideAiButton);
		AddDisplayOption(chats, Text(u"Скрыть стандартный приветственный стикер"_q,
			u"Hide the default greeting sticker"_q), Option::HideDefaultGreetingSticker);
		AddToggle(chats, Text(u"Запятая после упоминания"_q, u"Comma after a mention"_q),
			Core::Telega::CommaAfterMentionValue(), Core::Telega::SetCommaAfterMention);
		AddDisplayOption(chats, Text(u"Ответ с цитатой"_q, u"Reply with a quote"_q), Option::QuoteReplies);
		AddToggle(chats, Text(u"Скрывать уведомление о копировании"_q, u"Hide the text copied notification"_q),
			Core::Telega::HideCopyToastValue(), Core::Telega::SetHideCopyToast);
		AddDisplayOption(chats, Text(u"Счётчик символов"_q, u"Character count"_q), Option::CountCharacters);
		AddDisplayOption(chats, Text(u"Выделение служебных сообщений"_q,
			u"Select deletable service messages"_q), Option::SelectServiceMessages);
		AddDisplayOption(chats, Text(u"Участники в группах со скрытым списком"_q,
			u"Show known message authors when members are hidden"_q), Option::VisibleGroupAuthors);
		AddDisplayOption(chats, Text(u"Акцентный цвет имён"_q, u"Use the theme accent for sender names"_q),
			Option::AccentSenderNames);
		AddDisplayOption(chats, Text(u"Вычисления в поле ввода"_q, u"Compose calculator"_q), Option::InlineCalculator);
		AddDisplayOption(chats, Text(u"Скрыть кнопку «Отправить от имени»"_q,
			u"Hide the Send As button"_q), Option::HideSendAs);
		addGroupFooter(Text(u"Меню «Отправить от имени» остаётся доступным в меню чата."_q,
			u"Send As remains available in the chat menu."_q));
		const auto messages = addGroup(Text(u"Сообщения"_q, u"Messages"_q));
		const auto addVisualToggle = [&](QString text, rpl::producer<bool> value, void (*set)(bool)) {
			AddToggle(messages, std::move(text), std::move(value), [=](bool enabled) {
				set(enabled);
				Ui::ForceFullRepaint(controller->widget());
			});
		};
		addVisualToggle(Text(u"Убрать хвост сообщения"_q, u"Hide message bubble tails"_q),
			Core::Telega::HideBubbleTailsValue(), Core::Telega::SetHideBubbleTails);
		AddDisplayOption(messages, Text(u"Иконка вместо «изменено»"_q, u"Use an edited icon instead of text"_q),
			Option::EditedIcon);
		addVisualToggle(Text(u"Отключение цветных ответов"_q, u"Use plain reply previews"_q),
			Core::Telega::PlainReplyPreviewsValue(), Core::Telega::SetPlainReplyPreviews);
		AddDisplayOption(messages, Text(u"Индикатор «в сети»"_q, u"Show an online badge on sender avatars"_q),
			Option::OnlineSenderBadge);
		AddDisplayOption(messages, Text(u"Счётчик пересылок"_q, u"Show forward counts"_q), Option::ForwardCount);
		addVisualToggle(Text(u"Скрыть боковую кнопку «Поделиться»"_q, u"Hide the quick forward button"_q),
			Core::Telega::HideFastShareValue(), Core::Telega::SetHideFastShare);
		AddDisplayOption(messages, Text(u"Результаты до голосования"_q, u"Preview available poll results"_q),
			Option::PreviewPollResults);
		const auto menu = AddDisclosure(messages, Text(u"Меню сообщения"_q, u"Message context menu"_q),
			OptionCount({ Option::HideCopyPhotoAction, Option::HideSaveMediaAction,
				Option::RepeatMessageAction, Option::MessageDetailsAction }));
		AddDisplayOption(menu, Text(u"Скрыть «Копировать фото»"_q, u"Hide Copy Photo"_q), Option::HideCopyPhotoAction);
		AddDisplayOption(menu, Text(u"Скрыть сохранение медиа на диск"_q,
			u"Hide Save Media to Disk"_q), Option::HideSaveMediaAction);
		AddDisplayOption(menu, Text(u"Добавить «Повторить сообщение»"_q,
			u"Add Repeat Message"_q), Option::RepeatMessageAction);
		AddDisplayOption(menu, Text(u"Добавить «Сведения о сообщении»"_q,
			u"Add Message Details"_q), Option::MessageDetailsAction);
		SetupQuickMessageActions(messages, controller);
		const auto gestures = addGroup(Text(u"Жесты"_q, u"Gestures"_q), u"двойное нажатие click"_q);
		SetupGestures(gestures, controller);
		SetupMessages(controller, gestures);
		SetupChatListQuickAction(controller, gestures);
		const auto stickers = addGroup(Text(u"Стикеры и эмодзи"_q, u"Stickers and emoji"_q), u"реакции GIF"_q);
		const auto gif = stickers->lifetime().make_state<rpl::variable<QString>>(Core::Telega::GifSearchBot());
		const auto gifTitle = Text(u"Бот для поиска GIF"_q, u"GIF search bot"_q);
		AddValueAction(stickers, gifTitle, gif->value() | rpl::map([](const QString &value) {
			return value.isEmpty() ? u"@gif"_q : u"@"_q + value;
		}), [=] {
			const auto weak = QPointer<Ui::VerticalLayout>(stickers.get());
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				box->setTitle(rpl::single(gifTitle));
				const auto field = AddSettingsInput(box, u"@gif"_q, Core::Telega::GifSearchBot(), 33);
				box->setFocusCallback([=] { field->setFocusFast(); });
				box->addRow(object_ptr<Ui::FlatLabel>(box, rpl::single(Text(
					u"Запросы поиска GIF получит указанный бот. Если поле пустое, используется стандартный бот."_q,
					u"The selected bot receives GIF searches. Leave blank to use the default bot."_q)), st::boxDividerLabel));
				const auto save = [=] {
					if (!Core::Telega::SetGifSearchBot(field->text())) {
						field->showError();
						return;
					}
					if (weak) {
						*gif = Core::Telega::GifSearchBot();
					}
					box->closeBox();
				};
				QObject::connect(field.get(), &Ui::MaskedInputField::submitted, box.get(), save);
				box->addButton(tr::lng_settings_save(), save);
				box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
			}));
		});
		AddClientOption(stickers, controller, Text(u"Неограниченные недавние стикеры"_q,
			u"Show all available recent stickers"_q), ChatHelpers::kOptionUnlimitedRecentStickers);
		const auto reactions = AddDisclosure(stickers, Text(u"Скрывать реакции"_q, u"Hide reactions"_q),
			OptionCount({ Option::HidePrivateReactions, Option::HideGroupReactions, Option::HideChannelReactions }));
		AddDisplayOption(reactions, Text(u"В личных чатах"_q, u"In private chats"_q), Option::HidePrivateReactions);
		AddDisplayOption(reactions, Text(u"В группах"_q, u"In groups"_q), Option::HideGroupReactions);
		AddDisplayOption(reactions, Text(u"В каналах"_q, u"In channels"_q), Option::HideChannelReactions);
		SetupStickersEmoji(controller, stickers);
		AddToggle(addGroup(Text(u"Фото"_q, u"Photos"_q)),
			Text(u"Всегда отправлять в HD"_q, u"Prefer HD when sending photos"_q),
			Core::Telega::DefaultPhotoHdValue(), Core::Telega::SetDefaultPhotoHd);
		const auto video = addGroup(Text(u"Видео"_q, u"Video"_q), u"пауза pause original качество"_q);
		SetupVideoSeekStep(video, controller);
		AddToggle(video, Text(u"Исходное качество"_q, u"Original video quality"_q),
			Core::Telega::PreferOriginalVideoValue(), Core::Telega::SetPreferOriginalVideo);
		const auto pause = AddDisclosure(video, Text(u"Автопауза"_q, u"Auto pause"_q),
			rpl::combine(Core::Telega::PauseVideoInBackgroundValue(),
				Core::Telega::DisplayValue(Option::PauseVoiceInBackground),
				Core::Telega::DisplayValue(Option::PauseRoundVideoInBackground),
				[](bool videos, bool voice, bool rounds) { return u"%1/3"_q.arg(videos + voice + rounds); }));
		AddToggle(pause, Text(u"Видео"_q, u"Videos"_q), Core::Telega::PauseVideoInBackgroundValue(),
			Core::Telega::SetPauseVideoInBackground);
		AddDisplayOption(pause, Text(u"Голосовые сообщения"_q, u"Voice messages"_q), Option::PauseVoiceInBackground);
		AddDisplayOption(pause, Text(u"Видеосообщения"_q, u"Video messages"_q), Option::PauseRoundVideoInBackground);
		addGroupFooter(Text(u"Воспроизведение ставится на паузу при сворачивании Telega."_q,
			u"Playback pauses when Telega is minimized."_q));
		finishGroups();
	}

	rpl::producer<QString> title() override {
		return rpl::single(Text(u"Сообщения"_q, u"Messages"_q));
	}

private:
	void SetupGestures(
			not_null<Ui::VerticalLayout*> list,
			not_null<Window::SessionController*> controller) {
		using Action = HistoryView::DoubleClickQuickAction;
		const auto add = [&](bool outgoing, bool right, QString title) {
			auto actions = std::vector{ Action::None, Action::Reply, Action::React,
				Action::Edit, Action::Copy, Action::Forward };
			auto labels = QStringList{
				Text(u"Отключено"_q, u"Off"_q), Text(u"Ответить"_q, u"Reply"_q),
				Text(u"Любимая реакция"_q, u"Favorite reaction"_q), Text(u"Редактировать"_q, u"Edit"_q),
				Text(u"Копировать текст"_q, u"Copy text"_q), Text(u"Переслать"_q, u"Forward"_q),
			};
			if (!right) {
				actions.push_back(Action::Inherited);
				labels.push_back(Text(u"Настройка клиента"_q, u"Client default"_q));
			}
			AddIndexChoice(list, controller, std::move(title), std::move(labels), [=] {
				const auto action = HistoryView::MessageQuickAction(outgoing, right);
				const auto found = std::find(actions.begin(), actions.end(), action);
				return (found == actions.end()) ? 0 : int(found - actions.begin());
			}, [=](int index) { HistoryView::SetMessageQuickAction(outgoing, right, actions[index]); });
		};
		add(false, false, Text(u"Двойной левый клик: входящие"_q, u"Left double-click: incoming"_q));
		add(true, false, Text(u"Двойной левый клик: исходящие"_q, u"Left double-click: outgoing"_q));
		add(false, true, Text(u"Двойной правый клик: входящие"_q, u"Right double-click: incoming"_q));
		add(true, true, Text(u"Двойной правый клик: исходящие"_q, u"Right double-click: outgoing"_q));
	}

};

class Translation final : public TelegaPage<Translation> {
public:
	Translation(QWidget *parent, not_null<Window::SessionController*> controller)
	: TelegaPage(parent, controller)
	, _search(CreateSectionSearchRow(this)) {
		const auto settings = &Core::App().settings();
		const auto content = AddGroupedSettingsContent(_list);
		const auto inset = style::ConvertScale(8);
		content->add(object_ptr<Ui::RpWidget>::fromRaw(_search.row), { 0, inset, 0, 0 });
		const auto section = content->add(object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content, object_ptr<Ui::VerticalLayout>(content)));
		[[maybe_unused]] const auto heading = AddGroupedSettingsHeader(section->entity(), rpl::single(Text(
			u"Перевод сообщений"_q, u"Message translation"_q)));
		const auto group = AddGroupedSettingsRows(section->entity());
		using Row = Ui::SlideWrap<Ui::VerticalLayout>;
		auto searchRows = std::vector<std::pair<not_null<Row*>, QStringList>>();
		const auto addRow = [&](const QString &title, const QString &keywords) {
			const auto row = group->add(object_ptr<Row>(
				group, object_ptr<Ui::VerticalLayout>(group)));
			searchRows.emplace_back(row, SearchWords(title + u' ' + keywords));
			return row->entity();
		};
		const auto button = Text(u"Кнопка «Перевести»"_q, u"Translate button"_q);
		AddToggle(addRow(button, u"контекстное меню context menu"_q), button,
			rpl::single(settings->translateButtonEnabled()), [=](bool enabled) {
				settings->setTranslateButtonEnabled(enabled);
				Core::App().saveSettingsDelayed();
			});
		const auto chats = Text(u"Перевод чатов целиком"_q, u"Translate entire chats"_q);
		AddToggle(addRow(chats, QString()), chats,
			settings->translateChatEnabledValue(), [=](bool enabled) {
				settings->setTranslateChatEnabled(enabled);
				Core::App().saveSettingsDelayed();
			});
		const auto service = Text(u"Сервис перевода"_q, u"Translation service"_q);
		AddValueAction(addRow(service, u"Telega Google"_q), service,
			Core::Telega::UseGoogleTranslationValue() | rpl::map([](bool enabled) {
				return enabled ? u"Google"_q : u"Telega"_q;
			}), [=] {
				ShowRadioChoice(controller, service, { u"Telega"_q, u"Google"_q },
					Core::Telega::UseGoogleTranslation() ? 1 : 0, [=](int selected) {
						if (!selected) {
							Core::Telega::SetUseGoogleTranslation(false);
						} else if (!Core::Telega::UseGoogleTranslation()) {
							controller->show(Ui::MakeConfirmBox({
								.text = Text(
									u"Разрешить отправлять в Google текст сообщений, которые вы переводите? При переводе чата отправляется текст видимых сообщений. Защищённые сообщения не отправляются. Настройка действует для всех аккаунтов на этом устройстве."_q,
									u"Allow sending the text you translate to Google? Chat translation sends visible messages. Protected messages are excluded. This preference applies to all accounts on this device."_q),
								.confirmed = [] { Core::Telega::SetUseGoogleTranslation(true); },
								.confirmText = Text(u"Разрешить"_q, u"Allow"_q),
								.title = u"Google Translate"_q,
							}));
						}
					});
			});
		const auto language = Text(u"Язык перевода"_q, u"Translation language"_q);
		AddValueAction(addRow(language, u"язык интерфейса app language"_q), language,
			settings->translateToValue() | rpl::map(Ui::LanguageName), [=] {
				controller->show(Box([=](not_null<Ui::GenericBox*> box) {
					const auto save = [=](LanguageId id) {
						settings->setTranslateTo(id);
						Core::App().saveSettingsDelayed();
					};
					auto selected = std::vector<LanguageId>{ settings->translateTo() };
					for (const auto id : settings->skipTranslationLanguages()) {
						if (id != selected.front()) {
							selected.push_back(id);
						}
					}
					Ui::ChooseLanguageBox(box, rpl::single(language),
						[=](std::vector<LanguageId> ids) { save(ids.front()); },
						std::move(selected), false, nullptr);
					box->addLeftButton(rpl::single(Text(u"Как в интерфейсе"_q,
						u"Use app language"_q)), [=] {
							save(LanguageId());
							box->closeBox();
						});
				}));
			});
		const auto skip = Text(u"Не переводить"_q, u"Never translate"_q);
		AddValueAction(addRow(skip, u"языки исключения excluded languages"_q), skip,
			settings->skipTranslationLanguagesValue() | rpl::map([](const std::vector<LanguageId> &ids) {
				return ids.empty() ? QString() : Ui::LanguageName(ids.front())
					+ (ids.size() > 1 ? u" +%1"_q.arg(ids.size() - 1) : QString());
			}), [=] { controller->show(Ui::EditSkipTranslationLanguages()); });
		FinishGroupedSettingsRows(group);
		AddGroupedSettingsFooter(section->entity(), rpl::single(Text(
			u"Кнопка «Перевести» появляется в контекстном меню сообщения."_q,
			u"The Translate button appears in a message’s context menu."_q)));
		const auto empty = content->add(object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			content, object_ptr<Ui::FlatLabel>(content,
				rpl::single(Text(u"Ничего не найдено"_q, u"No results"_q)),
				st::macSettingsPageHeader), style::margins(inset, inset, inset, inset)));
		empty->hide(anim::type::instant);
		_search.controller->queryChanges() | rpl::on_next([=](QString query) {
			const auto words = SearchWords(query);
			auto matched = false;
			for (const auto &[row, terms] : searchRows) {
				const auto visible = MatchesWords(terms, words);
				row->toggle(visible, anim::type::instant);
				matched = matched || visible;
			}
			section->toggle(matched, anim::type::instant);
			empty->toggle(!matched, anim::type::instant);
		}, lifetime());
	}

	rpl::producer<QString> title() override {
		return rpl::single(Text(u"Перевод"_q, u"Translation"_q));
	}

private:
	const SectionSearchRow _search;
};

void SetupMessageArchive(
		not_null<Ui::VerticalLayout*> list,
		not_null<Window::SessionController*> controller) {
	using Option = Data::ArchiveOption;
	using Mark = Data::DeletedMark;
	const auto archive = &controller->session().data().messageArchive();
	const auto addToggle = [=](QString title, Option option) {
		AddToggle(list, std::move(title), archive->value(option), [=](bool enabled) {
			archive->setEnabled(option, enabled);
		});
	};
	addToggle(Text(u"Сохранять удалённые сообщения"_q, u"Save deleted messages"_q),
		Option::Deleted);
	addToggle(Text(u"Сохранять историю изменений"_q, u"Save edit history"_q),
		Option::Edits);
	addToggle(Text(u"Сохранять в чатах с ботами"_q, u"Save in bot chats"_q),
		Option::Bots);
	addToggle(Text(u"Полупрозрачные удалённые сообщения"_q,
		u"Translucent deleted messages"_q), Option::Translucent);

	const auto preview = list->add(object_ptr<Ui::FixedHeightWidget>(
		list, style::ConvertScale(66)));
	preview->setAccessibleName(Text(u"Предпросмотр сообщения"_q, u"Message preview"_q));
	preview->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(preview);
		p.setRenderHint(QPainter::Antialiasing);
		p.fillRect(preview->rect(), st::windowBg);
		const auto inset = style::ConvertScale(12);
		const auto width = std::min(style::ConvertScale(230), preview->width() - 2 * inset);
		if (width <= inset * 2) {
			return;
		}
		const auto bubble = QRect(inset, inset, width, preview->height() - 2 * inset);
		const auto radius = style::ConvertScale(14);
		if (archive->enabled(Option::Translucent)) {
			p.setOpacity(.55);
		}
		p.setPen(Qt::NoPen);
		p.setBrush(st::msgInBg);
		p.drawRoundedRect(bubble, radius, radius);
		p.setBrush(st::windowSubTextFg);
		const auto line = style::ConvertScale(4);
		p.drawRoundedRect(inset * 2, bubble.center().y() - line,
			std::min(width / 4, style::ConvertScale(66)), line, line / 2., line / 2.);
		auto mark = Data::DeletedMarkText(archive->mark());
		if (!mark.isEmpty()) {
			mark += ' ';
		}
		p.setFont(st::msgDateFont->f);
		p.setPen(st::windowSubTextFg);
		p.drawText(bubble.adjusted(inset, 0, -inset, -line),
			Qt::AlignRight | Qt::AlignBottom, mark + u"12:30"_q);
	}, preview->lifetime());
	archive->value(Option::Translucent) | rpl::on_next([=] {
		preview->update();
	}, preview->lifetime());
	archive->markValue() | rpl::on_next([=] {
		preview->update();
	}, preview->lifetime());
	style::PaletteChanged() | rpl::on_next([=] {
		preview->update();
	}, preview->lifetime());

	AddIndexChoice(list, controller, Text(u"Отметка удалённых"_q, u"Deleted message mark"_q), {
		Text(u"Без отметки"_q, u"None"_q),
		Text(u"Текст"_q, u"Text"_q),
		Text(u"Корзина"_q, u"Trash"_q),
		Text(u"Крестик"_q, u"Cross"_q),
		Text(u"Глаз"_q, u"Eye"_q),
	}, [=] { return int(archive->mark()); }, [=](int index) {
		archive->setMark(Mark(index));
	});
	const auto clear = Text(u"Очистить сохранённые сообщения"_q,
		u"Clear saved messages"_q);
	const auto button = list->add(object_ptr<Button>(
		list, rpl::single(clear), st::macSettingsPageAttentionButton));
	button->setAccessibleName(clear);
	button->setClickedCallback([=] {
		controller->show(Ui::MakeConfirmBox({
			.text = Text(
				u"Удалить сохранённые сообщения и историю изменений этого аккаунта с устройства?"_q,
				u"Clear saved messages and edit history for this account on this device?"_q),
			.confirmed = [=] {
				archive->clear();
				controller->showToast(Text(u"Сохранённые сообщения очищены."_q,
					u"Saved messages cleared."_q));
			},
			.confirmText = Text(u"Очистить"_q, u"Clear"_q),
			.confirmStyle = &st::attentionBoxButton,
		}));
	});
}

class Privacy final : public TelegaPage<Privacy> {
public:
	Privacy(QWidget *parent, not_null<Window::SessionController*> controller)
	: TelegaPage(parent, controller) {
		using Option = Core::Telega::DisplayOption;
		const auto session = &controller->session();
		const auto settings = &session->settings();
		beginGroups();
		const auto profile = addGroup(Text(u"Профиль"_q, u"Profile"_q), u"id dc номер phone"_q);
		AddDisplayOption(profile, Text(u"Относительное время в сети"_q, u"Relative last-seen time"_q),
			Option::RelativeLastSeen);
		AddToggle(profile, Text(u"Скрывать номер телефона"_q, u"Hide my phone number"_q),
			settings->phoneNumberHiddenValue(), [=](bool enabled) {
				settings->setPhoneNumberHidden(enabled);
				session->saveSettingsDelayed();
			});
		AddClientOption(profile, controller, Text(u"Показывать ID в профилях"_q, u"Show peer IDs in profiles"_q),
			Info::Profile::kOptionShowPeerIdBelowAbout);
		AddDisplayOption(profile, Text(u"Показывать дата-центр фото профиля"_q,
			u"Show the profile photo’s data center"_q), Option::ProfilePhotoDc);
		addGroupFooter(Text(u"Скрытие номера действует только на этом устройстве."_q,
			u"The phone number is hidden on this device."_q));
		const auto archive = addGroup(Text(u"Архив"_q, u"Archive"_q));
		AddToggle(archive, Text(u"Скрыть папку архива"_q, u"Move the archive out of the chat list"_q),
			rpl::single(settings->archiveInMainMenu()) | rpl::then(settings->archiveInMainMenuChanges()),
			[=](bool enabled) {
				settings->setArchiveInMainMenu(enabled);
				session->saveSettingsDelayed();
			});
		AddAction(archive, Text(u"Настройки архива"_q, u"Archive settings"_q), [=] {
			controller->show(Box(ArchiveSettingsBox, controller));
		});
		addGroupFooter(Text(u"Архив переносится в главное меню."_q, u"The archive moves to the main menu."_q));
		if (Platform::ScreenshotProtectionSupported()) {
			AddToggle(addGroup(QString(), u"скриншот запись захват экрана screenshot recording capture"_q),
				Text(u"Защита от записи экрана"_q, u"Protect the main window from capture"_q),
				Core::Telega::ProtectMainWindowValue(), Core::Telega::SetProtectMainWindow);
		}
		const auto visibility = addGroup(Text(u"Видимость"_q, u"Visibility"_q));
		const auto ghost = AddDisclosure(visibility, Text(u"Режим призрака"_q, u"Ghost options"_q),
			OptionCount({ Option::PauseAutomaticChatReads, Option::HideStoryViewReceipts,
				Option::HideOnlineAnnouncements, Option::HideTypingIndicator, Option::SetOfflineAfterSending }));
		AddDisplayOption(ghost, Text(u"Не отправлять отметку о прочтении"_q,
			u"Do not automatically mark chats as read"_q), Option::PauseAutomaticChatReads);
		AddDisplayOption(ghost, Text(u"Не отправлять просмотр историй"_q,
			u"Do not send story view receipts"_q), Option::HideStoryViewReceipts);
		AddDisplayOption(ghost, Text(u"Не отправлять статус «в сети»"_q,
			u"Do not announce online status from this client"_q), Option::HideOnlineAnnouncements);
		AddDisplayOption(ghost, Text(u"Не отправлять статус набора текста"_q,
			u"Do not send typing indicators"_q), Option::HideTypingIndicator);
		AddDisplayOption(ghost, Text(u"Устанавливать «не в сети» после отправки"_q,
			u"Set offline after sending when hiding online status"_q), Option::SetOfflineAfterSending);
		AddToggle(visibility, Text(u"Прочтение чата после отправки сообщения"_q,
			u"Mark chat as read after sending a message"_q),
			Core::Telega::DisplayValue(Option::KeepUnreadWhenSending)
				| rpl::map([](bool keep) { return !keep; }),
			[](bool enabled) { Core::Telega::SetDisplayEnabled(Option::KeepUnreadWhenSending, !enabled); });
		addGroupFooter(Text(u"Настройки действуют в этом клиенте и не дают полной невидимости."_q,
			u"These settings apply to this client and do not make you invisible."_q));
		SetupMessageArchive(addGroup(
			Text(u"Сохранение сообщений"_q, u"Message retention"_q),
			u"удалённые удаленные история изменений сохранение deleted edited retention"_q),
			controller);
		addGroupFooter(Text(
			u"Сообщения, которые клиент уже получил, сохраняются на этом устройстве. История изменений доступна в меню сообщения."_q,
			u"Messages this client has already received are saved on this device. Open a message’s menu to see its edit history."_q));
		AddAction(addGroup(QString()), Text(u"Конфиденциальность и безопасность"_q, u"Privacy and security"_q), [=] {
			showOther(PrivacySecurityId());
		});
		finishGroups();
	}

	rpl::producer<QString> title() override {
		return rpl::single(Text(u"Приватность"_q, u"Privacy"_q));
	}

};

#ifdef Q_OS_WIN
void ShowTelegaProxySettings(not_null<Window::SessionController*> controller) {
	using Proxy = Platform::BundledTgWsProxy;
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto options = Proxy::Configuration();
		box->setTitle(rpl::single(Text(u"Параметры Tg Ws Proxy"_q, u"Tg Ws Proxy settings"_q)));
		const auto field = [&](QString title, QString value, int limit) {
			box->addRow(object_ptr<Ui::FlatLabel>(box, rpl::single(title),
				st::macSettingsPageHeader));
			return AddSettingsInput(box, std::move(title), std::move(value), limit);
		};
		const auto number = [&](QString title, int value, int min, int max) {
			const auto input = field(std::move(title), QString::number(value), 5);
			const auto validator = new QIntValidator(min, max, input);
			auto locale = QLocale::c();
			locale.setNumberOptions(QLocale::RejectGroupSeparator);
			validator->setLocale(locale);
			input->setValidator(validator);
			return input;
		};
		const auto port = number(Text(u"Локальный порт"_q, u"Local port"_q), options.port, 1, 65535);
		const auto pool = number(Text(u"Пул соединений на дата-центр"_q, u"Connections per data center"_q),
			options.poolSize, 0, 32);
		const auto buffer = number(Text(u"Буфер сокета, КиБ"_q, u"Socket buffer, KiB"_q),
			options.bufferKiB, 4, 4096);
		const auto hosts = [&](QString title, const QStringList &values) {
			return field(std::move(title), values.join(u", "_q), 8192);
		};
		const auto dc = hosts(Text(u"Адреса дата-центров (ДЦ:IPv4)"_q,
			u"Data centers (DC:IPv4)"_q), options.dcOverrides);
		const auto domains = hosts(Text(u"Домены Cloudflare"_q, u"Cloudflare domains"_q), options.cloudflareDomains);
		const auto workers = hosts(Text(u"Домены Cloudflare Worker"_q, u"Cloudflare Worker domains"_q), options.workerDomains);
		const auto check = [&](QString title, bool enabled) {
			return box->addRow(object_ptr<Ui::Checkbox>(box, title, enabled, st::settingsCheckbox));
		};
		const auto cloudflare = check(Text(u"Резервное подключение через Cloudflare"_q,
			u"Cloudflare fallback"_q), options.cloudflareFallback);
		const auto http2 = check(Text(u"HTTP/2 для медиа"_q, u"HTTP/2 for media"_q), options.http2);
		const auto secure = check(Text(u"Защищённое подключение к серверам"_q,
			u"Secure connection to servers"_q), options.secure);
		const auto verbose = check(Text(u"Подробный журнал"_q, u"Verbose logging"_q), options.verbose);
		box->addRow(object_ptr<Ui::FlatLabel>(box, rpl::single(Text(
			u"Значения в списках разделяются запятыми. Изменения применяются после перезапуска. Если меняете порт, укажите его и в параметрах подключения Telega."_q,
			u"Separate list entries with commas. Changes apply after restart. If you change the port, also update it in Telega’s connection settings."_q)), st::boxDividerLabel));
		box->addButton(tr::lng_settings_save(), [=] {
			for (const auto input : { port, pool, buffer }) {
				if (!input->hasAcceptableInput()) {
					input->showError();
					return;
				}
			}
			const auto entries = [](not_null<Ui::MaskedInputField*> input) {
				auto result = QStringList();
				for (const auto &entry : input->text().split(u',')) {
					const auto trimmed = entry.trimmed();
					if (!trimmed.isEmpty() && !result.contains(trimmed)) {
						result.push_back(trimmed);
					}
				}
				return result;
			};
			if (!Proxy::SetConfiguration({
				.port = port->text().toInt(),
				.bufferKiB = buffer->text().toInt(),
				.poolSize = pool->text().toInt(),
				.verbose = verbose->checked(),
				.cloudflareFallback = cloudflare->checked(),
				.http2 = http2->checked(),
				.secure = secure->checked(),
				.dcOverrides = entries(dc),
				.cloudflareDomains = entries(domains),
				.workerDomains = entries(workers),
			})) {
				box->showToast(Text(u"Не удалось сохранить. Проверьте адреса и домены."_q,
					u"Could not save. Check the addresses and domains."_q));
				return;
			}
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}
#endif // Q_OS_WIN

class Network final : public TelegaPage<Network> {
public:
	Network(QWidget *parent, not_null<Window::SessionController*> controller)
	: TelegaPage(parent, controller) {
		beginGroups();
		const auto proxy = addGroup(QString(), u"прокси proxy tg-ws-proxy websocket cloudflare"_q);
#ifdef Q_OS_WIN
		using Proxy = Platform::BundledTgWsProxy;
		if (Proxy::Available()) {
			const auto button = proxy->add(object_ptr<Button>(proxy,
				rpl::single(Text(u"Использовать tg-ws-proxy"_q, u"Use tg-ws-proxy"_q)),
				st::macSettingsPageButtonNoIcon));
			button->toggleOn(
				Core::App().settings().proxy().connectionTypeValue() | rpl::map([] {
					return Core::App().bundledProxyEnabled();
				}), true);
			button->setClickedCallback([=] {
				if (!Core::App().setBundledProxyEnabled(!button->toggled())) {
					controller->showToast(Text(u"Не удалось запустить tg-ws-proxy."_q,
						u"Could not start tg-ws-proxy."_q));
				}
			});
			AddAction(proxy, Text(u"Параметры Tg Ws Proxy"_q, u"Tg Ws Proxy settings"_q), [=] {
				ShowTelegaProxySettings(controller);
			});
			addGroupFooter(Text(u"Встроенный Tg Ws Proxy запускается локально и подключается к серверам мессенджера через WebSocket."_q,
				u"The built-in Tg Ws Proxy runs locally and connects to the messaging servers over WebSocket."_q));
		}
#endif // Q_OS_WIN
		SetupConnectionType(&controller->window(), &controller->session().account(), proxy);
		const auto boost = Text(u"Ускорение загрузки"_q, u"Download acceleration"_q);
		const auto transfers = addGroup(boost, u"скорость загрузки выгрузки download upload speed"_q);
		const auto modes = QStringList{
			Text(u"Выкл."_q, u"Off"_q), Text(u"Быстро"_q, u"Fast"_q), Text(u"Максимум"_q, u"Maximum"_q),
		};
		AddSteppedSlider(transfers, boost, modes, modes,
			int(Core::Telega::TransferDownloadMode()), [](int mode) {
				Core::Telega::SetTransferDownloadMode(Core::Telega::DownloadMode(mode));
			});
		AddDisplayOption(transfers, Text(u"Ускорение выгрузки"_q, u"Upload acceleration"_q),
			Core::Telega::DisplayOption::UploadBoost);
		addGroupFooter(Text(
			u"На медленном соединении максимальное ускорение может мешать загрузке файлов и просмотру видео. Изменения применяются после перезапуска."_q,
			u"On a slow connection, maximum acceleration may interfere with downloads and video playback. Changes take effect after restart."_q));
		const auto storage = addGroup(Text(u"Хранилище"_q, u"Storage"_q), u"память downloads memory"_q);
		AddAction(storage, Text(u"Папка для сохранения"_q, u"Download folder"_q), [=] {
			controller->show(Box<DownloadPathBox>(controller));
		});
		AddAction(storage, Text(u"Загрузки и использование памяти"_q, u"Downloads and storage"_q), [=] {
			showOther(DataStorageId());
		});
		addGroupFooter(Text(u"Загруженные файлы сохраняются в выбранную папку."_q,
			u"Downloaded files are saved to the selected folder."_q));
		finishGroups();
	}

	rpl::producer<QString> title() override {
		return rpl::single(Text(u"Сеть"_q, u"Network"_q));
	}

};

void ShowTelegaReleaseNotes(
		not_null<Window::SessionController*> controller,
		QUrl releasesUrl) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Text(u"Список изменений"_q, u"Release notes"_q)));
		box->setWidth(style::ConvertScale(490));
		const auto label = box->addRow(object_ptr<Ui::FlatLabel>(box,
			rpl::single(Text(u"Загрузка списка изменений…"_q, u"Loading release notes…"_q)),
			st::boxDividerLabel));
		label->setSelectable(true);
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		box->addLeftButton(rpl::single(Text(u"Открыть GitHub"_q, u"Open GitHub"_q)), [=] {
			if (!QDesktopServices::openUrl(releasesUrl)) {
				box->showToast(Text(u"Не удалось открыть GitHub."_q, u"Could not open GitHub."_q));
			}
		});
		auto apiUrl = releasesUrl;
		apiUrl.setHost(u"api.github.com"_q);
		apiUrl.setPath(u"/repos"_q + releasesUrl.path() + u"/latest"_q);
		auto request = QNetworkRequest(apiUrl);
		request.setTransferTimeout(10000);
		request.setRawHeader("Accept", "application/vnd.github+json");
		request.setRawHeader("User-Agent", "Telega-Desktop/" TELEGA_VERSION);
		request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
			QNetworkRequest::ManualRedirectPolicy);
		const auto manager = new QNetworkAccessManager(box);
		const auto reply = manager->get(request);
		constexpr auto kMaxSize = 256 * 1024;
		reply->setReadBufferSize(kMaxSize + 1);
		QObject::connect(reply, &QNetworkReply::readyRead, box.get(), [=] {
			if (reply->bytesAvailable() > kMaxSize) {
				reply->abort();
			}
		});
		QObject::connect(reply, &QNetworkReply::finished, box.get(), [=] {
			const auto success = reply->error() == QNetworkReply::NoError
				&& reply->bytesAvailable() <= kMaxSize
				&& reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
			const auto release = success
				? QJsonDocument::fromJson(reply->readAll()).object()
				: QJsonObject();
			reply->deleteLater();
			const auto body = release.value(u"body"_q).toString().trimmed();
			const auto tag = release.value(u"tag_name"_q).toString().left(80);
			if (body.isEmpty()) {
				label->setText(Text(u"Список изменений недоступен. Его можно открыть на GitHub."_q,
					u"Release notes are unavailable here. You can open them on GitHub."_q));
			} else {
				const auto prefix = tag.isEmpty() ? QString() : (tag + u"\n\n"_q);
				label->setText(prefix + body.left(24000) + (body.size() > 24000 ? u"…"_q : QString()));
			}
		});
	}));
}

class Updates final : public TelegaPage<Updates> {
public:
	Updates(QWidget *parent, not_null<Window::SessionController*> controller)
	: TelegaPage(parent, controller) {
		beginGroups();
		const auto updates = addGroup(Text(u"Обновления"_q, u"Updates"_q),
			u"версия github проверка install changelog список изменений"_q);
		if (!HasUpdate()) {
			Ui::AddDividerText(updates, rpl::single(Text(
				u"Автообновления недоступны в этой сборке."_q,
				u"Automatic updates are unavailable in this build."_q)));
		}
		SetupUpdate(updates);
		const auto notes = Core::UpdateReleaseNotesUrl();
		if (!notes.isEmpty()) {
			if (HasUpdate()) {
				AddIndexChoice(updates, controller, Text(u"Режим скачивания"_q, u"Download mode"_q), {
					Text(u"Только скачать"_q, u"Download only"_q),
					Text(u"Скачать и установить после перезагрузки"_q,
						u"Download and install on restart"_q),
				}, [] { return Core::InstallUpdatesOnRestart() ? 1 : 0; }, [=](int index) {
						if (!Core::SetInstallUpdatesOnRestart(index == 1)) {
							controller->showToast(Text(u"Не удалось сохранить настройку."_q,
								u"Could not save the update preference."_q));
						}
					}, ShowUpdateInstallChoice);
			}
			AddAction(updates, Text(u"Список изменений"_q, u"Release notes"_q), [=] {
				ShowTelegaReleaseNotes(controller, QUrl(notes));
			});
		}
		SetupPreferencesBackup(controller, addGroup(Text(u"Резервная копия"_q, u"Backup"_q),
			u"экспорт импорт сброс export import reset"_q));
		finishGroups();
	}

	rpl::producer<QString> title() override {
		return rpl::single(Text(u"Обновления"_q, u"Updates"_q));
	}
};

class SupportProject final : public TelegaPage<SupportProject> {
public:
	SupportProject(
			QWidget *parent,
			not_null<Window::SessionController*> controller)
	: TelegaPage(parent, controller) {
		beginGroups();
		if (Platform::IsWindows()) {
			const auto defaults = addGroup(
				QString(), u"приложения по умолчанию links default app tg ton tonsite"_q);
			AddButtonWithIcon(
				defaults,
				rpl::single(Text(u"Клиент по умолчанию для ссылок"_q,
					u"Default app for links"_q)),
				st::macSettingsPageButton,
				{ &st::menuIconLink }
			)->setClickedCallback([=] {
				const auto direct = Platform::IsWindows11OrGreater()
					&& QDesktopServices::openUrl(QUrl(
						u"ms-settings:defaultapps?registeredAppUser=Telega%20Desktop"_q));
				if (!direct && !QDesktopServices::openUrl(QUrl(u"ms-settings:defaultapps"_q))) {
					controller->showToast(Text(u"Не удалось открыть настройки Windows."_q,
						u"Could not open Windows Settings."_q));
				}
			});
			addGroupFooter(Text(
				u"Выберите Telega Desktop в настройках Windows для ссылок tg://, ton:// и tonsite://."_q,
				u"Choose Telega Desktop in Windows Settings for tg://, ton:// and tonsite:// links."_q));
		}
		const auto donations = addGroup(
			Text(u"Поддержать проект"_q, u"Support the project"_q),
			u"CloudTips донат пожертвования donate tip"_q);
		AddButtonWithIcon(
			donations,
			rpl::single(Text(u"Поддержать через CloudTips"_q, u"Support via CloudTips"_q)),
			st::macSettingsPageButton,
			{ &st::menuIconPayment }
		)->setClickedCallback([=] {
			if (!QDesktopServices::openUrl(QUrl(u"https://pay.cloudtips.ru/p/141b036a"_q))) {
				controller->showToast(Text(u"Не удалось открыть CloudTips."_q,
					u"Could not open CloudTips."_q));
			}
		});
		addGroupFooter(Text(u"Пожертвования идут на разработку Telega Desktop."_q,
			u"Donations support the development of Telega Desktop."_q));
		const auto contacts = addGroup(
			u"Telega Desktop"_q, u"разработчик канал developer channel dev_outside windows_custom"_q);
		const auto addContact = [&](
				QString title,
				QString username,
				const style::icon &icon,
				Window::ResolveType type) {
			AddButtonWithLabel(
				contacts,
				rpl::single(std::move(title)),
				rpl::single(u"@"_q + username),
				st::macSettingsPageButton,
				{ &icon }
			)->setClickedCallback([=] {
				controller->showPeerByLink({
					.usernameOrId = username,
					.resolveType = type,
				});
			});
		};
		addContact(Text(u"Разработчик"_q, u"Developer"_q),
			u"dev_outside"_q, st::menuIconProfile, Window::ResolveType::Profile);
		addContact(Text(u"Канал"_q, u"Channel"_q),
			u"windows_custom"_q, st::menuIconChannel, Window::ResolveType::Default);
		finishGroups();
	}

	rpl::producer<QString> title() override {
		return rpl::single(Text(u"Поддержать"_q, u"Support"_q));
	}

};

struct AiChatColor {
	const char *preference;
	QRgb fallback;
};

constexpr auto kAiBackground = AiChatColor{ "telega.ai.background", 0x292B36 };
constexpr auto kAiHeader = AiChatColor{ "telega.ai.header", 0x292B36 };
constexpr auto kAiComposer = AiChatColor{ "telega.ai.composer", 0x434754 };
constexpr auto kAiAccent = AiChatColor{ "telega.ai.accent", 0x53C3E8 };

QColor ReadAiColor(AiChatColor field) {
	const auto saved = Core::App().settings().readPref<QByteArray>(
		field.preference);
	const auto color = QColor(QString::fromLatin1(saved));
	return color.isValid() ? color : QColor::fromRgb(field.fallback);
}

rpl::event_stream<> &AiChatColorsChanged() {
	static auto changed = rpl::event_stream<>();
	return changed;
}

void SetupAiChatColors(
		not_null<Ui::VerticalLayout*> rows,
		not_null<Window::SessionController*> controller) {
	const auto add = [&](QString title, AiChatColor field) {
		auto value = rpl::merge(
			rpl::single(rpl::empty),
			AiChatColorsChanged().events(),
			style::PaletteChanged()
		) | rpl::map([=] { return ReadAiColor(field).name().toUpper(); });
		AddValueAction(rows, title, std::move(value), [=] {
			ShowColorPicker(controller, title, ReadAiColor(field), [=](QColor color) {
				Core::App().settings().writePref<QByteArray>(
					field.preference, color.name().toLatin1());
				AiChatColorsChanged().fire({});
			});
		});
	};
	add(Text(u"Фон переписки"_q, u"Conversation background"_q), kAiBackground);
	add(Text(u"Шапка чата"_q, u"Chat header"_q), kAiHeader);
	add(Text(u"Поле ввода"_q, u"Message field"_q), kAiComposer);
	add(Text(u"Акцент"_q, u"Accent"_q), kAiAccent);
	AddAction(rows, Text(u"Сбросить цвета"_q, u"Reset colors"_q), [] {
		for (const auto field : { kAiBackground, kAiHeader, kAiComposer, kAiAccent }) {
			Core::App().settings().clearPref(field.preference);
		}
		AiChatColorsChanged().fire({});
	});
}

void ShowAiChatSettings(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		using Provider = Gemini::Provider;
		const auto provider = Gemini::ChatProvider();
		box->setTitle(rpl::single(Text(u"Настройки ИИ-чата"_q, u"AI chat settings"_q)));
		box->setWidth(style::ConvertScale(456));
		box->setMaxHeight(style::ConvertScale(560));
		box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });
		const auto content = AddGroupedSettingsContent(box->verticalLayout());
		[[maybe_unused]] const auto generalHeading = AddGroupedSettingsHeader(
			content, rpl::single(Text(u"Основное"_q, u"General"_q)));
		const auto general = AddGroupedSettingsRows(content);
		const auto serviceTitle = Text(u"Сервис"_q, u"Service"_q);
		AddValueAction(general, serviceTitle,
			rpl::single(Gemini::ProviderName(provider)), [=] {
				const auto weak = QPointer<Ui::GenericBox>(box.get());
				ShowRadioChoice(controller, serviceTitle, {
					Gemini::ProviderName(Provider::Gemini),
					Gemini::ProviderName(Provider::OpenAI),
					Gemini::ProviderName(Provider::Claude),
					Gemini::ProviderName(Provider::Perplexity),
				}, int(provider), [=](int index) {
					if (!weak || index < 0 || index > int(Provider::Perplexity)
						|| index == int(provider)) {
						return;
					}
					if (Gemini::SetChatProvider(Provider(index))) {
						weak->closeBox();
						ShowAiChatSettings(controller);
					} else {
						controller->showToast(Text(
							u"Дождитесь ответа или остановите его."_q,
							u"Wait for the reply or stop generation."_q));
					}
				});
			});
		AddAiKeyAction(general, controller, Text(u"API-ключ"_q, u"API key"_q), provider);
		const auto modelTitle = Text(u"Модель ИИ-чата"_q, u"AI chat model"_q);
		const auto model = general->lifetime().make_state<rpl::variable<QString>>(
			Gemini::ChatModelName(provider));
		const auto loading = general->lifetime().make_state<bool>(false);
		AddValueAction(general, modelTitle, model->value(), [=] {
			if (*loading) {
				return;
			} else if (!Gemini::HasKey(provider) && provider != Provider::Perplexity) {
				controller->show(Ui::MakeInformBox(Text(
					u"Сначала введите ключ %1."_q, u"Enter a %1 API key first."_q)
					.arg(Gemini::ProviderName(provider))));
				return;
			}
			*loading = true;
			*model = Text(u"Загрузка…"_q, u"Loading…"_q);
			const auto weak = QPointer<Ui::GenericBox>(box.get());
			Gemini::FetchModels(provider, [=](QStringList names) {
				if (!weak) {
					return;
				}
				*loading = false;
				const auto current = Gemini::ChatModelName(provider);
				*model = current;
				if (names.isEmpty()) {
					controller->show(Ui::MakeInformBox(Text(u"Не удалось загрузить модели. Проверьте ключ и соединение."_q,
						u"Could not load models. Check your key and connection."_q)));
					return;
				}
				if (!names.contains(current)) {
					names.push_front(current);
				}
				ShowRadioChoice(controller, modelTitle, names, int(names.indexOf(current)),
					[=](int index) {
						if (weak && index >= 0 && index < names.size()) {
							if (Gemini::SetChatModelName(provider, names[index])) {
								*model = Gemini::ChatModelName(provider);
							}
						}
					});
			});
		});
		const auto addChoice = [&](QString title, const char *pref, int initial, QStringList options) {
			const auto index = std::clamp(Gemini::Preference(pref, initial), 0, int(options.size()) - 1);
			const auto value = general->lifetime().make_state<rpl::variable<QString>>(options[index]);
			const auto weak = QPointer<Ui::GenericBox>(box.get());
			AddValueAction(general, title, value->value(), [=] {
				ShowRadioChoice(controller, title, options, int(options.indexOf(value->current())),
					[=](int selected) {
						if (weak && selected >= 0 && selected < options.size()) {
							Gemini::SetPreference(pref, selected);
							*value = options[selected];
						}
					});
			});
		};
		addChoice(Text(u"Роль"_q, u"Role"_q), "role", 0, {
			Text(u"Ассистент"_q, u"Assistant"_q),
			Text(u"Переводчик"_q, u"Translator"_q),
			Text(u"Редактор"_q, u"Editor"_q),
			Text(u"Краткое содержание"_q, u"Summarize"_q),
		});
		addChoice(Text(u"Контекст переписки"_q, u"Conversation context"_q), "context", 1, {
			Text(u"Только новое сообщение"_q, u"New message only"_q),
			Text(u"10 сообщений"_q, u"10 messages"_q),
			Text(u"30 сообщений"_q, u"30 messages"_q),
			Text(u"60 сообщений"_q, u"60 messages"_q),
		});
		FinishGroupedSettingsRows(general, &st::boxBg);
		AddGroupedSettingsFooter(content, rpl::single(Text(
			u"Выбранный сервис получает отправленные сообщения и контекст из своей истории. История каждого сервиса хранится отдельно."_q,
			u"The selected service receives the message and context from its own history. Each service has a separate local history."_q)));
		[[maybe_unused]] const auto generationHeading = AddGroupedSettingsHeader(
			content, rpl::single(Text(u"Генерация"_q, u"Generation"_q)));
		const auto generation = AddGroupedSettingsRows(content);
		AddToggle(generation, Text(u"Потоковый ответ"_q, u"Stream responses"_q),
			rpl::single(Gemini::Preference("streaming", 1) != 0), [](bool enabled) {
				Gemini::SetPreference("streaming", enabled ? 1 : 0);
			});
		FinishGroupedSettingsRows(generation, &st::boxBg);
		AddGroupedSettingsFooter(content, rpl::single(Text(u"Ответ показывается по мере генерации."_q,
			u"The response appears while it is being generated."_q)));
		if (provider == Provider::Gemini) {
			const auto temperature = Text(u"Температура"_q, u"Temperature"_q);
			[[maybe_unused]] const auto temperatureHeading = AddGroupedSettingsHeader(
				content, rpl::single(temperature));
			const auto temperatureGroup = AddGroupedSettingsRows(content);
			auto temperatures = QStringList();
			for (auto step = 0; step <= 20; ++step) {
				temperatures.push_back(QString::number(step / 10., 'f', 1));
			}
			AddSteppedSlider(temperatureGroup, temperature, { u"0.0"_q, u"2.0"_q }, temperatures,
				std::clamp(Gemini::Preference("temperature", 10), 0, 20), [](int value) {
					Gemini::SetPreference("temperature", value);
				});
			FinishGroupedSettingsRows(temperatureGroup, &st::boxBg);
			AddGroupedSettingsFooter(content, rpl::single(Text(
				u"Низкая температура делает ответы более предсказуемыми, высокая — более разнообразными."_q,
				u"A lower temperature makes answers more predictable; a higher one makes them more varied."_q)));
		}
		[[maybe_unused]] const auto colorsHeading = AddGroupedSettingsHeader(
			content, rpl::single(Text(u"Оформление"_q, u"Appearance"_q)));
		const auto colors = AddGroupedSettingsRows(content);
		SetupAiChatColors(colors, controller);
		FinishGroupedSettingsRows(colors, &st::boxBg);
		box->addSkip(style::ConvertScale(14));
		box->addButton(tr::lng_box_done(), [=] { box->closeBox(); });
	}));
}

class GeminiInput final : public QPlainTextEdit {
public:
	using QPlainTextEdit::QPlainTextEdit;
	Fn<void()> submit;

protected:
	void keyPressEvent(QKeyEvent *event) override {
		const auto key = event->key();
		if ((key == Qt::Key_Return || key == Qt::Key_Enter)
			&& !(event->modifiers() & Qt::ShiftModifier)) {
			event->accept();
			if (submit) {
				submit();
			}
			return;
		}
		QPlainTextEdit::keyPressEvent(event);
	}

};

class GeminiPanel final : public Ui::RpWidget {
public:
	GeminiPanel(QWidget *parent, not_null<Window::SessionController*> controller)
	: RpWidget(parent) {
		const auto layout = new QVBoxLayout(this);
		const auto padding = style::ConvertScale(16);
		layout->setContentsMargins(padding, padding, padding, padding);
		layout->setSpacing(style::ConvertScale(6));
		layout->setSizeConstraint(QLayout::SetNoConstraint);
		const auto empty = new QLabel(Text(u"Задайте вопрос, чтобы начать разговор."_q,
			u"Ask a question to start the conversation."_q), this);
		empty->setAlignment(Qt::AlignCenter);
		empty->setWordWrap(true);
		layout->addWidget(empty, 0, Qt::AlignHCenter);
		const auto history = new QPlainTextEdit(this);
		history->setReadOnly(true);
		history->setUndoRedoEnabled(false);
		history->setFrameShape(QFrame::NoFrame);
		history->viewport()->setAutoFillBackground(false);
		history->setAccessibleName(Text(u"История ИИ-чата"_q, u"AI chat history"_q));
		layout->addWidget(history, 1);
		const auto status = new QLabel(this);
		status->setWordWrap(true);
		layout->addWidget(status);
		const auto composer = new QHBoxLayout();
		composer->setSpacing(style::ConvertScale(8));
		composer->addStretch(1);
		const auto field = new Ui::RpWidget(this);
		const auto fieldLayout = new QHBoxLayout(field);
		fieldLayout->setContentsMargins(padding, style::ConvertScale(6), padding, style::ConvertScale(6));
		field->setMaximumWidth(style::ConvertScale(720));
		const auto input = new GeminiInput(field);
		input->setFrameShape(QFrame::NoFrame);
		input->viewport()->setAutoFillBackground(false);
		input->setPlaceholderText(Text(u"Сообщение"_q, u"Message"_q));
		input->setAccessibleName(input->placeholderText());
		input->setFixedHeight(style::ConvertScale(46));
		fieldLayout->addWidget(input);
		setFocusProxy(input);
		field->paintRequest() | rpl::on_next([=, this] {
			auto painter = QPainter(field);
			painter.setRenderHint(QPainter::Antialiasing);
			painter.setPen(Qt::NoPen);
			painter.setBrush(_composerColor);
			const auto radius = style::ConvertScale(20);
			painter.drawRoundedRect(field->rect(), radius, radius);
		}, field->lifetime());
		composer->addWidget(field, 50);
		const auto send = new Ui::SendButton(this, st::historySendMac);
		send->setFixedSize(send->size());
		composer->addWidget(send, 0, Qt::AlignVCenter);
		composer->addStretch(1);
		layout->addLayout(composer);
		const auto submit = [=] {
			const auto text = input->toPlainText();
			const auto provider = Gemini::ChatProvider();
			if (text.trimmed().isEmpty() || Gemini::AiChatWaiting()) {
				return;
			} else if (!Gemini::HasKey(provider)) {
				ShowAiChatSettings(controller);
				status->setText(Text(
					u"Укажите ключ %1 в настройках ИИ-чата."_q,
					u"Set a %1 API key in AI chat settings."_q)
					.arg(Gemini::ProviderName(provider)));
				status->show();
			} else if (text.size() > 16000) {
				status->setText(Text(u"Максимум 16 000 символов."_q, u"Limit: 16,000 characters."_q));
				status->show();
			} else if (Gemini::AiChatSend(text)) {
				input->clear();
			}
		};
		input->submit = submit;
		send->setClickedCallback([=] {
			if (Gemini::AiChatWaiting()) {
				Gemini::AiChatCancel();
			} else {
				submit();
			}
		});
		const auto refreshSend = [=] {
			const auto waiting = Gemini::AiChatWaiting();
			send->setDisabled(!waiting && input->toPlainText().trimmed().isEmpty());
			send->setState({
				.type = waiting ? Ui::SendButton::Type::Stop : Ui::SendButton::Type::Send,
				.fillBgOverride = ReadAiColor(kAiAccent),
			});
			const auto label = waiting ? Text(u"Остановить"_q, u"Stop"_q)
				: Text(u"Отправить"_q, u"Send"_q);
			send->setAccessibleName(label);
			send->setToolTip(label);
		};
		connect(input, &QPlainTextEdit::textChanged, this, refreshSend);
		const auto applyColors = [=, this] {
			const auto background = ReadAiColor(kAiBackground);
			const auto accent = ReadAiColor(kAiAccent);
			_composerColor = ReadAiColor(kAiComposer);
			const auto text = Theme::LocalThemeTextColor(background);
			const auto inputText = Theme::LocalThemeTextColor(_composerColor);
			const auto placeholder = QColor::fromRgbF(
				(_composerColor.redF() + inputText.redF()) / 2,
				(_composerColor.greenF() + inputText.greenF()) / 2,
				(_composerColor.blueF() + inputText.blueF()) / 2);
			auto palette = history->palette();
			palette.setColor(QPalette::Base, background);
			palette.setColor(QPalette::Text, text);
			palette.setColor(QPalette::Highlight, accent);
			palette.setColor(QPalette::HighlightedText, Theme::LocalThemeTextColor(accent));
			history->setPalette(palette);
			palette.setColor(QPalette::Base, _composerColor);
			palette.setColor(QPalette::Text, inputText);
			palette.setColor(QPalette::PlaceholderText, placeholder);
			input->setPalette(palette);
			palette.setColor(QPalette::WindowText, text);
			status->setPalette(palette);
			empty->setStyleSheet(u"background-color: %1; color: %2; border-radius: %3px; padding: %4px %5px;"_q
				.arg(_composerColor.name(), inputText.name())
				.arg(style::ConvertScale(12)).arg(style::ConvertScale(5)).arg(padding));
			field->update();
			refreshSend();
		};
		applyColors();
		rpl::merge(
			style::PaletteChanged(),
			AiChatColorsChanged().events()
		) | rpl::on_next(applyColors, lifetime());
		const auto refresh = [=] {
			const auto waiting = Gemini::AiChatWaiting();
			const auto provider = Gemini::ChatProvider();
			const auto providerName = Gemini::ProviderName(provider);
			const auto scrollbar = history->verticalScrollBar();
			const auto position = scrollbar->value();
			const auto follow = (provider != _provider)
				|| (waiting && !_wasWaiting)
				|| (position >= scrollbar->maximum());
			_provider = provider;
			const auto &messages = Gemini::AiChatHistory();
			const auto isEmpty = !waiting && messages.empty();
			empty->setVisible(isEmpty);
			if (!waiting || !_wasWaiting) {
				auto text = QString();
				for (const auto &message : messages) {
					text += (message.user
						? Text(u"Вы"_q, u"You"_q)
						: (message.error ? Text(u"Статус"_q, u"Status"_q) : providerName))
						+ u"\n"_q + message.text + u"\n\n"_q;
				}
				if (waiting) {
					text += providerName + u"\n"_q;
				}
				history->setPlainText(text);
				_historyEnd = history->document()->characterCount() - 1;
				_partial.clear();
			}
			if (waiting) {
				const auto partial = Gemini::AiChatPartial();
				if (partial != _partial) {
					auto cursor = QTextCursor(history->document());
					cursor.movePosition(QTextCursor::End);
					if (partial.startsWith(_partial)) {
						cursor.insertText(partial.mid(_partial.size()));
					} else {
						cursor.setPosition(_historyEnd, QTextCursor::KeepAnchor);
						cursor.insertText(partial);
					}
					_partial = partial;
				}
			}
			scrollbar->setValue(follow ? scrollbar->maximum() : position);
			status->setText(waiting
				? Text(u"%1 готовит ответ…"_q, u"%1 is responding…"_q).arg(providerName)
				: QString());
			status->setVisible(waiting);
			_wasWaiting = waiting;
			refreshSend();
		};
		refresh();
		Gemini::AiChatUpdates() | rpl::on_next(refresh, lifetime());
	}

private:
	QColor _composerColor;
	Gemini::Provider _provider = Gemini::Provider::Gemini;
	QString _partial;
	int _historyEnd = 0;
	bool _wasWaiting = false;

};

class AiChatMemento;

class AiChat final : public Window::SectionWidget {
public:
	AiChat(QWidget *parent, not_null<Window::SessionController*> controller)
	: SectionWidget(parent, controller)
	, _panel(new GeminiPanel(this, controller)) {
		const auto layout = new QVBoxLayout(this);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(0);
		layout->setSizeConstraint(QLayout::SetNoConstraint);
		const auto bar = new Ui::RpWidget(this);
		bar->setFixedHeight(st::topBarHeight);
		const auto barLayout = new QHBoxLayout(bar);
		barLayout->setContentsMargins(0, 0, 0, 0);
		const auto back = new Ui::IconButton(bar, st::historyTopBarBack);
		back->setFixedSize(back->size());
		back->setAccessibleName(Text(u"Назад"_q, u"Back"_q));
		back->setClickedCallback([this] { showBackInternal(); });
		barLayout->addWidget(back);
		const auto title = new QLabel(TelegaAiChatLabel(), bar);
		title->setFont(st::semiboldFont->f);
		barLayout->addWidget(title);
		barLayout->addStretch();
		const auto headerButton = [&](const style::icon *icon, const QString &name) {
			const auto button = new Ui::IconButton(bar, st::historyTopBarBack);
			button->setFixedSize(button->size());
			button->setIconOverride(icon, icon);
			button->setAccessibleName(name);
			button->setToolTip(name);
			barLayout->addWidget(button);
			return button;
		};
		const auto clearTitle = Text(u"Очистить чат"_q, u"Clear chat"_q);
		const auto clear = headerButton(&st::menuIconDelete, clearTitle);
		const auto clearState = [=] {
			clear->setDisabled(Gemini::AiChatWaiting() || Gemini::AiChatHistory().empty());
		};
		clearState();
		Gemini::AiChatUpdates() | rpl::on_next(clearState, clear->lifetime());
		clear->setClickedCallback([=] {
			const auto provider = Gemini::ChatProvider();
			controller->show(Ui::MakeConfirmBox({
				.text = Text(u"Удалить локальную историю %1?"_q,
					u"Delete the local history for %1?"_q)
					.arg(Gemini::ProviderName(provider)),
				.confirmed = [=] {
					if (!Gemini::AiChatClear(provider)) {
						controller->showToast(Text(u"Не удалось удалить историю."_q,
							u"Could not clear the history."_q));
					}
				},
				.confirmText = Text(u"Очистить"_q, u"Clear"_q),
				.confirmStyle = &st::attentionBoxButton,
				.title = clearTitle,
			}));
		});
		const auto settings = headerButton(&st::menuIconSettings,
			Text(u"Настройки ИИ-чата"_q, u"AI chat settings"_q));
		settings->setClickedCallback([=] { ShowAiChatSettings(controller); });
		const auto applyColors = [=, this] {
			_background = ReadAiColor(kAiBackground);
			_header = ReadAiColor(kAiHeader);
			const auto text = Theme::LocalThemeTextColor(_header);
			auto palette = title->palette();
			palette.setColor(QPalette::WindowText, text);
			title->setPalette(palette);
			for (const auto button : { back, clear, settings }) {
				button->setIconColorOverride(text);
			}
			bar->update();
			update();
		};
		applyColors();
		rpl::merge(
			style::PaletteChanged(),
			AiChatColorsChanged().events()
		) | rpl::on_next(applyColors, lifetime());
		bar->paintRequest() | rpl::on_next([=, this](const QRect &clip) {
			auto painter = QPainter(bar);
			painter.fillRect(clip, _header);
		}, bar->lifetime());
		layout->addWidget(bar);
		layout->addWidget(_panel, 1);
	}

	QRect floatPlayerAvailableRect() override {
		return QRect(_panel->mapToGlobal(QPoint()), _panel->size());
	}

	bool floatPlayerHandleWheelEvent(QEvent *) override {
		return false;
	}

	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	bool showBackInternal() override {
		const auto window = controller();
		const auto content = window->content();
		if (content->stackIsEmpty()
			&& content->macSettingsShown()
			&& content->showMacSettings()) {
			return true;
		}
		auto params = Window::SectionShow();
		params.forbidLayer = true;
		window->showBackFromStack(params);
		return true;
	}

	std::shared_ptr<Window::SectionMemento> createMemento() override;

protected:
	void doSetInnerFocus() override {
		_panel->setFocus();
	}

	void paintEvent(QPaintEvent *event) override {
		if (animatingShow()) {
			SectionWidget::paintEvent(event);
		} else {
			auto painter = QPainter(this);
			painter.fillRect(event->rect(), _background);
		}
	}

private:
	const not_null<GeminiPanel*> _panel;
	QColor _background;
	QColor _header;

};

class AiChatMemento final : public Window::SectionMemento {
public:
	object_ptr<Window::SectionWidget> createWidget(
			QWidget *parent,
			not_null<Window::SessionController*> controller,
			Window::Column column,
			const QRect &geometry) override {
		if (column == Window::Column::Third) {
			return nullptr;
		}
		auto result = object_ptr<AiChat>(parent, controller);
		result->setGeometry(geometry);
		Ui::SendPendingMoveResizeEvents(result.data());
		return result;
	}

};

bool AiChat::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &) {
	return dynamic_cast<AiChatMemento*>(memento.get()) != nullptr;
}

std::shared_ptr<Window::SectionMemento> AiChat::createMemento() {
	return std::make_shared<AiChatMemento>();
}

struct Category {
	QString title;
	QString subtitle;
	QString keywords;
	const style::icon *icon;
	Type section;
};

class CategoryButton final : public QAbstractButton {
public:
	CategoryButton(QWidget *parent, Category category)
	: QAbstractButton(parent)
	, _category(std::move(category)) {
		setAccessibleName(_category.title);
		setAccessibleDescription(_category.subtitle);
		setCursor(Qt::PointingHandCursor);
		setFocusPolicy(Qt::StrongFocus);
	}

	bool matches(const QString &query) const {
		const auto searchable = _category.title + u" "_q + _category.subtitle
			+ u" "_q + _category.keywords;
		for (const auto &term : query.split(u' ', Qt::SkipEmptyParts)) {
			if (!searchable.contains(term, Qt::CaseInsensitive)) {
				return false;
			}
		}
		return true;
	}

protected:
	void paintEvent(QPaintEvent *) override {
		auto p = QPainter(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(hasFocus() ? QPen(st::windowActiveTextFg->c) : QPen(Qt::NoPen));
		const auto material = Core::Telega::UseMaterial3Style();
		const auto over = underMouse() || isDown();
		const auto fill = over ? st::windowBgRipple->c : st::windowBgOver->c;
		p.setBrush(material
			? style::MaterialTone(fill, st::windowActiveTextFg->c, over ? .17 : .09)
			: fill);
		const auto radius = style::ConvertScale(material ? 18 : 13);
		p.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), radius, radius);
		const auto inset = style::ConvertScale(14);
		const auto iconSize = style::ConvertScale(38);
		auto iconBg = st::windowActiveTextFg->c;
		iconBg.setAlpha(36);
		p.setPen(Qt::NoPen);
		p.setBrush(iconBg);
		const auto tileRadius = style::ConvertScale(material ? 12 : 10);
		p.drawRoundedRect(
			QRect(inset, (height() - iconSize) / 2, iconSize, iconSize),
			tileRadius,
			tileRadius);
		const auto iconTop = (height() - _category.icon->height()) / 2;
		_category.icon->paint(p, inset + (iconSize - _category.icon->width()) / 2,
			iconTop, width());
		const auto left = inset + iconSize + style::ConvertScale(12);
		const auto available = std::max(width() - left - inset, 0);
		if (!available) {
			return;
		}
		auto subtitleFont = font();
		subtitleFont.setPixelSize(style::ConvertScale(12));
		auto titleFont = subtitleFont;
		titleFont.setPixelSize(style::ConvertScale(15));
		titleFont.setWeight(QFont::DemiBold);
		const auto titleMetrics = QFontMetrics(titleFont);
		const auto subtitleMetrics = QFontMetrics(subtitleFont);
		auto subtitle = QTextLayout(_category.subtitle, subtitleFont);
		subtitle.beginLayout();
		auto first = subtitle.createLine();
		if (first.isValid()) {
			first.setLineWidth(available);
		}
		auto second = subtitle.createLine();
		if (second.isValid()) {
			second.setLineWidth(available);
		}
		subtitle.endLayout();
		const auto lines = _category.subtitle.isEmpty() ? 0 : second.isValid() ? 2 : 1;
		const auto gap = lines ? style::ConvertScale(2) : 0;
		const auto block = titleMetrics.height() + gap
			+ lines * subtitleMetrics.lineSpacing();
		const auto top = (height() - block) / 2;
		p.setFont(titleFont);
		p.setPen(st::windowFg->c);
		p.drawText(left, top + titleMetrics.ascent(),
			titleMetrics.elidedText(_category.title, Qt::ElideRight, available));
		if (lines && first.isValid()) {
			p.setFont(subtitleFont);
			p.setPen(st::windowSubTextFg->c);
			const auto baseline = top + titleMetrics.height() + gap
				+ subtitleMetrics.ascent();
			p.drawText(left, baseline, subtitleMetrics.elidedText(
				_category.subtitle.left(first.textLength()), Qt::ElideRight, available));
			if (lines == 2) {
				p.drawText(left, baseline + subtitleMetrics.lineSpacing(),
					subtitleMetrics.elidedText(
						_category.subtitle.mid(second.textStart()), Qt::ElideRight, available));
			}
		}
	}

private:
	const Category _category;
};

class Telega final : public Section<Telega> {
public:
	Telega(QWidget *parent, not_null<Window::SessionController*> controller)
	: Section(parent, controller)
	, _mark(new QLabel(this))
	, _brand(new QLabel(u"Telega Desktop"_q, this))
	, _version(new QLabel(QString::fromLatin1(TELEGA_DISPLAY_VERSION), this))
	, _search(CreateSectionSearchRow(this))
	, _empty(new QLabel(Text(u"Ничего не найдено"_q, u"No results"_q), this)) {
		ThemeQtWidget(this);
		_mark->setAlignment(Qt::AlignCenter);
		const auto ratio = style::DevicePixelRatio();
		const auto size = style::ConvertScale(72) * ratio;
		auto logo = QPixmap::fromImage(Window::ApplicationLogo().scaled(
			size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
		logo.setDevicePixelRatio(ratio);
		_mark->setPixmap(logo);
		_brand->setAlignment(Qt::AlignCenter);
		_version->setAlignment(Qt::AlignCenter);
		auto font = _brand->font();
		font.setPixelSize(style::ConvertScale(20));
		font.setBold(true);
		_brand->setFont(font);
		_version->setForegroundRole(QPalette::PlaceholderText);
		_empty->setAlignment(Qt::AlignCenter);
		auto categories = std::vector<Category>{
			{ Text(u"Оформление"_q, u"Appearance"_q), Text(u"Логотип, шрифт, папки"_q, u"Logo, font, folders"_q), u"stories историй аватар avatar radius скругление style macos windows стиль шрифт font папки folders статус status стикеры stickers timestamps emoji эмодзи logo логотип icon иконка значок zalgo шум"_q, &st::menuIconPalette, Appearance::Id() },
			{ Text(u"Темы и фон"_q, u"Themes and wallpaper"_q), Text(u"Создание, импорт и экспорт тем"_q, u"Create, import and export themes"_q), u"custom локальные хранение цвет акцент яркость wallpaper background brightness theme colors"_q, &st::menuIconImportTheme, Themes::Id() },
			{ Text(u"Сообщения"_q, u"Messages"_q), Text(u"Жесты, ввод, стикеры, медиа"_q, u"Gestures, input, stickers, media"_q), u"emoji reactions реакции send запятая упоминание копирование comma mention copy toast hd video перемотка видео хвостиков bubble tails пауза pause клик click секунды seconds просмотр views пересылка forward цитата quote count символы gifs голосовые voice калькулятор calculator меню menu online сведения details quick actions быстрые действия poll опрос gemini расшифровка"_q, &st::menuIconChatBubble, Messages::Id() },
			{ Text(u"Перевод"_q, u"Translation"_q), Text(u"Сервис, язык, чаты"_q, u"Service, language, chats"_q), u"language переводчик google встроенный"_q, &st::menuIconTranslate, Translation::Id() },
			{ Text(u"Приватность"_q, u"Privacy"_q), Text(u"Профиль и сохранение сообщений"_q, u"Profile and message retention"_q), u"номер phone профиль profile dc дата-центр last seen посещения password capture скриншот захват music обложка печатает typing прочитано read receipt истории stories online offline ghost авторы участники authors members удалённые удаленные изменения deleted edited message retention"_q, &st::menuIconLock, Privacy::Id() },
			{ Text(u"Сеть"_q, u"Network"_q), Text(u"Прокси, загрузки"_q, u"Proxy and downloads"_q), u"tg-ws-proxy connection memory папка загрузок download folder upload ускорение speed cloudflare http/2 домены worker"_q, &st::menuIconNetwork, Network::Id() },
			{ Text(u"Обновления"_q, u"Updates"_q), Text(u"GitHub, резервная копия"_q, u"GitHub, preferences backup"_q), u"auto версия импорт экспорт установить install restart перезапуск"_q, &st::menuIconRestore, Updates::Id() },
			{ Text(u"Поддержать"_q, u"Support"_q), Text(u"CloudTips, разработчик, канал"_q, u"CloudTips, developer, channel"_q), u"донат пожертвования donate dev_outside windows_custom"_q, &st::menuIconLike, SupportProject::Id() },
		};
		for (auto &category : categories) {
			const auto target = category.section;
			const auto button = new CategoryButton(this, std::move(category));
			connect(button, &QAbstractButton::clicked, this, [=] {
				showOther(target);
			});
			_cards.push_back(button);
		}
		_search.controller->queryChanges() | rpl::on_next([=](QString query) {
			_query = query.simplified();
			resizeToWidth(width());
		}, lifetime());
	}

	rpl::producer<QString> title() override {
		return rpl::single(u"Telega Desktop"_q);
	}

protected:
	int resizeGetHeight(int newWidth) override {
		const auto pad = style::ConvertScale(14);
		const auto gap = style::ConvertScale(Core::Telega::UseMaterial3Style() ? 2 : 6);
		const auto cardHeight = style::ConvertScale(72);
		const auto wide = newWidth >= style::ConvertScale(330);
		const auto columns = wide ? 2 : 1;
		const auto inner = std::max(newWidth - 2 * pad, 0);
		const auto cell = std::max((inner - (columns - 1) * gap) / columns, 0);
		const auto markHeight = style::ConvertScale(72);
		const auto brandHeight = style::ConvertScale(30);
		const auto versionHeight = style::ConvertScale(20);
		const auto brandTop = pad + markHeight + gap;
		const auto versionTop = brandTop + brandHeight;
		const auto searchTop = versionTop + versionHeight + pad;
		_mark->setGeometry(pad, pad, inner, markHeight);
		_brand->setGeometry(pad, brandTop, inner, brandHeight);
		_version->setGeometry(pad, versionTop, inner, versionHeight);
		_search.row->resizeToWidth(inner);
		_search.row->move(pad, searchTop);
		const auto start = searchTop + _search.row->height() + pad;
		auto count = 0;
		for (const auto card : _cards) {
			const auto visible = card->matches(_query);
			card->setVisible(visible);
			if (visible) {
				card->setGeometry(pad + (count % columns) * (cell + gap),
					start + (count / columns) * (cardHeight + gap), cell, cardHeight);
				++count;
			}
		}
		_empty->setVisible(!count);
		_empty->setGeometry(pad, start, inner, cardHeight);
		const auto rows = (count + columns - 1) / columns;
		return start + (rows ? rows * (cardHeight + gap) : cardHeight) + pad;
	}

private:
	const not_null<QLabel*> _mark;
	const not_null<QLabel*> _brand;
	const not_null<QLabel*> _version;
	const SectionSearchRow _search;
	const not_null<QLabel*> _empty;
	std::vector<CategoryButton*> _cards;
	QString _query;

};

} // namespace

Type TelegaId() {
	return Telega::Id();
}

bool IsTelegaAiChat(const Window::SectionWidget *section) {
	return dynamic_cast<const AiChat*>(section) != nullptr;
}

void ShowTelegaAiChat(not_null<Window::SessionController*> controller) {
	if (controller->window().locked()) {
		return;
	}

	const auto content = controller->content();
	auto params = Window::SectionShow();
	params.forbidLayer = true;
	if (!content->macSettingsShown() && !content->showMacSettings()) {
		controller->showSettings(params);
	}
	controller->showSection(std::make_shared<AiChatMemento>(), params);
}

QString TelegaSettingsLabel() {
	return Text(u"Настройки Telega Desktop"_q, u"Telega Desktop Settings"_q);
}

QString TelegaAiChatLabel() {
	return Text(u"ИИ-чат"_q, u"AI Chat"_q);
}

} // namespace Settings
