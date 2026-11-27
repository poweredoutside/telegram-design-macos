#pragma once

#include <QtCore/QStringList>

namespace Api::GeminiChat {

enum class Provider { Gemini, OpenAI, Claude, Perplexity };

struct AiChatMessage {
	bool user = false;
	bool error = false;
	QString text;
	TimeId date = 0;
};

[[nodiscard]] int Preference(std::string_view name, int fallback);
void SetPreference(std::string_view name, int value);
[[nodiscard]] bool VoiceTranscriptionEnabled();
[[nodiscard]] rpl::producer<> VoiceTranscriptionChanges();
void SetKey(QByteArray key, Provider provider);
[[nodiscard]] bool HasKey();
[[nodiscard]] bool HasKey(Provider provider);
[[nodiscard]] QString ModelName();
void SetModelName(const QString &model);
void FetchModels(Provider provider, Fn<void(QStringList)> done);
[[nodiscard]] Provider ChatProvider();
[[nodiscard]] QString ProviderName(Provider provider);
[[nodiscard]] bool SetChatProvider(Provider provider);
[[nodiscard]] QString ChatModelName(Provider provider);
[[nodiscard]] bool SetChatModelName(Provider provider, const QString &model);
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
[[nodiscard]] bool AiChatClear(Provider provider);
void AiChatCancel();
bool AiChatSend(const QString &text);

}
