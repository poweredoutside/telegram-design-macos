#include "api/api_gemini_chat.h"

#include "base/unixtime.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "settings.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtCore/QElapsedTimer>

namespace Api::GeminiChat {
namespace {

constexpr auto kModels = std::array{
	"gemini-flash-latest",
	"gemini-flash-lite-latest",
	"gemini-pro-latest",
};
constexpr auto kModelPref = "telega.gemini.model-name";

bool ValidModelName(const QString &name) {
	static const auto pattern = QRegularExpression(u"\\Agemini-[A-Za-z0-9._-]{1,80}\\z"_q);
	return pattern.match(name).hasMatch();
}

[[nodiscard]] QString ErrorFrom(const QJsonObject &object) {
	const auto status = object.value(u"error"_q).toObject()
		.value(u"code"_q).toInt();
	if (status) {
		return u"Gemini HTTP %1"_q.arg(status);
	}
	const auto blocked = object.value(u"promptFeedback"_q).toObject()
		.value(u"blockReason"_q).toString();
	if (!blocked.isEmpty()) {
		return u"Blocked: "_q + blocked;
	}
	const auto candidates = object.value(u"candidates"_q).toArray();
	if (!candidates.isEmpty()) {
		const auto reason = candidates[0].toObject()
			.value(u"finishReason"_q).toString();
		if (!reason.isEmpty() && reason != u"STOP"_q) {
			return u"Finish reason: "_q + reason;
		}
	}
	return QString();
}

struct State {
	std::vector<AiChatMessage> history;
	QNetworkAccessManager *manager = nullptr;
	QPointer<QNetworkReply> reply;
	QString partial;
	QByteArray key;
	bool waiting = false;
};

State &Chat() {
	static auto result = State();
	return result;
}
[[nodiscard]] QString PresetModelName() {
	const auto index = std::clamp(
		Preference("model", 0),
		0,
		int(kModels.size()) - 1);
	return QString::fromLatin1(kModels[index]);
}

[[nodiscard]] QString RolePrompt() {
	switch (Preference("role", 0)) {
	case 1: return u"You are a translator. Translate the user's text into Russian if it is in another language, otherwise into English. Reply with the translation only."_q;
	case 2: return u"You are an editor. Fix spelling, grammar and style of the user's text and reply with the corrected text only."_q;
	case 3: return u"You summarize the user's text in a few short sentences."_q;
	}
	return u"You are a helpful assistant."_q;
}

[[nodiscard]] int HistoryLimit() {
	switch (Preference("context", 1)) {
	case 0: return 1;
	case 1: return 10;
	case 2: return 30;
	}
	return 60;
}

void Request(
		Fn<void(QString answer, bool ok)> done,
		Fn<void(QString partial)> progress) {
	auto &chat = Chat();
	if (!chat.manager) {
		chat.manager = new QNetworkAccessManager(qApp);
	}
	auto contents = QJsonArray();
	auto messages = std::vector<AiChatMessage>();
	for (const auto &message : chat.history) {
		if (!message.error) {
			messages.push_back(message);
		}
	}
	const auto limit = HistoryLimit();
	auto skip = std::max(int(messages.size()) - limit, 0);
	while (skip < int(messages.size()) && !messages[skip].user) {
		++skip;
	}
	auto index = 0;
	for (const auto &message : messages) {
		if (index++ < skip) {
			continue;
		}
		contents.push_back(QJsonObject{
			{ u"role"_q, message.user ? u"user"_q : u"model"_q },
			{ u"parts"_q, QJsonArray{ QJsonObject{
				{ u"text"_q, message.text },
			} } },
		});
	}
	const auto streaming = Preference("streaming", 1);
	auto request = QNetworkRequest(QUrl(
		u"https://generativelanguage.googleapis.com/v1beta/models/"_q
		+ ModelName()
		+ (streaming
			? u":streamGenerateContent?alt=sse"_q
			: u":generateContent"_q)));
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	request.setRawHeader(
		"x-goog-api-key",
		chat.key);
	const auto body = QJsonDocument(QJsonObject{
		{ u"contents"_q, contents },
		{ u"systemInstruction"_q, QJsonObject{
			{ u"parts"_q, QJsonArray{ QJsonObject{
				{ u"text"_q, RolePrompt() },
			} } },
		} },
		{ u"generationConfig"_q, QJsonObject{
			{ u"temperature"_q, std::clamp(Preference("temperature", 10), 0, 20) / 10. },
			{ u"maxOutputTokens"_q, 4096 },
		} },
	}).toJson(QJsonDocument::Compact);
	request.setTransferTimeout(60000);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::ManualRedirectPolicy);
	const auto reply = chat.manager->post(request, body);
	chat.reply = reply;
	reply->setReadBufferSize(64 * 1024);
	constexpr auto kMaxResponseBytes = 2 * 1024 * 1024;
	const auto received = std::make_shared<qsizetype>(0);
	const auto buffer = std::make_shared<QByteArray>();
	const auto read = [=] {
		const auto chunk = reply->read(kMaxResponseBytes - *received + 1);
		*received += chunk.size();
		if (*received <= kMaxResponseBytes) {
			buffer->append(chunk);
			return true;
		}
		return false;
	};
	const auto fail = [=] {
		if (*received > kMaxResponseBytes) {
			done(u"Gemini: response too large."_q, false);
			return true;
		} else if (reply->error() == QNetworkReply::OperationCanceledError) {
			done(QString(), false);
			return true;
		}
		return false;
	};
	if (streaming) {
		const auto text = std::make_shared<QString>();
		const auto error = std::make_shared<QString>();
		const auto lastProgress = std::make_shared<QElapsedTimer>();
		const auto parse = [=] {
			while (true) {
				const auto end = buffer->indexOf('\n');
				if (end < 0) {
					break;
				}
				const auto line = buffer->left(end).trimmed();
				buffer->remove(0, end + 1);
				if (!line.startsWith("data:")) {
					continue;
				}
				const auto object = QJsonDocument::fromJson(
					line.mid(5).trimmed()).object();
				const auto message = ErrorFrom(object);
				if (!message.isEmpty()) {
					*error = message;
				}
				const auto candidates = object.value(
					u"candidates"_q).toArray();
				if (candidates.isEmpty()) {
					continue;
				}
				const auto parts = candidates[0].toObject()
					.value(u"content"_q).toObject()
					.value(u"parts"_q).toArray();
				for (const auto &part : parts) {
					const auto item = part.toObject();
					if (!item.value(u"thought"_q).toBool()) {
						*text += item.value(u"text"_q).toString().left(16000 - text->size());
					}
				}
				if (!lastProgress->isValid() || lastProgress->elapsed() >= 40) {
					lastProgress->start();
					progress(*text);
				}
			}
		};
		QObject::connect(reply, &QNetworkReply::readyRead, [=] {
			if (read()) {
				parse();
			} else {
				reply->abort();
			}
		});
		QObject::connect(reply, &QNetworkReply::finished, [=] {
			reply->deleteLater();
			if (*received <= kMaxResponseBytes) {
				read();
			}
			if (fail()) {
				return;
			}
			const auto rawError = ErrorFrom(QJsonDocument::fromJson(*buffer).object());
			buffer->append('\n');
			parse();
			if (!text->isEmpty()) {
				done(text->trimmed(), true);
			} else if (!error->isEmpty()) {
				done(*error, false);
			} else {
				done(rawError.isEmpty() ? u"Gemini: no reply received."_q : rawError, false);
			}
		});
		return;
	}
	QObject::connect(reply, &QNetworkReply::readyRead, [=] {
		if (!read()) {
			reply->abort();
		}
	});
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (*received <= kMaxResponseBytes) {
			read();
		}
		if (fail()) {
			return;
		}
		const auto &data = *buffer;
		const auto object = QJsonDocument::fromJson(data).object();
		const auto candidates = object.value(u"candidates"_q).toArray();
		auto text = QString();
		if (!candidates.isEmpty()) {
			const auto parts = candidates[0].toObject()
				.value(u"content"_q).toObject()
				.value(u"parts"_q).toArray();
			for (const auto &part : parts) {
				const auto item = part.toObject();
				if (!item.value(u"thought"_q).toBool()) {
					text += item.value(u"text"_q).toString().left(16000 - text.size());
				}
			}
		}
		if (!text.isEmpty()) {
			done(text.trimmed(), true);
			return;
		}
		const auto error = ErrorFrom(object);
		done(error.isEmpty() ? u"Gemini: no reply received."_q : error, false);
	});
}

