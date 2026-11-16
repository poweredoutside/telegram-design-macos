#pragma once

#include "ui/effects/liquid_glass.h"
#include <rpl/producer.h>

namespace Window::Theme {

[[nodiscard]] Ui::LiquidGlassStyle GlassStyle();
void SetGlassStyle(Ui::LiquidGlassStyle style);
[[nodiscard]] rpl::producer<Ui::LiquidGlassStyle> GlassStyleValue();

} // namespace Window::Theme
