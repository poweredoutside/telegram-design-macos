#pragma once

#include "rpl/producer.h"

#include <QtCore/QStringList>

namespace Core::Telega {

[[nodiscard]] bool UseTelegramStyle();
[[nodiscard]] QStringList TelegramNavigationItems();
[[nodiscard]] QStringList TelegramNavigation();
void SetTelegramNavigation(const QStringList &items);
void ResetTelegramNavigation();

[[nodiscard]] rpl::producer<bool> GhostModeValue();
void SetGhostModeEnabled(bool enabled);
void ResetGhostModeSelection();

[[nodiscard]] bool StreamerModeEnabled();

}
