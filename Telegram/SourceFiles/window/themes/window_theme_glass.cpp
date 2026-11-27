#include "window/themes/window_theme_glass.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "core/core_telega_preferences.h"
#include "ui/effects/layer_backdrop.h"
#include "ui/layers/box_background.h"
#include "ui/layers/layer_widget.h"
#include "ui/style/style_core.h"

#include <QtGui/QPainter>
#include <rpl/event_stream.h>
#include <rpl/then.h>

namespace Window::Theme {
namespace {

constexpr auto kGlassDialogsPreference = "macos/glass/dialogs";

rpl::event_stream<Ui::LiquidGlassStyle> Changes;
rpl::event_stream<bool> DialogChanges;

struct DialogBackdrop {
	QRect geometry;
	Ui::LiquidGlassStyle style;
	QColor tint;
	int ratio = 0;
	int radius = 0;
	QImage image;
};

Ui::BoxBackground MakeGlassDialogBackground(not_null<Ui::LayerWidget*> layer) {
	if (!Core::Telega::UseMacOSStyle()) {
		return {};
	}
	const auto state = std::make_shared<DialogBackdrop>();
	return {
		.enabled = GlassDialogsEnabled,
		.image = [=](QColor tint, int radius) {
			if (layer->size().isEmpty() || Ui::CapturingUnderLayer()) {
				return QImage();
			}
			auto glass = GlassStyle();
			glass.enabled = true;
			glass.opacity = std::max(glass.opacity, 65);
			const auto area = layer->rect();
			const auto geometry = QRect(
				layer->mapToGlobal(QPoint()), area.size());
			const auto ratio = style::DevicePixelRatio();
			if (state->geometry == geometry
				&& state->style == glass
				&& state->tint == tint
				&& state->ratio == ratio
				&& state->radius == radius) {
				return state->image;
			}
			const auto cache = state;
			const auto backdrop = glass.opacity < 100
				? Ui::CaptureUnderLayer(layer)
				: QImage();
			auto result = QImage(
				area.size() * ratio, QImage::Format_ARGB32_Premultiplied);
			result.setDevicePixelRatio(ratio);
			result.fill(Qt::transparent);
			{
				auto p = QPainter(&result);
				Ui::PaintLiquidGlass(p, area, backdrop, tint, glass, radius);
			}
			*cache = { geometry, glass, tint, ratio, radius, result };
			return result;
		},
	};
}


} // namespace

Ui::LiquidGlassStyle GlassStyle() {
	auto &settings = Core::App().settings();
	return {
		.enabled = Core::Telega::UseMacOSStyle()
			&& settings.readPref<bool>("macos/glass/enabled", true),
		.opacity = std::clamp(settings.readPref<int>("macos/glass/opacity", 15), 15, 100),
		.blur = std::clamp(settings.readPref<int>("macos/glass/blur", 32), 0, 40),
		.refraction = std::clamp(settings.readPref<int>("macos/glass/refraction", 35), 0, 100),
	};
}

void SetGlassStyle(Ui::LiquidGlassStyle style) {
	if (!Core::Telega::UseMacOSStyle()) {
		return;
	}
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

bool GlassDialogsEnabled() {
	return Core::Telega::UseMacOSStyle()
		&& Core::App().settings().readPref<bool>(kGlassDialogsPreference, false);
}

void SetGlassDialogsEnabled(bool enabled) {
	if (!Core::Telega::UseMacOSStyle() || GlassDialogsEnabled() == enabled) {
		return;
	}
	Core::App().settings().writePref<bool>(kGlassDialogsPreference, enabled);
	Core::App().saveSettingsDelayed();
	DialogChanges.fire_copy(enabled);
}

rpl::producer<bool> GlassDialogsEnabledValue() {
	return rpl::single(GlassDialogsEnabled()) | rpl::then(DialogChanges.events());
}

void InitGlassDialogs() {
	Ui::SetBoxBackgroundFactory(MakeGlassDialogBackground);
}

} // namespace Window::Theme
