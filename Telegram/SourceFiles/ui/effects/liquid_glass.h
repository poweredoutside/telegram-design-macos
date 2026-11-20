#pragma once

#include <QtGui/QImage>
#include <QtCore/QRect>
#include <QtGui/QColor>

class QPainter;

namespace Ui {

struct LiquidGlassStyle {
	bool enabled = true;
	int opacity = 15;
	int blur = 32;
	int refraction = 35;

	friend bool operator==(const LiquidGlassStyle&, const LiquidGlassStyle&) = default;
};

class LiquidGlassBackdropCache {
public:
	[[nodiscard]] QImage prepare(
		const QImage &image,
		QSize size,
		int radius,
		int blur,
		int refraction);

private:
	QImage _source;
	QImage _filtered;
	QSize _size;
	int _radius = 0;
	int _blur = 0;
	int _refraction = 0;

};

void PaintLiquidGlass(
	QPainter &p,
	QRect rect,
	const QImage &backdrop,
	QColor tint,
	const LiquidGlassStyle &style,
	int radius,
	LiquidGlassBackdropCache *cache = nullptr);

} // namespace Ui
