#include "dialogs/ui/dialogs_navigation_list.h"

#include "boxes/peer_list_controllers.h"
#include "calls/group/calls_group_invite_controller.h"
#include "calls/calls_box_controller.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_user.h"
#include "dialogs/ui/dialogs_navigation.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/effects/ripple_animation.h"
#include "ui/style/style_core_scale.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"

#include <QtGui/QKeyEvent>
#include <QtWidgets/QTextEdit>

#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"

namespace Dialogs {
namespace {

class ContactsController final : public ContactsBoxController {
public:
	explicit ContactsController(
		not_null<Window::SessionController*> controller);

	void rowClicked(not_null<PeerListRow*> row) override;
	void setNavigationSize(QSize size);
	void setSearchQuery(const QString &query);
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

protected:
	void prepareViewHook() override;
	std::unique_ptr<PeerListRow> createRow(not_null<UserData*> user) override;

private:
	const not_null<Window::SessionController*> _controller;
	QSize _navigationSize;
	QString _mentionHighlight;
	PeerListRowId _activeRow = 0;

};

class NavigationList final
	: public ::Ui::RpWidget
	, private PeerListContentDelegateShow {
public:
	NavigationList(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		NavigationAction action,
		Fn<void()> repaintBackdrop);

	~NavigationList();

private:
	void peerListSetTitle(rpl::producer<QString> title) override;

	void peerListSetDescription(object_ptr<::Ui::FlatLabel> description) override;

	void peerListScrollToTop() override;
	void peerListRefreshRows() override;
	void peerListSortRows(
		Fn<bool(const PeerListRow &a, const PeerListRow &b)> compare) override;

	void paintEvent(QPaintEvent *e) override;

	bool eventFilter(QObject *watched, QEvent *event) override;

	const bool _hasSearch = false;
	std::unique_ptr<PeerListController> _controller;
	object_ptr<::Ui::InputField> _search;
	object_ptr<::Ui::ScrollArea> _scroll;
	QString _title;
	rpl::variable<bool> _callsPresent = false;

};

ContactsController::ContactsController(
	not_null<Window::SessionController*> controller)
: ContactsBoxController(&controller->session())
, _controller(controller) {
}

void ContactsController::rowClicked(not_null<PeerListRow*> row) {
	_controller->showPeerHistory(row->peer());
}

void ContactsController::setNavigationSize(QSize size) {
	_navigationSize = size;
}

void ContactsController::setSearchQuery(const QString &query) {
	_mentionHighlight = query.startsWith('@') ? query.mid(1) : query;
}

void ContactsController::prepareViewHook() {
	setSortMode(SortMode::Online);
	_controller->activeChatValue() | rpl::on_next([=](Key key) {
		const auto history = key.history();
		if (key && !history) {
			return;
		}
		const auto id = history ? history->peer->id.value : PeerListRowId();
		if (_activeRow == id) {
			return;
		}
		const auto previous = std::exchange(_activeRow, id);
		if (const auto row = delegate()->peerListFindRow(previous)) {
			delegate()->peerListUpdateRow(row);
		}
		if (const auto row = delegate()->peerListFindRow(_activeRow)) {
			delegate()->peerListUpdateRow(row);
		}
	}, lifetime());
}

std::unique_ptr<PeerListRow> ContactsController::createRow(
		not_null<UserData*> user) {
	return user->isSelf() ? nullptr : ContactsBoxController::createRow(user);
}

int ContactsController::customRowHeight() {
	return computeListSt().item.height;
}

void ContactsController::customRowPaint(
		Painter &p,
		crl::time now,
		not_null<PeerListRow*> row,
		bool selected) {
	const auto active = (row->id() == _activeRow);
	auto st = computeListSt().item;
	if (active) {
		st.nameFg = st::dialogsNameFgActive;
		st.statusFg = st::dialogsTextFgActive;
		st.statusFgOver = st::dialogsTextFgActive;
		st.statusFgActive = st::dialogsTextFgActive;
	}
	const auto width = _navigationSize.width();
	p.fillRect(
		0,
		0,
		width,
		st.height,
		active ? st::dialogsBgActive
			: selected ? st::dialogsBgOver : st::dialogsBg);
	row->paintRipple(p, st, 0, 0, width);
	row->paintUserpic(
		p,
		st,
		st.photoPosition.x(),
		st.photoPosition.y(),
		width);
	const auto gap = style::ConvertScale(1);
	const auto nameY = (st.height - st.nameStyle.font->height
		- st::contactsStatusFont->height - gap) / 2;
	const auto statusY = nameY + st.nameStyle.font->height + gap;
	const auto nameX = st.namePosition.x();
	const auto statusX = st.statusPosition.x();
	const auto right = st.photoPosition.x();
	const auto repaint = [=] { delegate()->peerListUpdateRow(row); };
	const auto leading = row->paintNameIconGetLeadingWidth(
		p,
		repaint,
		now,
		nameX,
		nameY,
		width,
		active);
	auto available = std::max(0, width - nameX - right - leading);
	available -= row->paintNameIconGetWidth(
		p,
		repaint,
		now,
		nameX + leading,
		nameY,
		row->name().maxWidth(),
		available,
		width,
		active);
	p.setPen(st.nameFg);
	row->name().drawLeftElided(
		p,
		nameX + leading,
		nameY,
		std::max(0, available),
		width);
	const auto statusWidth = std::max(0, width - statusX - right);
	const auto username = row->peer()->username();
	if (row->isSearchResult()
		&& !_mentionHighlight.isEmpty()
		&& username.startsWith(_mentionHighlight, Qt::CaseInsensitive)) {
		const auto font = st::contactsStatusFont;
		const auto highlighted = '@' + username.mid(0, _mentionHighlight.size());
		const auto highlightedWidth = font->width(highlighted);
		p.setFont(font);
		p.setPen(st.statusFgActive);
		p.drawTextLeft(
			statusX,
			statusY,
			width,
			font->elided(highlighted, statusWidth));
		if (highlightedWidth < statusWidth) {
			p.setPen(st.statusFg);
			p.drawTextLeft(
				statusX + highlightedWidth,
				statusY,
				width,
				font->elided(
					username.mid(_mentionHighlight.size()),
					statusWidth - highlightedWidth));
		}
	} else {
		row->paintStatusText(
			p,
			st,
			statusX,
			statusY,
			statusWidth,
			width,
			active);
	}
	const auto bottom = p.transform().map(QPoint(0, st.height)).y();
	if (!active
		&& bottom < _navigationSize.height() - computeListSt().padding.bottom()) {
		p.fillRect(
			nameX,
			st.height - st::lineWidth,
			std::max(0, width - nameX),
			st::lineWidth,
			st::shadowFg);
	}
}

bool ContactsController::customRowSelectionPoint(
		not_null<PeerListRow*>,
		int x,
		int y) {
	return QRect(0, 0, _navigationSize.width(), customRowHeight()).contains(x, y);
}

Fn<QImage()> ContactsController::customRowRippleMaskGenerator() {
	return [=] {
		return ::Ui::RippleAnimation::RectMask(
			QSize(_navigationSize.width(), customRowHeight()));
	};
}

NavigationList::NavigationList(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	NavigationAction action,
	Fn<void()> repaintBackdrop)
: RpWidget(parent)
, PeerListContentDelegateShow(controller->uiShow())
, _hasSearch(action == NavigationAction::Contacts)
, _controller(action == NavigationAction::Contacts
	? std::unique_ptr<PeerListController>(
		std::make_unique<ContactsController>(controller))
	: std::make_unique<Calls::BoxController>(controller,
		Calls::BoxController::Presentation::MacNavigation))
, _search(this, st::dialogsFilterMac, tr::lng_dlg_filter())
, _scroll(this, st::defaultScrollArea) {
	_controller->setStyleOverrides(_hasSearch
		? &st::dialogsMacPeerList
		: &st::dialogsMacCallList);
	const auto content = _scroll->setOwnedWidget(
		object_ptr<::Ui::VerticalLayout>(_scroll));
	if (!_hasSearch) {
		const auto top = content->add(
			object_ptr<::Ui::SlideWrap<::Ui::VerticalLayout>>(
				content, object_ptr<::Ui::VerticalLayout>(content)));
		const auto create = top->entity()->add(Settings::CreateButtonWithIcon(
			top->entity(),
			tr::lng_call_navigation_create(),
			st::dialogsMacCreateCall,
			{ &st::dialogsMacCreateCallIcon, Settings::IconType::Simple }));
		create->setAllowTextLines(1);
		create->setClickedCallback([=] {
			controller->show(Calls::Group::PrepareCreateCallBox(controller));
		});
		const auto section = top->entity()->add(
			object_ptr<::Ui::FixedHeightWidget>(
				top->entity(), style::ConvertScale(20)));
		section->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(section);
			p.fillRect(section->rect(), st::boxSearchBg);
		}, section->lifetime());
		const auto label = ::Ui::CreateChild<::Ui::FlatLabel>(section,
			tr::lng_call_navigation_title(), st::dialogsMacCallSection);
		section->sizeValue() | rpl::on_next([=](QSize size) {
			label->resizeToWidth(std::max(0,
				size.width() - 2 * style::ConvertScale(10)));
			label->moveToLeft(style::ConvertScale(10),
				(size.height() - label->height()) / 2);
		}, label->lifetime());
		top->toggleOn(_callsPresent.value(), anim::type::instant);
	}
	const auto list = content->add(object_ptr<PeerListContent>(
		content, _controller.get()));
	setContent(list);
	if (_hasSearch) {
		list->setMode(PeerListContent::Mode::Custom);
		const auto contacts = static_cast<ContactsController*>(_controller.get());
		list->sizeValue() | rpl::on_next([=](QSize size) {
			contacts->setNavigationSize(size);
		}, list->lifetime());
	}
	if (!_hasSearch) {
		list->setMode(PeerListContent::Mode::Custom);
		const auto calls = static_cast<Calls::BoxController*>(_controller.get());
		list->widthValue() | rpl::on_next([=](int width) {
			calls->setNavigationWidth(width);
		}, list->lifetime());
	}
	if (!_hasSearch) {
		const auto empty = content->add(
			object_ptr<::Ui::SlideWrap<::Ui::LinkButton>>(
				content, object_ptr<::Ui::LinkButton>(content,
					QString())), {}, style::al_top);
		tr::lng_call_navigation_create() | rpl::on_next([=](QString text) {
			empty->entity()->setText(text);
		}, empty->lifetime());
		empty->entity()->setClickedCallback([=] {
			controller->show(Calls::Group::PrepareCreateCallBox(controller));
		});
		empty->toggleOn(_callsPresent.value()
			| rpl::map([](bool present) { return !present; }),
			anim::type::instant);
	}
	::Ui::AddSkip(content, st::dialogsNavigationHeight);
	_scroll->scrolls() | rpl::on_next([=] {
		repaintBackdrop();
	}, lifetime());
	list->heightValue() | rpl::skip(1) | rpl::on_next([=] {
		repaintBackdrop();
	}, lifetime());
	_search->changes() | rpl::on_next([=] {
		_scroll->scrollToY(0);
		if (_hasSearch) {
			static_cast<ContactsController*>(_controller.get())->setSearchQuery(
				_search->getLastText());
		}
		list->searchQueryChanged(_search->getLastText());
	}, lifetime());
	_search->submits() | rpl::on_next([=] { list->submitted(); }, lifetime());
	_search->rawTextEdit()->installEventFilter(this);
	_search->setVisible(_hasSearch);
	list->setFocusPolicy(Qt::StrongFocus);
	list->installEventFilter(this);
	setFocusProxy(_hasSearch
		? static_cast<QWidget*>(_search->rawTextEdit())
		: list);
	list->scrollToRequests() | rpl::on_next([=](::Ui::ScrollToRequest request) {
		_scroll->scrollToY(request.ymin, request.ymax);
	}, lifetime());
	sizeValue() | rpl::on_next([=](QSize size) {
		const auto inset = st::dialogsMacSearchInset;
		_search->setGeometryToLeft(inset, st::dialogsMacStatusHeight,
			std::max(1, size.width() - 2 * inset), st::dialogsFilterMac.heightMin);
		const auto top = _hasSearch
			? st::dialogsMacHeaderHeight
			: st::dialogsMacStatusHeight;
		_scroll->setGeometry(0, top, size.width(), std::max(0, size.height() - top));
		content->resizeToWidth(size.width());
	}, lifetime());
	style::PaletteChanged() | rpl::on_next([=] { update(); }, lifetime());
	_controller->setDelegate(static_cast<PeerListDelegate*>(this));
	list->setSearchMode(_hasSearch
		? PeerListSearchMode::Enabled
		: PeerListSearchMode::Disabled);
	_controller->showFinished();
}

