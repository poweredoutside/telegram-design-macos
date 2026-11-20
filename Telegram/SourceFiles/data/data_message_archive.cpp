#include "data/data_message_archive.h"

#include "api/api_text_entities.h"
#include "base/unixtime.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_instance.h"
#include "main/main_session.h"
#include "storage/storage_account.h"

#include <QtCore/QDataStream>

#include <cstring>

namespace Data {
namespace {

constexpr auto kOptionKeys = std::array{
	"telega.archive.deleted",
	"telega.archive.edits",
	"telega.archive.bots",
	"telega.archive.translucent",
};
constexpr auto kMarkKey = "telega.archive.mark";
constexpr auto kArchiveKey = "telega.archive.messages";
constexpr auto kArchiveVersion = quint32(1);
constexpr auto kMaxEntries = 2000;
constexpr auto kMaxRevisions = 20;
constexpr auto kMaxArchiveBytes = 8 * 1024 * 1024;
constexpr auto kMaxPayloadBytes = 64 * 1024;
constexpr auto kMaxMediaEntries = 4096;

template <typename Type>
[[nodiscard]] QByteArray Serialize(const Type &value) {
	auto buffer = mtpBuffer();
	value.write(buffer);
	const auto bytes = buffer.size() * sizeof(mtpPrime);
	return (bytes <= kMaxPayloadBytes)
		? QByteArray(reinterpret_cast<const char*>(buffer.constData()), bytes)
		: QByteArray();
}

template <typename Type>
[[nodiscard]] std::optional<Type> Deserialize(const QByteArray &bytes) {
	if (bytes.isEmpty()
		|| bytes.size() > kMaxPayloadBytes
		|| (bytes.size() % sizeof(mtpPrime))) {
		return std::nullopt;
	}
	auto buffer = mtpBuffer(bytes.size() / sizeof(mtpPrime));
	std::memcpy(buffer.data(), bytes.constData(), bytes.size());
	auto from = buffer.constData();
	const auto end = from + buffer.size();
	auto result = Type();
	return (result.read(from, end) && from == end)
		? std::optional<Type>(std::move(result))
		: std::nullopt;
}

[[nodiscard]] bool SupportedMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaPhoto) {
		const auto &photo = media.c_messageMediaPhoto();
		return photo.vphoto() && !photo.vttl_seconds();
	} else if (media.type() == mtpc_messageMediaDocument) {
		const auto &document = media.c_messageMediaDocument();
		return document.vdocument() && !document.vttl_seconds();
	}
	return false;
}

} // namespace

QString DeletedMarkText(DeletedMark mark) {
	switch (mark) {
	case DeletedMark::None: return {};
	case DeletedMark::Text: return Lang::GetInstance().id().startsWith(u"ru"_q)
		? u"Удалено"_q
		: u"Deleted"_q;
	case DeletedMark::Trash: return u"\U0001F5D1"_q;
	case DeletedMark::Cross: return u"✕"_q;
	case DeletedMark::Eye: return u"\U0001F441"_q;
	}
	Unexpected("Unknown deleted message mark.");
}

MessageArchive::MessageArchive(not_null<Main::Session*> session)
: _session(session) {
	for (auto i = 0; i != int(kOptionKeys.size()); ++i) {
		_options[i] = _session->local().readPref<bool>(
			kOptionKeys[i], i == int(ArchiveOption::Bots));
	}
	const auto mark = _session->local().readPref<QString>(kMarkKey, QString());
	auto ok = false;
	const auto selected = mark.toInt(&ok);
	if (ok && selected >= 0 && selected <= int(DeletedMark::Eye)) {
		_mark = DeletedMark(selected);
	}
	read();
}

bool MessageArchive::enabled(ArchiveOption option) const {
	return _options[int(option)].current();
}

rpl::producer<bool> MessageArchive::value(ArchiveOption option) const {
	return _options[int(option)].value();
}

