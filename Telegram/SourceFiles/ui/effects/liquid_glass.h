#pragma once

#include <QtGui/QImage>
#include <QtCore/QRect>
#include <QtGui/QColor>

class QPainter;

namespace Ui {

struct LiquidGlassStyle {
	bool enabled = true;
	int opacity = 72;
	int blur = 18;
	int refraction = 35;

	friend bool operator==(const LiquidGlassStyle&, const LiquidGlassStyle&) = default;
};

void PaintLiquidGlass(
	QPainter &p,
	QRect rect,
	const QImage &backdrop,
	QColor tint,
	const LiquidGlassStyle &style,
	int radius);

} // namespace Ui
