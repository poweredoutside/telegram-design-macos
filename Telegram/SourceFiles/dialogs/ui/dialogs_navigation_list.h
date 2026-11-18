#pragma once

#include "ui/rp_widget.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Dialogs {

enum class NavigationAction;

[[nodiscard]] std::unique_ptr<::Ui::RpWidget> CreateNavigationList(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	NavigationAction action,
	Fn<void()> repaintBackdrop);

} // namespace Dialogs
