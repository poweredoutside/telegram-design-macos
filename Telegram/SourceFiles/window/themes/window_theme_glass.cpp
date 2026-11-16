#include "window/themes/window_theme_glass.h"

#include "base/platform/base_platform_info.h"
#include "core/application.h"
#include "core/core_settings.h"
#include <rpl/event_stream.h>
#include <rpl/then.h>

namespace Window::Theme {
namespace {

rpl::event_stream<Ui::LiquidGlassStyle> Changes;

} // namespace

Ui::LiquidGlassStyle GlassStyle() {
	auto &settings = Core::App().settings();
	return {
		.enabled = Platform::IsWindows()
			&& settings.readPref<bool>("macos/glass/enabled", true),
		.opacity = std::clamp(settings.readPref<int>("macos/glass/opacity", 72), 15, 100),
		.blur = std::clamp(settings.readPref<int>("macos/glass/blur", 18), 0, 40),
		.refraction = std::clamp(settings.readPref<int>("macos/glass/refraction", 35), 0, 100),
	};
}

void SetGlassStyle(Ui::LiquidGlassStyle style) {
	style.opacity = std::clamp(style.opacity, 15, 100);
	style.blur = std::clamp(style.blur, 0, 40);
	style.refraction = std::clamp(style.refraction, 0, 100);
	if (style == GlassStyle()) {
		return;
	}
	auto &settings = Core::App().settings();
	settings.writePref<bool>("macos/glass/enabled", style.enabled);
	settings.writePref<int>("macos/glass/opacity", style.opacity);
	settings.writePref<int>("macos/glass/blur", style.blur);
	settings.writePref<int>("macos/glass/refraction", style.refraction);
	Core::App().saveSettingsDelayed();
	Changes.fire_copy(style);
}

rpl::producer<Ui::LiquidGlassStyle> GlassStyleValue() {
	return rpl::single(GlassStyle()) | rpl::then(Changes.events());
}

} // namespace Window::Theme
