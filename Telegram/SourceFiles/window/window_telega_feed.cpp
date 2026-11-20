#include "window/window_telega_feed.h"

#include "apiwrap.h"
#include "core/core_telega_preferences.h"
#include "data/data_channel.h"
#include "data/data_folder.h"
#include "data/data_histories.h"
#include "data/data_session.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_instance.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "styles/style_info.h"
#include "styles/style_widgets.h"
#include "ui/layers/layer_widget.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "window/section_memento.h"
#include "window/section_widget.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLocale>
#include <QtCore/QSignalBlocker>
#include <QtCore/QTimer>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QVBoxLayout>

namespace Window {
namespace {

constexpr auto kMaxChannels = 30;
constexpr auto kPostsPerChannel = 10;
constexpr auto kMaxPosts = 150;
constexpr auto kParallel = 4;
constexpr auto kMaxCategories = 20;

using ChannelIds = base::flat_set<uint64>;

struct Category {
	QString title;
	ChannelIds channels;
};

QString Text(const QString &russian, const QString &english) {
	return Lang::GetInstance().id().startsWith(u"ru"_q) ? russian : english;
}

ChannelIds ReadIds(const QJsonValue &value) {
	auto result = ChannelIds();
	for (const auto &entry : value.toArray()) {
		if (const auto id = entry.toString().toULongLong()) {
			result.emplace(id);
		}
	}
	return result;
}

QJsonArray WriteIds(const ChannelIds &ids) {
	auto result = QJsonArray();
	for (const auto id : ids) {
		result.append(QString::number(id));
	}
	return result;
}

bool IsPost(const HistoryItem *item) {
	return item && item->isRegular() && !item->isService()
		&& !item->isEphemeral();
}

QString Preview(not_null<HistoryItem*> item) {
	if (!item->allowsForward()) {
		return Text(u"Защищённая публикация"_q, u"Protected post"_q);
	}
	const auto &text = item->originalText();
	if (ranges::any_of(text.entities, [](const EntityInText &entity) {
		return entity.type() == EntityType::Spoiler;
	})) {
		return Text(u"Публикация со спойлером"_q, u"Post with hidden text"_q);
	}
	const auto plain = text.text.simplified();
	return (plain.size() > 220)
		? plain.left(220) + QChar(0x2026)
		: !plain.isEmpty()
		? plain
		: Text(u"Открыть публикацию"_q, u"Open post"_q);
}

class FeedMemento;

class Feed final : public SectionWidget {
public:
	Feed(QWidget *parent, not_null<SessionController*> controller, int category);
	~Feed() override;

	bool showInternal(
		not_null<SectionMemento*> memento,
		const SectionShow &params) override;
	std::shared_ptr<SectionMemento> createMemento() override;
	QRect floatPlayerAvailableRect() override;
	bool floatPlayerHandleWheelEvent(QEvent *) override {
		return false;
	}

protected:
	void doSetInnerFocus() override;
	void paintEvent(QPaintEvent *event) override;

private:
	[[nodiscard]] std::vector<not_null<History*>> channels(bool all = false) const;
	void save();
	void rebuildCategories(int index);
	void showOptions();
	void editChannels(int category);
	void markRead();
	void reload();
	void cancelRequests();
	void pump();
	void requestPosts(not_null<History*> history);
	void render();

	const not_null<Main::Session*> _session;
	const not_null<Data::Session*> _owner;
	std::vector<Category> _categories;
	ChannelIds _hidden;
	std::vector<FullMsgId> _fetched;
	std::vector<FullMsgId> _shown;
	std::vector<PeerId> _pending;
	base::flat_set<mtpRequestId> _requests;
	QComboBox *_category = nullptr;
	QLabel *_status = nullptr;
	QListWidget *_posts = nullptr;
	QPushButton *_read = nullptr;
	QPushButton *_options = nullptr;
	QTimer _renderTimer;
	bool _archive = false;
	bool _loadFailed = false;

};

class FeedMemento final : public SectionMemento {
public:
	explicit FeedMemento(int category = 0) : _category(category) {
	}

