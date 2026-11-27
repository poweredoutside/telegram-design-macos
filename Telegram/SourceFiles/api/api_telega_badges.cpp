#include "api/api_telega_badges.h"

#include "settings.h"
#include "telega_version.h"

#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QLoggingCategory>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtWidgets/QApplication>

#include <limits>
#include <map>
#include <set>

namespace Api::TelegaBadges {
namespace {

constexpr auto kListUrl = "https://raw.githubusercontent.com/"
	"poweredoutside/telega-badges/main/badges.json";
constexpr auto kContentsUrl = "https://api.github.com/repos/"
	"poweredoutside/telega-badges/contents/badges.json?ref=main";
constexpr auto kMaxListSize = 256 * 1024;
constexpr auto kMaxResponseSize = 512 * 1024;
constexpr auto kRefreshInterval = 15 * 60 * 1000;
constexpr auto kDaySeconds = qint64(24 * 60 * 60);

struct Badge {
	Type type;
	int number = 0;
	QString comment;
	qint64 expiresAt = 0;

	friend bool operator==(const Badge &, const Badge &) = default;
};

struct Registry {
	std::map<uint64, Badge> badges;
	int nextNumber = 1;

	friend bool operator==(const Registry &, const Registry &) = default;
};

struct Cache {
	rpl::variable<Registry> registry;
	rpl::event_stream<> expiration;
	uint64 revision = 0;
};

Cache &Shared() {
	static auto cache = Cache();
	return cache;
}

bool IsActive(const Badge &badge, qint64 now) {
	return !badge.expiresAt || badge.expiresAt > now;
}

void ScheduleExpiration();

QTimer &ExpirationTimer() {
	static const auto timer = [] {
		const auto result = new QTimer(qApp);
		result->setSingleShot(true);
		result->setTimerType(Qt::PreciseTimer);
		QObject::connect(result, &QTimer::timeout, result, [] {
			Shared().expiration.fire({});
			ScheduleExpiration();
		});
		return result;
	}();
	return *timer;
}

void ScheduleExpiration() {
	const auto now = QDateTime::currentSecsSinceEpoch();
	auto nearest = qint64(0);
	for (const auto &[id, badge] : Shared().registry.current().badges) {
		if (badge.expiresAt > now && (!nearest || badge.expiresAt < nearest)) {
			nearest = badge.expiresAt;
		}
	}
	auto &timer = ExpirationTimer();
	if (nearest) {
		const auto maxSeconds = qint64(std::numeric_limits<int>::max() / 1000);
		timer.start(int(std::min(nearest - now, maxSeconds) * 1000));
	} else {
		timer.stop();
	}
}

QString Name(Type type) {
	return (type == Type::Developer) ? u"developer"_q : u"supporter"_q;
}

std::optional<Type> ParseType(const QJsonValue &value) {
	const auto name = value.toString();
	if (name == Name(Type::Developer)) {
		return Type::Developer;
	} else if (name == Name(Type::Supporter)) {
		return Type::Supporter;
	}
	return std::nullopt;
}

std::optional<Registry> ParseList(const QByteArray &bytes) {
	if (bytes.size() > kMaxListSize) {
		return std::nullopt;
	}
	const auto root = QJsonDocument::fromJson(bytes).object();
	const auto list = root.value(u"badges"_q);
	if (root.value(u"version"_q).toInt() != 1 || !list.isObject()) {
		return std::nullopt;
	}
	const auto badges = list.toObject();
	auto registry = Registry();
	for (auto i = badges.constBegin(); i != badges.constEnd(); ++i) {
		const auto key = i.key();
		const auto id = key.toULongLong();
		const auto type = ParseType(i.value());
		if (!id || key != QString::number(id) || !type) {
			return std::nullopt;
		}
		registry.badges.emplace(id, Badge{ *type });
	}
	const auto numbers = root.value(u"numbers"_q);
	const auto nextNumber = root.value(u"nextNumber"_q);
	if (numbers.isUndefined() && nextNumber.isUndefined()) {
		for (auto &[id, badge] : registry.badges) {
			badge.number = registry.nextNumber++;
		}
	} else {
		const auto assigned = numbers.toObject();
		registry.nextNumber = nextNumber.toInt();
		if (!numbers.isObject() || registry.nextNumber < 1
			|| size_t(assigned.size()) != registry.badges.size()) {
			return std::nullopt;
		}
		auto used = std::set<int>();
		for (auto &[id, badge] : registry.badges) {
			badge.number = assigned.value(QString::number(id)).toInt();
			if (badge.number < 1 || badge.number >= registry.nextNumber
				|| !used.insert(badge.number).second) {
				return std::nullopt;
			}
		}
	}
	const auto comments = root.value(u"comments"_q);
	if (!comments.isUndefined() && !comments.isObject()) {
		return std::nullopt;
	}
	const auto values = comments.toObject();
	for (auto i = values.constBegin(); i != values.constEnd(); ++i) {
		const auto key = i.key();
		const auto id = key.toULongLong();
		const auto badge = registry.badges.find(id);
		const auto comment = i.value().toString().trimmed();
		if (badge == end(registry.badges) || key != QString::number(id)
			|| !i.value().isString() || comment.size() > kCommentLimit) {
			return std::nullopt;
		}
		badge->second.comment = comment;
	}
	const auto expirations = root.value(u"expiresAt"_q);
	if (!expirations.isUndefined() && !expirations.isObject()) {
		return std::nullopt;
	}
	const auto dates = expirations.toObject();
	for (auto i = dates.constBegin(); i != dates.constEnd(); ++i) {
		const auto key = i.key();
		const auto id = key.toULongLong();
		const auto badge = registry.badges.find(id);
		const auto expiresAt = i.value().toInteger();
		if (badge == end(registry.badges) || key != QString::number(id)
			|| expiresAt <= 0 || expiresAt > std::numeric_limits<qint64>::max() / 1000) {
			return std::nullopt;
		}
		badge->second.expiresAt = expiresAt;
	}
	return registry;
}

QByteArray Serialize(const Registry &registry) {
	auto badges = QJsonObject();
	auto numbers = QJsonObject();
	auto comments = QJsonObject();
	auto expirations = QJsonObject();
	for (const auto &[id, badge] : registry.badges) {
		const auto key = QString::number(id);
		badges.insert(key, Name(badge.type));
		numbers.insert(key, badge.number);
		if (!badge.comment.isEmpty()) {
			comments.insert(key, badge.comment);
		}
		if (badge.expiresAt) {
			expirations.insert(key, qint64(badge.expiresAt));
		}
	}
	return QJsonDocument(QJsonObject{
		{ u"version"_q, 1 },
		{ u"badges"_q, badges },
		{ u"numbers"_q, numbers },
		{ u"nextNumber"_q, registry.nextNumber },
		{ u"comments"_q, comments },
		{ u"expiresAt"_q, expirations },
	}).toJson(QJsonDocument::Indented);
}

QString CachePath() {
	return cWorkingDir() + u"tdata/telega-public-badges.json"_q;
}

void SaveToCache(Registry registry, const QByteArray &bytes) {
	auto &cache = Shared();
	++cache.revision;
	if (cache.registry.current() != registry) {
		cache.registry = std::move(registry);
	}
	ScheduleExpiration();
	auto file = QSaveFile(CachePath());
	if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size()) {
		if (!file.commit()) {
			qWarning("Could not save the public Telega badge cache.");
		}
	}
}

QNetworkAccessManager &Network() {
	static const auto manager = new QNetworkAccessManager(qApp);
	return *manager;
}

QNetworkRequest Request(const char *url) {
	auto request = QNetworkRequest(QUrl(QString::fromLatin1(url)));
	request.setTransferTimeout(12000);
	request.setRawHeader("User-Agent", "Telega-Desktop/" TELEGA_VERSION);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::ManualRedirectPolicy);
	return request;
}

