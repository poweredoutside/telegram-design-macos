#pragma once

#include "data/data_msg_id.h"
#include "rpl/variable.h"

#include <array>
#include <map>

class History;
class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace Data {

enum class ArchiveOption { Deleted, Edits, Bots, Translucent };
enum class DeletedMark { None, Text, Trash, Cross, Eye };

[[nodiscard]] QString DeletedMarkText(DeletedMark mark);

class MessageArchive final {
public:
	explicit MessageArchive(not_null<Main::Session*> session);

	[[nodiscard]] bool enabled(ArchiveOption option) const;
	[[nodiscard]] rpl::producer<bool> value(ArchiveOption option) const;
	void setEnabled(ArchiveOption option, bool enabled);
	[[nodiscard]] DeletedMark mark() const;
	[[nodiscard]] rpl::producer<DeletedMark> markValue() const;
	void setMark(DeletedMark mark);

	void rememberMedia(not_null<HistoryItem*> item, const MTPMessageMedia *media);
	void saveEdit(
		not_null<HistoryItem*> item,
		TimeId date,
		const TextWithEntities &next);
	[[nodiscard]] bool willSaveDeleted(not_null<HistoryItem*> item) const;
	[[nodiscard]] bool saveDeleted(const std::vector<not_null<HistoryItem*>> &items);
	void restore(not_null<History*> history);
	void forget(FullMsgId id);
	void clear();

	[[nodiscard]] bool isDeleted(FullMsgId id) const;
	struct Revision {
		TimeId date = 0;
		QString text;
	};
	[[nodiscard]] const std::vector<Revision> &edits(FullMsgId id) const;

private:
	struct Deleted {
		PeerId from = 0;
		TimeId date = 0;
		MsgId thread = 0;
		MsgId topic = 0;
		bool out = false;
		bool post = false;
		QString text;
		QByteArray entities;
		QByteArray media;
	};
	struct Entry {
		TimeId savedAt = 0;
		std::optional<Deleted> deleted;
		std::vector<Revision> revisions;
		FullMsgId copy;
	};

	[[nodiscard]] bool canSave(not_null<HistoryItem*> item) const;
	void read();
	void write();
	void removeCopies(const std::vector<FullMsgId> &ids);
	void refreshCopies();

	const not_null<Main::Session*> _session;
	std::array<rpl::variable<bool>, 4> _options;
	rpl::variable<DeletedMark> _mark = DeletedMark::Trash;
	std::map<FullMsgId, Entry> _entries;
	std::map<FullMsgId, MTPMessageMedia> _media;
	std::map<FullMsgId, FullMsgId> _copies;

};

} // namespace Data
