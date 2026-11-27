#pragma once

#include "settings/settings_type.h"

namespace Window {
class SectionWidget;
class SessionController;
} // namespace Window

namespace Settings {

enum class MainSettingsItem {
	Premium,
	Stars,
	Business,
	SendGift,
	Faq,
	Features,
	AskQuestion,
};

[[nodiscard]] rpl::producer<bool> MainSettingsItemShownValue(
	MainSettingsItem item);
[[nodiscard]] Type TelegaId();
[[nodiscard]] bool IsTelegaAiChat(const Window::SectionWidget *section);
void ShowTelegaAiChat(not_null<Window::SessionController*> controller);
[[nodiscard]] QString TelegaSettingsLabel();
[[nodiscard]] QString TelegaAiChatLabel();
[[nodiscard]] QString GhostModeLabel();
[[nodiscard]] QString StreamerModeLabel();

} // namespace Settings