[[nodiscard]] rpl::event_stream<> &Updates() {
	static auto result = rpl::event_stream<>();
	return result;
}

[[nodiscard]] QString HistoryPath() {
	return cWorkingDir() + u"tdata/telega-ai-chat.json"_q;
}

void SaveHistory() {
	auto list = QJsonArray();
	for (const auto &message : Chat().history) {
		list.push_back(QJsonObject{
			{ u"user"_q, message.user },
			{ u"error"_q, message.error },
			{ u"text"_q, message.text },
			{ u"date"_q, qint64(message.date) },
		});
	}
	auto file = QSaveFile(HistoryPath());
	const auto data = QJsonDocument(list).toJson(QJsonDocument::Compact);
	if (!file.open(QIODevice::WriteOnly)
		|| file.write(data) != data.size() || !file.commit()) {
		LOG(("Gemini: failed to save local chat history"));
	}
}

void LoadHistory() {
	static auto loaded = false;
	if (std::exchange(loaded, true)) {
		return;
	}
	auto file = QFile(HistoryPath());
	if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024) {
		return;
	}
	const auto entries = QJsonDocument::fromJson(file.readAll()).array();
	for (auto index = std::max(entries.size() - 100, qsizetype(0)); index != entries.size(); ++index) {
		const auto entry = entries.at(index);
		const auto object = entry.toObject();
		const auto text = object.value(u"text"_q).toString().left(16000);
		if (text.isEmpty()) {
			continue;
		}
		Chat().history.push_back({
			.user = object.value(u"user"_q).toBool(),
			.error = object.value(u"error"_q).toBool(),
			.text = text,
			.date = TimeId(object.value(u"date"_q).toInteger()),
		});
	}
}

} // namespace

