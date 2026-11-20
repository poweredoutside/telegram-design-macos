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
constexpr auto kProviderPref = "telega.ai.provider";
constexpr auto kProviderSlugs = std::array{
	"gemini", "openai", "claude", "perplexity",
};
constexpr auto kProviderNames = std::array{
	"Google Gemini", "OpenAI", "Claude", "Perplexity",
};
constexpr auto kDefaultModels = std::array{
	"", "gpt-5.4-mini", "claude-sonnet-4-6", "auto",
};

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
	QPointer<QNetworkReply> reply;
	QString partial;
	QByteArray key;
	bool waiting = false;
	bool loaded = false;
};

State &Chat(Provider provider = Provider::Gemini) {
	static auto chats = std::array<State, kProviderSlugs.size()>();
	return chats[int(provider)];
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

[[nodiscard]] QNetworkAccessManager *Network() {
	static const auto manager = new QNetworkAccessManager(qApp);
	return manager;
}

void Authorize(QNetworkRequest &request, Provider provider) {
	const auto &key = Chat(provider).key;
	if (provider == Provider::Gemini) {
		request.setRawHeader("x-goog-api-key", key);
	} else if (provider == Provider::Claude) {
		request.setRawHeader("x-api-key", key);
		request.setRawHeader("anthropic-version", "2023-06-01");
	} else if (!key.isEmpty()) {
		request.setRawHeader("Authorization", "Bearer " + key);
	}
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::ManualRedirectPolicy);
}

[[nodiscard]] QString TextBlocks(QJsonArray blocks, const QString &type) {
	auto text = QString();
	for (const auto &entry : blocks) {
		const auto block = entry.toObject();
		if (!block.value(u"thought"_q).toBool()
			&& (type.isEmpty() || block.value(u"type"_q).toString() == type)) {
			text += block.value(u"text"_q).toString().left(16000 - text.size());
		}
	}
	return text;
}

[[nodiscard]] QString ResponseText(
		Provider provider,
		const QJsonObject &object,
		bool streaming) {
	const auto type = object.value(u"type"_q).toString();
	if (provider == Provider::Gemini) {
		const auto candidates = object.value(u"candidates"_q).toArray();
		return candidates.isEmpty() ? QString() : TextBlocks(
			candidates.first().toObject().value(u"content"_q).toObject()
				.value(u"parts"_q).toArray(),
			QString());
	} else if (provider == Provider::Claude) {
		if (!streaming) {
			return TextBlocks(object.value(u"content"_q).toArray(), u"text"_q);
		} else if (type == u"content_block_start"_q) {
			return TextBlocks({ object.value(u"content_block"_q) }, u"text"_q);
		}
		const auto delta = object.value(u"delta"_q).toObject();
		return (type == u"content_block_delta"_q
			&& delta.value(u"type"_q).toString() == u"text_delta"_q)
			? delta.value(u"text"_q).toString()
			: QString();
	} else if (streaming) {
		return (type == u"response.output_text.delta"_q)
			? object.value(u"delta"_q).toString()
			: QString();
	}
	auto result = QString();
	for (const auto &entry : object.value(u"output"_q).toArray()) {
		const auto item = entry.toObject();
		if (item.value(u"type"_q).toString() == u"message"_q) {
			result += TextBlocks(item.value(u"content"_q).toArray(), u"output_text"_q)
				.left(16000 - result.size());
		}
	}
	return result;
}

struct GenerationRequest {
	QNetworkRequest request;
	QByteArray body;
};