void MessageArchive::setEnabled(ArchiveOption option, bool enabled) {
	if (this->enabled(option) == enabled) {
		return;
	}
	_session->local().writePref<bool>(kOptionKeys[int(option)], enabled);
	_options[int(option)] = enabled;
	if (option == ArchiveOption::Translucent) {
		refreshCopies();
	} else if (option == ArchiveOption::Deleted && !enabled) {
		_media.clear();
	}
}

DeletedMark MessageArchive::mark() const {
	return _mark.current();
}

rpl::producer<DeletedMark> MessageArchive::markValue() const {
	return _mark.value();
}

void MessageArchive::setMark(DeletedMark mark) {
	if (_mark.current() != mark) {
		_session->local().writePref<QString>(kMarkKey, QString::number(int(mark)));
		_mark = mark;
		refreshCopies();
	}
}

bool MessageArchive::canSave(not_null<HistoryItem*> item) const {
	if (!IsServerMsgId(item->id)
		|| !item->isRegular()
		|| item->isService()
		|| item->isEphemeral()
		|| item->ttlDestroyAt()
		|| item->forbidsSaving()
		|| item->hasPossibleRestrictions()
		|| item->paidType() != PaidPostType::None) {
		return false;
	}
	const auto peer = item->history()->peer;
	const auto user = peer->asUser();
	return peer->allowsForwarding()
		&& (!user || !user->isBot() || enabled(ArchiveOption::Bots));
}

void MessageArchive::rememberMedia(
		not_null<HistoryItem*> item,
		const MTPMessageMedia *media) {
	const auto id = item->fullId();
	if (!media || !enabled(ArchiveOption::Deleted)
		|| !SupportedMedia(*media) || !canSave(item)) {
		_media.erase(id);
		return;
	}
	if (_media.size() >= kMaxMediaEntries && !_media.contains(id)) {
		_media.erase(_media.begin());
	}
	_media.insert_or_assign(id, *media);
}

void MessageArchive::saveEdit(
		not_null<HistoryItem*> item,
		TimeId date,
		const TextWithEntities &next) {
	if (!enabled(ArchiveOption::Edits) || !date || !canSave(item)) {
		return;
	}
	const auto savedMedia = item->Get<HistoryMessageSavedMediaData>();
	const auto &previous = savedMedia ? savedMedia->text : item->originalText();
	if (previous.text.isEmpty()
		|| previous.text == next.text
		|| previous.text.size() > kMaxPayloadBytes / 2) {
		return;
	}
	auto &entry = _entries[item->fullId()];
	if (!entry.revisions.empty() && entry.revisions.back().date >= date
		&& entry.revisions.back().text == previous.text) {
		return;
	}
	if (entry.revisions.size() == kMaxRevisions) {
		entry.revisions.erase(entry.revisions.begin());
	}
	entry.revisions.push_back({ date, previous.text });
	entry.savedAt = base::unixtime::now();
	write();
}

bool MessageArchive::willSaveDeleted(not_null<HistoryItem*> item) const {
	return enabled(ArchiveOption::Deleted)
		&& canSave(item)
		&& (!item->originalText().empty() || _media.contains(item->fullId()));
}

bool MessageArchive::saveDeleted(
		const std::vector<not_null<HistoryItem*>> &items) {
	if (!enabled(ArchiveOption::Deleted)) {
		return false;
	}
	auto changed = false;
	for (const auto item : items) {
		if (!canSave(item)) {
			continue;
		}
		const auto id = item->fullId();
		const auto &text = item->originalText();
		const auto media = _media.find(id);
		auto payload = (media != end(_media))
			? Serialize(media->second)
			: QByteArray();
		if (text.text.isEmpty() && payload.isEmpty()) {
			continue;
		}
		auto &entry = _entries[id];
		if (entry.deleted) {
			continue;
		}
		entry.savedAt = base::unixtime::now();
		entry.deleted = Deleted{
			.from = item->from()->id,
			.date = item->date(),
			.thread = item->replyToTop(),
			.topic = item->history()->isForum() ? item->topicRootId() : MsgId(),
			.out = item->out(),
			.post = item->isPost(),
			.text = text.text,
			.entities = Serialize(Api::EntitiesToMTP(_session, text.entities)),
			.media = std::move(payload),
		};
		changed = true;
	}
	if (changed) {
		write();
	}
	return changed;
}