int Preference(std::string_view name, int fallback) {
	return Core::App().settings().readPref<int>(
		"telega.gemini." + std::string(name), fallback);
}

void SetPreference(std::string_view name, int value) {
	Core::App().settings().writePref<int>(
		"telega.gemini." + std::string(name), value);
	Core::App().saveSettingsDelayed();
}

void SetKey(QByteArray key) {
	Chat().key = key.trimmed();
}

bool HasKey() {
	return !Chat().key.isEmpty();
}

QString ModelName() {
	const auto saved = QString::fromUtf8(
		Core::App().settings().readPref<QByteArray>(kModelPref));
	return ValidModelName(saved) ? saved : PresetModelName();
}

void SetModelName(const QString &model) {
	if (model.isEmpty() || ValidModelName(model)) {
		Core::App().settings().writePref<QByteArray>(kModelPref, model.toUtf8());
		Core::App().saveSettingsDelayed();
	}
}

void FetchModels(Fn<void(QStringList)> done) {
	auto &chat = Chat();
	if (chat.key.isEmpty()) {
		done({});
		return;
	}
	if (!chat.manager) {
		chat.manager = new QNetworkAccessManager(qApp);
	}
	auto request = QNetworkRequest(QUrl(
		u"https://generativelanguage.googleapis.com/v1beta/models?pageSize=100"_q));
	request.setRawHeader("x-goog-api-key", chat.key);
	request.setTransferTimeout(15000);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::ManualRedirectPolicy);
	const auto reply = chat.manager->get(request);
	constexpr auto kMaxBytes = 1024 * 1024;
	reply->setReadBufferSize(kMaxBytes);
	QObject::connect(reply, &QNetworkReply::readyRead, reply, [=] {
		if (reply->bytesAvailable() >= kMaxBytes) {
			reply->abort();
		}
	});
	QObject::connect(reply, &QNetworkReply::finished, reply, [=, done = std::move(done)] {
		auto names = QStringList();
		if (reply->error() == QNetworkReply::NoError && reply->bytesAvailable() < kMaxBytes) {
			const auto models = QJsonDocument::fromJson(reply->readAll())
				.object().value(u"models"_q).toArray();
			for (const auto &model : models) {
				const auto object = model.toObject();
				const auto name = object.value(u"name"_q).toString();
				const auto methods = object.value(u"supportedGenerationMethods"_q).toArray();
				if (name.startsWith(u"models/"_q)
					&& methods.contains(QJsonValue(u"generateContent"_q))) {
					const auto id = name.mid(7);
					if (ValidModelName(id) && !names.contains(id)) {
						names.push_back(id);
					}
				}
			}
		}
		reply->deleteLater();
		done(std::move(names));
	});
}

