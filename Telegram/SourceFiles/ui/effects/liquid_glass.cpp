#include "ui/effects/liquid_glass.h"

#include "ui/image/image_prepare.h"

#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

#include <algorithm>

namespace Ui {
namespace {

QImage BlurBackdrop(const QImage &image, QSize size, int blur) {
	return Images::BlurLargeImage(
		image.scaled(
			QSize(std::max(1, size.width() / 2), std::max(1, size.height() / 2)),
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation),
		std::clamp(blur / 2, 0, std::max(0, size.height() / 2 - 1)));
}

} // namespace

QImage LiquidGlassBackdropCache::prepare(
		const QImage &image,
		QSize size,
		int blur) {
	if (_size != size
		|| _blur != blur
		|| _source.devicePixelRatio() != image.devicePixelRatio()
		|| _source != image) {
		_source = image;
		_size = size;
		_blur = blur;
		_filtered = BlurBackdrop(image, size, blur);
	}
	return _filtered;
}

void PaintLiquidGlass(
		QPainter &p,
		QRect rect,
		const QImage &backdrop,
		QColor tint,
		const LiquidGlassStyle &style,
		int radius,
		LiquidGlassBackdropCache *cache) {
	if (rect.isEmpty()) {
		return;
	}
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	auto path = QPainterPath();
	path.addRoundedRect(QRectF(rect), radius, radius);
	p.setClipPath(path, Qt::IntersectClip);
	if (!style.enabled) {
		p.fillPath(path, tint);
		p.restore();
		return;
	}
	if (!backdrop.isNull()) {
		const auto blurred = (style.blur <= 0)
			? backdrop
			: cache
			? cache->prepare(backdrop, rect.size(), style.blur)
			: BlurBackdrop(backdrop, rect.size(), style.blur);
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
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(dark ? QColor(255, 255, 255, 26) : QColor(0, 0, 0, 10), 1.));
	p.drawRoundedRect(QRectF(rect).adjusted(.5, .5, -.5, -.5), radius, radius);
	p.restore();
}

} // namespace Ui