[[nodiscard]] GenerationRequest BuildRequest(Provider provider, bool streaming) {
	const auto model = ChatModelName(provider);
	const auto &history = Chat(provider).history;
	auto messages = std::vector<const AiChatMessage*>();
	messages.reserve(history.size());
	for (const auto &message : history) {
		if (!message.error) {
			messages.push_back(&message);
		}
	}
	auto first = std::max(int(messages.size()) - HistoryLimit(), 0);
	while (first < int(messages.size()) && !messages[first]->user) {
		++first;
	}
	auto contents = QJsonArray();
	for (auto index = first; index < int(messages.size()); ++index) {
		const auto message = messages[index];
		if (provider == Provider::Gemini) {
			contents.push_back(QJsonObject{
				{ u"role"_q, message->user ? u"user"_q : u"model"_q },
				{ u"parts"_q, QJsonArray{ QJsonObject{
					{ u"text"_q, message->text },
				} } },
			});
		} else {
			contents.push_back(QJsonObject{
				{ u"role"_q, message->user ? u"user"_q : u"assistant"_q },
				{ u"content"_q, message->text },
			});
		}
	}
	auto url = QString();
	auto body = QJsonObject();
	switch (provider) {
	case Provider::Gemini:
		url = u"https://generativelanguage.googleapis.com/v1beta/models/"_q
			+ model
			+ (streaming
				? u":streamGenerateContent?alt=sse"_q
				: u":generateContent"_q);
		body = QJsonObject{
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
		};
		break;
	case Provider::Claude:
		url = u"https://api.anthropic.com/v1/messages"_q;
		body = QJsonObject{
			{ u"model"_q, model },
			{ u"messages"_q, contents },
			{ u"system"_q, RolePrompt() },
			{ u"max_tokens"_q, 4096 },
			{ u"stream"_q, streaming },
		};
		break;
	case Provider::OpenAI:
	case Provider::Perplexity:
		url = (provider == Provider::OpenAI)
			? u"https://api.openai.com/v1/responses"_q
			: u"https://api.perplexity.ai/v1/agent"_q;
		body = QJsonObject{
			{ u"input"_q, contents },
			{ u"instructions"_q, RolePrompt() },
			{ u"max_output_tokens"_q, 4096 },
			{ u"stream"_q, streaming },
			{ u"store"_q, false },
		};
		if (provider == Provider::Perplexity && model == u"auto"_q) {
			body.insert(u"effort"_q, u"fast"_q);
		} else {
			body.insert(u"model"_q, model);
		}
		break;
	}
	auto request = QNetworkRequest(QUrl(url));
	request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_q);
	request.setTransferTimeout(60000);
	Authorize(request, provider);
	return {
		std::move(request),
		QJsonDocument(body).toJson(QJsonDocument::Compact),
	};
}

[[nodiscard]] QString ResponseError(Provider provider, const QJsonObject &object) {
	if (provider == Provider::Gemini) {
		return ErrorFrom(object);
	}
	const auto type = object.value(u"type"_q).toString();
	if (object.value(u"error"_q).isObject()
		|| type == u"response.failed"_q
		|| type == u"error"_q) {
		return ProviderName(provider) + u": request failed."_q;
	}
	return {};
}

void Request(
		Provider provider,
		Fn<void(QString answer, bool ok)> done,
		Fn<void(QString partial)> progress) {
	const auto streaming = Preference("streaming", 1) != 0;
	const auto generation = BuildRequest(provider, streaming);
	const auto reply = Network()->post(generation.request, generation.body);
	Chat(provider).reply = reply;
	reply->setReadBufferSize(64 * 1024);
	constexpr auto kMaxResponseBytes = 2 * 1024 * 1024;
	struct Response {
		QByteArray buffer;
		QString text;
		QString error;
		QElapsedTimer lastProgress;
		qsizetype received = 0;
	};
	const auto state = std::make_shared<Response>();
	const auto read = [=] {
		const auto chunk = reply->read(kMaxResponseBytes - state->received + 1);
		state->received += chunk.size();
		if (state->received > kMaxResponseBytes) {
			return false;
		}
		state->buffer += chunk;
		return true;
	};
	const auto parse = [=] {
		while (true) {
			const auto end = state->buffer.indexOf('\n');
			if (end < 0) {
				break;
			}
			const auto line = state->buffer.left(end).trimmed();
			state->buffer.remove(0, end + 1);
			if (!line.startsWith("data:")) {
				continue;
			}
			const auto object = QJsonDocument::fromJson(line.mid(5).trimmed()).object();
			const auto error = ResponseError(provider, object);
			if (!error.isEmpty()) {
				state->error = error;
			}
			const auto text = ResponseText(provider, object, true);
			if (text.isEmpty() || state->text.size() >= 16000) {
				continue;
			}
			state->text += text.left(16000 - state->text.size());
			if (!state->lastProgress.isValid() || state->lastProgress.elapsed() >= 40) {
				state->lastProgress.start();
				progress(state->text);
			}
		}
	};
	QObject::connect(reply, &QNetworkReply::readyRead, reply, [=] {
		if (!read()) {
			reply->abort();
		} else if (streaming) {
			parse();
		}
	});
	QObject::connect(reply, &QNetworkReply::finished, reply, [=] {
		reply->deleteLater();
		if (state->received > kMaxResponseBytes || !read()) {
			done(ProviderName(provider) + u": response too large."_q, false);
			return;
		} else if (reply->error() == QNetworkReply::OperationCanceledError) {
			done({}, false);
			return;
		}
		const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() != QNetworkReply::NoError || (status && status >= 300)) {
			done(status
				? u"%1 HTTP %2"_q.arg(ProviderName(provider)).arg(status)
				: (ProviderName(provider) + u": network request failed."_q), false);
			return;
		}
		const auto object = QJsonDocument::fromJson(state->buffer).object();
		if (streaming) {
			state->buffer.append('\n');
			parse();
		} else {
			state->text = ResponseText(provider, object, false);
		}
		const auto answer = state->text.trimmed();
		if (!answer.isEmpty()) {
			done(answer, true);
		} else {
			const auto error = state->error.isEmpty()
				? ResponseError(provider, object)
				: state->error;
			done(error.isEmpty()
				? (ProviderName(provider) + u": no reply received."_q)
				: error, false);
		}
	});
}


