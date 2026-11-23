#pragma once

#include <QtGui/QColor>

namespace style {

struct MaterialDesignOptions {
	bool enabled = false;
	bool grouped = true;
	bool roundedSelection = true;
	bool floatingField = true;
	bool roundedMenu = true;
	bool expressive = true;
	bool switches = true;
	bool mutedSwitches = false;
	bool sliders = true;
	bool windows11 = false;
};

[[nodiscard]] const MaterialDesignOptions &MaterialDesign();
void SetMaterialDesign(MaterialDesignOptions options);

[[nodiscard]] QColor MaterialTone(
	QColor surface,
	QColor accent,
	qreal amount);

} // namespace style