void Transcribe(
		QByteArray bytes,
		Recording recording,
		Fn<void(QString, bool)> done) {
	auto &chat = Chat();
	if (chat.key.isEmpty() || bytes.isEmpty() || bytes.size() > kRecordingBytesLimit) {
		done({}, false);
		return;
	}
	if (!chat.manager) {
		chat.manager = new QNetworkAccessManager(qApp);
	}
	auto request = QNetworkRequest(QUrl(
		u"https://generativelanguage.googleapis.com/v1beta/models/"_q
		+ ModelName() + u":generateContent"_q));
	request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_q);
	request.setRawHeader("x-goog-api-key", chat.key);
	request.setTransferTimeout(60000);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::ManualRedirectPolicy);
	const auto body = QJsonDocument(QJsonObject{
		{ u"contents"_q, QJsonArray{ QJsonObject{
			{ u"parts"_q, QJsonArray{
				QJsonObject{ { u"text"_q, u"Transcribe the spoken audio in its original language. Return only the transcript, without commentary."_q } },
				QJsonObject{ { u"inlineData"_q, QJsonObject{
					{ u"mimeType"_q, (recording == Recording::RoundVideo)
						? u"video/mp4"_q : u"audio/ogg"_q },
					{ u"data"_q, QString::fromLatin1(bytes.toBase64()) },
				} } },
			} },
		} } },
		{ u"generationConfig"_q, QJsonObject{ { u"maxOutputTokens"_q, 4096 } } },
	}).toJson(QJsonDocument::Compact);
	const auto reply = chat.manager->post(request, body);
	constexpr auto kMaxReply = 512 * 1024;
	reply->setReadBufferSize(kMaxReply);
	QObject::connect(reply, &QNetworkReply::readyRead, reply, [=] {
		if (reply->bytesAvailable() >= kMaxReply) {
			reply->abort();
		}
	});
	QObject::connect(reply, &QNetworkReply::finished, reply, [=, done = std::move(done)] {
		auto transcript = QString();
		if (reply->error() == QNetworkReply::NoError
			&& reply->bytesAvailable() < kMaxReply) {
			const auto result = QJsonDocument::fromJson(reply->readAll()).object();
			const auto candidates = result.value(u"candidates"_q).toArray();
			if (!candidates.isEmpty()) {
				const auto parts = candidates.first().toObject()
					.value(u"content"_q).toObject().value(u"parts"_q).toArray();
				for (const auto &part : parts) {
					const auto object = part.toObject();
					if (!object.value(u"thought"_q).toBool()) {
						transcript += object.value(u"text"_q).toString();
					}
				}
			}
		}
		reply->deleteLater();
		transcript = transcript.trimmed();
		const auto success = !transcript.isEmpty();
		done(std::move(transcript), success);
	});
}

const std::vector<AiChatMessage> &AiChatHistory() {
	LoadHistory();
	return Chat().history;
}

QString AiChatPartial() {
	return Chat().partial;
}

bool AiChatWaiting() {
	return Chat().waiting;
}

rpl::producer<> AiChatUpdates() {
	return Updates().events();
}

bool AiChatClear() {
	LoadHistory();
	if (Chat().waiting) {
		return false;
	}
	auto file = QFile(HistoryPath());
	if (file.exists() && !file.remove()) {
		return false;
	}
	Chat().history.clear();
	Updates().fire({});
	return true;
}

void AiChatCancel() {
	if (Chat().waiting && Chat().reply) {
		Chat().reply->abort();
	}
}

bool AiChatSend(
		const QString &text) {
	LoadHistory();
	if (text.trimmed().isEmpty() || Chat().waiting) {
		return false;
	} else if (Chat().key.isEmpty() || text.size() > 16000) {
		return false;
	}
	if (Chat().history.size() >= 100) {
		Chat().history.erase(Chat().history.begin(), Chat().history.begin() + 2);
	}
	Chat().history.push_back({
		.user = true,
		.text = text.trimmed(),
		.date = base::unixtime::now(),
	});
	Chat().waiting = true;
	Chat().partial = QString();
	SaveHistory();
	Updates().fire({});
	Request([=](QString answer, bool ok) {
		Chat().reply = nullptr;
		Chat().waiting = false;
		Chat().partial = QString();
		Chat().history.push_back({
			.error = !ok,
			.text = answer.isEmpty() ? u"Canceled"_q : answer.left(16000),
			.date = base::unixtime::now(),
		});
		SaveHistory();
		Updates().fire({});
	}, [=](QString partial) {
		Chat().partial = partial;
		Updates().fire({});
	});
	return true;
}

}