void MessageArchive::restore(not_null<History*> history) {
	if (_entries.empty() || !history->peer->allowsForwarding()) {
		return;
	}
	const auto owner = &_session->data();
	const auto peer = history->peer->id;
	const auto start = _entries.lower_bound(FullMsgId(peer, 0));
	for (auto i = start; i != end(_entries) && i->first.peer == peer; ++i) {
		auto &entry = i->second;
		if (!entry.deleted || entry.copy || owner->message(i->first)) {
			continue;
		}
		const auto &record = *entry.deleted;
		const auto id = owner->nextLocalMessageId();
		entry.copy = FullMsgId(peer, id);
		_copies.emplace(entry.copy, i->first);
		auto text = TextWithEntities{ .text = record.text };
		if (const auto entities
			= Deserialize<MTPVector<MTPMessageEntity>>(record.entities)) {
			text.entities = Api::EntitiesFromMTP(_session, entities->v);
		}
		const auto media = Deserialize<MTPMessageMedia>(record.media);
		const auto attachment = (media && SupportedMedia(*media))
			? *media
			: MTPMessageMedia(MTP_messageMediaEmpty());
		const auto flags = MessageFlag::Local
			| MessageFlag::HasFromId
			| ((record.thread || record.topic)
				? MessageFlag::HasReplyInfo
				: MessageFlag(0))
			| (record.out ? MessageFlag::Outgoing : MessageFlag(0))
			| (record.post ? MessageFlag::Post : MessageFlag(0));
		history->makeMessage(HistoryItemCommonFields{
			.id = id,
			.flags = flags,
			.from = record.from,
			.replyTo = {
				.messageId = record.thread ? FullMsgId(peer, record.thread) : FullMsgId(),
				.topicRootId = record.topic,
			},
			.date = record.date,
		}, text, attachment);
	}
}

void MessageArchive::forget(FullMsgId id) {
	_media.erase(id);
	const auto i = _copies.find(id);
	if (i != end(_copies)) {
		if (const auto j = _entries.find(i->second); j != end(_entries)) {
			j->second.copy = {};
		}
		_copies.erase(i);
	}
}

bool MessageArchive::isDeleted(FullMsgId id) const {
	return IsClientMsgId(id.msg) && _copies.contains(id);
}

const std::vector<MessageArchive::Revision> &MessageArchive::edits(
		FullMsgId id) const {
	static const auto kEmpty = std::vector<Revision>();
	if (const auto i = _copies.find(id); i != end(_copies)) {
		id = i->second;
	}
	const auto i = _entries.find(id);
	return (i != end(_entries)) ? i->second.revisions : kEmpty;
}

void MessageArchive::removeCopies(const std::vector<FullMsgId> &ids) {
	auto &owner = _session->data();
	auto items = std::vector<not_null<HistoryItem*>>();
	for (const auto id : ids) {
		if (const auto item = owner.message(id)) {
			items.push_back(item);
		} else {
			_copies.erase(id);
		}
	}
	if (!items.empty()) {
		owner.notifyItemsAboutToBeDestroyed(items);
		for (const auto item : items) {
			item->destroy();
		}
	}
}

void MessageArchive::refreshCopies() {
	auto &owner = _session->data();
	for (const auto &[copy, original] : _copies) {
		if (const auto item = owner.message(copy)) {
			owner.requestItemViewRefresh(item);
		}
	}
}

