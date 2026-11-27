#pragma once

#include "data/data_peer_id.h"

#include <QtCore/QString>

namespace Api::TelegaBadges {

enum class Type { Supporter, Developer };
enum class Result { Saved, Unchanged, Conflict, Failed };
inline constexpr auto kCommentLimit = 240;

struct Details {
	Type type;
	int number = 0;
	int holders = 0;
	QString comment;
	qint64 expiresAt = 0;

	friend bool operator==(const Details &, const Details &) = default;
};

[[nodiscard]] std::optional<Type> CurrentType(UserId user);
[[nodiscard]] rpl::producer<> Changes();
[[nodiscard]] rpl::producer<std::optional<Details>> Value(UserId user);
void Set(
	not_null<QObject*> context,
	UserId user,
	std::optional<Type> type,
	QString comment,
	std::optional<int> days,
	const QByteArray &token,
	Fn<void(Result)> done);

} // namespace Api::TelegaBadges
