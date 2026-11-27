/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class FlatLabel;
class RpWidget;
class SettingsButton;
class VerticalLayout;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

namespace Window {

class SessionController;

enum class MainMenuStyle {
	Unigram,
	Material3,
};

struct MainMenuOption {
	QString id;
	QString title;
	bool shownByDefault = true;
};

[[nodiscard]] std::vector<MainMenuOption> MainMenuOptions(
	MainMenuStyle style,
	not_null<Main::Session*> session);
[[nodiscard]] bool MainMenuOptionShown(
	MainMenuStyle style,
	const QString &id,
	bool shownByDefault = true);
void SetMainMenuOptionShown(MainMenuStyle style, const QString &id, bool shown);

[[nodiscard]] not_null<Ui::FlatLabel*> AddVersionLabel(
	not_null<Ui::RpWidget*>);

[[nodiscard]] not_null<Ui::SettingsButton*> AddMyChannelsBox(
	not_null<Ui::SettingsButton*> button,
	not_null<SessionController*> controller,
	bool chats);

void SetupMenuBots(
	not_null<Ui::VerticalLayout*> container,
	not_null<SessionController*> controller);

} // namespace Window
