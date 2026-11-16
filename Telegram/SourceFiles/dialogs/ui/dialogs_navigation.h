/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include <rpl/variable.h>
#include <QtGui/QImage>

namespace Dialogs {

enum class NavigationAction {
	Contacts,
	Calls,
	Chats,
	Settings,
};

class NavigationBar final : public Ui::RpWidget {
public:
	NavigationBar(
		QWidget *parent,
		Fn<void(NavigationAction)> activate,
		Fn<QImage()> backdrop = nullptr,
		Fn<void()> menu = nullptr);
	void setActive(NavigationAction action);

private:
	rpl::variable<NavigationAction> _active = NavigationAction::Chats;

};

} // namespace Dialogs
