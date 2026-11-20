#pragma once

#include <QtCore/QStringList>

namespace Api::GeminiChat {

struct AiChatMessage {
	bool user = false;
	bool error = false;
	QString text;
	TimeId date = 0;
};

[[nodiscard]] int Preference(std::string_view name, int fallback);
void SetPreference(std::string_view name, int value);
void SetKey(QByteArray key);
[[nodiscard]] bool HasKey();
[[nodiscard]] QString ModelName();
void SetModelName(const QString &model);
void FetchModels(Fn<void(QStringList)> done);
inline constexpr auto kRecordingBytesLimit = 8 * 1024 * 1024;
enum class Recording { Voice, RoundVideo };
void Transcribe(
	QByteArray bytes,
	Recording recording,
	Fn<void(QString, bool)> done);
[[nodiscard]] const std::vector<AiChatMessage> &AiChatHistory();
[[nodiscard]] QString AiChatPartial();
[[nodiscard]] bool AiChatWaiting();
[[nodiscard]] rpl::producer<> AiChatUpdates();
[[nodiscard]] bool AiChatClear();
void AiChatCancel();
bool AiChatSend(const QString &text);

}