void MessageArchive::clear() {
	auto ids = std::vector<FullMsgId>();
	ids.reserve(_copies.size());
	for (const auto &[copy, original] : _copies) {
		ids.push_back(copy);
	}
	_entries.clear();
	_session->local().clearPref(kArchiveKey);
	removeCopies(ids);
}

void MessageArchive::read() {
	const auto bytes = _session->local().readPref<QByteArray>(kArchiveKey);
	if (bytes.size() > kMaxArchiveBytes) {
		return;
	}
	auto stream = QDataStream(bytes);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = quint32();
	auto count = quint32();
	stream >> version >> count;
	if (version != kArchiveVersion || count > kMaxEntries) {
		return;
	}
	auto entries = decltype(_entries)();
	for (auto index = 0U; index != count; ++index) {
		auto peer = quint64();
		auto message = qint64();
		auto savedAt = qint32();
		auto deleted = bool();
		stream >> peer >> message >> savedAt >> deleted;
		auto entry = Entry{ .savedAt = savedAt };
		if (deleted) {
			auto record = Deleted();
			auto from = quint64();
			auto date = qint32();
			auto thread = qint64();
			auto topic = qint64();
			stream >> from >> date >> thread >> topic
				>> record.out >> record.post >> record.text
				>> record.entities >> record.media;
			record.from = PeerId(from);
			record.date = date;
			record.thread = MsgId(thread);
			record.topic = MsgId(topic);
			entry.deleted.emplace(std::move(record));
		}
		auto revisions = quint32();
		stream >> revisions;
		if (stream.status() != QDataStream::Ok || revisions > kMaxRevisions
			|| !peer || !IsServerMsgId(MsgId(message))) {
			return;
		}
		for (auto revision = 0U; revision != revisions; ++revision) {
			auto date = qint32();
			auto text = QString();
			stream >> date >> text;
			entry.revisions.push_back({ date, std::move(text) });
		}
		entries.emplace(FullMsgId(PeerId(peer), MsgId(message)), std::move(entry));
	}
	if (stream.status() == QDataStream::Ok && stream.atEnd()) {
		_entries = std::move(entries);
	}
}

void MessageArchive::write() {
	auto recent = std::vector<decltype(_entries)::iterator>();
	recent.reserve(_entries.size());
	for (auto i = begin(_entries); i != end(_entries); ++i) {
		recent.push_back(i);
	}
	std::stable_sort(begin(recent), end(recent), [](const auto &a, const auto &b) {
		return a->second.savedAt > b->second.savedAt;
	});
	auto bytes = QByteArray();
	auto stream = QDataStream(&bytes, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << kArchiveVersion << quint32(0);
	auto count = quint32();
	auto removed = std::vector<FullMsgId>();
	for (const auto i : recent) {
		const auto &[id, entry] = *i;
		const auto start = bytes.size();
		if (count < kMaxEntries) {
			stream << quint64(id.peer.value) << qint64(id.msg.bare)
				<< qint32(entry.savedAt) << entry.deleted.has_value();
			if (entry.deleted) {
				const auto &record = *entry.deleted;
				stream << quint64(record.from.value) << qint32(record.date)
					<< qint64(record.thread.bare) << qint64(record.topic.bare)
					<< record.out << record.post << record.text
					<< record.entities << record.media;
			}
			stream << quint32(entry.revisions.size());
			for (const auto &revision : entry.revisions) {
				stream << qint32(revision.date) << revision.text;
			}
			if (bytes.size() <= kMaxArchiveBytes) {
				++count;
				continue;
			}
			bytes.truncate(start);
			stream.device()->seek(start);
		}
		if (entry.copy) {
			removed.push_back(entry.copy);
		}
		_entries.erase(i);
	}
	stream.device()->seek(sizeof(kArchiveVersion));
	stream << count;
	_session->local().writePref<QByteArray>(kArchiveKey, std::move(bytes));
	removeCopies(removed);
}

} // namespace Data
