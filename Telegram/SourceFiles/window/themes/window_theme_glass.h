#pragma once

#include "ui/effects/liquid_glass.h"
#include <rpl/producer.h>

namespace Window::Theme {

[[nodiscard]] Ui::LiquidGlassStyle GlassStyle();
void SetGlassStyle(Ui::LiquidGlassStyle style);
[[nodiscard]] rpl::producer<Ui::LiquidGlassStyle> GlassStyleValue();

[[nodiscard]] bool GlassDialogsEnabled();
void SetGlassDialogsEnabled(bool enabled);
[[nodiscard]] rpl::producer<bool> GlassDialogsEnabledValue();
void InitGlassDialogs();

} // namespace Window::Theme