[[nodiscard]] rpl::event_stream<> &Updates() {
	static auto result = rpl::event_stream<>();
	return result;
}

[[nodiscard]] QString HistoryPath(Provider provider) {
	const auto base = cWorkingDir() + u"tdata/telega-ai-chat"_q;
	return base + ((provider == Provider::Gemini)
		? QString()
		: (u"-"_q + QString::fromLatin1(kProviderSlugs[int(provider)])))
		+ u".json"_q;
}

void SaveHistory(Provider provider) {
	auto list = QJsonArray();
	for (const auto &message : Chat(provider).history) {
		list.push_back(QJsonObject{
			{ u"user"_q, message.user },
			{ u"error"_q, message.error },
			{ u"text"_q, message.text },
			{ u"date"_q, qint64(message.date) },
		});
	}
	auto file = QSaveFile(HistoryPath(provider));
	const auto data = QJsonDocument(list).toJson(QJsonDocument::Compact);
	if (!file.open(QIODevice::WriteOnly)
		|| file.write(data) != data.size() || !file.commit()) {
		LOG(("AI chat: failed to save local history for %1").arg(ProviderName(provider)));
	}
}

void LoadHistory(Provider provider) {
	auto &chat = Chat(provider);
	if (std::exchange(chat.loaded, true)) {
		return;
	}
	auto file = QFile(HistoryPath(provider));
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
		chat.history.push_back({
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

void SetKey(QByteArray key, Provider provider) {
	Chat(provider).key = key.trimmed();
}

bool HasKey(Provider provider) {
	return !Chat(provider).key.isEmpty();
}

bool HasKey() {
	return HasKey(Provider::Gemini);
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

Provider ChatProvider() {
	const auto index = Core::App().settings().readPref<int>(kProviderPref);
	return Provider(std::clamp(index, 0, int(kProviderSlugs.size()) - 1));
}

QString ProviderName(Provider provider) {
	return QString::fromLatin1(kProviderNames[int(provider)]);
}

bool SetChatProvider(Provider provider) {
	const auto current = ChatProvider();
	if (Chat(current).waiting) {
		return false;
	}
	if (provider != current) {
		Core::App().settings().writePref<int>(kProviderPref, int(provider));
		Core::App().saveSettingsDelayed();
		Updates().fire({});
	}
	return true;
}

[[nodiscard]] static std::string ChatModelPref(Provider provider) {
	return "telega.ai.model." + std::string(kProviderSlugs[int(provider)]);
}

[[nodiscard]] static bool ValidChatModel(Provider provider, const QString &name) {
	if (provider == Provider::Gemini) {
		return ValidModelName(name);
	}
	static const auto simple = QRegularExpression(u"\\A[A-Za-z0-9_.:-]{1,120}\\z"_q);
	static const auto qualified = QRegularExpression(
		u"\\A[A-Za-z0-9_.:-]{1,60}/[A-Za-z0-9_.:-]{1,100}\\z"_q);
	return (provider == Provider::Perplexity)
		? (name == u"auto"_q || qualified.match(name).hasMatch())
		: simple.match(name).hasMatch();
}

QString ChatModelName(Provider provider) {
	if (provider == Provider::Gemini) {
		return ModelName();
	}
	const auto saved = QString::fromUtf8(
		Core::App().settings().readPref<QByteArray>(ChatModelPref(provider)));
	return ValidChatModel(provider, saved)
		? saved
		: QString::fromLatin1(kDefaultModels[int(provider)]);
}

bool SetChatModelName(Provider provider, const QString &model) {
	if (!model.isEmpty() && !ValidChatModel(provider, model)) {
		return false;
	}
	if (provider == Provider::Gemini) {
		SetModelName(model);
	} else {
		Core::App().settings().writePref<QByteArray>(
			ChatModelPref(provider), model.toUtf8());
		Core::App().saveSettingsDelayed();
	}
	return true;
}

void FetchModels(Provider provider, Fn<void(QStringList)> done) {
	if (!HasKey(provider) && provider != Provider::Perplexity) {
		done({});
		return;
	}
	const auto urls = std::array{
		u"https://generativelanguage.googleapis.com/v1beta/models?pageSize=100"_q,
		u"https://api.openai.com/v1/models"_q,
		u"https://api.anthropic.com/v1/models?limit=100"_q,
		u"https://api.perplexity.ai/v1/models"_q,
	};
	auto request = QNetworkRequest(QUrl(urls[int(provider)]));
	request.setTransferTimeout(15000);
	Authorize(request, provider);
	const auto reply = Network()->get(request);
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
			const auto gemini = (provider == Provider::Gemini);
			const auto models = QJsonDocument::fromJson(reply->readAll()).object()
				.value(gemini ? u"models"_q : u"data"_q).toArray();
			for (const auto &model : models) {
				const auto object = model.toObject();
				auto id = object.value(gemini ? u"name"_q : u"id"_q).toString();
				if (gemini) {
					const auto methods = object.value(u"supportedGenerationMethods"_q).toArray();
					if (!id.startsWith(u"models/"_q)
						|| !methods.contains(QJsonValue(u"generateContent"_q))) {
						continue;
					}
					id = id.mid(7);
				}
				if (ValidChatModel(provider, id) && !names.contains(id)) {
					names.push_back(id);
				}
			}
		}
		if (provider == Provider::Perplexity && !names.contains(u"auto"_q)) {
			names.push_front(u"auto"_q);
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
	const auto reply = Network()->post(request, body);
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
	const auto provider = ChatProvider();
	LoadHistory(provider);
	return Chat(provider).history;
}

QString AiChatPartial() {
	return Chat(ChatProvider()).partial;
}

bool AiChatWaiting() {
	return Chat(ChatProvider()).waiting;
}

rpl::producer<> AiChatUpdates() {
	return Updates().events();
}

bool AiChatClear(Provider provider) {
	LoadHistory(provider);
	auto &chat = Chat(provider);
	if (chat.waiting) {
		return false;
	}
	auto file = QFile(HistoryPath(provider));
	if (file.exists() && !file.remove()) {
		return false;
	}
	chat.history.clear();
	Updates().fire({});
	return true;
}

void AiChatCancel() {
	auto &chat = Chat(ChatProvider());
	if (chat.waiting && chat.reply) {
		chat.reply->abort();
	}
}

bool AiChatSend(
		const QString &text) {
	const auto provider = ChatProvider();
	LoadHistory(provider);
	auto &chat = Chat(provider);
	const auto input = text.trimmed();
	if (input.isEmpty() || chat.waiting) {
		return false;
	} else if (chat.key.isEmpty() || input.size() > 16000) {
		return false;
	}
	if (chat.history.size() >= 100) {
		chat.history.erase(chat.history.begin(), chat.history.begin() + 2);
	}
	chat.history.push_back({
		.user = true,
		.text = input,
		.date = base::unixtime::now(),
	});
	chat.waiting = true;
	chat.partial = QString();
	SaveHistory(provider);
	Updates().fire({});
	Request(provider, [=](QString answer, bool ok) {
		auto &chat = Chat(provider);
		chat.reply = nullptr;
		chat.waiting = false;
		chat.partial = QString();
		chat.history.push_back({
			.error = !ok,
			.text = answer.isEmpty() ? u"Canceled"_q : answer.left(16000),
			.date = base::unixtime::now(),
		});
		SaveHistory(provider);
		Updates().fire({});
	}, [=](QString partial) {
		Chat(provider).partial = partial;
		Updates().fire({});
	});
	return true;
}

}
