#pragma once

#include "settings/settings_type.h"

namespace Window {
class SectionWidget;
class SessionController;
} // namespace Window

namespace Settings {

[[nodiscard]] Type TelegaId();
[[nodiscard]] bool IsTelegaAiChat(const Window::SectionWidget *section);
void ShowTelegaAiChat(not_null<Window::SessionController*> controller);
[[nodiscard]] QString TelegaSettingsLabel();
[[nodiscard]] QString TelegaAiChatLabel();

} // namespace Settings
