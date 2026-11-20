/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "data/data_msg_id.h"

namespace HistoryView {

enum class DoubleClickQuickAction {
	Reply, // Default.
	React,
	None,
	Edit,
	Copy,
	Forward,
	Inherited = -1,
};

[[nodiscard]] DoubleClickQuickAction CurrentQuickAction();
[[nodiscard]] DoubleClickQuickAction MessageQuickAction(bool outgoing, bool right);
void SetMessageQuickAction(bool outgoing, bool right, DoubleClickQuickAction action);

class RightClickQuickAction {
public:
	[[nodiscard]] bool delayMenu(FullMsgId item, Fn<void()> show);
	void cancel();

private:
	FullMsgId _item;
	base::Timer _timer;

};

} // namespace HistoryView
