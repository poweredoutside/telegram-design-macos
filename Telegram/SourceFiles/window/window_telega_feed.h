#pragma once

namespace Window {

class SessionController;

[[nodiscard]] QString TelegaFeedLabel();
void ShowTelegaFeed(not_null<SessionController*> controller);

}
