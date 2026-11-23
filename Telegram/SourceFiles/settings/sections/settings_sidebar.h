#pragma once

#include "ui/rp_widget.h"
#include "settings/settings_type.h"
#include <rpl/variable.h>

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

class Sidebar final : public Ui::RpWidget {
public:
	Sidebar(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Fn<void()> menu,
		Fn<void()> repaintBackdrop,
		Fn<void()> close);
	[[nodiscard]] Type selected() const {
		return _selected.current();
	}

private:
	rpl::variable<Type> _selected;

};

} // namespace Settings