void ReadReply(
		not_null<QNetworkReply*> reply,
		not_null<QObject*> context,
		int limit,
		Fn<void(int, QByteArray)> done) {
	reply->setReadBufferSize(limit + 1);
	QObject::connect(reply, &QNetworkReply::readyRead, reply.get(), [=] {
		if (reply->bytesAvailable() > limit) {
			reply->abort();
		}
	});
	QObject::connect(reply, &QNetworkReply::finished, context.get(), [=] {
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto success = (reply->error() == QNetworkReply::NoError);
		const auto tooLarge = reply->bytesAvailable() > limit;
		done((tooLarge || (!success && status < 400)) ? 0 : status,
			(success && !tooLarge) ? reply->readAll() : QByteArray());
	});
	QObject::connect(
		reply,
		&QNetworkReply::finished,
		reply.get(),
		&QObject::deleteLater);
}

void Refresh() {
	const auto revision = Shared().revision;
	ReadReply(Network().get(Request(kListUrl)), qApp, kMaxListSize,
		[=](int status, const QByteArray &bytes) {
			if (status == 200 && revision == Shared().revision) {
				if (auto badges = ParseList(bytes)) {
					SaveToCache(std::move(*badges), bytes);
				}
			}
		});
}

QTimer &RefreshTimer() {
	static const auto timer = [] {
		auto file = QFile(CachePath());
		if (file.open(QIODevice::ReadOnly)) {
			if (auto badges = ParseList(file.read(kMaxListSize + 1))) {
				Shared().registry = std::move(*badges);
				ScheduleExpiration();
			}
		}
		const auto result = new QTimer(qApp);
		result->setInterval(kRefreshInterval);
		QObject::connect(result, &QTimer::timeout, result, Refresh);
		result->start();
		Refresh();
		return result;
	}();
	return *timer;
}

} // namespace

