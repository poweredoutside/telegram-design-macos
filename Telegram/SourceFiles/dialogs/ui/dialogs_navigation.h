/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/effects/liquid_glass.h"
#include "ui/rp_widget.h"
#include <rpl/event_stream.h>
#include <rpl/variable.h>
#include <QtGui/QImage>
#include <optional>

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
		Fn<QImage(QRect)> backdrop = nullptr,
		Fn<void()> menu = nullptr);
	~NavigationBar();
	void setActive(NavigationAction action);
	void setUnreadCount(int count);

private:
	struct SelectionGesture {
		QPoint start;
		float64 center = 0.;
		float64 position = 0.;
		bool dragging = false;
	};

	bool eventHook(QEvent *e) override;
	bool eventFilter(QObject *watched, QEvent *e) override;
	void cancelSelection(bool animated);
	void animateSelection(float64 from, float64 to);
	[[nodiscard]] float64 positionAt(float64 x) const;
	[[nodiscard]] float64 selectionPosition() const;

	const Fn<void(NavigationAction)> _activate;
	rpl::variable<NavigationAction> _active = NavigationAction::Chats;
	rpl::variable<NavigationAction> _preview = NavigationAction::Chats;
	rpl::variable<bool> _tracking = false;
	rpl::variable<int> _unreadCount = 0;
	rpl::event_stream<NavigationAction> _iconAnimations;
	Ui::Animations::Simple _selection;
	Ui::LiquidGlassBackdropCache _backdropCache;
	std::optional<SelectionGesture> _gesture;
	QWidget *_grabber = nullptr;

};

} // namespace Dialogs
