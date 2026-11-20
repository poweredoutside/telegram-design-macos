/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_quick_action.h"

#include "core/application.h"
#include "core/core_settings.h"

#include <QtWidgets/QApplication>

namespace HistoryView {
namespace {

const char *PreferenceKey(bool outgoing, bool right) {
	return right
		? (outgoing ? "telega.gesture.right.outgoing" : "telega.gesture.right.incoming")
		: (outgoing ? "telega.gesture.left.outgoing" : "telega.gesture.left.incoming");
}

} // namespace

DoubleClickQuickAction CurrentQuickAction() {
	return Core::App().settings().chatQuickAction();
}

DoubleClickQuickAction MessageQuickAction(bool outgoing, bool right) {
	using Action = DoubleClickQuickAction;
	const auto value = Core::App().settings().readPref<int>(
		PreferenceKey(outgoing, right),
		int(right ? Action::None : Action::Inherited));
	return (value >= int(Action::Reply) && value <= int(Action::Forward))
		? Action(value)
		: (right ? Action::None : Action::Inherited);
}

void SetMessageQuickAction(bool outgoing, bool right, DoubleClickQuickAction action) {
	Core::App().settings().writePref<int>(PreferenceKey(outgoing, right), int(action));
	Core::App().saveSettingsDelayed();
}

bool RightClickQuickAction::delayMenu(FullMsgId item, Fn<void()> show) {
	if (_timer.isActive() && _item == item) {
		cancel();
		return false;
	}
	_item = item;
	_timer.setCallback(std::move(show));
	_timer.callOnce(std::min(QApplication::doubleClickInterval(), 200));
	return true;
}

void RightClickQuickAction::cancel() {
	_timer.cancel();
	_item = FullMsgId();
}

} // namespace HistoryView