std::optional<Type> CurrentType(UserId user) {
	const auto &badges = Shared().registry.current().badges;
	const auto i = badges.find(user.bare);
	return (i != end(badges) && IsActive(i->second, QDateTime::currentSecsSinceEpoch()))
		? std::optional(i->second.type)
		: std::nullopt;
}

rpl::producer<> Changes() {
	RefreshTimer();
	return rpl::merge(
		Shared().registry.changes() | rpl::to_empty,
		Shared().expiration.events());
}

rpl::producer<std::optional<Details>> Value(UserId user) {
	return (rpl::single(rpl::empty) | rpl::then(Changes())) | rpl::map([=] {
		const auto &registry = Shared().registry.current();
		const auto now = QDateTime::currentSecsSinceEpoch();
		const auto i = registry.badges.find(user.bare);
		if (i == end(registry.badges) || !IsActive(i->second, now)) {
			return std::optional<Details>();
		}
		const auto type = i->second.type;
		const auto count = ranges::count_if(registry.badges, [=](const auto &pair) {
			return pair.second.type == type && IsActive(pair.second, now);
		});
		return std::optional(Details{
			type, i->second.number, int(count), i->second.comment, i->second.expiresAt,
		});
	}) | rpl::distinct_until_changed();
}

void Set(
		not_null<QObject*> context,
		UserId user,
		std::optional<Type> type,
		QString comment,
		std::optional<int> days,
		const QByteArray &token,
		Fn<void(Result)> done) {
	comment = comment.trimmed();
	if (!user.bare || token.isEmpty()
		|| (type && (comment.size() > kCommentLimit || (days && *days <= 0)))) {
		done(Result::Failed);
		return;
	}
	auto request = Request(kContentsUrl);
	request.setRawHeader("Accept", "application/vnd.github+json");
	request.setRawHeader("Authorization", "Bearer " + token);
	ReadReply(Network().get(request), context, kMaxResponseSize,
		[=](int status, const QByteArray &bytes) {
			if (status != 200) {
				done(Result::Failed);
				return;
			}
			const auto source = QJsonDocument::fromJson(bytes).object();
			const auto sha = source.value(u"sha"_q).toString();
			const auto encoded = source.value(u"content"_q).toString();
			const auto current = QByteArray::fromBase64(encoded.toLatin1());
			auto parsed = ParseList(current);
			if (sha.isEmpty() || !parsed) {
				done(Result::Failed);
				return;
			}
			auto registry = std::move(*parsed);
			auto &badges = registry.badges;
			const auto i = badges.find(user.bare);
			const auto previous = (i == end(badges))
				? std::nullopt
				: std::optional(i->second.type);
			const auto expiresAt = (type && days)
				? QDateTime::currentSecsSinceEpoch() + *days * kDaySeconds
				: qint64(0);
			if (previous == type && (!type || (i->second.comment == comment
				&& i->second.expiresAt == expiresAt))) {
				RefreshTimer().start();
				SaveToCache(std::move(registry), current);
				done(Result::Unchanged);
				return;
			}
			if (!type) {
				badges.erase(i);
			} else if (previous) {
				i->second.type = *type;
				i->second.comment = comment;
				i->second.expiresAt = expiresAt;
			} else {
				if (registry.nextNumber == std::numeric_limits<int>::max()) {
					done(Result::Failed);
					return;
				}
				badges.emplace(user.bare, Badge{
					*type, registry.nextNumber++, comment, expiresAt,
				});
			}
			const auto content = Serialize(registry);
			if (content.size() > kMaxListSize) {
				done(Result::Failed);
				return;
			}
			const auto body = QJsonObject{
				{ u"branch"_q, u"main"_q },
				{ u"message"_q, u"Update Telega Desktop badges"_q },
				{ u"content"_q, QString::fromLatin1(content.toBase64()) },
				{ u"sha"_q, sha },
			};
			auto put = request;
			auto url = put.url();
			url.setQuery(QString());
			put.setUrl(url);
			put.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_q);
			const auto payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
			ReadReply(Network().put(put, payload), context, kMaxResponseSize,
				[=](int status, const QByteArray &) {
					if (status == 200 || status == 201) {
						RefreshTimer().start();
						SaveToCache(registry, content);
						done(Result::Saved);
					} else {
						done((status == 409 || status == 422)
							? Result::Conflict
							: Result::Failed);
					}
				});
		});
}

} // namespace Api::TelegaBadges
