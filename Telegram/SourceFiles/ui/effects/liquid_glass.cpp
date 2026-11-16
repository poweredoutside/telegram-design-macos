#include "ui/effects/liquid_glass.h"

#include "ui/image/image_prepare.h"

#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

#include <algorithm>

namespace Ui {

void PaintLiquidGlass(
		QPainter &p,
		QRect rect,
		const QImage &backdrop,
		QColor tint,
		const LiquidGlassStyle &style,
		int radius) {
	if (!style.enabled || rect.isEmpty()) {
		p.fillRect(rect, tint);
		return;
	}
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	auto path = QPainterPath();
	path.addRoundedRect(QRectF(rect), radius, radius);
	p.setClipPath(path, Qt::IntersectClip);
	if (!backdrop.isNull()) {
		auto blurred = (style.blur > 0)
			? backdrop.scaled(
				QSize(std::max(1, rect.width() / 2), std::max(1, rect.height() / 2)),
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation)
			: backdrop;
		if (style.blur > 0) {
			blurred = Images::BlurLargeImage(
				std::move(blurred),
				std::clamp(style.blur / 2, 0, std::max(0, rect.height() / 2 - 1)));
		}
		p.drawImage(rect, blurred);
		const auto lens = std::clamp(style.refraction, 0, 100) / 100.;
		const auto depth = std::min(rect.height() / 3, std::max(1, radius));
		p.setOpacity(lens * .4);
		p.drawImage(
			QRect(rect.x(), rect.y(), rect.width(), depth),
			blurred,
			QRectF(blurred.width() * .02, 0, blurred.width() * .96, depth));
		p.setOpacity(1.);
	}
	tint.setAlphaF(std::clamp(style.opacity, 0, 100) / 100.);
	p.fillPath(path, tint);
	const auto dark = tint.lightnessF() < .5;
	auto sheen = QLinearGradient(rect.topLeft(), rect.bottomRight());
	sheen.setColorAt(0., QColor(255, 255, 255, dark ? 26 : 110));
	sheen.setColorAt(.45, QColor(255, 255, 255, 0));
	sheen.setColorAt(1., QColor(255, 255, 255, dark ? 7 : 35));
	p.fillPath(path, sheen);
	auto rim = QLinearGradient(rect.topLeft(), rect.bottomLeft());
	rim.setColorAt(0., QColor(255, 255, 255, dark ? 80 : 215));
	rim.setColorAt(.5, QColor(255, 255, 255, 15));
	rim.setColorAt(1., QColor(dark ? 255 : 0, dark ? 255 : 0, dark ? 255 : 0, 22));
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QBrush(rim), 1.));
	p.drawRoundedRect(QRectF(rect).adjusted(.5, .5, -.5, -.5), radius, radius);
	p.restore();
}

} // namespace Ui
