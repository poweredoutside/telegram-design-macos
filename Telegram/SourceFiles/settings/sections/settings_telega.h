#pragma once

#include "settings/settings_type.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

[[nodiscard]] Type TelegaId();
void ShowTelegaAiChat(not_null<Window::SessionController*> controller);
[[nodiscard]] QString TelegaSettingsLabel();
[[nodiscard]] QString TelegaAiChatLabel();

} // namespace Settings