	object_ptr<SectionWidget> createWidget(
			QWidget *parent,
			not_null<SessionController*> controller,
			Column column,
			const QRect &geometry) override {
		if (column == Column::Third) {
			return nullptr;
		}
		auto result = object_ptr<Feed>(parent, controller, _category);
		result->setGeometry(geometry);
		Ui::SendPendingMoveResizeEvents(result.data());
		return result;
	}

private:
	const int _category;

};

Feed::Feed(QWidget *parent, not_null<SessionController*> controller, int category)
: SectionWidget(parent, controller)
, _session(&controller->session())
, _owner(&_session->data()) {
	const auto stored = QJsonDocument::fromJson(
		_session->settings().telegaFeed()).object();
	_archive = stored.value(u"archive"_q).toBool();
	_hidden = ReadIds(stored.value(u"hidden"_q));
	for (const auto &entry : stored.value(u"categories"_q).toArray()) {
		const auto object = entry.toObject();
		const auto title = object.value(u"title"_q).toString().trimmed().left(48);
		if (!title.isEmpty() && int(_categories.size()) < kMaxCategories) {
			_categories.push_back({ title, ReadIds(object.value(u"channels"_q)) });
		}
	}

	const auto layout = new QVBoxLayout(this);
	const auto pad = style::ConvertScale(12);
	layout->setContentsMargins(pad, 0, pad, pad);
	layout->setSizeConstraint(QLayout::SetNoConstraint);
	layout->setSpacing(style::ConvertScale(8));
	const auto bar = new QHBoxLayout();
	const auto back = new Ui::IconButton(this, st::historyTopBarBack);
	back->setAccessibleName(Text(u"Назад"_q, u"Back"_q));
	back->setClickedCallback([=] { controller->showBackFromStack(); });
	bar->addWidget(back);
	const auto title = new QLabel(TelegaFeedLabel(), this);
	title->setFont(st::semiboldFont->f);
	bar->addWidget(title, 1);
	layout->addLayout(bar);

	const auto toolbar = new QHBoxLayout();
	_category = new QComboBox(this);
	_category->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	_category->setAccessibleName(Text(u"Категория"_q, u"Category"_q));
	toolbar->addWidget(_category, 1);
	const auto refresh = new QPushButton(Text(u"Обновить"_q, u"Refresh"_q), this);
	toolbar->addWidget(refresh);
	_options = new QPushButton(Text(u"Настройки"_q, u"Options"_q), this);
	toolbar->addWidget(_options);
	layout->addLayout(toolbar);

	const auto footer = new QHBoxLayout();
	_status = new QLabel(this);
	_status->setWordWrap(true);
	footer->addWidget(_status, 1);
	_read = new QPushButton(Text(u"Прочитать всё"_q, u"Mark all as read"_q), this);
	footer->addWidget(_read);

	_posts = new QListWidget(this);
	_posts->setWordWrap(true);
	_posts->setSpacing(style::ConvertScale(4));
	_posts->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_posts->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	layout->addWidget(_posts, 1);
	layout->addLayout(footer);

	const auto updatePalette = [=] {
		auto palette = this->palette();
		palette.setColor(QPalette::Window, st::windowBg->c);
		palette.setColor(QPalette::Base, st::windowBgOver->c);
		palette.setColor(QPalette::Button, st::windowBgOver->c);
		palette.setColor(QPalette::WindowText, st::windowFg->c);
		palette.setColor(QPalette::Text, st::windowFg->c);
		palette.setColor(QPalette::ButtonText, st::windowFg->c);
		palette.setColor(QPalette::Highlight, st::windowActiveTextFg->c);
		palette.setColor(QPalette::HighlightedText, st::windowBg->c);
		setPalette(palette);
		update();
	};
	updatePalette();
	style::PaletteChanged() | rpl::on_next(updatePalette, lifetime());

	rebuildCategories(category);
	connect(_category, &QComboBox::currentIndexChanged, this, [=] { reload(); });
	connect(refresh, &QPushButton::clicked, this, [=] { reload(); });
	connect(_options, &QPushButton::clicked, this, [=] { showOptions(); });
	connect(_read, &QPushButton::clicked, this, [=] { markRead(); });
	connect(_posts, &QListWidget::itemActivated, this, [=](QListWidgetItem *row) {
		const auto index = _posts->row(row);
		if (index >= 0 && index < int(_shown.size())) {
			if (const auto item = _owner->message(_shown[index])) {
				controller->showMessage(item, SectionShow::Way::Forward);
			}
		}
	});
	_renderTimer.setSingleShot(true);
	_renderTimer.setInterval(120);
	connect(&_renderTimer, &QTimer::timeout, this, [=] { render(); });
	const auto schedule = [this] {
		if (!_renderTimer.isActive()) {
			_renderTimer.start();
		}
	};
	_owner->chatsListChanges() | rpl::on_next(schedule, lifetime());
	Core::Telega::DisplayValue(
		Core::Telega::DisplayOption::TimeWithSeconds
	) | rpl::skip(1) | rpl::on_next(schedule, lifetime());
	_owner->unreadBadgeChanges() | rpl::on_next(schedule, lifetime());
	_owner->itemDataChanges() | rpl::on_next([=](not_null<HistoryItem*> item) {
		if (ranges::contains(_shown, item->fullId())) {
			schedule();
		}
	}, lifetime());
	_owner->itemRemoved() | rpl::on_next([=](not_null<const HistoryItem*> item) {
		if (ranges::contains(_shown, item->fullId())) {
			schedule();
		}
	}, lifetime());
	_owner->chatsListLoadedEvents() | rpl::on_next([=](Data::Folder *folder) {
		if (!folder || (_archive && folder->id() == Data::Folder::kId)) {
			reload();
		}
	}, lifetime());
	QTimer::singleShot(0, this, [=] { reload(); });
}

Feed::~Feed() {
	cancelRequests();
}

bool Feed::showInternal(
		not_null<SectionMemento*> memento,
		const SectionShow &) {
	return dynamic_cast<FeedMemento*>(memento.get()) != nullptr;
}

std::shared_ptr<SectionMemento> Feed::createMemento() {
	return std::make_shared<FeedMemento>(_category->currentIndex());
}

QRect Feed::floatPlayerAvailableRect() {
	return QRect(_posts->mapToGlobal(QPoint()), _posts->size());
}

void Feed::doSetInnerFocus() {
	_posts->setFocus();
}

void Feed::paintEvent(QPaintEvent *event) {
	if (animatingShow()) {
		SectionWidget::paintEvent(event);
	} else {
		auto painter = QPainter(this);
		painter.fillRect(event->rect(), st::windowBg->c);
	}
}

std::vector<not_null<History*>> Feed::channels(bool all) const {
	auto result = std::vector<not_null<History*>>();
	auto seen = ChannelIds();
	const auto index = _category->currentIndex() - 1;
	const auto category = (index >= 0 && index < int(_categories.size()))
		? &_categories[index]
		: nullptr;
	const auto collect = [&](Data::Folder *folder) {
		for (const auto &row : _owner->chatsList(folder)->indexed()->all()) {
			const auto history = row->history();
			const auto channel = history ? history->peer->asChannel() : nullptr;
			if (!channel || !channel->isBroadcast() || !channel->amIn()) {
				continue;
			}
			const auto id = channel->id.value;
			if ((all || (!_hidden.contains(id)
					&& (!category || category->channels.contains(id))))
				&& seen.emplace(id).second) {
				result.push_back(history);
			}
		}
	};
	collect(nullptr);
	if (all || _archive) {
		if (const auto folder = _owner->folderLoaded(Data::Folder::kId)) {
			collect(folder);
		}
	}
	return result;
}

void Feed::save() {
	auto categories = QJsonArray();
	for (const auto &category : _categories) {
		categories.append(QJsonObject{
			{ u"title"_q, category.title },
			{ u"channels"_q, WriteIds(category.channels) },
		});
	}
	_session->settings().setTelegaFeed(QJsonDocument(QJsonObject{
		{ u"archive"_q, _archive },
		{ u"hidden"_q, WriteIds(_hidden) },
		{ u"categories"_q, categories },
	}).toJson(QJsonDocument::Compact));
	_session->saveSettingsDelayed();
}

void Feed::rebuildCategories(int index) {
	const auto blocked = QSignalBlocker(_category);
	_category->clear();
	_category->addItem(Text(u"Все каналы"_q, u"All channels"_q));
	for (const auto &category : _categories) {
		_category->addItem(category.title);
	}
	_category->setCurrentIndex(std::clamp(index, 0, int(_categories.size())));
}

void Feed::showOptions() {
	const auto menu = new QMenu(this);
	menu->setAttribute(Qt::WA_DeleteOnClose);
	const auto archive = menu->addAction(Text(u"Включать архив"_q, u"Include archive"_q));
	archive->setCheckable(true);
	archive->setChecked(_archive);
	connect(archive, &QAction::toggled, this, [=](bool checked) {
		_archive = checked;
		save();
		reload();
	});
	menu->addAction(Text(u"Выбрать каналы"_q, u"Choose channels"_q), this,
		[=] { editChannels(-1); });
	const auto create = menu->addAction(Text(u"Создать категорию"_q, u"New category"_q), this,
		[=] { editChannels(int(_categories.size())); });
	create->setEnabled(int(_categories.size()) < kMaxCategories);
	const auto selected = _category->currentIndex() - 1;
	if (selected >= 0) {
		menu->addAction(Text(u"Редактировать категорию"_q, u"Edit category"_q), this,
			[=] { editChannels(selected); });
		menu->addAction(Text(u"Удалить категорию"_q, u"Remove category"_q), this, [=] {
			_categories.erase(begin(_categories) + selected);
			save();
			rebuildCategories(0);
			reload();
		});
	}
	menu->popup(_options->mapToGlobal(QPoint(0, _options->height())));
}

void Feed::editChannels(int category) {
	const auto isCategory = (category >= 0);
	const auto existing = isCategory && (category < int(_categories.size()));
	const auto dialog = new QDialog(this);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(isCategory
		? Text(u"Категория ленты"_q, u"Feed category"_q)
		: Text(u"Каналы в ленте"_q, u"Channels in the feed"_q));
	dialog->resize(style::ConvertScale(380), style::ConvertScale(460));
	const auto layout = new QVBoxLayout(dialog);
	const auto title = new QLineEdit(dialog);
	title->setMaxLength(48);
	title->setPlaceholderText(Text(u"Название (можно с эмодзи)"_q, u"Name (emoji allowed)"_q));
	title->setVisible(isCategory);
	layout->addWidget(title);
	if (existing) {
		title->setText(_categories[category].title);
	}
	const auto search = new QLineEdit(dialog);
	search->setPlaceholderText(Text(u"Поиск каналов"_q, u"Search channels"_q));
	layout->addWidget(search);
	const auto list = new QListWidget(dialog);
	list->setSortingEnabled(true);
	layout->addWidget(list, 1);
	for (const auto history : channels(true)) {
		const auto id = history->peer->id.value;
		const auto row = new QListWidgetItem(history->peer->name(), list);
		row->setData(Qt::UserRole, qulonglong(id));
		row->setCheckState((isCategory
			? (existing && _categories[category].channels.contains(id))
			: !_hidden.contains(id)) ? Qt::Checked : Qt::Unchecked);
	}
	connect(search, &QLineEdit::textChanged, dialog, [=](const QString &query) {
		for (auto index = 0; index != list->count(); ++index) {
			const auto row = list->item(index);
			row->setHidden(!row->text().contains(query.trimmed(), Qt::CaseInsensitive));
		}
	});
	const auto buttons = new QDialogButtonBox(
		QDialogButtonBox::Save | QDialogButtonBox::Cancel, dialog);
	buttons->button(QDialogButtonBox::Save)->setText(Text(u"Сохранить"_q, u"Save"_q));
	buttons->button(QDialogButtonBox::Cancel)->setText(Text(u"Отмена"_q, u"Cancel"_q));
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	connect(buttons, &QDialogButtonBox::accepted, dialog, [=, this] {
		const auto name = title->text().trimmed();
		if (isCategory && name.isEmpty()) {
			title->setFocus();
			return;
		}
		auto ids = isCategory
			? (existing ? _categories[category].channels : ChannelIds())
			: _hidden;
		for (auto index = 0; index != list->count(); ++index) {
			const auto row = list->item(index);
			const auto id = uint64(row->data(Qt::UserRole).toULongLong());
			if ((row->checkState() == Qt::Checked) == isCategory) {
				ids.emplace(id);
			} else {
				ids.remove(id);
			}
		}
		if (!isCategory) {
			_hidden = std::move(ids);
		} else if (existing) {
			_categories[category] = { name, std::move(ids) };
		} else {
			_categories.push_back({ name, std::move(ids) });
		}
		save();
		rebuildCategories(isCategory ? category + 1 : _category->currentIndex());
		reload();
		dialog->accept();
	});
	dialog->open();
}

void Feed::markRead() {
	const auto box = new QMessageBox(QMessageBox::Question, TelegaFeedLabel(),
		Text(u"Отметить прочитанными все каналы этой категории?"_q,
			u"Mark all channels in the current category as read?"_q),
		QMessageBox::Yes | QMessageBox::Cancel, this);
	box->setAttribute(Qt::WA_DeleteOnClose);
	box->setDefaultButton(QMessageBox::Cancel);
	box->button(QMessageBox::Yes)->setText(Text(u"Прочитать"_q, u"Mark as read"_q));
	box->button(QMessageBox::Cancel)->setText(Text(u"Отмена"_q, u"Cancel"_q));
	connect(box, &QMessageBox::finished, this, [=, this](int result) {
		if (result == QMessageBox::Yes) {
			for (const auto history : channels()) {
				if (history->unreadCount() > 0) {
					_owner->histories().readInbox(history);
				}
			}
			render();
		}
	});
	box->open();
}

void Feed::reload() {
	cancelRequests();
	_fetched.clear();
	_loadFailed = false;
	if (!_owner->chatsListLoaded()) {
		_session->api().requestDialogs();
	}
	if (_archive) {
		const auto folder = _owner->folder(Data::Folder::kId);
		if (!_owner->chatsListLoaded(folder)) {
			_session->api().requestDialogs(folder);
		}
	}
	for (const auto history : channels()) {
		_pending.push_back(history->peer->id);
		if (int(_pending.size()) == kMaxChannels) {
			break;
		}
	}
	ranges::reverse(_pending);
	pump();
	render();
}

void Feed::cancelRequests() {
	_pending.clear();
	for (const auto request : _requests) {
		_session->api().request(request).cancel();
	}
	_requests.clear();
}

void Feed::pump() {
	while (int(_requests.size()) < kParallel && !_pending.empty()) {
		const auto id = _pending.back();
		_pending.pop_back();
		const auto history = _owner->historyLoaded(id);
		const auto channel = history ? history->peer->asChannel() : nullptr;
		if (channel && channel->amIn()) {
			requestPosts(history);
		}
	}
}

void Feed::requestPosts(not_null<History*> history) {
	const auto peer = history->peer->id;
	const auto finish = [this](mtpRequestId request) {
		_requests.remove(request);
		pump();
		if (!_renderTimer.isActive()) {
			_renderTimer.start();
		}
	};
	_requests.emplace(_session->api().request(MTPmessages_GetHistory(
		history->peer->input(), MTP_int(0), MTP_int(0), MTP_int(0),
		MTP_int(kPostsPerChannel), MTP_int(0), MTP_int(0), MTP_long(0)
	)).done([=, this](const MTPmessages_Messages &result, mtpRequestId request) {
		result.match([&](const MTPDmessages_messagesNotModified &) {
		}, [&](const auto &data) {
			_owner->processUsers(data.vusers());
			_owner->processChats(data.vchats());
			for (const auto &message : data.vmessages().v) {
				if (PeerFromMessage(message) == peer && DateFromMessage(message)) {
					const auto item = _owner->addNewMessage(
						message, MessageFlags(), NewMessageType::Existing);
					if (IsPost(item)) {
						_fetched.push_back(item->fullId());
					}
				}
			}
		});
		finish(request);
	}).fail([=, this](const MTP::Error &, mtpRequestId request) {
		_loadFailed = true;
		finish(request);
	}).send());
}

void Feed::render() {
	auto included = ChannelIds();
	auto items = std::vector<not_null<HistoryItem*>>();
	auto seen = base::flat_set<FullMsgId>();
	auto unread = int64(0);
	const auto add = [&](HistoryItem *item) {
		if (IsPost(item) && seen.emplace(item->fullId()).second) {
			items.push_back(item);
		}
	};
	for (const auto history : channels()) {
		included.emplace(history->peer->id.value);
		unread += std::max(history->unreadCount(), 0);
		add(history->lastMessage());
	}
	for (const auto &id : _fetched) {
		if (included.contains(id.peer.value)) {
			add(_owner->message(id));
		}
	}
	ranges::sort(items, ranges::greater(), [](not_null<HistoryItem*> item) {
		return item->position();
	});
	if (int(items.size()) > kMaxPosts) {
		items.erase(begin(items) + kMaxPosts, end(items));
	}
	const auto scroll = _posts->verticalScrollBar();
	const auto top = scroll->value();
	const auto current = _posts->currentRow();
	const auto selected = (current >= 0 && current < int(_shown.size()))
		? _shown[current]
		: FullMsgId();
	auto ids = std::vector<FullMsgId>();
	auto rows = QStringList();
	ids.reserve(items.size());
	rows.reserve(items.size());
	for (const auto item : items) {
		const auto timestamp = QDateTime::fromSecsSinceEpoch(item->date());
		const auto date = QLocale().toString(timestamp.date(), QLocale::ShortFormat)
			+ u' ' + Core::Telega::FormatTime(timestamp.time());
		rows.push_back(item->history()->peer->name() + u"  ·  "_q + date
			+ u'\n' + Preview(item));
		ids.push_back(item->fullId());
	}
	if (ids == _shown) {
		for (auto index = 0; index != rows.size(); ++index) {
			_posts->item(index)->setText(rows[index]);
		}
	} else {
		_posts->setUpdatesEnabled(false);
		_posts->clear();
		_shown = std::move(ids);
		_posts->addItems(rows);
		for (auto index = 0; index != int(_shown.size()); ++index) {
			if (selected == _shown[index]) {
				_posts->setCurrentRow(index);
				break;
			}
		}
		scroll->setValue(top);
		_posts->setUpdatesEnabled(true);
	}
	_read->setEnabled(unread > 0);
	_status->setText(!_requests.empty()
		? Text(u"Загрузка публикаций…"_q, u"Loading posts…"_q)
		: _loadFailed
		? Text(u"Часть публикаций не загрузилась."_q, u"Some posts could not be loaded."_q)
		: items.empty()
		? Text(u"Нет публикаций. Проверьте выбранные каналы."_q,
			u"No posts. Check the selected channels."_q)
		: Text(u"Непрочитано: %1 · Публикаций: %2"_q,
			u"Unread: %1 · Posts: %2"_q).arg(unread).arg(items.size()));
}

}

QString TelegaFeedLabel() {
	return Text(u"Лента каналов"_q, u"Channel feed"_q);
}

void ShowTelegaFeed(not_null<SessionController*> controller) {
	controller->window().hideSettingsAndLayer();
	controller->showSection(std::make_shared<FeedMemento>());
}

}
