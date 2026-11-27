#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Info::Stories {

[[nodiscard]] QString NewStoryLabel();
void ShowPublishBox(not_null<Window::SessionController*> controller);

} // namespace Info::Stories