NavigationList::~NavigationList() {
	_scroll.destroy();
}

void NavigationList::peerListSetTitle(rpl::producer<QString> title) {
	if (!_hasSearch) {
		title = tr::lng_call_navigation_title();
	}
	std::move(title) | rpl::on_next([=](QString text) {
		_title = std::move(text);
		setAccessibleName(_title);
		update();
	}, lifetime());
}

void NavigationList::peerListSetDescription(object_ptr<::Ui::FlatLabel> description) {
	content()->setDescription(std::move(description));
}

void NavigationList::peerListScrollToTop() {
	_scroll->scrollToY(0);
}

void NavigationList::peerListRefreshRows() {
	PeerListContentDelegateShow::peerListRefreshRows();
	_callsPresent = content()->fullRowsCount() > 0;
}

void NavigationList::peerListSortRows(
		Fn<bool(const PeerListRow &a, const PeerListRow &b)> compare) {
	PeerListContentDelegateShow::peerListSortRows([=](
			const PeerListRow &a,
			const PeerListRow &b) {
		return compare(a, b)
			|| (_hasSearch && !compare(b, a) && a.id() < b.id());
	});
}

void NavigationList::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.fillRect(rect(), st::dialogsBg);
	p.setFont(st::dialogsNameFont);
	p.setPen(st::windowFg);
	const auto inset = st::dialogsMacSearchInset;
	const auto available = std::max(0, width() - 2 * inset);
	p.drawText(QRect(inset, 0, available, st::dialogsMacStatusHeight),
		Qt::AlignCenter, st::dialogsNameFont->elided(_title, available));
	if (!_hasSearch) {
		p.fillRect(
			0,
			st::dialogsMacStatusHeight - st::lineWidth,
			width(),
			st::lineWidth,
			st::shadowFg);
	}
}

