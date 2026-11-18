/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_box.h"
#include "ui/layers/generic_box.h"
#include "mtproto/sender.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Calls {
namespace GroupCalls {

class ListController : public PeerListController {
public:
	explicit ListController(not_null<::Window::SessionController*> window);

	[[nodiscard]] rpl::producer<bool> shownValue() const;

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowRightActionClicked(not_null<PeerListRow*> row) override;

private:
	const not_null<::Window::SessionController*> _window;
	base::flat_map<PeerId, not_null<PeerListRow*>> _groupCalls;
	rpl::variable<int> _fullCount;

};

} // namespace GroupCalls

class BoxController : public PeerListController {
public:
	enum class Presentation {
		Standard,
		MacNavigation,
	};

	explicit BoxController(
		not_null<::Window::SessionController*> window,
		Presentation presentation = Presentation::Standard);

	void setNavigationWidth(int width);
	int customRowHeight() override;
	void customRowPaint(
		Painter &p,
		crl::time now,
		not_null<PeerListRow*> row,
		bool selected) override;
	bool customRowSelectionPoint(
		not_null<PeerListRow*> row,
		int x,
		int y) override;
	Fn<QImage()> customRowRippleMaskGenerator() override;

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowRightActionClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;

private:
	void receivedCalls(const QVector<MTPMessage> &result);
	void refreshAbout();

	class GroupCallRow;
	class Row;
	Row *rowForItem(not_null<const HistoryItem*> item);

	enum class InsertWay {
		Append,
		Prepend,
	};
	bool insertRow(not_null<HistoryItem*> item, InsertWay way);
	std::unique_ptr<PeerListRow> createRow(
		not_null<HistoryItem*> item) const;

	const not_null<::Window::SessionController*> _window;
	MTP::Sender _api;
	const Presentation _presentation;
	int _navigationWidth = 0;

	MsgId _offsetId = 0;
	int _loadRequestId = 0; // Not a real mtpRequestId.
	bool _allLoaded = false;

};

void ClearCallsBox(
	not_null<Ui::GenericBox*> box,
	not_null<::Window::SessionController*> window);

void ShowCallsBox(
	not_null<::Window::SessionController*> window,
	bool highlightStartCall = false);

} // namespace Calls