bool NavigationList::eventFilter(QObject *watched, QEvent *event) {
	if (event->type() == QEvent::KeyPress) {
		const auto key = static_cast<QKeyEvent*>(event)->key();
		if (key == Qt::Key_Up || key == Qt::Key_Down) {
			content()->selectSkip(key == Qt::Key_Up ? -1 : 1);
			return true;
		} else if (key == Qt::Key_PageUp || key == Qt::Key_PageDown) {
			content()->selectSkipPage(
				_scroll->height(), key == Qt::Key_PageUp ? -1 : 1);
			return true;
		} else if (watched == content().get()
			&& (key == Qt::Key_Return || key == Qt::Key_Enter)) {
			content()->submitted();
			return true;
		} else if (key == Qt::Key_Escape
			&& !_search->getLastText().isEmpty()) {
			_search->clear();
			return true;
		}
	}
	return RpWidget::eventFilter(watched, event);
}

} // namespace

std::unique_ptr<::Ui::RpWidget> CreateNavigationList(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		NavigationAction action,
		Fn<void()> repaintBackdrop) {
	Expects(action == NavigationAction::Contacts || action == NavigationAction::Calls);
	return std::make_unique<NavigationList>(
		parent, controller, action, std::move(repaintBackdrop));
}

} // namespace Dialogs
